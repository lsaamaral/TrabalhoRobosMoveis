#include "atr/sistema.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace atr {

    namespace {
        Evento novo_evento(int robo_id, TipoEvento tipo, int tarefa_id,
                           int baia, int zona, int caixa_id, int mascara,
                           const std::string& texto) {
            Evento evento{};
            evento.ts_ms = agora_ms();
            evento.robo_id = robo_id;
            evento.tipo = tipo;
            evento.tarefa_id = tarefa_id;
            evento.baia = baia;
            evento.zona = zona;
            evento.caixa_id = caixa_id;
            evento.mascara_robos = mascara;
            std::snprintf(evento.texto, sizeof(evento.texto), "%s", texto.c_str());
            return evento;
        }
    }

    ColetorDados::ColetorDados(std::string arquivo) : arquivo_(std::move(arquivo)) {
        std::ofstream saida(arquivo_, std::ios::trunc);
        if (!saida) throw std::runtime_error("nao foi possivel criar o arquivo de eventos");
        saida << "timestamp,robo,tipo,tarefa,baia,zona,caixa,robos,texto\n";
    }

    void ColetorDados::registrar(Evento evento) {
        std::lock_guard<std::mutex> lock(mtx_);
        eventos_.push_back(evento);
        while (eventos_.size() > TAM_MEM_CIRC) eventos_.pop_front();
        std::ofstream saida(arquivo_, std::ios::app);
        if (!saida) throw std::runtime_error("nao foi possivel abrir o arquivo de eventos");
        saida << ts_iso(evento.ts_ms) << ',' << evento.robo_id << ','
              << to_string(evento.tipo) << ',' << evento.tarefa_id << ','
              << evento.baia << ',' << evento.zona << ',' << evento.caixa_id
              << ',' << evento.mascara_robos << ',' << evento.texto << '\n';
    }

    std::vector<Evento> ColetorDados::historico() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return {eventos_.begin(), eventos_.end()};
    }

    GestorRobos::GestorRobos(ColetorDados& coletor) : coletor_(coletor) {}
    GestorRobos::~GestorRobos() = default;

    int GestorRobos::disponibilizar_robo(Ponto posicao) {
        std::lock_guard<std::mutex> lock(mtx_);
        const int id = proximo_id_++;
        robos_.push_back(std::make_unique<Robo>(id, posicao));
        coletor_.registrar(novo_evento(id, TipoEvento::RoboOnline, -1, -1, -1,
                                       -1, 1 << id, "robo disponibilizado"));
        return id;
    }

    Robo* GestorRobos::obter_robo(int id) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& robo : robos_) if (robo->id() == id) return robo.get();
        return nullptr;
    }

    const Robo* GestorRobos::obter_robo(int id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& robo : robos_) if (robo->id() == id) return robo.get();
        return nullptr;
    }

    std::vector<int> GestorRobos::ids() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<int> resultado;
        for (const auto& robo : robos_) resultado.push_back(robo->id());
        return resultado;
    }

    std::size_t GestorRobos::quantidade() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return robos_.size();
    }

    OrganizadorLogistica::OrganizadorLogistica(GestorRobos& gestor,
                                               ColetorDados& coletor)
        : gestor_(gestor), coletor_(coletor),
          worker_(&OrganizadorLogistica::executar, this) {}

    OrganizadorLogistica::~OrganizadorLogistica() {
        parar_ = true;
        if (worker_.joinable()) worker_.join();
    }

    void OrganizadorLogistica::adicionar_carga(Carga carga) {
        if (carga.baia < 0 || carga.baia >= NUM_BAIAS ||
            carga.zona < 0 || carga.zona >= NUM_ZONAS ||
            carga.tamanho < 1 || carga.tamanho > MAX_GRUPOS) {
            throw std::invalid_argument("carga fora dos limites do galpao");
        }
        std::lock_guard<std::mutex> lock(mtx_);
        if (carga.id == 0) carga.id = proximo_tarefa_++;
        fila_.push_back(carga);
    }

    bool OrganizadorLogistica::carga_concluida(int id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        return std::find(concluidas_.begin(), concluidas_.end(), id) != concluidas_.end();
    }

    std::size_t OrganizadorLogistica::cargas_pendentes() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return fila_.size() + ativas_.size();
    }

    bool OrganizadorLogistica::grupo_na_posicao(const TarefaAtiva& tarefa,
                                                Ponto destino) const {
        for (int id : tarefa.robos) {
            const Robo* robo = gestor_.obter_robo(id);
            if (robo == nullptr || robo->ler_status().pos != destino) return false;
        }
        return true;
    }

    void OrganizadorLogistica::registrar_evento(TipoEvento tipo,
                                                const TarefaAtiva& tarefa,
                                                const std::string& texto) {
        int mascara = 0;
        for (int id : tarefa.robos) mascara |= 1 << id;
        coletor_.registrar(novo_evento(-1, tipo, tarefa.carga.id,
                                       tarefa.carga.baia, tarefa.carga.zona,
                                       tarefa.carga.id, mascara, texto));
    }

    void OrganizadorLogistica::executar() {
        TemporizadorPeriodico timer(T_ORGANIZADOR_MS);
        while (!parar_) {
            timer.esperar();
            std::lock_guard<std::mutex> lock(mtx_);
            if (ativas_.empty() && !fila_.empty()) {
                Carga carga = fila_.front();
                fila_.pop_front();
                TarefaAtiva tarefa;
                tarefa.carga = carga;
                for (int id : gestor_.ids()) {
                    const Robo* robo = gestor_.obter_robo(id);
                    if (robo != nullptr && robo->ler_status().estado == Estado::Disponivel) {
                        tarefa.robos.push_back(id);
                        if (static_cast<int>(tarefa.robos.size()) == carga.tamanho) break;
                    }
                }
                if (static_cast<int>(tarefa.robos.size()) == carga.tamanho) {
                    const Ponto destino = obter_mapa().pega_baia(carga.baia, 0);
                    for (int id : tarefa.robos) gestor_.obter_robo(id)->enviar_destino(destino);
                    registrar_evento(TipoEvento::TarefaCriada, tarefa, "tarefa criada");
                    ativas_.push_back(std::move(tarefa));
                } else {
                    fila_.push_front(carga);
                }
            }
            if (ativas_.empty()) continue;
            TarefaAtiva& tarefa = ativas_.front();
            const Ponto baia = obter_mapa().pega_baia(tarefa.carga.baia, 0);
            const Ponto zona = obter_mapa().solta_zona(tarefa.carga.zona, 0);
            if (tarefa.fase == TarefaAtiva::Fase::IndoBaia &&
                grupo_na_posicao(tarefa, baia)) {
                for (int id : tarefa.robos) gestor_.obter_robo(id)->comandar_acoplamento(true);
                tarefa.fase = TarefaAtiva::Fase::Acoplando;
                registrar_evento(TipoEvento::PegouCarga, tarefa, "carga acoplada");
            } else if (tarefa.fase == TarefaAtiva::Fase::Acoplando) {
                for (int id : tarefa.robos) {
                    gestor_.obter_robo(id)->comandar_acoplamento(false);
                    gestor_.obter_robo(id)->enviar_destino(zona);
                }
                tarefa.fase = TarefaAtiva::Fase::IndoZona;
            } else if (tarefa.fase == TarefaAtiva::Fase::IndoZona &&
                       grupo_na_posicao(tarefa, zona)) {
                registrar_evento(TipoEvento::EntregouCarga, tarefa, "carga entregue");
                concluidas_.push_back(tarefa.carga.id);
                ativas_.clear();
            }
        }
    }
}
