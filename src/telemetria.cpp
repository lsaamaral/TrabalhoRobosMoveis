// Ponte de telemetria para a primeira interface grafica.
// Apenas observa os robos reais criados pelo sistema C++ existente.
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <algorithm>
#include "atr/sistema.hpp"
#include "atr/mapa.hpp"
#include "atr/mqtt.hpp"

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    constexpr int MAX_ROBOS_OPERACAO = 4;
    int quantidade = 3;
    bool usar_mqtt = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--mqtt") {
            usar_mqtt = true;
        } else if (std::string(argv[i]) == "--robos" && i + 1 < argc) {
            quantidade = std::atoi(argv[++i]);
        } else {
            std::cerr << "Uso: atr_telemetria [--robos N] [--mqtt]\n";
            return 2;
        }
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
    std::unique_ptr<atr::MqttPublicador> mqtt;
    if (usar_mqtt) {
        try {
            mqtt = std::make_unique<atr::MqttPublicador>(
                "127.0.0.1", 1883, "atr/telemetria");
        } catch (const std::exception& erro) {
            std::cerr << "MQTT indisponivel: " << erro.what() << '\n';
            return 3;
        }
        mqtt->assinar("atr/comandos");
    }

    const auto processar_comando = [&](const std::string& linha) {
        std::cerr << "COMANDO_RECEBIDO: " << linha << '\n';
        std::istringstream comando(linha);
        std::string operacao;
        int id = -1;
        std::string valor;
        std::string terceiro;
        comando >> operacao >> id >> valor >> terceiro;

        if (operacao == "falha" && id >= 0 &&
            id < static_cast<int>(gestor.quantidade()) &&
            (valor == "on" || valor == "off")) {
            gestor.obter_robo(id)->injetar_falha(valor == "on");
        } else if (operacao == "bateria" && id >= 0 &&
            id < static_cast<int>(gestor.quantidade())) {
            try {
                const int nivel = std::stoi(valor);
                gestor.obter_robo(id)->ajustar_bateria(nivel);
                std::cerr << "BATERIA_APLICADA: robo=" << id
                          << " nivel=" << nivel << '\n';
            } catch (const std::exception&) {
                std::cerr << "Nivel de bateria invalido. Use: bateria <id> 0..100\n";
            }
        } else if (operacao == "caixa") {
            try {
                const int zona = std::stoi(valor);
                const int tamanho = std::stoi(terceiro);
                organizador.adicionar_carga({0, id, zona, tamanho});
            } catch (const std::exception&) {
                std::cerr << "Caixa invalida. Use: caixa <baia> <zona> <tamanho>\n";
            }
        } else if (operacao == "destino" && id >= 0 &&
            id < static_cast<int>(gestor.quantidade())) {
            try {
                const int x = std::stoi(valor);
                const int y = std::stoi(terceiro);
                if (!atr::obter_mapa().caminhavel(x, y)) {
                    throw std::invalid_argument("destino bloqueado");
                }
                gestor.obter_robo(id)->enviar_destino_manual({x, y});
            } catch (const std::exception&) {
                std::cerr << "Destino invalido. Use uma celula livre: destino <id> <x> <y>\n";
            }
        } else if (operacao == "automatico" && id >= 0 &&
            id < static_cast<int>(gestor.quantidade())) {
            gestor.obter_robo(id)->retornar_ao_automatico();
        } else if (operacao == "robo") {
            if (gestor.quantidade() >= MAX_ROBOS_OPERACAO) {
                std::cerr << "Limite de quatro robos atingido.\n";
            } else {
                gestor.disponibilizar_robo(
                    mapa.ponto_inicial(static_cast<int>(gestor.quantidade())));
            }
        } else if (!linha.empty()) {
            std::cerr << "Comando invalido. Use: falha <id> on|off, "
                         "bateria <id> 0..100, caixa <baia> <zona> <tamanho> "
                         "destino <id> <x> <y>, automatico <id> ou robo\n";
        }
    };

    // Uma linha JSON por amostragem; sem dependencia de biblioteca externa.
    // O backend continua responsavel pelo planejamento e pelos movimentos.
    std::string entrada_buffer;
    while (true) {
        pollfd entrada{STDIN_FILENO, POLLIN, 0};
        if (poll(&entrada, 1, 0) > 0 && (entrada.revents & POLLIN)) {
            char buffer[4096];
            const ssize_t lidos = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (lidos > 0) {
                entrada_buffer.append(buffer, static_cast<std::size_t>(lidos));
                std::size_t fim = 0;
                while ((fim = entrada_buffer.find('\n')) != std::string::npos) {
                    processar_comando(entrada_buffer.substr(0, fim));
                    entrada_buffer.erase(0, fim + 1);
                }
            }
        }
        if (mqtt) {
            try {
                for (const auto& comando : mqtt->receber()) {
                    processar_comando(comando);
                }
            } catch (const std::exception& erro) {
                std::cerr << "MQTT perdeu a conexao ao receber; "
                             "tentando reconectar: "
                          << erro.what() << '\n';
                try {
                    mqtt->reconectar();
                } catch (const std::exception& reconexao) {
                    std::cerr << "Reconexao MQTT falhou: "
                              << reconexao.what() << '\n';
                }
            }
        }

        const bool concluida = organizador.cargas_pendentes() == 0 &&
            organizador.carga_concluida(1);
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

        std::map<int, atr::Evento> cargas;
        for (const auto& evento : eventos) {
            if (evento.tarefa_id > 0) {
                cargas[evento.tarefa_id] = evento;
            }
        }

        std::ostringstream linha;
        linha << "{\"tipo\":\"estado\",\"ms\":" << atr::mono_ms()
            << ",\"carga_concluida\":" << (concluida ? "true" : "false")
            << ",\"pendentes\":" << organizador.cargas_pendentes()
            << ",\"cargas\":[";
        bool primeira_carga = true;
        for (const auto& [id, evento] : cargas) {
            const char* estado_carga = "planejada";
            if (evento.tipo == atr::TipoEvento::PegouCarga) {
                estado_carga = "transportando";
            } else if (evento.tipo == atr::TipoEvento::EntregouCarga) {
                estado_carga = "concluida";
            } else if (evento.tipo == atr::TipoEvento::TarefaAbortada) {
                estado_carga = "abortada";
            }
            if (!primeira_carga) linha << ',';
            primeira_carga = false;
            linha << "{\"id\":" << id
                << ",\"baia\":" << evento.baia
                << ",\"zona\":" << evento.zona
                << ",\"robos\":" << evento.mascara_robos
                << ",\"estado\":\"" << estado_carga << "\"}";
        }
        linha << "],\"robos\":[";
        bool primeiro = true;
        for (int id : gestor.ids()) {
            const auto* robo = gestor.obter_robo(id);
            if (!robo) continue;
            const auto status = robo->ler_status();
            if (!primeiro) linha << ',';
            primeiro = false;
            linha << "{\"id\":" << id
                << ",\"x\":" << status.pos.x
                << ",\"y\":" << status.pos.y
                << ",\"bateria\":" << status.bateria
                << ",\"distancia\":" << status.distancia
                << ",\"falha\":" << (status.falha ? "true" : "false")
                << ",\"transportando\":" << ((mascara_transportando & (1 << id)) ? "true" : "false")
                << ",\"estado\":\"" << atr::to_string(status.estado) << "\"}";
        }
        linha << "],\"eventos\":[";
        const std::size_t inicio = eventos.size() > 5 ? eventos.size() - 5 : 0;
        for (std::size_t i = inicio; i < eventos.size(); ++i) {
            if (i > inicio) linha << ',';
            linha << "\"" << atr::to_string(eventos[i].tipo) << "\"";
        }
        linha << "]}";
        const std::string payload = linha.str();
        if (!mqtt) std::cout << payload << std::endl;
        if (mqtt) {
            try {
                mqtt->publicar(payload);
            } catch (const std::exception& erro) {
                std::cerr << "MQTT perdeu a conexao; tentando reconectar: "
                          << erro.what() << '\n';
                try {
                    mqtt->reconectar();
                } catch (const std::exception& reconexao) {
                    std::cerr << "Reconexao MQTT falhou: "
                              << reconexao.what() << '\n';
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
