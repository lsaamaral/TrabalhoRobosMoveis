#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#include "atr/sistema.hpp"
#include "teste_util.hpp"

int main() {
    const char* arquivo = "teste_eventos.csv";
    std::remove(arquivo);

    atr::ColetorDados coletor(arquivo);
    atr::GestorRobos gestor(coletor);
    const int id = gestor.disponibilizar_robo({8, 19});
    VERIFICA(gestor.quantidade() == 1, "Gestor nao criou o robo automaticamente.");

    atr::OrganizadorLogistica organizador(gestor, coletor);
    organizador.adicionar_carga({42, 0, 0, 1});

    bool terminou = false;
    for (int i = 0; i < 160; ++i) {
        if (organizador.carga_concluida(42)) {
            terminou = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    VERIFICA(terminou, "Organizador nao concluiu a carga.");
    const auto eventos = coletor.historico();
    VERIFICA(eventos.size() >= 3, "Coletor nao registrou o ciclo da carga.");
    VERIFICA(gestor.obter_robo(id) != nullptr, "Gestor perdeu o robo.");
    std::remove(arquivo);
    std::cout << "[SUCESSO] Gestor, Organizador e Coletor integrados.\n";
    return 0;
}
