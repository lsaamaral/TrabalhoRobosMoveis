// Ponte de telemetria para a primeira interface grafica.
// Apenas observa os robos reais criados pelo sistema C++ existente.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <algorithm>
#include "atr/sistema.hpp"
#include "atr/mapa.hpp"

int main(int argc, char** argv) {
    int quantidade = 3;
    if (argc == 3 && std::string(argv[1]) == "--robos") {
        quantidade = std::atoi(argv[2]);
    }
    if (quantidade < 1 || quantidade > atr::MAX_ROBOS) {
        std::cerr << "Quantidade de robos deve estar entre 1 e " << atr::MAX_ROBOS << "\n";
        return 2;
    }

    atr::ColetorDados coletor("eventos_visual.csv");
    atr::GestorRobosProcessos gestor(coletor);
    const auto& mapa = atr::obter_mapa();
    for (int i = 0; i < quantidade; ++i) {
        gestor.disponibilizar_robo(mapa.ponto_inicial(i));
    }
    atr::OrganizadorLogisticaIPC organizador(gestor, coletor);
    organizador.adicionar_carga({ 1, 0, 0, 2 });

    // Uma linha JSON por amostragem; sem dependencia de biblioteca externa.
    // O backend continua responsavel pelo planejamento e pelos movimentos.
    bool terminado = false;
    int quadros_finais = 0;
    for (int ciclo = 0; ciclo < 300; ++ciclo) {
        const bool concluida = organizador.carga_concluida(1);
        // O estado de transporte e inferido dos eventos registrados pelo Organizador.
        // Isto indica a fase da missao, nao confirma acoplamento mecanico independente.
        const auto eventos = coletor.historico();
        int mascara_transportando = 0;
        for (const auto& evento : eventos) {
            if (evento.tipo == atr::TipoEvento::PegouCarga) {
                mascara_transportando |= evento.mascara_robos;
            }
            else if (evento.tipo == atr::TipoEvento::EntregouCarga ||
                evento.tipo == atr::TipoEvento::TarefaAbortada) {
                mascara_transportando &= ~evento.mascara_robos;
            }
        }

        std::cout << "{\"tipo\":\"estado\",\"ms\":" << atr::mono_ms()
            << ",\"carga_concluida\":" << (concluida ? "true" : "false")
            << ",\"pendentes\":" << organizador.cargas_pendentes()
            << ",\"robos\":[";
        bool primeiro = true;
        for (int id : gestor.ids()) {
            const auto* robo = gestor.obter_robo(id);
            if (!robo) continue;
            const auto status = robo->ler_status();
            if (!primeiro) std::cout << ',';
            primeiro = false;
            std::cout << "{\"id\":" << id
                << ",\"x\":" << status.pos.x
                << ",\"y\":" << status.pos.y
                << ",\"bateria\":" << status.bateria
                << ",\"distancia\":" << status.distancia
                << ",\"falha\":" << (status.falha ? "true" : "false")
                << ",\"transportando\":" << ((mascara_transportando & (1 << id)) ? "true" : "false")
                << ",\"estado\":\"" << atr::to_string(status.estado) << "\"}";
        }
        std::cout << "],\"eventos\":[";
        const std::size_t inicio = eventos.size() > 5 ? eventos.size() - 5 : 0;
        for (std::size_t i = inicio; i < eventos.size(); ++i) {
            if (i > inicio) std::cout << ',';
            std::cout << "\"" << atr::to_string(eventos[i].tipo) << "\"";
        }
        std::cout << "]}" << std::endl;
        if (concluida) {
            if (++quadros_finais >= 10) { terminado = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return terminado ? 0 : 1;
}
