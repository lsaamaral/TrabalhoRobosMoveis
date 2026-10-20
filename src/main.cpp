#include <chrono>
#include <iostream>
#include <thread>
#include "atr/sistema.hpp"

int main() {
    atr::ColetorDados coletor("eventos.csv");
    atr::GestorRobosProcessos gestor(coletor);
    gestor.disponibilizar_robo({8, 19});
    atr::OrganizadorLogisticaIPC organizador(gestor, coletor);
    organizador.adicionar_carga({1, 0, 0, 1});
    for (int i = 0; i < 100 && !organizador.carga_concluida(1); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "robos=" << gestor.quantidade()
              << " cargas_pendentes=" << organizador.cargas_pendentes()
              << " carga_1_concluida=" << organizador.carga_concluida(1)
              << " eventos=" << coletor.historico().size() << '\n';
    return 0;
}