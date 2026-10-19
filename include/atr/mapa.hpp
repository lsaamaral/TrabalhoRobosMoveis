// mapa dO galpao em 2d. Baias, zonas, lugar de recarga e passagens pequenas
//   x ->  0         7 8                 21 22 23 24      30 31
//  y=0                                                  
//         | baias 1..4 |   zona de transporte   | gates | zonas A..D
//         | (cols 1-7) |  (+ sala de recarga,   | (cap) |
//         |            |    bloco central)      |       |
//
#pragma once
#include <string>
#include <vector>
#include "definicoes.hpp"

namespace atr {
    class MapaGalpao {
        public:
            static constexpr int LARGURA = 32,  ALTURA = 21;

            MapaGalpao();

            /// @brief Verificar se a posicao dada esta livre
            /// @param x coordenada x
            /// @param y coordenada y
            /// @return Livre ou nao, v ou f
            bool caminhavel(int x, int y) const {
                return x >= 0 && y >= 0 && x < LARGURA && y < ALTURA && grade_[y * LARGURA + x] == 0;
            }

            /// @brief saber se tem um corredor, qual e o id dele ou se nao tem 
            /// @param x coordenada x
            /// @param y coordenada y
            /// @return Retorna ID do corredor da coordenada passada ou -1 se nao for um corredor
            int corredor_em(int x, int y) const {
                return (x < 0 || y < 0 || x >= LARGURA || y >= ALTURA) ? -1 : corredores_[y * LARGURA + x];
            }

            /// @brief Saber quantos robos cabem nesse corredor
            /// @param c corredor correspondente
            /// @return Retorna quantidade maxima de robos naquele corredor
            static int capacidade_corredor(int c);

            /// @brief coordenada baia
            /// @param baia qual baia e
            /// @param idx posicao no grupo (0..3)
            /// @return Retorna coordenada x y da baia que o robo tem que ir para pegar uma carga na baia indicada
            Ponto pega_baia(int baia, int idx) const {
                return {7, 1 + 5 * baia + idx};
            }

            /// @brief coordenada zona
            /// @param zona qual zona e
            /// @param idx posicao no grupo (0..3)
            /// @return Retorna a coordenada para o robo descarregar o item na zona destino
            Ponto solta_zona(int zona, int idx) const {
                return {27, 1 + 5 * zona + idx};
            }

            /// @brief Saber posicao da base de recarga
            /// @param i qual bateria e
            /// @return Retorna coordenada da base de recarga daquela bateria
            Ponto carregador(int i) const;

            /// @brief responsavel por fazer com que um robo nao nasca em cima do outro
            /// @param n ID do robo
            /// @return Retorna a coordenada em que o robo nasce no mapa
            Ponto ponto_inicial(int n) const {
                return {8 + (n % 14), 19};
            }

            /// @brief saber se a carga cabe no corredor
            /// @param corredor qual corredor e
            /// @param tamanho tamanho da carga
            /// @return Retorna se a carga cabe naquele corredor - v ou f
            bool corredor_aceita_carga(int corredor, int tamanho) const { 
                return capacidade_corredor(corredor) >= tamanho;
            }

            /// @brief gps do robo
            /// @param de onde o robo esta agora 
            /// @param para onde o robo precisa ir
            /// @param tamanho_grupo quantos robos fazem parte dessa tarefa
            /// @return lista de coordenadas com a rota que o robo precisa seguir 
            std::vector<Ponto> caminho(Ponto de, Ponto para, int tamanho_grupo = 1) const;

            /// @brief mostrar o mapa no terminal
            /// @param robos 
            /// @return 
            std::string renderizar(const std::vector<Ponto>& robos = {}) const;
        private:
            std::vector<uint8_t> grade_; // guarda o desenho do galpao, onde tem parede ou nao
            std::vector<uint8_t> corredores_; // posicao dos corredores
    };

    const MapaGalpao& obter_mapa();
}