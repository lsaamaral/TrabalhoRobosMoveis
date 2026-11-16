
#include "atr/sistema.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
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

        void executar_robo(int comando_fd, int status_fd, Ponto inicial, ControleTrafego* trafego, int id) {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            HardwareFalso hardware(inicial, trafego, id);
            ContextoRobo contexto;
            contexto.status.pos = inicial;
            contexto.ponto_inicial = inicial;

            const int flags = fcntl(status_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(status_fd, F_SETFL, flags | O_NONBLOCK);
            }

            std::atomic<bool> parar{ false };

            std::thread navegacao(
                loop_navegacao,
                std::ref(hardware),
                std::ref(contexto),
                std::ref(parar)
            );

            std::thread status(
                loop_status,
                std::ref(hardware),
                std::ref(contexto),
                std::ref(parar)
            );

            while (!parar) {
                pollfd descritor{ comando_fd, POLLIN, 0 };
                const int resultado = poll(&descritor, 1, T_STATUS_MS);

                if (resultado > 0 && (descritor.revents & POLLIN)) {
                    ComandoRoboIPC comando{};

                    if (read(comando_fd, &comando, sizeof(comando)) !=
                        static_cast<ssize_t>(sizeof(comando))) {
                        break;
                    }

                    {
                        std::lock_guard<std::mutex> lock(contexto.mtx);

                        // Comandos de bateria e falha nao apagam
                        // o destino da missao em andamento.
                        if (!comando.alterar_falha &&
                            !comando.alterar_bateria) {

                            contexto.destino = comando.destino;
                            contexto.tem_destino = comando.tem_destino;
                            contexto.acoplar = comando.acoplar;
                            contexto.modo_manual = comando.modo_manual;

                            if (comando.tem_destino) {
                                contexto.estado_nav = comando.modo_manual
                                    ? Estado::Manual
                                    : Estado::IndoParaBaia;
                            }
                            else if (!comando.modo_manual) {
                                contexto.estado_nav = Estado::Disponivel;
                            }
                        }

                        // Atualizacoes independentes da missao
                        if (comando.alterar_falha) {
                            hardware.injetar_falha(comando.falha);
                        }

                        if (comando.alterar_bateria) {
                            hardware.ajustar_bateria(comando.bateria);
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
                    resposta.carga_atracada = contexto.status.carga_atracada;
                }

                const ssize_t escrito = write(
                    status_fd, &resposta, sizeof(resposta)
                );
                if (escrito < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    continue;
                }
                if (escrito < 0 && errno == EINTR) {
                    continue;
                }
                if (escrito != static_cast<ssize_t>(sizeof(resposta))) {
                    break;
                }
            }

            parar = true;
            navegacao.join();
            status.join();

            close(comando_fd);
            close(status_fd);

            _exit(0);
        }

    } // namespace anonimo


    // ======================================================
    // PROCESSO INDIVIDUAL DO ROBO
    // ======================================================

    ProcessoRobo::ProcessoRobo(
    int id, Ponto posicao, ControleTrafego& trafego
) : id_(id), trafego_(&trafego) {
    if (!trafego_->registrar(id, posicao)) {
        throw std::runtime_error(
            "posicao inicial ocupada ou invalida"
        );
    }
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

            executar_robo(comandos[0], status[1], posicao, trafego_, id_);
        }

        close(comandos[0]);
        close(status[1]);

        comando_fd_ = comandos[1];
        status_fd_ = status[0];

        const int comando_flags = fcntl(comando_fd_, F_GETFL, 0);
        fcntl(comando_fd_, F_SETFL, comando_flags | O_NONBLOCK);
        const int flags = fcntl(status_fd_, F_GETFL, 0);
        fcntl(status_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    ProcessoRobo::~ProcessoRobo() {
        if (pid_ < 0) {
            return;
        }

        try {
            enviar_comando({ {}, false, false, true });
        } catch (const std::exception&) {
        }

        close(comando_fd_);
        close(status_fd_);

        waitpid(pid_, nullptr, 0);
        trafego_->remover(id_);
    }

    void ProcessoRobo::enviar_comando(const ComandoRoboIPC& comando) {
        const ssize_t escrito = write(comando_fd_, &comando, sizeof(comando));
        if (escrito < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            throw std::runtime_error("fila de comandos IPC cheia");
        }
        if (escrito < 0 && errno == EINTR) {
            throw std::runtime_error("envio de comando IPC interrompido");
        }
        if (escrito != static_cast<ssize_t>(sizeof(comando))) {
            throw std::runtime_error("falha ao enviar comando por IPC");
        }
    }

    void ProcessoRobo::injetar_falha(bool falhar) {
        ComandoRoboIPC comando{};
        comando.alterar_falha = true;
        comando.falha = falhar;

        enviar_comando(comando);
    }

    void ProcessoRobo::ajustar_bateria(int nivel) {
        if (nivel < 0 || nivel > 100) {
            throw std::invalid_argument(
                "nivel de bateria fora do intervalo 0..100"
            );
        }

        ComandoRoboIPC comando{};
        comando.alterar_bateria = true;
        comando.bateria = nivel;

        enviar_comando(comando);
    }

    void ProcessoRobo::enviar_destino_manual(Ponto destino) {
        ComandoRoboIPC comando{};
        comando.destino = destino;
        comando.tem_destino = true;
        comando.modo_manual = true;

        enviar_comando(comando);
    }

    void ProcessoRobo::retornar_ao_automatico() {
        ComandoRoboIPC comando{};
        comando.modo_manual = false;

        enviar_comando(comando);
    }

    StatusRobo ProcessoRobo::ler_status() const {
        StatusRoboIPC resposta{};

        while (read(status_fd_, &resposta, sizeof(resposta)) ==
            static_cast<ssize_t>(sizeof(resposta))) {

            std::lock_guard<std::mutex> lock(mtx_);
            ultimo_status_ = resposta.status;
            ultimo_status_.carga_atracada = resposta.carga_atracada;
        }

        std::lock_guard<std::mutex> lock(mtx_);
        return ultimo_status_;
    }


    // ======================================================
    // GESTOR DOS PROCESSOS DOS ROBOS
    // ======================================================

    GestorRobosProcessos::GestorRobosProcessos(
        ColetorDados& coletor
    ) : coletor_(coletor) {
    }

    GestorRobosProcessos::~GestorRobosProcessos() = default;

    int GestorRobosProcessos::disponibilizar_robo(Ponto posicao) {
        std::lock_guard<std::mutex> lock(mtx_);

        const int id = proximo_id_;

        robos_.push_back(std::make_unique<ProcessoRobo>(id, posicao, trafego_));
        ++proximo_id_;

        coletor_.registrar(
            evento_processo(
                id,
                TipoEvento::RoboOnline,
                "processo do robo criado"
            )
        );

        return id;
    }

    ProcessoRobo* GestorRobosProcessos::obter_robo(int id) {
        std::lock_guard<std::mutex> lock(mtx_);

        for (auto& robo : robos_) {
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


    // ======================================================
    // ORGANIZADOR DE LOGISTICA IPC
    // ======================================================

    OrganizadorLogisticaIPC::OrganizadorLogisticaIPC(
        GestorRobosProcessos& gestor,
        ColetorDados& coletor
    )
        : gestor_(gestor),
        coletor_(coletor),
        worker_(&OrganizadorLogisticaIPC::executar, this) {
    }

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

            throw std::invalid_argument(
                "carga fora dos limites do galpao"
            );
        }

        std::lock_guard<std::mutex> lock(mtx_);

        if (carga.id == 0) {
            carga.id = proximo_id_++;
        }
        else {
            proximo_id_ = std::max(
                proximo_id_, carga.id + 1
            );
        }

        fila_.push_back(carga);
    }

    bool OrganizadorLogisticaIPC::carga_concluida(int id) const {
        std::lock_guard<std::mutex> lock(mtx_);

        return std::find(
            concluidas_.begin(),
            concluidas_.end(),
            id
        ) != concluidas_.end();
    }

    std::size_t OrganizadorLogisticaIPC::cargas_pendentes() const {
        std::lock_guard<std::mutex> lock(mtx_);

        return fila_.size() + ativas_.size();
    }


    // ======================================================
    // VERIFICACAO DA POSICAO DO GRUPO
    // ======================================================

    bool OrganizadorLogisticaIPC::grupo_na_posicao(
        const Tarefa& tarefa,
        bool na_baia
    ) {
        const auto& mapa = obter_mapa();

        for (std::size_t i = 0; i < tarefa.robos.size(); ++i) {
            int id = tarefa.robos[i];

            const Ponto destino = na_baia
                ? mapa.pega_baia(
                    tarefa.carga.baia,
                    static_cast<int>(i)
                )
                : mapa.solta_zona(
                    tarefa.carga.zona,
                    static_cast<int>(i)
                );

            ProcessoRobo* robo = gestor_.obter_robo(id);

            if (robo == nullptr) {
                return false;
            }

            const StatusRobo status = robo->ler_status();

            if (status.falha ||
                status.estado == Estado::Offline ||
                status.pos != destino) {
                return false;
            }
        }

        return true;
    }


    // ======================================================
    // REGISTRO DOS EVENTOS
    // ======================================================

    void OrganizadorLogisticaIPC::registrar(
        TipoEvento tipo,
        const Tarefa& tarefa,
        const char* texto
    ) {
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

        std::snprintf(
            evento.texto,
            sizeof(evento.texto),
            "%s",
            texto
        );

        coletor_.registrar(evento);
    }


    // ======================================================
    // EXECUCAO PERIODICA DO ORGANIZADOR
    // ======================================================

    void OrganizadorLogisticaIPC::executar() {
        TemporizadorPeriodico timer(T_ORGANIZADOR_MS);

        while (!parar_) {
            timer.esperar();

            std::lock_guard<std::mutex> lock(mtx_);

            if (ativas_.empty() && !fila_.empty()) {
                Tarefa tarefa;
                auto carga_it = fila_.end();

                for (auto it = fila_.begin(); it != fila_.end(); ++it) {
                    int disponiveis = 0;
                    for (int id : gestor_.ids()) {
                        ProcessoRobo* robo = gestor_.obter_robo(id);

                        if (robo != nullptr &&
                            robo->ler_status().estado == Estado::Disponivel) {
                            ++disponiveis;
                        }
                    }
                    if (disponiveis >= it->tamanho) {
                        carga_it = it;
                        break;
                    }
                }

                if (carga_it != fila_.end()) {
                    tarefa.carga = *carga_it;
                    fila_.erase(carga_it);

                    for (int id : gestor_.ids()) {
                        ProcessoRobo* robo = gestor_.obter_robo(id);

                        if (robo != nullptr &&
                            robo->ler_status().estado == Estado::Disponivel) {
                            tarefa.robos.push_back(id);
                            if (static_cast<int>(tarefa.robos.size()) ==
                                tarefa.carga.tamanho) {
                                break;
                            }
                        }
                    }

                    if (static_cast<int>(tarefa.robos.size()) ==
                        tarefa.carga.tamanho) {

                        // Cada robo recebe uma posicao diferente na baia.
                        for (std::size_t i = 0;
                            i < tarefa.robos.size(); ++i) {
                            const Ponto baia = obter_mapa().pega_baia(
                                tarefa.carga.baia,
                                static_cast<int>(i)
                            );
                            gestor_.obter_robo(tarefa.robos[i])
                                ->enviar_comando({
                                    baia, true, false, false
                                });
                        }

                        registrar(
                            TipoEvento::TarefaCriada,
                            tarefa,
                            "tarefa criada por IPC"
                        );

                        ativas_.push_back(std::move(tarefa));
                    }
                }
            }

            if (ativas_.empty()) {
                continue;
            }

            Tarefa& tarefa = ativas_.front();

            bool interrompida = false;

            for (int id : tarefa.robos) {
                ProcessoRobo* robo = gestor_.obter_robo(id);

                if (robo == nullptr) {
                    interrompida = true;
                    break;
                }

                const StatusRobo status = robo->ler_status();

                if (status.falha ||
                    status.estado == Estado::Offline ||
                    status.estado == Estado::Manual ||
                    status.estado == Estado::IndoRecarga) {

                    interrompida = true;
                    break;
                }
            }

            if (interrompida) {
                for (int id : tarefa.robos) {
                    ProcessoRobo* robo = gestor_.obter_robo(id);

                    if (robo == nullptr) {
                        continue;
                    }

                    const StatusRobo status = robo->ler_status();

                    if (status.estado != Estado::Manual) {
                        robo->enviar_comando({
                            status.pos,
                            false,
                            false,
                            false
                            });
                    }
                }

                registrar(
                    TipoEvento::TarefaAbortada,
                    tarefa,
                    "tarefa abortada por falha ou comando manual"
                );

                fila_.push_back(tarefa.carga);
                ativas_.clear();

                continue;
            }

            // Todos os robos devem chegar a baia
            // antes do acoplamento.
            if (tarefa.fase == 0 &&
                grupo_na_posicao(tarefa, true)) {
                bool acoplada = true;
                for (int id : tarefa.robos) {
                    ProcessoRobo* robo = gestor_.obter_robo(id);
                    if (robo == nullptr || !robo->ler_status().carga_atracada) {
                        acoplada = false;
                    }
                }

                if (!acoplada) {
                    for (std::size_t i = 0; i < tarefa.robos.size(); ++i) {

                        const Ponto baia = obter_mapa().pega_baia(
                            tarefa.carga.baia,
                            static_cast<int>(i)
                        );

                        gestor_.obter_robo(tarefa.robos[i])
                            ->enviar_comando({
                                baia, true, true, false
                                });
                    }
                } else {
                    tarefa.fase = 1;

                    registrar(
                        TipoEvento::PegouCarga,
                        tarefa,
                        "grupo acoplado por IPC"
                    );
                }
            }
            else if (tarefa.fase == 1) {

                // Destinos individuais para cada robo
                // na zona de entrega.
                for (std::size_t i = 0;
                    i < tarefa.robos.size(); ++i) {

                    const Ponto zona = obter_mapa().solta_zona(
                        tarefa.carga.zona,
                        static_cast<int>(i)
                    );

                    gestor_.obter_robo(tarefa.robos[i])
                        ->enviar_comando({
                        zona, true, true, false
                            });
                }

                tarefa.fase = 2;
            }
            else if (tarefa.fase == 2 &&
                grupo_na_posicao(tarefa, false)) {
                for (int id : tarefa.robos) {
                    gestor_.obter_robo(id)->enviar_comando({
                        obter_mapa().ponto_inicial(id), true, false, false
                    });
                }

                registrar(
                    TipoEvento::EntregouCarga,
                    tarefa,
                    "carga entregue por IPC"
                );

                concluidas_.push_back(tarefa.carga.id);
                ativas_.clear();
            }
        }
    }

} // namespace atr
