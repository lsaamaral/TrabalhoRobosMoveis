#include <iostream>
#include "atr/comum.hpp"
#include "teste_util.hpp"
#include "atr/definicoes.hpp"

int main() {
    std::cout << "[TESTE] Iniciando testes do TemporizadorPeriodico\n";

    const int periodo_ms = 10;
    const int total_ativacoes = 50;
    atr::TemporizadorPeriodico timer(periodo_ms);

    int64_t inicio_ms = atr::mono_ms();

    for (int i = 0; i < total_ativacoes; i++) {
        timer.esperar();
    }

    int64_t fim_ms = atr::mono_ms();
    int64_t tempo_total_ms = fim_ms - inicio_ms;

    std::cout << "[TIMER] Tempo total para " << total_ativacoes << " ciclos de " << periodo_ms 
              << "ms: " << tempo_total_ms << " ms\n";

    std::string relatorio = timer.relatorio();
    std::cout << "[TIMER] Relatorio gerado: " << relatorio << "\n";

    VERIFICA(!relatorio.empty(), "Erro: O relatorio do temporizador nao pode estar vazio");
    VERIFICA(tempo_total_ms >= 490 && tempo_total_ms <= 560, "Erro: O tempo total de execucao dos ciclos fugiu da faixa esperada");

    std::cout << "[SUCESSO] Todos os asserts do temporizador passaram\n";
    return 0;
}