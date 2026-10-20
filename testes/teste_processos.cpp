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
