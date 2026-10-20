#include <iostream>
#include <vector>
#include <thread>
#include "atr/comum.hpp"
#include "teste_util.hpp"

int main() {
    std::cout << "[TESTE] Iniciando testes robustos da FilaBloqueante...\n";

    // Retirar de uma fila vazia com timeout de 50 ms deve retornar false
    {
        atr::FilaBloqueante<int> fila_vazia;
        int valor_lixo = 999;
        bool resultado = fila_vazia.retirar(valor_lixo, 50);
        
        VERIFICA(resultado == false, "Erro: Retirar de fila vazia com timeout deveria retornar false");
        std::cout << "[SUCESSO] Comportamento da fila vazia com timeout validado (retornou false corretamente).\n";
    }

    // Teste principal de Produtor / Consumidor com concorrencia
    atr::FilaBloqueante<int> fila;
    const int total_itens = 1000;

    std::thread produtor([&]() {
        for (int i = 0; i < total_itens; i++) {
            fila.inserir(i);
        }
        std::cout << "[PRODUTOR] Todos os " << total_itens << " itens foram inseridos com sucesso.\n";
    });

    std::vector<int> consumidos;
    std::thread consumidor([&]() {
        int item = 0;
        for (int i = 0; i < total_itens; i++) {
            bool sucesso = fila.retirar(item, 2000);
            VERIFICA(sucesso, "Erro: Timeout! A fila demorou demais para responder ou travou.");
            consumidos.push_back(item);
        }
        std::cout << "[CONSUMIDOR] Todos os " << total_itens << " itens foram retirados com sucesso.\n";
    });

    produtor.join();
    consumidor.join();

    VERIFICA(consumidos.size() == static_cast<size_t>(total_itens), "Erro: A quantidade de itens consumidos nao confere");

    for (int i = 0; i < total_itens; i++) {
        VERIFICA(consumidos[i] == i, "Erro: A ordem dos itens foi corrompida ou perdida!");
    }

    std::cout << "[SUCESSO] Todos os asserts da fila passaram 100%.\n";
    return 0;
}