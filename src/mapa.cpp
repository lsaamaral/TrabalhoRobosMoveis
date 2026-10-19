#include "mapa.hpp"
#include <algorithm>
#include <deque>

namespace atr {
    static const int kCapacidadeCorredor[NUM_CAMINHO_PEQ] = {2, 1, 3, 4, 2}; // Quantos robos cabem em cada um dos corredores

    int MapaGalpao::capacidade_corredor(int c) {
        return (c >= 0 && c < NUM_CAMINHO_PEQ) ? kCapacidadeCorredor[c] : 99;
    }

    MapaGalpao::MapaGalpao() : grade_(LARGURA * ALTURA, 0), corredores_(LARGURA * ALTURA, -1) {
        // Funcao auxiliar para desenhar blocos retangulares de paredes (valor 1) ou espacos (0)
        auto retangulo = [&](int x0, int y0, int x1, int y1, uint8_t v) {
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    grade_[y * LARGURA + x] = v;
                }
            }
        };

        // Funcao auxiliar para marcar os corredores restritos com seus respectivos IDs
        auto marcar_corredor = [&](int id, int x0, int y0, int x1, int y1) {
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    corredores_[y * LARGURA + x] = static_cast<int8_t>(id);
                }
            }
        };

        // Construcao das paredes do galpao
        retangulo(0, 0, LARGURA - 1, 0, 1);             // Parede superior
        retangulo(0, ALTURA - 1, LARGURA - 1, ALTURA - 1, 1); // Parede inferior
        retangulo(0, 0, 0, ALTURA - 1, 1);             // Parede esquerda
        retangulo(LARGURA - 1, 0, LARGURA - 1, ALTURA - 1, 1); // Parede direita

        // Divisorias entre as baias (colunas 1 a 7) e zonas de entrega (colunas 24 a 30)
        for (int r : {5, 10, 15}) { 
            retangulo(1, r, 7, r, 1); 
            retangulo(24, r, 30, r, 1); 
        }

        // Parede dupla antes das zonas de entrega, contendo os 4 portoes
        retangulo(22, 1, 23, ALTURA - 2, 1);
        
        retangulo(22, 2, 23, 3, 0);   marcar_corredor(0, 22, 2, 23, 3);    // Corredor 0 (Gate Zona A): cap 2 robos
        retangulo(22, 7, 23, 7, 0);   marcar_corredor(1, 22, 7, 23, 7);    // Corredor 1 (Gate Zona B): cap 1 robô
        retangulo(22, 11, 23, 13, 0); marcar_corredor(2, 22, 11, 23, 13); // Corredor 2 (Gate Zona C): cap 3 robôs
        retangulo(22, 16, 23, 19, 0); marcar_corredor(3, 22, 16, 23, 19); // Corredor 3 (Gate Zona D): cap 4 robôs

        // Sala de recarga (colunas 13 a 19, linhas 1 a 3) com porta de 2 células em (16..17, 4)
        retangulo(12, 1, 12, 4, 1); 
        retangulo(20, 1, 20, 4, 1); 
        retangulo(12, 4, 20, 4, 1);
        retangulo(16, 4, 17, 4, 0);   marcar_corredor(4, 16, 4, 17, 4); // Corredor 4 (Porta recarga): cap 2 robos

        // Bloco central (obstaculo fisico) para forcar o desvio dos robos na zona de transporte central
        retangulo(12, 8, 17, 12, 1);
    }

    Ponto MapaGalpao::carregador(int i) const { 
        return {13 + 2 * (i % NUM_RECARGA), 2}; 
    }

    // Algoritmo de menor caminho
    std::vector<Ponto> MapaGalpao::caminho(Ponto de, Ponto para, int tamanho_grupo) const {
        // Se o ponto de partida ou o destino forem invalidos, retorna caminho vazio
        if (!caminhavel(de.x, de.y) || !caminhavel(para.x, para.y)) return {};

        std::vector<int> anterior(LARGURA * ALTURA, -2);
        std::deque<int> fila;

        int inicio = de.y * LARGURA + de.x;
        int destino = para.y * LARGURA + para.x;

        anterior[inicio] = -1;
        fila.push_back(inicio);

        // Vetores de direcao: direita, esquerda, baixo, cima
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};

        while (!fila.empty() && anterior[destino] == -2) {
            int atual = fila.front();
            fila.pop_front();

            int cx = atual % LARGURA;
            int cy = atual / LARGURA;

            for (int d = 0; d < 4; d++) {
                int nx = cx + dx[d];
                int ny = cy + dy[d];

                // Verifica se a celula vizinha e caminhavel
                if (!caminhavel(nx, ny)) continue;

                // Se for corredor, verifica se ele suporta o tamanho do grupo
                int c = corredor_em(nx, ny);
                if (c >= 0 && !corredor_aceita_carga(c, tamanho_grupo)) continue;

                int proximo_indice = ny * LARGURA + nx;

                // Se ainda nao foi visitado
                if (anterior[proximo_indice] != -2) continue;

                anterior[proximo_indice] = atual;
                fila.push_back(proximo_indice);
            }
        }

        // Se nao encontrou caminho ate o destino
        if (anterior[destino] == -2) return {};

        // Reconstroi o caminho de tras para frente (do destino ate a origem)
        std::vector<Ponto> rota;
        for (int atual = destino; atual != -1; atual = anterior[atual]) {
            rota.push_back({atual % LARGURA, atual / LARGURA});
        }

        // Inverte a rota para que ela va da origem ate o destino correto
        std::reverse(rota.begin(), rota.end());
        return rota;
    }

    // Renderiza o mapa completo e a posicao atual dos robos em formato de texto para o terminal
    std::string MapaGalpao::renderizar(const std::vector<Ponto>& robos) const {
        std::vector<std::string> g(ALTURA, std::string(LARGURA, '.'));

        for (int y = 0; y < ALTURA; y++) {
            for (int x = 0; x < LARGURA; x++) {
                if (grade_[y * LARGURA + x]) {
                    g[y][x] = '#'; // # representa parede
                } else if (corredores_[y * LARGURA + x] >= 0) {
                    g[y][x] = ':'; // : representa corredor/gate restrito
                }
            }
        }

        // Marca as baias de coleta com numeros de '1' a '4'
        for (int i = 0; i < NUM_BAIAS; i++) { 
            Ponto p = pega_baia(i, 0); 
            g[p.y][p.x] = static_cast<char>('1' + i); 
        }

        // Marca as zonas de entrega com as letras 'A', 'B', 'C', 'D'
        for (int i = 0; i < NUM_ZONAS; i++) { 
            Ponto p = solta_zona(i, 0); 
            g[p.y][p.x] = letra_zona(i); 
        }

        // Marca os carregadores de bateria com 'C'
        for (int i = 0; i < NUM_RECARGA; i++) { 
            Ponto p = carregador(i); 
            g[p.y][p.x] = 'C'; 
        }

        // Posiciona os robos no mapa usando letras minusculas (a, b, c...)
        for (size_t i = 0; i < robos.size(); i++) {
            if (robos[i].x >= 0 && robos[i].x < LARGURA && robos[i].y >= 0 && robos[i].y < ALTURA) {
                g[robos[i].y][robos[i].x] = static_cast<char>('a' + i % 26);
            }
        }

        // Constroi a string final concatenando todas as linhas com quebra de linha (\n)
        std::string saida;
        for (auto& linha : g) {
            saida += linha + "\n";
        }
        return saida;
    }

    // Instancia unica global do mapa
    const MapaGalpao& obter_mapa() {
        static const MapaGalpao instancia_unica;
        return instancia_unica;
    }
}