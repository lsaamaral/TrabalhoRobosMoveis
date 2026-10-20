#include <iostream>
#include <cassert>
#include "atr/mapa.hpp"

int main() {
    std::cout << "[TESTE] Iniciando testes do MapaGalpao...\n";

    const atr::MapaGalpao& mapa = atr::obter_mapa();

    atr::Ponto baia = mapa.pega_baia(0, 0);
    
    atr::Ponto zona = mapa.solta_zona(0, 0);

    std::cout << "[TESTE] Baia de origem: (" << baia.x << ", " << baia.y << ")\n";
    std::cout << "[TESTE] Zona de destino: (" << zona.x << ", " << zona.y << ")\n";

    std::vector<atr::Ponto> rota = mapa.caminho(baia, zona, 1);

    assert(!rota.empty() && "Erro: O caminho entre a baia e a zona nao deveria ser vazio");
    std::cout << "[SUCESSO] Caminho encontrado com " << rota.size() << " passos.\n";

    for (const auto& ponto : rota) {
        bool livre = mapa.caminhavel(ponto.x, ponto.y);
        
        assert(livre && "Erro: O caminho passa por uma celula de parede proibida");
    }
    std::cout << "[SUCESSO] Verificacao concluida: NENHUM passo do caminho passa por paredes.\n";

    std::cout << "[TESTE] Todos os asserts passaram com sucesso, o ambiente e o mapa estao perfeitos.\n";
    return 0;
}