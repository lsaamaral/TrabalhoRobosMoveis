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

        // Navegacao -> Status
        Estado estado_nav = Estado::Disponivel;

        StatusRobo status{};
    };

}