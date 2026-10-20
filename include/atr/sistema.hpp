#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "comum.hpp"
#include "robo.hpp"

namespace atr {

    struct Carga {
        int id = 0;
        int baia = 0;
        int zona = 0;
        int tamanho = 1;
    };

    class ColetorDados {
    public:
        explicit ColetorDados(std::string arquivo = "eventos.csv");

        void registrar(Evento evento);
        std::vector<Evento> historico() const;
        std::string arquivo() const { return arquivo_; }

    private:
        std::string arquivo_;
        mutable std::mutex mtx_;
        std::deque<Evento> eventos_;
    };

    class GestorRobos {
    public:
        explicit GestorRobos(ColetorDados& coletor);
        ~GestorRobos();

        int disponibilizar_robo(Ponto posicao);
        Robo* obter_robo(int id);
        const Robo* obter_robo(int id) const;
        std::vector<int> ids() const;
        std::size_t quantidade() const;

    private:
        ColetorDados& coletor_;
        mutable std::mutex mtx_;
        std::vector<std::unique_ptr<Robo>> robos_;
        int proximo_id_ = 0;
    };

    class OrganizadorLogistica {
    public:
        OrganizadorLogistica(GestorRobos& gestor, ColetorDados& coletor);
        ~OrganizadorLogistica();

        void adicionar_carga(Carga carga);
        bool carga_concluida(int id) const;
        std::size_t cargas_pendentes() const;

    private:
        struct TarefaAtiva {
            Carga carga;
            std::vector<int> robos;
            enum class Fase { IndoBaia, Acoplando, IndoZona } fase = Fase::IndoBaia;
        };

        void executar();
        bool grupo_na_posicao(const TarefaAtiva& tarefa, Ponto destino) const;
        void registrar_evento(TipoEvento tipo, const TarefaAtiva& tarefa, const std::string& texto);

        GestorRobos& gestor_;
        ColetorDados& coletor_;
        mutable std::mutex mtx_;
        std::deque<Carga> fila_;
        std::vector<TarefaAtiva> ativas_;
        std::vector<int> concluidas_;
        std::atomic<bool> parar_{false};
        std::thread worker_;
        int proximo_tarefa_ = 1;
    };

    struct ComandoRoboIPC {
        Ponto destino{};
        bool tem_destino = false;
        bool acoplar = false;
        bool parar = false;
    };

    struct StatusRoboIPC {
        StatusRobo status{};
        bool carga_atracada = false;
    };

    class ProcessoRobo {
    public:
        ProcessoRobo(int id, Ponto posicao);
        ~ProcessoRobo();

        ProcessoRobo(const ProcessoRobo&) = delete;
        ProcessoRobo& operator=(const ProcessoRobo&) = delete;

        int id() const { return id_; }
        void enviar_comando(const ComandoRoboIPC& comando);
        StatusRobo ler_status() const;

    private:
        int id_;
        int comando_fd_ = -1;
        int status_fd_ = -1;
        int pid_ = -1;
        mutable std::mutex mtx_;
        mutable StatusRobo ultimo_status_{};
    };

    class GestorRobosProcessos {
    public:
        explicit GestorRobosProcessos(ColetorDados& coletor);
        ~GestorRobosProcessos();

        int disponibilizar_robo(Ponto posicao);
        ProcessoRobo* obter_robo(int id);
        std::vector<int> ids() const;
        std::size_t quantidade() const;

    private:
        ColetorDados& coletor_;
        mutable std::mutex mtx_;
        std::vector<std::unique_ptr<ProcessoRobo>> robos_;
        int proximo_id_ = 0;
    };

    class OrganizadorLogisticaIPC {
    public:
        OrganizadorLogisticaIPC(GestorRobosProcessos& gestor, ColetorDados& coletor);
        ~OrganizadorLogisticaIPC();

        void adicionar_carga(Carga carga);
        bool carga_concluida(int id) const;
        std::size_t cargas_pendentes() const;

    private:
        struct Tarefa {
            Carga carga;
            std::vector<int> robos;
            int fase = 0;
        };

        void executar();
        bool grupo_na_posicao(const Tarefa& tarefa, Ponto destino);
        void registrar(TipoEvento tipo, const Tarefa& tarefa, const char* texto);

        GestorRobosProcessos& gestor_;
        ColetorDados& coletor_;
        mutable std::mutex mtx_;
        std::deque<Carga> fila_;
        std::vector<Tarefa> ativas_;
        std::vector<int> concluidas_;
        std::atomic<bool> parar_{false};
        std::thread worker_;
        int proximo_id_ = 1;
    };

}
