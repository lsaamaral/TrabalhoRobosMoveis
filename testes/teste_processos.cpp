#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#include "atr/sistema.hpp"
#include "teste_util.hpp"

int main() {
    const char* arquivo = "teste_processos.csv";
    std::remove(arquivo);
    atr::ColetorDados coletor(arquivo);
    atr::GestorRobosProcessos gestor(coletor);

    const int primeiro = gestor.disponibilizar_robo({8, 19});
    const int segundo = gestor.disponibilizar_robo({9, 19});
    VERIFICA(gestor.quantidade() == 2, "Gestor nao criou dois processos.");
    VERIFICA(gestor.obter_robo(primeiro) != nullptr, "Primeiro processo nao encontrado.");
    VERIFICA(gestor.obter_robo(segundo) != nullptr, "Segundo processo nao encontrado.");

    gestor.obter_robo(primeiro)->injetar_falha(true);
    bool falhou = false;
    for (int i = 0; i < 20; ++i) {
        if (gestor.obter_robo(primeiro)->ler_status().falha) {
            falhou = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    VERIFICA(falhou, "Falha nao atravessou o pipe ate o processo do robo.");
    gestor.obter_robo(primeiro)->injetar_falha(false);
    bool recuperou = false;
    for (int i = 0; i < 20; ++i) {
        if (!gestor.obter_robo(primeiro)->ler_status().falha) {
            recuperou = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    VERIFICA(recuperou, "Robo nao saiu do estado de falha.");

    gestor.obter_robo(segundo)->ajustar_bateria(37);
    bool bateria_ajustada = false;
    for (int i = 0; i < 20; ++i) {
        if (gestor.obter_robo(segundo)->ler_status().bateria == 37) {
            bateria_ajustada = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    VERIFICA(bateria_ajustada, "Bateria nao atravessou o pipe ate o processo do robo.");

    gestor.obter_robo(segundo)->enviar_destino_manual({20, 19});
    bool modo_manual = false;
    for (int i = 0; i < 20; ++i) {
        if (gestor.obter_robo(segundo)->ler_status().estado == atr::Estado::Manual) {
            modo_manual = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    VERIFICA(modo_manual, "Destino manual nao alterou o estado do robo.");
    gestor.obter_robo(segundo)->retornar_ao_automatico();
    bool modo_automatico = false;
    for (int i = 0; i < 20; ++i) {
        if (gestor.obter_robo(segundo)->ler_status().estado ==
            atr::Estado::Disponivel) {
            modo_automatico = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    VERIFICA(modo_automatico, "Robo nao retornou ao modo automatico.");

    atr::OrganizadorLogisticaIPC organizador(gestor, coletor);
    organizador.adicionar_carga({77, 0, 0, 2});

    bool terminou = false;
    for (int i = 0; i < 180; ++i) {
        if (organizador.carga_concluida(77)) {
            terminou = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    VERIFICA(terminou, "Organizador nao concluiu carga usando IPC.");
    VERIFICA(coletor.historico().size() >= 3,
             "Coletor nao recebeu eventos dos processos.");
    std::remove(arquivo);
    std::cout << "[SUCESSO] Dois processos de robo comunicaram por pipes.\n";
    return 0;
}
