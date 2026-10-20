#include <iostream>
#include <thread>
#include <chrono>
#include "atr/hardware.hpp"
#include "atr/mapa.hpp"
#include "teste_util.hpp"

int main() {
    std::cout << "[TESTE] Iniciando testes do HardwareFalso...\n";

    // Instancia o hardware falso em (8, 19) conforme ponto_inicial(0) do mapa
    atr::HardwareFalso hw({8, 19});

    // Escrever um setpoint e esperar chegar
    atr::Ponto destino{8, 15}; // 4 celulas acima
    hw.escrever_saidas({destino, false});

    std::cout << "[TESTE] Movendo de (8,19) para (8,15). Aguardando chegada...\n";
    bool chegou = false;
    int tentativas = 0;
    
    // Aguarda ate 50 tentativas
    while (tentativas < 50) {
        auto entradas = hw.ler_entradas();
        if (entradas.posicao_atual == destino) {
            chegou = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tentativas++;
    }

    VERIFICA(chegou, "Erro: O robô falhou em atingir o setpoint de destino no tempo limite.");
    std::cout << "[SUCESSO] Robô chegou ao destino com sucesso.\n";

    // Injetar falha e verificar que a posicao para de mudar
    hw.injetar_falha(true);
    auto pos_antes_falha = hw.ler_entradas().posicao_atual;
    
    // Tenta mandar para outro destino distante
    hw.escrever_saidas({{8, 10}, false});
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    auto pos_apos_falha = hw.ler_entradas().posicao_atual;
    VERIFICA(pos_antes_falha == pos_apos_falha, "Erro: O robô não deveria se mover enquanto estiver em falha.");
    std::cout << "[SUCESSO] Comportamento de falha validado: posicao congelada.\n";

    hw.injetar_falha(false);

    // Zerar a bateria e verificar que ela nao anda mais
    hw.ajustar_bateria(0);
    auto entradas_bat = hw.ler_entradas();
    VERIFICA(entradas_bat.bateria == 0, "Erro na leitura da bateria ajustada.");
    
    std::cout << "[SUCESSO] Todos os asserts do HardwareFalso passaram.\n";
    return 0;
}