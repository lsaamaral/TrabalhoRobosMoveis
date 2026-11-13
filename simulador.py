
import json
import os
import queue
import signal
import subprocess
import sys
import threading
from pathlib import Path

try:
    import pygame
except ImportError:
    sys.exit(
        "Pygame nao instalado. Execute: sudo apt install python3-pygame"
    )

RAIZ = Path(__file__).resolve().parent

BINARIO = RAIZ / "build" / "atr_telemetria"
MAPA = RAIZ / "assets" / "mapafinal.png.png"

SPRITES = {
    "normal": "walle_normal.png",
    "carga": "walle_carregando.png",
    "falha": "walle_falha.png"
}

TAM_CELULA = 32
LARGURA_MAPA = 1024
ALTURA_MAPA = 672
TAMANHO_JANELA = (1390, 754)

PRETO = (17, 23, 33)
PAINEL = (28, 37, 50)
TEXTO = (239, 244, 248)
SUAVE = (155, 173, 190)
VERDE = (112, 217, 160)
AMARELO = (250, 205, 98)
VERMELHO = (245, 105, 112)


def iniciar_backend():
    if not BINARIO.is_file():
        sys.exit(
            "Executavel build/atr_telemetria nao encontrado. "
            "Compile com CMake primeiro."
        )

    return subprocess.Popen(
        [str(BINARIO), "--robos", "3"],
        cwd=str(RAIZ),
        stdout=subprocess.PIPE,
        stderr=None,
        text=True,
        bufsize=1,
        start_new_session=True
    )


def leitor_json(proc, fila):
    try:
        for linha in proc.stdout:
            try:
                dado = json.loads(linha)
            except ValueError:
                continue

            try:
                fila.put_nowait(dado)
            except queue.Full:
                try:
                    fila.get_nowait()
                except queue.Empty:
                    pass

                fila.put_nowait(dado)
    finally:
        try:
            proc.stdout.close()
        except Exception:
            pass


def texto(tela, fonte, conteudo, x, y, cor=TEXTO):
    superficie = fonte.render(str(conteudo), True, cor)
    tela.blit(superficie, (x, y))


def barra_bateria(tela, valor, x, y, largura=190):
    pygame.draw.rect(
        tela,
        (68, 82, 98),
        (x, y, largura, 10),
        border_radius=4
    )

    if valor > 40:
        cor = VERDE
    elif valor > 20:
        cor = AMARELO
    else:
        cor = VERMELHO

    percentual = max(0, min(100, valor))
    largura_atual = percentual * largura // 100

    pygame.draw.rect(
        tela,
        cor,
        (x, y, largura_atual, 10),
        border_radius=4
    )


def encerrar_backend(proc):
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()


