// Estruturas de tempo, variaveis, tipos e etc
#pragma once
#include <unistd.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace atr {

    constexpr int MAX_ROBOS       = 16;
    constexpr int MAX_GRUPOS      = 4;
    constexpr int NUM_BAIAS       = 4;
    constexpr int NUM_ZONAS       = 4;
    constexpr int NUM_CAMINHO_PEQ = 5;
    constexpr int NUM_RECARGA     = 4;
    constexpr int TAM_MEM_CIRC    = 512; // Guardar ultimos 512 eventos

    // Periodo tarefas periodicas
    constexpr int T_NAV_MS         = 50;
    constexpr int T_STATUS_MS      = 100;
    constexpr int T_ORGANIZADOR_MS = 200;
    constexpr int T_SIMULADOR_MS   = 100;

    // Status da bateria
    constexpr int BAT_BAIXA      = 20;
    constexpr int BAT_OK         = 95;
    constexpr int BAT_MIN_TAREFA = 35;
    constexpr int BAT_CHEIA      = 100;

    constexpr const char* NOME_MEM_COMP  = "/atr_fleet_shm";
    constexpr const char* FILA_RESPOSTAS = "/atr_reply";
    inline std::string nome_fila_comando(int id) {
        return "/atr_cmd_" + std::to_string(id);
    }

    // Estados
    enum class Estado : int32_t {
        Offline, Disponivel, IndoParaBaia, AguardandoGrupo, Acoplando, OperandoCarga,
        AguardandoPassagem, IndoRecarga, Recarregando, Manual, Defeito
    };
    inline const char* to_string(Estado e) {
        switch (e) {
            case Estado::Offline:            return "offline";
            case Estado::Disponivel:         return "disponivel";
            case Estado::IndoParaBaia:       return "indo_para_baia";
            case Estado::AguardandoGrupo:    return "aguardando_grupo";
            case Estado::Acoplando:          return "acoplando";
            case Estado::OperandoCarga:      return "operando_carga";
            case Estado::AguardandoPassagem: return "aguardando_passagem";
            case Estado::IndoRecarga:        return "indo_recarga";
            case Estado::Recarregando:       return "recarregando";
            case Estado::Manual:             return "manual";
            case Estado::Defeito:            return "defeito";
        }
        return "?";
    }

    enum class Modo : int32_t {
        Automatico, Manual
    };

    // Posicao robo
    struct Ponto {
        int32_t x = 0, y = 0;
        bool operator==(const Ponto& o) const { return x == o.x && y == o.y; }
        bool operator!=(const Ponto& o) const { return !(*this == o); }
    };
    inline int manhattan(Ponto a, Ponto b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

    // Eventos do Coletor de Dados
    enum class TipoEvento : int32_t {
        RoboOnline, RoboOffline, Falha, FalhaResolvida, RecargaIniciada, RecargaConcluida,
        IndoRecarga, TarefaCriada, TarefaAbortada, PegouCarga, EntregouCarga, ModoMudou, ComandoManual
    };
    inline const char* to_string(TipoEvento t) {
        static const char* n[] = {
            "ROBO_ONLINE", "ROBO_OFFLINE", "FALHA", "FALHA_RESOLVIDA",
            "RECARGA_INICIADA", "RECARGA_CONCLUIDA", "INDO_RECARGA",
            "TAREFA_CRIADA", "TAREFA_ABORTADA", "PEGOU_CARGA",
            "ENTREGOU_CARGA", "MODO", "COMANDO_MANUAL"
        };
        return n[static_cast<int>(t)];
    }
    struct Evento {
    int64_t ts_ms;               // timestamp de quando o evento aconteceu
    int32_t robo_id;             // -1 = evento do sistema, qual robo fez
    TipoEvento tipo;
    int32_t tarefa_id, baia, zona, caixa_id;   // -1 = n/a (baia e zona 0-based)
    int32_t mascara_robos;       // bitmask dos robos envolvidos
    char    texto[96];
    };

    // Organizar tempo
    inline int64_t agora_ms() { // tempo no mundo real
        timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    }
    inline int64_t mono_ms() { // tempo desde que o sistema foi iniciado
        timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    }
    inline std::string ts_iso(int64_t ms) { // transformar ms em texto padrao
        time_t s = static_cast<time_t>(ms / 1000);
        tm t; localtime_r(&s, &t);
        char b[40], o[64];
        strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &t);
        snprintf(o, sizeof o, "%s.%03d", b, static_cast<int>(ms % 1000));
        return o;
    }
    inline void logf(const char* tag, const std::string& msg) {
        std::ostringstream o;
        o << ts_iso(agora_ms()) << " [" << std::setw(5) << getpid() << "] "
        << std::left << std::setw(9) << tag << " " << msg << "\n";
        std::clog << o.str();
    }

    inline char letra_zona(int z) {
        switch (z) {
            case 0:  return 'A';
            case 1:  return 'B';
            case 2:  return 'C';
            case 3:  return 'D';
            default: return '?';
        }
    }

}
 