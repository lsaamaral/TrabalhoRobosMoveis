#include "atr/sistema.hpp"

#include <algorithm>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace atr {

    namespace {
        Evento evento_processo(int id, TipoEvento tipo, const char* texto) {
            Evento evento{};
            evento.ts_ms = agora_ms();
            evento.robo_id = id;
            evento.tipo = tipo;
            evento.tarefa_id = evento.baia = evento.zona = evento.caixa_id = -1;
            evento.mascara_robos = 1 << id;
            std::snprintf(evento.texto, sizeof(evento.texto), "%s", texto);
            return evento;
        }

        void executar_robo(int comando_fd, int status_fd, Ponto inicial) {
            HardwareFalso hardware(inicial);
            ContextoRobo contexto;
            contexto.status.pos = inicial;
            std::atomic<bool> parar{false};
            std::thread navegacao(loop_navegacao, std::ref(hardware), std::ref(contexto), std::ref(parar));
            std::thread status(loop_status, std::ref(hardware), std::ref(contexto), std::ref(parar));

            while (!parar) {
                pollfd descritor{comando_fd, POLLIN, 0};
                const int resultado = poll(&descritor, 1, T_STATUS_MS);
                if (resultado > 0 && (descritor.revents & POLLIN)) {
                    ComandoRoboIPC comando{};
                    if (read(comando_fd, &comando, sizeof(comando)) != static_cast<ssize_t>(sizeof(comando))) {
                        break; 
                    }
                    {
                        std::lock_guard<std::mutex> lock(contexto.mtx);
                        contexto.destino = comando.destino;
                        contexto.tem_destino = comando.tem_destino;
                        contexto.acoplar = comando.acoplar;
                        if (comando.tem_destino) {
                            contexto.estado_nav = Estado::IndoParaBaia;
                        }
                    }
                    if (comando.parar) {
                        parar = true;
                    }
                }
                StatusRoboIPC resposta{};
                {
                    std::lock_guard<std::mutex> lock(contexto.mtx);
                    resposta.status = contexto.status;
                }
                if (write(status_fd, &resposta, sizeof(resposta)) !=static_cast<ssize_t>(sizeof(resposta))) break;
            }
            parar = true;
            navegacao.join();
            status.join();
            close(comando_fd);
            close(status_fd);
            _exit(0);
        }
    }

    ProcessoRobo::ProcessoRobo(int id, Ponto posicao) : id_(id) {
        int comandos[2]{};
        int status[2]{};
        if (pipe(comandos) != 0 || pipe(status) != 0) {
            throw std::runtime_error("falha ao criar pipes do robo");
        }
        pid_ = static_cast<int>(fork());
        if (pid_ < 0) {
            throw std::runtime_error("falha ao criar processo do robo");
        }
        if (pid_ == 0) {
            close(comandos[1]);
            close(status[0]);
            executar_robo(comandos[0], status[1], posicao);
        }
        close(comandos[0]);
        close(status[1]);
        comando_fd_ = comandos[1];
        status_fd_ = status[0];
        const int flags = fcntl(status_fd_, F_GETFL, 0);
        fcntl(status_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    ProcessoRobo::~ProcessoRobo() {
        if (pid_ < 0) {
            return;
        }
        enviar_comando({{}, false, false, true});
        close(comando_fd_);
        close(status_fd_);
        waitpid(pid_, nullptr, 0);
    }

    void ProcessoRobo::enviar_comando(const ComandoRoboIPC& comando) {
        if (write(comando_fd_, &comando, sizeof(comando)) != static_cast<ssize_t>(sizeof(comando))) {
            throw std::runtime_error("falha ao enviar comando por IPC");
        }
    }

    StatusRobo ProcessoRobo::ler_status() const {
        StatusRoboIPC resposta{};
        while (read(status_fd_, &resposta, sizeof(resposta)) == static_cast<ssize_t>(sizeof(resposta))) {
            std::lock_guard<std::mutex> lock(mtx_);
            ultimo_status_ = resposta.status;
        }
        std::lock_guard<std::mutex> lock(mtx_);
        return ultimo_status_;
    }

    GestorRobosProcessos::GestorRobosProcessos(ColetorDados& coletor) : coletor_(coletor) {}
    GestorRobosProcessos::~GestorRobosProcessos() = default;

    int GestorRobosProcessos::disponibilizar_robo(Ponto posicao) {
        std::lock_guard<std::mutex> lock(mtx_);
        const int id = proximo_id_++;
        robos_.push_back(std::make_unique<ProcessoRobo>(id, posicao));
        coletor_.registrar(evento_processo(id, TipoEvento::RoboOnline, "processo do robo criado"));
        return id;
    }

    ProcessoRobo* GestorRobosProcessos::obter_robo(int id) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& robo : robos_){
            if (robo->id() == id) {
                return robo.get();
            }
        }
        return nullptr;
    }

    std::vector<int> GestorRobosProcessos::ids() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<int> resultado;
        for (const auto& robo : robos_) {
            resultado.push_back(robo->id());
        }
        return resultado;
    }

    std::size_t GestorRobosProcessos::quantidade() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return robos_.size();
    }

    OrganizadorLogisticaIPC::OrganizadorLogisticaIPC(
        GestorRobosProcessos& gestor, ColetorDados& coletor)
        : gestor_(gestor), coletor_(coletor),
          worker_(&OrganizadorLogisticaIPC::executar, this) {}

    OrganizadorLogisticaIPC::~OrganizadorLogisticaIPC() {
        parar_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void OrganizadorLogisticaIPC::adicionar_carga(Carga carga) {
        if (carga.baia < 0 || carga.baia >= NUM_BAIAS ||
            carga.zona < 0 || carga.zona >= NUM_ZONAS ||
            carga.tamanho < 1 || carga.tamanho > MAX_GRUPOS) {
                throw std::invalid_argument("carga fora dos limites do galpao");
        }
        std::lock_guard<std::mutex> lock(mtx_);
        if (carga.id == 0) {
            carga.id = proximo_id_++;
        }
        fila_.push_back(carga);
    }

    bool OrganizadorLogisticaIPC::carga_concluida(int id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        return std::find(concluidas_.begin(), concluidas_.end(), id) != concluidas_.end();
    }

    std::size_t OrganizadorLogisticaIPC::cargas_pendentes() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return fila_.size() + ativas_.size();
    }

    bool OrganizadorLogisticaIPC::grupo_na_posicao(const Tarefa& tarefa, Ponto destino) {
        for (int id : tarefa.robos) {
            ProcessoRobo* robo = gestor_.obter_robo(id);
            if (robo == nullptr || robo->ler_status().pos != destino) {
                return false;
            }
        }
        return true;
    }

    void OrganizadorLogisticaIPC::registrar(TipoEvento tipo, const Tarefa& tarefa, const char* texto) {
        int mascara = 0;
        for (int id : tarefa.robos) {
            mascara |= 1 << id;
        }
        Evento evento{};
        evento.ts_ms = agora_ms();
        evento.robo_id = -1;
        evento.tipo = tipo;
        evento.tarefa_id = tarefa.carga.id;
        evento.baia = tarefa.carga.baia;
        evento.zona = tarefa.carga.zona;
        evento.caixa_id = tarefa.carga.id;
        evento.mascara_robos = mascara;
        std::snprintf(evento.texto, sizeof(evento.texto), "%s", texto);
        coletor_.registrar(evento);
    }

    void OrganizadorLogisticaIPC::executar() {
        TemporizadorPeriodico timer(T_ORGANIZADOR_MS);
        while (!parar_) {
            timer.esperar();
            std::lock_guard<std::mutex> lock(mtx_);
            if (ativas_.empty() && !fila_.empty()) {
                Tarefa tarefa;
                tarefa.carga = fila_.front();
                fila_.pop_front();
                for (int id : gestor_.ids()) {
                    ProcessoRobo* robo = gestor_.obter_robo(id);
                    if (robo != nullptr && robo->ler_status().estado == Estado::Disponivel) {
                        tarefa.robos.push_back(id);
                        if (static_cast<int>(tarefa.robos.size()) == tarefa.carga.tamanho) break;
                    }
                }
                if (static_cast<int>(tarefa.robos.size()) == tarefa.carga.tamanho) {
                    const Ponto baia = obter_mapa().pega_baia(tarefa.carga.baia, 0);
                    for (int id : tarefa.robos)
                        gestor_.obter_robo(id)->enviar_comando({baia, true, false, false});
                    registrar(TipoEvento::TarefaCriada, tarefa, "tarefa criada por IPC");
                    ativas_.push_back(std::move(tarefa));
                } else {
                    fila_.push_front(tarefa.carga);
                }
            }
            if (ativas_.empty()) continue;
            Tarefa& tarefa = ativas_.front();
            const Ponto baia = obter_mapa().pega_baia(tarefa.carga.baia, 0);
            const Ponto zona = obter_mapa().solta_zona(tarefa.carga.zona, 0);
            if (tarefa.fase == 0 && grupo_na_posicao(tarefa, baia)) {
                for (int id : tarefa.robos)
                    gestor_.obter_robo(id)->enviar_comando({baia, true, true, false});
                tarefa.fase = 1;
                registrar(TipoEvento::PegouCarga, tarefa, "grupo acoplado por IPC");
            } else if (tarefa.fase == 1) {
                for (int id : tarefa.robos)
                    gestor_.obter_robo(id)->enviar_comando({zona, true, false, false});
                tarefa.fase = 2;
            } else if (tarefa.fase == 2 && grupo_na_posicao(tarefa, zona)) {
                registrar(TipoEvento::EntregouCarga, tarefa, "carga entregue por IPC");
                concluidas_.push_back(tarefa.carga.id);
                ativas_.clear();
            }
        }
    }
}
