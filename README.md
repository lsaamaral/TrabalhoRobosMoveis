# Trabalho de Robos Moveis - Etapa 1

Implementação em C++17 de um sistema centralizado para robôs móveis autônomos. Na Etapa 1, o Gestor cria um processo Linux separado para cada robô com `fork()`. Cada processo inicia threads de navegação e status e troca dados com o Organizador por pipes POSIX.

## Requisitos

- Linux
- Compilador C++ com suporte a C++17
- CMake
- pthreads

## Compilar e executar

Todos os caminhos são referentes ao diretório raiz do projeto:

```bash
cmake -S . -B build
cmake --build build -j
./build/atr
```

O executável cria um processo de robô, envia uma carga pelo Organizador e aguarda a entrega. Para executar toda a verificação:

```bash
ctest --test-dir build --output-on-failure
```

## Arquitetura da Etapa 1

```mermaid
flowchart TB
    classDef gerencia fill:#4A90E2,stroke:#357ABD,color:#fff,font-weight:bold;
    classDef processo fill:#F5A623,stroke:#D48806,color:#fff,font-weight:bold;
    classDef thread fill:#7ED321,stroke:#62A814,color:#fff;
    classDef infra fill:#9B59B6,stroke:#8E44AD,color:#fff;
    classDef dados fill:#1ABC9C,stroke:#16A085,color:#fff;
    classDef futuro fill:#95A5A6,stroke:#7F8C8D,color:#fff,stroke-dasharray: 5 5;

    subgraph Controle["Controle e Coordenacao"]
        G["Gestor de Robos"]
        O["Organizador de Logistica"]
    end
    class G,O gerencia;

    subgraph Robo1["Processo Robo 1"]
        P1["Processo Core 1"]
        N1["Thread Navegacao 1"]
        S1["Thread Status 1"]
        CTX1["ContextoRobo 1"]
        H1["Hardware Falso 1"]

        P1 --> N1
        P1 --> S1
        N1 <-->|mutex| CTX1
        S1 <-->|mutex| CTX1
        N1 -->|setpoint| H1
        S1 -->|leitura| H1
    end
    class P1 processo;
    class N1,S1 thread;
    class CTX1,H1 infra;

    subgraph Robo2["Processo Robo 2"]
        P2["Processo Core 2"]
        N2["Thread Navegacao 2"]
        S2["Thread Status 2"]
        CTX2["ContextoRobo 2"]
        H2["Hardware Falso 2"]

        P2 --> N2
        P2 --> S2
        N2 <-->|mutex| CTX2
        S2 <-->|mutex| CTX2
        N2 -->|setpoint| H2
        S2 -->|leitura| H2
    end
    class P2 processo;
    class N2,S2 thread;
    class CTX2,H2 infra;

    subgraph Dados["Mapa, Coletor e Persistencia"]
        M["Mapa do Galpao"]
        C["Coletor de Dados"]
        D[("eventos.csv")]
    end
    class M infra;
    class C,D dados;

    G -->|fork| P1
    G -->|fork| P2
    G -->|IDs de robos disponiveis| O

    O ==>|ComandoRoboIPC por pipe| P1
    O ==>|ComandoRoboIPC por pipe| P2
    P1 ==>|StatusRoboIPC por pipe| O
    P2 ==>|StatusRoboIPC por pipe| O

    M --> N1
    M --> N2

    G -->|eventos de robos| C
    O -->|eventos de tarefas e cargas| C
    C -->|CSV com timestamp| D

    F["FilaBloqueante<br/>mutex + condition_variable"]
    T["TemporizadorPeriodico<br/>CLOCK_MONOTONIC"]
    class F,T infra;
    F -.-> O
    T -.-> N1
    T -.-> S1
    T -.-> N2
    T -.-> S2

    MQTT["MQTT e Tela de Operacao<br/>Etapa 2"]
    class MQTT futuro;
    O -.->|futuro| MQTT
    C -.->|futuro| MQTT
```

`ContextoRobo::mtx` protege os dados compartilhados dentro de cada processo. A thread de navegação possui periodo de 50 ms e a thread de status possui período de 100 ms.
`HardwareFalso` possui uma thread própria, executada a cada 100 ms, e protege suas entradas e saidas com outro mutex. Os pipes carregam estruturas binárias de tamanho fixo, comandos descem do Organizador para o filho e os status sobem para o processo pai.
