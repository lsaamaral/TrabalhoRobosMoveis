# Trabalho de Robos Moveis - Etapas 1 e 2

Implementação em C++17 de um sistema centralizado para robôs móveis autônomos. O Gestor cria um processo Linux separado para cada robô com `fork()`. Cada processo inicia threads de navegação e status e troca dados com o Organizador por pipes POSIX. A interface Pygame acompanha a telemetria, envia comandos operacionais e pode usar MQTT.

## Requisitos

- Linux
- Compilador C++ com suporte a C++17
- CMake
- pthreads
- Python 3 e Pygame para o simulador gráfico

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

Para medir criação de processos, tempo de disponibilização e latência de comando
para 1 a 4 robôs:

```bash
./build/benchmark_escalabilidade
```

O resultado é impresso em CSV com cinco repetições e as colunas `repeticao`,
`robos`, `criacao_ms`, `online_ms` e `latencia_bateria_ms`.
As linhas brutas também são salvas em `benchmark_escalabilidade.csv`. No
terminal, o programa exibe um resumo com média, mínimo e máximo de cada medida
para cada quantidade de robôs. Valores `-1` indicam que o limite de espera foi
atingido sem receber a resposta.

### Resultados registrados

O arquivo `benchmark_escalabilidade.csv` contém cinco repetições por quantidade
de robôs. As médias registradas foram:

| Robôs | Criação | Online | Latência da bateria |
|---:|---:|---:|---:|
| 1 | 0,0 ms | 223,0 ms | 101,0 ms |
| 2 | 0,8 ms | 263,6 ms | 101,2 ms |
| 3 | 1,2 ms | 284,4 ms | 101,2 ms |
| 4 | 2,4 ms | 304,0 ms | 100,8 ms |

O aumento do tempo para os robôs ficarem online acompanha a criação de mais
processos. A latência do comando de bateria permanece próxima de 100 ms,
compatível com o período da thread de status.

### Roteiro de demonstração da Etapa 2

1. Iniciar o Mosquitto e executar `python3 simulador.py`.
2. Mostrar os robôs e a missão colaborativa no mapa.
3. Selecionar robôs pelas teclas `1` a `4`.
4. Ativar o modo manual com `M` e enviar um destino pelo mapa.
5. Desativar `M` e mostrar o retorno do robô ao estado automático/disponível.
6. Usar `F` ou o comando MQTT `falha <id> on|off`.
7. Alterar a bateria com `B` ou `bateria <id> <nivel>`.
8. Adicionar caixas com `C` e um robô com `N`.
9. Mostrar a publicação em `atr/telemetria` e o recebimento em
   `atr/comandos`.
10. Encerrar a interface e confirmar que o backend é finalizado junto.

## Simulador e MQTT

```bash
python3 simulador.py
```

Ao fechar a janela ou pressionar `Ctrl+C`, o simulador encerra também o
backend-filho e os processos dos robôs.

Com um broker Mosquitto em `127.0.0.1:1883`, a tela e o backend usam MQTT:

```bash
./build/atr_telemetria --mqtt
```

Os tópicos são:

- `atr/telemetria`: estados JSON publicados pelo backend;
- `atr/comandos`: comandos operacionais recebidos pelo backend.

O simulador envia periodicamente `PINGREQ` para manter a assinatura MQTT ativa
mesmo quando nenhum comando é enviado pela interface.

Sem broker, a tela usa automaticamente o canal local de entrada do backend.

## Arquitetura da Etapa 1

```mermaid
flowchart TB
    classDef gerencia fill:#4A90E2,stroke:#357ABD,color:#fff,font-weight:bold;
    classDef processo fill:#F5A623,stroke:#D48806,color:#fff,font-weight:bold;
    classDef thread fill:#7ED321,stroke:#62A814,color:#fff;
    classDef infra fill:#9B59B6,stroke:#8E44AD,color:#fff;
    classDef dados fill:#1ABC9C,stroke:#16A085,color:#fff;
    classDef interface fill:#E67E22,stroke:#CA6F1E,color:#fff,font-weight:bold;

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
    class MQTT interface;
    O -->|telemetria e comandos| MQTT
    C -->|telemetria| MQTT
```

`ContextoRobo::mtx` protege os dados compartilhados dentro de cada processo. A thread de navegação possui periodo de 50 ms e a thread de status possui período de 100 ms.
`HardwareFalso` possui uma thread própria, executada a cada 100 ms, e protege suas entradas e saidas com outro mutex. Os pipes carregam estruturas binárias de tamanho fixo, comandos descem do Organizador para o filho e os status sobem para o processo pai.
