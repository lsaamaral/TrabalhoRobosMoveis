#pragma once
#include <time.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include "definicoes.hpp"

namespace atr {
    /// @brief produtor consumidor
    /// @tparam T qualquer tipo de dado
    template <class T>
    class FilaBloqueante {
        public: 

        /// @brief inserir dado na fila de forma segura
        /// @param dado informacao que vai ser inserida na fila
        void inserir(T dado){
            { std::lock_guard<std::mutex> l(mutex_); fila_.push_back(std::move(dado));}
            cond_var_.notify_one();
        }

        /// @brief retira item da fila de forma segura
        /// @param saida lugar em que o item que vai ser retirado sera armazenado
        /// @param timeout_ms tempo maximo de espera se a fila estiver vazia
        /// @return se conseguiu tirar ou nao, n ou f
        bool retirar(T& saida, int timeout_ms) {
            std::unique_lock<std::mutex> l(mutex_);
            if(!cond_var_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return !fila_.empty(); })) {
                return false;
            }
            saida = std::move(fila_.front());
            fila_.pop_front();
            return true;

        }

        private:
            std::mutex mutex_;
            std::condition_variable cond_var_;
            std::deque<T> fila_;
    };

    /// @brief temporizador e jitter
    class TemporizadorPeriodico {
        public:
            explicit TemporizadorPeriodico(int periodo_ms) : periodo_ns_(periodo_ms * 1000000LL) {
                clock_gettime(CLOCK_MONOTONIC, &proximo_);
            }

            void esperar() {
                proximo_.tv_nsec += periodo_ns_;
                while (proximo_.tv_nsec >= 1000000000L) { 
                    proximo_.tv_nsec -= 1000000000L; 
                    proximo_.tv_sec++; 
                }
                while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &proximo_, nullptr) == EINTR) {}
                
                timespec agora; 
                clock_gettime(CLOCK_MONOTONIC, &agora);
                
                long long atraso_us = ((agora.tv_sec - proximo_.tv_sec) * 1000000000LL + (agora.tv_nsec - proximo_.tv_nsec)) / 1000;
                if (atraso_us < 0) {
                    atraso_us = 0;
                }

                max_us_ = std::max(max_us_, atraso_us);
                soma_us_ += atraso_us; 
                contador_++;
                
                if (atraso_us > periodo_ns_ / 1000) {
                    estouros_++;
                    proximo_ = agora;
                }
            }

            std::string relatorio() const {
                char b[160];
                snprintf(b, sizeof b, "ativacoes=%lld jitter_medio=%lldus jitter_max=%lldus estouros=%lld",
                        contador_, contador_ ? soma_us_ / contador_ : 0, max_us_, estouros_);
                return b;
            }

        private:
            long long periodo_ns_;
            timespec proximo_{};
            long long max_us_ = 0, soma_us_ = 0, contador_ = 0, estouros_ = 0;
    };
}