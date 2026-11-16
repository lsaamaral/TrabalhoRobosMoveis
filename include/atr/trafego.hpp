
#pragma once
#include "definicoes.hpp"
#include <pthread.h>
#include <vector>

namespace atr {

    enum class ResultadoMovimento {
        Aprovado, Ocupado, CorredorCheio, Invalido
    };

    // Criar no processo pai, antes do fork().
    class ControleTrafego {
    public:
        ControleTrafego();
        ~ControleTrafego();

        ControleTrafego(const ControleTrafego&) = delete;
        ControleTrafego& operator=(const ControleTrafego&) = delete;

        bool registrar(int id, Ponto inicio);
        void remover(int id);
        ResultadoMovimento mover(int id, Ponto destino);
        bool posicao(int id, Ponto& resultado) const;
    std::vector<Ponto> ocupadas(int exceto_id) const;

    private:
        struct Dados {
            pthread_mutex_t mutex;
            bool ativo[MAX_ROBOS];
            Ponto pos[MAX_ROBOS];
        };

        Dados* dados_ = nullptr;

        void travar() const;
        void destravar() const;
    };

}
