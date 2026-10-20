#include <chrono>
#include <iostream>
#include <thread>
#include "atr/robo.hpp"
#include "teste_util.hpp"

int main() {
    std::cout << "[TESTE] Troca de dados entre navegacao, status e hardware...\n";
    atr::Robo robo(0, {8, 19});
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    robo.enviar_destino({8, 15});

    bool chegou = false;
    for (int i = 0; i < 80; ++i) {
        if (robo.ler_status().pos == atr::Ponto{8, 15}) {
            chegou = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    VERIFICA(chegou, "O processo logico do robo nao chegou ao destino.");
    VERIFICA(robo.ler_status().distancia >= 3,
             "A thread de status nao acumulou a distancia percorrida.");

    robo.hardware().injetar_falha(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    VERIFICA(robo.ler_status().estado == atr::Estado::Defeito,
             "A falha do hardware nao foi publicada no status.");

    std::cout << "[SUCESSO] As duas threads trocaram dados com sincronizacao.\n";
    return 0;
}
