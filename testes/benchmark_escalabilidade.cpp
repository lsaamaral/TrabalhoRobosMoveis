#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "atr/mapa.hpp"
#include "atr/sistema.hpp"

namespace {
    using Relogio = std::chrono::steady_clock;

    long long milissegundos(Relogio::time_point inicio,
                            Relogio::time_point fim) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            fim - inicio).count();
    }

    struct Resultado {
        int repeticao;
        int robos;
        long long criacao_ms;
        long long online_ms;
        long long latencia_bateria_ms;
    };

    void registrar(std::ostream& saida, const Resultado& resultado) {
        saida << resultado.repeticao << ',' << resultado.robos << ','
              << resultado.criacao_ms << ',' << resultado.online_ms << ','
              << resultado.latencia_bateria_ms << '\n';
    }

    void imprimir_resumo(const std::vector<Resultado>& resultados) {
        std::cerr << "\nresumo,robos,medida,media_ms,min_ms,max_ms\n";
        for (int quantidade = 1; quantidade <= 4; ++quantidade) {
            for (const char* medida : {"criacao_ms", "online_ms",
                                       "latencia_bateria_ms"}) {
                long long soma = 0;
                long long minimo = std::numeric_limits<long long>::max();
                long long maximo = std::numeric_limits<long long>::min();
                int validos = 0;

                for (const Resultado& resultado : resultados) {
                    if (resultado.robos != quantidade) continue;

                    long long valor = 0;
                    if (std::string(medida) == "criacao_ms") {
                        valor = resultado.criacao_ms;
                    } else if (std::string(medida) == "online_ms") {
                        valor = resultado.online_ms;
                    } else {
                        valor = resultado.latencia_bateria_ms;
                    }
                    if (valor < 0) continue;

                    soma += valor;
                    minimo = std::min(minimo, valor);
                    maximo = std::max(maximo, valor);
                    ++validos;
                }

                if (validos == 0) {
                    std::cerr << "resumo," << quantidade << ',' << medida
                              << ",indisponivel,indisponivel,indisponivel\n";
                } else {
                    std::cerr << "resumo," << quantidade << ',' << medida
                              << ',' << static_cast<double>(soma) / validos
                              << ',' << minimo << ',' << maximo << '\n';
                }
            }
        }
    }
}

int main() {
    constexpr int REPETICOES = 5;
    constexpr char CABECALHO[] =
        "repeticao,robos,criacao_ms,online_ms,latencia_bateria_ms\n";
    std::ofstream arquivo_csv("benchmark_escalabilidade.csv");
    if (!arquivo_csv) {
        std::cerr << "Erro: nao foi possivel criar benchmark_escalabilidade.csv\n";
        return 1;
    }

    std::cout << CABECALHO;
    arquivo_csv << CABECALHO;
    std::vector<Resultado> resultados;
    resultados.reserve(REPETICOES * 4);

    for (int repeticao = 1; repeticao <= REPETICOES; ++repeticao) {
        for (int quantidade = 1; quantidade <= 4; ++quantidade) {
            const std::string arquivo =
                "/tmp/atr_benchmark_" + std::to_string(quantidade) + ".csv";
            std::remove(arquivo.c_str());
            atr::ColetorDados coletor(arquivo);

            const auto inicio_criacao = Relogio::now();
            atr::GestorRobosProcessos gestor(coletor);
            const auto& mapa = atr::obter_mapa();
            for (int id = 0; id < quantidade; ++id) {
                gestor.disponibilizar_robo(mapa.ponto_inicial(id));
            }
            const auto fim_criacao = Relogio::now();

            const auto inicio_online = Relogio::now();
            bool todos_online = false;
            while (milissegundos(inicio_online, Relogio::now()) < 2000) {
                todos_online = true;
                for (int id : gestor.ids()) {
                    if (gestor.obter_robo(id)->ler_status().estado ==
                        atr::Estado::Offline) {
                        todos_online = false;
                        break;
                    }
                }
                if (todos_online) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            const auto inicio_comando = Relogio::now();
            gestor.obter_robo(quantidade - 1)->ajustar_bateria(37);
            bool bateria_recebida = false;
            while (milissegundos(inicio_comando, Relogio::now()) < 2000) {
                if (gestor.obter_robo(quantidade - 1)->ler_status().bateria ==
                    37) {
                    bateria_recebida = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            Resultado resultado{
                repeticao,
                quantidade,
                milissegundos(inicio_criacao, fim_criacao),
                todos_online ? milissegundos(inicio_online, Relogio::now()) : -1,
                bateria_recebida
                    ? milissegundos(inicio_comando, Relogio::now())
                    : -1};
            registrar(std::cout, resultado);
            registrar(arquivo_csv, resultado);
            resultados.push_back(resultado);
        }
    }

    imprimir_resumo(resultados);
    std::cerr << "\nCSV salvo em benchmark_escalabilidade.csv\n";
}
