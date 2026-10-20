# Trabalho de Robos Moveis - Etapa 1

Implementacao em C++17 de um sistema centralizado para robos moveis
autonomos. Na Etapa 1, o Gestor cria um processo Linux separado para cada
robo com `fork()`. Cada processo inicia threads de navegacao e status e
troca dados com o Organizador por pipes POSIX. MQTT, telas e simulacao
grafica ficam para a Etapa 2.

## Requisitos

- Linux
- compilador C++ com suporte a C++17
- CMake 3.16 ou superior
- pthreads (localizado automaticamente pelo CMake)

## Compilar e executar

Todos os caminhos sao relativos ao diretorio raiz do projeto:

```bash
cmake -S . -B build
cmake --build build -j
./build/atr
```

O executavel cria um processo de robo, envia uma carga pelo Organizador e
aguarda a entrega. Para executar toda a verificacao:

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

    subgraph Camada_Controle [Controle & Coordenação]
        G[Gestor de Robôs]
        O[Organizador de Logística]
    end
    class G,O gerencia;

    subgraph Robô_1 [Processo Robô 1]
        P1[Processo Core 1]
        N1[Thread Navegação 1]
        S1[Thread Status 1]
        
        P1 --> N1
        P1 --> S1
    end
    class P1 processo;
    class N1,S1 thread;

    subgraph Robô_2 [Processo Robô 2]
        P2[Processo Core 2]
        N2[Thread Navegação 2]
        S2[Thread Status 2]
        
        P2 --> N2
        P2 --> S2
    end
    class P2 processo;
    class N2,S2 thread;

    subgraph Infra [Mundo Físico & Dados]
        H1[Hardware Falso]
        M[Mapa Galpão]
        C[Coletor de Dados]
        D[(Disco)]
    end
    class H1,M infra;
    class C,D dados;

    G -->|fork| P1
    G -->|fork| P2

    O ====|ComandoRoboIPC via Pipe| P1
    O ====|ComandoRoboIPC via Pipe| P2
    P1 ====|StatusRoboIPC via Pipe| O
    P2 ====|StatusRoboIPC via Pipe| O

    M --> N1
    N1 --> H1
    S1 --> H1
    
    N2 -.-> H1
    S2 -.-> H1

    C -->|CSV com timestamp| D

```

`ContextoRobo::mtx` protege os dados compartilhados dentro de cada processo.
A thread de navegacao
possui periodo de 50 ms e a thread de status possui periodo de 100 ms.
`HardwareFalso` possui uma thread propria, executada a cada 100 ms, e
protege suas entradas e saidas com outro mutex. Os pipes carregam estruturas
binarias de tamanho fixo: comandos descem do Organizador para o filho e
snapshots de status sobem para o processo pai.

## O que ja esta implementado

- mapa 2D com baias, zonas, carregadores e corredores restritos
- busca de menor caminho respeitando a capacidade dos corredores
- fila produtor-consumidor com `mutex`, `condition_variable` e timeout
- temporizador periodico com medicao de jitter
- hardware falso com movimento, bateria, acoplamento e injecao de falha
- processo Linux separado para cada robo com `fork()`
- duas threads em cada processo: navegacao e status
- IPC bidirecional por pipes POSIX
- Gestor, Organizador e Coletor funcionais
- grupo de 1 a 4 robos para uma carga
- testes unitarios e testes de integracao entre processos

## Limites da Etapa 1

Esta etapa implementa os processos, threads, mapa, hardware simulado,
Gestor, Organizador, Coletor e IPC. MQTT, tela de operacao e interface
grafica ainda nao existem porque foram reservados para a Etapa 2.
