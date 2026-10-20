#include <iostream>
#include "atr/mapa.hpp"
#include "teste_util.hpp"

int main() {
    std::cout << "[TESTE] Iniciando testes robustos do MapaGalpao\n";

    const atr::MapaGalpao& mapa = atr::obter_mapa();

    // Caminho da baia 0 para zona A
    atr::Ponto baia0 = mapa.pega_baia(0, 0);
    atr::Ponto zonaA = mapa.solta_zona(0, 0);
    std::vector<atr::Ponto> rota1 = mapa.caminho(baia0, zonaA, 1);
    
    VERIFICA(!rota1.empty(), "Erro: O caminho entre a baia e a zona A nao deveria ser vazio!");
    for (const auto& ponto : rota1) {
        VERIFICA(mapa.caminhavel(ponto.x, ponto.y), "Erro: O caminho passa por uma celula de parede!");
    }
    std::cout << "[SUCESSO] Caminho baia 0 -> zona A (tamanho 1) validado\n";

    // Tamanho 4 para a zona A deve devolver caminho vazio (gate A comporta no maximo 2)
    std::vector<atr::Ponto> rota_tamanho4_A = mapa.caminho(baia0, zonaA, 4);
    VERIFICA(rota_tamanho4_A.empty(), "Erro: Grupo tamanho 4 deveria ser bloqueado pelo gate da Zona A (cap 2)");
    std::cout << "[SUCESSO] Bloqueio correto: Zona A rejeitou tamanho 4.\n";

    // Tamanho 4 para a zona D deve ter caminho (gate D comporta 4 robos)
    atr::Ponto zonaD = mapa.solta_zona(3, 0);
    std::vector<atr::Ponto> rota_tamanho4_D = mapa.caminho(baia0, zonaD, 4);
    VERIFICA(!rota_tamanho4_D.empty(), "Erro: Zona D deveria aceitar grupo de tamanho 4 (cap 4)");
    std::cout << "[SUCESSO] Caminho para Zona D com grupo de tamanho 4 encontrado com sucesso.\n";

    // Os 4 carregadores devem ser alcançáveis a partir do ponto_inicial
    atr::Ponto origem = mapa.ponto_inicial(0);
    for (int i = 0; i < 4; i++) {
        atr::Ponto carga = mapa.carregador(i);
        std::vector<atr::Ponto> rota_carga = mapa.caminho(origem, carga, 1);
        VERIFICA(!rota_carga.empty(), "Erro: O carregador deve ser alcancavel a partir do ponto inicial");
    }
    std::cout << "[SUCESSO] Todos os 4 carregadores sao alcancaveis a partir do ponto inicial 0.\n";

    // ponto_inicial(n) deve ser caminhavel para n de 0 a 13
    for (int n = 0; n <= 13; n++) {
        atr::Ponto p_inicial = mapa.ponto_inicial(n);
        VERIFICA(mapa.caminhavel(p_inicial.x, p_inicial.y), "Erro: O ponto inicial do robo deve ser uma celula livre");
    }
    std::cout << "[SUCESSO] Todos os pontos iniciais (0 a 13) sao perfeitamente caminhaveis.\n";

    std::cout << "[TESTE] Todos os asserts do mapa passaram.\n";
    return 0;
}