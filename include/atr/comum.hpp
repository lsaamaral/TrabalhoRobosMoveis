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
}