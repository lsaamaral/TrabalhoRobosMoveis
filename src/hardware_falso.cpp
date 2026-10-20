#include "atr/hardware.hpp"
#include "atr/mapa.hpp"
#include "atr/comum.hpp"
#include <algorithm>

namespace atr {

    HardwareFalso::HardwareFalso(Ponto pos_inicial) 
        : pos_(pos_inicial), sp_(pos_inicial) {
        worker_ = std::thread(&HardwareFalso::thread_simulador, this);
    }

    HardwareFalso::~HardwareFalso() {
        parar_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    EntradasHw HardwareFalso::ler_entradas() {
        std::lock_guard<std::mutex> lock(mtx_);
        return {pos_, carga_atracada_, bateria_, falha_};
    }

    void HardwareFalso::escrever_saidas(const SaidasHw& saidas) {
        std::lock_guard<std::mutex> lock(mtx_);
        sp_ = saidas.setpoint;
        acoplar_e_erguer_ = saidas.acoplar_e_erguer;
    }

    void HardwareFalso::injetar_falha(bool falhar) {
        std::lock_guard<std::mutex> lock(mtx_);
        falha_ = falhar;
    }

    void HardwareFalso::ajustar_bateria(int nivel) {
        std::lock_guard<std::mutex> lock(mtx_);
        bateria_ = std::clamp(nivel, 0, 100);
    }

    void HardwareFalso::thread_simulador() {
        TemporizadorPeriodico timer(T_SIMULADOR_MS);
        const MapaGalpao& mapa = obter_mapa();

        while (!parar_) {
            timer.esperar();

            std::lock_guard<std::mutex> lock(mtx_);

            if (falha_) {
                continue;
            }

            // Simulacao de movimento
            if (pos_ != sp_) {
                Ponto proxima = pos_;
                if (pos_.x < sp_.x) proxima.x++;
                else if (pos_.x > sp_.x) proxima.x--;
                else if (pos_.y < sp_.y) proxima.y++;
                else if (pos_.y > sp_.y) proxima.y--;

                if (mapa.caminhavel(proxima.x, proxima.y)) {
                    pos_ = proxima;
                    bateria_ = std::max(0, bateria_ - 1);
                }
            } else {
                // Verifica se esta sobre um carregador para recarregar
                bool em_carregador = false;
                for (int i = 0; i < NUM_RECARGA; i++) {
                    if (pos_ == mapa.carregador(i)) {
                        em_carregador = true;
                        break;
                    }
                }
                if (em_carregador) {
                    bateria_ = std::min(100, bateria_ + 5);
                }
            }

            // Simulacao de acoplamento de carga
            if (acoplar_e_erguer_) {
                ticks_acoplamento_++;
                if (ticks_acoplamento_ >= 2) {
                    carga_atracada_ = true;
                }
            } else {
                ticks_acoplamento_ = 0;
                carga_atracada_ = false;
            }
        }
    }

}
