#pragma once
#include "definicoes.hpp"
#include "mapa.hpp"
#include "trafego.hpp"
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace atr {
    /// @brief Entradas Hardware, o que o robo ve do mundo
    struct EntradasHw {
        Ponto posicao_atual;
        bool carga_atracada;    // se esta com carga ou nao
        int bateria;            // valor bateria 0 a 100
        bool falha;             // se tem falha ou nao
    };

    // Saidas hardware, robo envia para o mundo
    struct SaidasHw {
        Ponto setpoint;               // setpoint
        bool acoplar_e_erguer;        // comando para acoplar e erguer carga na baia
    };

    /// @brief definir fronteira de hardware
    class InterfaceHardware {
        public:
            virtual std::vector<Ponto> celulas_ocupadas() const {
                return {};
            }
            virtual ~InterfaceHardware() = default;
            virtual EntradasHw ler_entradas() = 0;
            virtual void escrever_saidas(const SaidasHw& saidas) = 0;
    };

    class HardwareFalso : public InterfaceHardware {
    public:
        explicit HardwareFalso(
            Ponto pos_inicial,
            ControleTrafego* trafego = nullptr,
            int id = -1
        );
        ~HardwareFalso() override;

        EntradasHw ler_entradas() override;
        void escrever_saidas(const SaidasHw& saidas) override;

        void injetar_falha(bool falhar);
        void ajustar_bateria(int nivel);
        std::vector<Ponto> celulas_ocupadas() const override;

    private:
        void thread_simulador();

        ControleTrafego* trafego_ = nullptr;
        int id_ = -1;
        std::mutex mtx_;
        Ponto pos_;
        Ponto sp_;
        bool acoplar_e_erguer_ = false;
        bool carga_atracada_ = false;
        double bateria_int_ = 100.0;
        bool falha_ = false;

        int ticks_acoplamento_ = 0;

        std::atomic<bool> parar_{false};
        std::thread worker_;
    };
}