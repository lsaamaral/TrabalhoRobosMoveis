#pragma once
#include <mutex>
#include <atomic>
#include <thread>
#include "definicoes.hpp"
#include "hardware.hpp"
#include "mapa.hpp"

namespace atr {

    struct StatusRobo {
        Estado estado = Estado::Offline;
        Ponto pos{};
        int bateria = 0;
        bool falha = false;
        int distancia = 0;
    };

    struct ContextoRobo {
        std::mutex mtx;
        
        // Status -> Navegacao
        bool falha = false;
        bool pedir_recarga = false; // Indica se o sistema pediu ida ao carregador
        Ponto destino_recarga{};    // Coordenadas do carregador mais proximo
        Ponto destino{};            // Destino solicitado pelo organizador
        bool tem_destino = false;
        bool acoplar = false;

        // Navegacao -> Status
        Estado estado_nav = Estado::Disponivel;

        StatusRobo status{};
    };

    void loop_navegacao(InterfaceHardware& hw, ContextoRobo& ctx,
                        std::atomic<bool>& parar);
    void loop_status(InterfaceHardware& hw, ContextoRobo& ctx,
                     std::atomic<bool>& parar);

    /// @brief Processo logico de um robo, com as tarefas de navegacao e status
    class Robo {
    public:
        Robo(int id, Ponto pos_inicial);
        ~Robo();

        Robo(const Robo&) = delete;
        Robo& operator=(const Robo&) = delete;

        int id() const { return id_; }
        void enviar_destino(Ponto destino);
        void comandar_acoplamento(bool acoplar);
        StatusRobo ler_status() const;
        HardwareFalso& hardware() { return hardware_; }

    private:
        int id_;
        HardwareFalso hardware_;
        mutable ContextoRobo contexto_;
        std::atomic<bool> parar_{false};
        std::thread thread_navegacao_;
        std::thread thread_status_;
    };

}