def executar():
    pygame.init()

    tela = pygame.display.set_mode(TAMANHO_JANELA)
    pygame.display.set_caption(
        "ATR | Simulador de Frota - C++"
    )

    relogio = pygame.time.Clock()

    fonte = pygame.font.SysFont("dejavusans", 16)
    fonte_peq = pygame.font.SysFont("dejavusans", 13)
    fonte_titulo = pygame.font.SysFont(
        "dejavusans", 22, bold=True
    )

    fundo = pygame.image.load(str(MAPA)).convert_alpha()

    sprites = {
        tipo: pygame.image.load(
            str(RAIZ / "assets" / arquivo)
        ).convert_alpha()
        for tipo, arquivo in SPRITES.items()
    }

    proc = iniciar_backend()

    fila = queue.Queue(maxsize=6)

    threading.Thread(
        target=leitor_json,
        args=(proc, fila),
        daemon=True
    ).start()

    estado = {
        "robos": [],
        "eventos": [],
        "pendentes": 1,
        "carga_concluida": False
    }

    grade = False
    executando = True

    while executando:

        # Eventos do teclado e da janela
        for evento in pygame.event.get():

            if evento.type == pygame.QUIT:
                executando = False

            elif evento.type == pygame.KEYDOWN:

                if evento.key == pygame.K_ESCAPE:
                    executando = False

                if evento.key == pygame.K_g:
                    grade = not grade

                if evento.key == pygame.K_r:
                    encerrar_backend(proc)

                    proc = iniciar_backend()
                    fila = queue.Queue(maxsize=6)

                    threading.Thread(
                        target=leitor_json,
                        args=(proc, fila),
                        daemon=True
                    ).start()

                    estado = {
                        "robos": [],
                        "eventos": [],
                        "pendentes": 1,
                        "carga_concluida": False
                    }

        # Recebe as atualizacoes do C++
        try:
            while True:
                estado = fila.get_nowait()
        except queue.Empty:
            pass

        tela.fill(PRETO)

        texto(
            tela, fonte_titulo,
            "ATR / FROTA DE ROBOS MOVEIS",
            18, 10
        )

        tela.blit(fundo, (16, 49))

        pygame.draw.rect(
            tela,
            (84, 101, 113),
            (16, 49, LARGURA_MAPA, ALTURA_MAPA),
            2
        )

        # Grade opcional
        if grade:
            for x in range(33):
                pygame.draw.line(
                    tela, (33, 36, 44),
                    (16 + x * 32, 49),
                    (16 + x * 32, 721)
                )

            for y in range(22):
                pygame.draw.line(
                    tela, (33, 36, 44),
                    (16, 49 + y * 32),
                    (1040, 49 + y * 32)
                )

        # Painel lateral
        pygame.draw.rect(
            tela,
            PAINEL,
            (1051, 48, 323, 673),
            border_radius=8
        )

        texto(
            tela, fonte_titulo,
            "CENTRAL DE OPERACAO",
            1066, 64
        )

        texto(
            tela, fonte_peq,
            "Posicao / bateria / estado",
            1066, 99, SUAVE
        )

        pronto = estado.get("carga_concluida", False)

        rotulo = (
            "CARGA 1 ENTREGUE"
            if pronto
            else "MISSAO EM EXECUCAO"
        )

        texto(
            tela, fonte, rotulo,
            1066, 126,
            VERDE if pronto else AMARELO
        )

        texto(
            tela, fonte_peq,
            f"Cargas pendentes: {estado.get('pendentes', 0)}",
            1066, 151, SUAVE
        )

        # Desenho dos robos
        for indice, robo in enumerate(
            estado.get("robos", [])
        ):
            x = robo.get("x", -1)
            y = robo.get("y", -1)

            if (
                robo.get("estado") != "offline"
                and 0 <= x < 32
                and 0 <= y < 21
            ):
                if robo.get("falha", False):
                    modo = "falha"
                elif robo.get("transportando", False):
                    modo = "carga"
                else:
                    modo = "normal"

                px = 16 + x * TAM_CELULA
                py = 49 + y * TAM_CELULA

                tela.blit(
                    sprites[modo],
                    (px, py)
                )

                pygame.draw.rect(
                    tela, PRETO,
                    (px, py, 21, 15),
                    border_radius=2
                )

                texto(
                    tela, fonte_peq,
                    str(robo["id"]),
                    px + 4, py - 1
                )

            # Informacoes do robo
            yi = 185 + indice * 108

            if yi + 92 > 585:
                continue

            pygame.draw.rect(
                tela,
                (39, 51, 66),
                (1063, yi, 295, 94),
                border_radius=5
            )

            texto(
                tela, fonte,
                f"ROBO {robo['id']}",
                1073, yi + 7
            )

            texto(
                tela, fonte_peq,
                f"({x}, {y})   {robo.get('estado', '?')}",
                1073, yi + 32,
                SUAVE
            )

            bateria = robo.get("bateria", 0)

            barra_bateria(
                tela, bateria,
                1073, yi + 63
            )

            texto(
                tela, fonte_peq,
                f"{bateria}%",
                1275, yi + 57
            )

        # Ultimos eventos
        pygame.draw.line(
            tela,
            (71, 84, 101),
            (1065, 575),
            (1358, 575)
        )

        texto(
            tela, fonte,
            "ULTIMOS EVENTOS",
            1066, 588
        )

        for indice, evento_nome in enumerate(
            estado.get("eventos", [])[-5:]
        ):
            texto(
                tela, fonte_peq,
                "- " + evento_nome,
                1067,
                616 + indice * 18,
                SUAVE
            )

        if proc.poll() is not None:
            if proc.returncode == 0:
                msg = "Execucao concluida"
                cor = VERDE
            else:
                msg = "Backend finalizou com erro"
                cor = VERMELHO

            texto(
                tela, fonte_peq,
                msg, 1066, 708, cor
            )

        texto(
            tela, fonte_peq,
            "G: grade | R: repetir | ESC: fechar",
            17, 727, SUAVE
        )

        pygame.display.flip()
        relogio.tick(30)

    encerrar_backend(proc)
    pygame.quit()


if __name__ == "__main__":
    executar()
