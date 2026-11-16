
#include "atr/trafego.hpp"
#include "atr/mapa.hpp"

#include <cerrno>
#include <new>
#include <stdexcept>
#include <system_error>
#include <sys/mman.h>

namespace atr {

    namespace {

        void exigir(int rc, const char* nome) {
            if (rc != 0) {
                throw std::system_error(
                    rc, std::generic_category(), nome
                );
            }
        }

        bool id_ok(int id) {
            return id >= 0 && id < MAX_ROBOS;
        }

    }

    ControleTrafego::ControleTrafego() {
        void* bloco = mmap(
            nullptr,
            sizeof(Dados),
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS,
            -1, 0
        );

        if (bloco == MAP_FAILED) {
            throw std::system_error(
                errno, std::generic_category(), "mmap"
            );
        }

        dados_ = static_cast<Dados*>(bloco);
        new (dados_) Dados{};

        pthread_mutexattr_t attr;
        int rc = pthread_mutexattr_init(&attr);

        if (rc == 0) {
            rc = pthread_mutexattr_setpshared(
                &attr, PTHREAD_PROCESS_SHARED
            );
        }
        if (rc == 0) {
            rc = pthread_mutexattr_setrobust(
                &attr, PTHREAD_MUTEX_ROBUST
            );

            if (rc == 0) {
                rc = pthread_mutex_init(&dados_->mutex, &attr);
            }

            pthread_mutexattr_destroy(&attr);
        }

        if (rc != 0) {
            munmap(dados_, sizeof(Dados));
            dados_ = nullptr;
            exigir(rc, "mutex compartilhado");
        }
    }

    ControleTrafego::~ControleTrafego() {
        if (dados_) {
            pthread_mutex_destroy(&dados_->mutex);
            dados_->~Dados();
            munmap(dados_, sizeof(Dados));
        }
    }

    void ControleTrafego::travar() const {
        int rc = pthread_mutex_lock(&dados_->mutex);
        if (rc == EOWNERDEAD) {
            exigir(
                pthread_mutex_consistent(&dados_->mutex),
                "mutex compartilhado inconsistente"
            );
            rc = 0;
        }
        exigir(rc, "lock");
    }

    void ControleTrafego::destravar() const {
        exigir(
            pthread_mutex_unlock(&dados_->mutex), "unlock"
        );
    }

    bool ControleTrafego::registrar(int id, Ponto inicio) {
        const auto& mapa = obter_mapa();

        if (!id_ok(id) ||
            !mapa.caminhavel(inicio.x, inicio.y)) {
            return false;
        }

        travar();

        bool ok = !dados_->ativo[id];
        int corredor = mapa.corredor_em(inicio.x, inicio.y);
        int ocupantes = 0;

        for (int i = 0; i < MAX_ROBOS; ++i) {
            if (!dados_->ativo[i]) continue;

            if (dados_->pos[i] == inicio) {
                ok = false;
            }

            if (corredor >= 0 &&
                mapa.corredor_em(
                    dados_->pos[i].x,
                    dados_->pos[i].y
                ) == corredor) {
                ++ocupantes;
            }
        }

        if (corredor >= 0 &&
            ocupantes >=
            MapaGalpao::capacidade_corredor(corredor)) {
            ok = false;
        }

        if (ok) {
            dados_->ativo[id] = true;
            dados_->pos[id] = inicio;
        }

        destravar();
        return ok;
    }

    void ControleTrafego::remover(int id) {
        if (!id_ok(id)) return;

        travar();
        dados_->ativo[id] = false;
        destravar();
    }

    bool ControleTrafego::posicao(
        int id, Ponto& resultado
    ) const {
        if (!id_ok(id)) return false;

        travar();

        bool existe = dados_->ativo[id];

        if (existe) {
            resultado = dados_->pos[id];
        }

        destravar();
        return existe;
    }

    std::vector<Ponto> ControleTrafego::ocupadas(
    int exceto_id
) const {
    std::vector<Ponto> lista;
    travar();

    for (int i = 0; i < MAX_ROBOS; ++i) {
        if (dados_->ativo[i] && i != exceto_id) {
            lista.push_back(dados_->pos[i]);
        }
    }

    destravar();
    return lista;
}

ResultadoMovimento ControleTrafego::mover(
        int id, Ponto destino
    ) {
        const auto& mapa = obter_mapa();

        if (!id_ok(id) ||
            !mapa.caminhavel(destino.x, destino.y)) {
            return ResultadoMovimento::Invalido;
        }

        travar();

        ResultadoMovimento resposta =
            ResultadoMovimento::Invalido;

        if (dados_->ativo[id] &&
            manhattan(dados_->pos[id], destino) == 1) {

            resposta = ResultadoMovimento::Aprovado;

            int corredor_atual = mapa.corredor_em(
                dados_->pos[id].x,
                dados_->pos[id].y
            );

            int corredor_novo = mapa.corredor_em(
                destino.x, destino.y
            );

            int ocupantes = 0;

            for (int i = 0; i < MAX_ROBOS; ++i) {
                if (!dados_->ativo[i] || i == id) continue;

                if (dados_->pos[i] == destino) {
                    resposta = ResultadoMovimento::Ocupado;
                }

                if (corredor_novo >= 0 &&
                    mapa.corredor_em(
                        dados_->pos[i].x,
                        dados_->pos[i].y
                    ) == corredor_novo) {
                    ++ocupantes;
                }
            }

            if (resposta == ResultadoMovimento::Aprovado &&
                corredor_novo >= 0 &&
                corredor_novo != corredor_atual &&
                ocupantes >= MapaGalpao::capacidade_corredor(
                    corredor_novo
                )) {
                resposta = ResultadoMovimento::CorredorCheio;
            }

            if (resposta == ResultadoMovimento::Aprovado) {
                dados_->pos[id] = destino;
            }
        }

        destravar();
        return resposta;
    }

}
