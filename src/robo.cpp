#include "atr/robo.hpp"
#include "atr/comum.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>

namespace atr {

    void loop_navegacao(InterfaceHardware& hw, ContextoRobo& ctx, std::atomic<bool>& parar) {
        TemporizadorPeriodico timer(T_NAV_MS);
        const MapaGalpao& mapa = obter_mapa();

        while (!parar) {
            timer.esperar();
            const EntradasHw entradas = hw.ler_entradas();
            SaidasHw saidas{entradas.posicao_atual, false};

            {
                std::lock_guard<std::mutex> lock(ctx.mtx);
                saidas.acoplar_e_erguer = ctx.acoplar &&
                    ctx.estado_nav == Estado::Acoplando;

                Ponto destino = entradas.posicao_atual;
                if (ctx.pedir_recarga) {
                    destino = ctx.destino_recarga;
                    ctx.estado_nav = Estado::IndoRecarga;
                } else if (ctx.tem_destino) {
                    destino = ctx.destino;
                    if (entradas.posicao_atual == destino) {
                        ctx.estado_nav = saidas.acoplar_e_erguer
                            ? Estado::Acoplando : Estado::Disponivel;
                    } else {
                        ctx.estado_nav = Estado::IndoParaBaia;
                    }
                }

                if (!entradas.falha && entradas.posicao_atual != destino) {
                    const auto rota = mapa.caminho(entradas.posicao_atual, destino);
                    if (rota.size() > 1) {
                        saidas.setpoint = rota[1];
                    }
                }
            }

            hw.escrever_saidas(saidas);
        }
    }

    void loop_status(InterfaceHardware& hw, ContextoRobo& ctx, std::atomic<bool>& parar) {
        TemporizadorPeriodico timer(T_STATUS_MS);
        const MapaGalpao& mapa = obter_mapa();

        Ponto ultima_pos = {-1, -1};
        int distancia_total_percorrida = 0;
        bool primeira_leitura = true;

        while (!parar) {
            timer.esperar();

            // Le as entradas do hardware
            EntradasHw entradas = hw.ler_entradas();

            // Inicializa a ultima posicao na primeira execucao
            if (primeira_leitura) {
                ultima_pos = entradas.posicao_atual;
                primeira_leitura = false;
            }

            // Acumula a distancia percorrida se a posição mudou
            if (entradas.posicao_atual != ultima_pos) {
                distancia_total_percorrida += manhattan(entradas.posicao_atual, ultima_pos);
                ultima_pos = entradas.posicao_atual;
            }

            // Verifica se o robo esta estacionado em cima de um dos carregadores
            bool esta_no_carregador = false;
            int indice_carregador_mais_proximo = 0;
            int menor_distancia = 100000;

            for (int i = 0; i < NUM_RECARGA; i++) {
                Ponto c_pos = mapa.carregador(i);
                int dist = manhattan(entradas.posicao_atual, c_pos);
                if (dist < menor_distancia) {
                    menor_distancia = dist;
                    indice_carregador_mais_proximo = i;
                }
                if (entradas.posicao_atual == c_pos) {
                    esta_no_carregador = true;
                }
            }

            bool recarregando_local = false;
            Estado estado_nav_copia = Estado::Disponivel;
            std::vector<std::string> msgs;

            {
                std::lock_guard<std::mutex> lock(ctx.mtx);

                // Atualiza falha
                if (ctx.falha != entradas.falha) {
                    ctx.falha = entradas.falha;
                    msgs.push_back("Alerta: Mudanca no estado de falha do robo -> " + std::string(ctx.falha ? "ATIVO" : "NORMAL"));
                }

                // Verifica se esta carregando
                recarregando_local = esta_no_carregador && (entradas.bateria < BAT_CHEIA);

                estado_nav_copia = ctx.estado_nav;

                // Decisao de recarga
                bool condicao_precisa_recarregar = (entradas.bateria <= BAT_BAIXA) || 
                    ((ctx.estado_nav == Estado::Disponivel) && (entradas.bateria < BAT_MIN_TAREFA));

                if (condicao_precisa_recarregar && !entradas.carga_atracada && !ctx.pedir_recarga) {
                    ctx.pedir_recarga = true;
                    ctx.destino_recarga = mapa.carregador(indice_carregador_mais_proximo);
                    msgs.push_back("Bateria baixa (" + std::to_string(entradas.bateria) + "%). Solicitando recarga no carregador mais proximo.");
                }

                if (ctx.pedir_recarga && (entradas.bateria >= BAT_OK) && esta_no_carregador) {
                    ctx.pedir_recarga = false;
                    msgs.push_back("Bateria recuperada (" + std::to_string(entradas.bateria) + "%). Recarga concluida.");
                }

                // Determinacao do Estado final aplicando a prioridade
                // defeito > recarregando > indo recarga > manual > estado_nav
                Estado estado_final = estado_nav_copia;
                if (entradas.falha) {
                    estado_final = Estado::Defeito;
                } else if (recarregando_local) {
                    estado_final = Estado::Recarregando;
                } else if (ctx.pedir_recarga) {
                    estado_final = Estado::IndoRecarga;
                }

                ctx.status.estado = estado_final;
                ctx.status.pos = entradas.posicao_atual;
                ctx.status.bateria = entradas.bateria;
                ctx.status.falha = entradas.falha;
                ctx.status.distancia = distancia_total_percorrida;
            }
            // log fora do lock
            for (const auto& m : msgs) {
                logf("STATUS", m);
            }
        }
    }

    Robo::Robo(int id, Ponto pos_inicial) : id_(id), hardware_(pos_inicial) {
        contexto_.status.pos = pos_inicial;
        thread_navegacao_ = std::thread(&loop_navegacao, std::ref(hardware_), std::ref(contexto_), std::ref(parar_));
        thread_status_ = std::thread(&loop_status, std::ref(hardware_), std::ref(contexto_), std::ref(parar_));
    }

    Robo::~Robo() {
        parar_ = true;
        if (thread_navegacao_.joinable()) {
            thread_navegacao_.join();
        }
        if (thread_status_.joinable()) {
            thread_status_.join();
        }
    }

    void Robo::enviar_destino(Ponto destino) {
        std::lock_guard<std::mutex> lock(contexto_.mtx);
        contexto_.destino = destino;
        contexto_.tem_destino = true;
        if (contexto_.estado_nav != Estado::Acoplando) {
            contexto_.estado_nav = Estado::IndoParaBaia;
        }
    }

    void Robo::comandar_acoplamento(bool acoplar) {
        std::lock_guard<std::mutex> lock(contexto_.mtx);
        contexto_.acoplar = acoplar;
        if (acoplar) {
            contexto_.estado_nav = Estado::Acoplando;
        }
    }

    StatusRobo Robo::ler_status() const {
        std::lock_guard<std::mutex> lock(contexto_.mtx);
        return contexto_.status;
    }
}