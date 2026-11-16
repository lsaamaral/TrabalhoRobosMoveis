
import atexit
import json
import os
import queue
import random
import signal
import socket
import subprocess
import sys
import threading
import time
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
BLOCOS_LIXO = RAIZ / "assets" / "blocos_lixo.png.png"

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
MQTT_HOST = "127.0.0.1"
MQTT_PORTA = 1883


class MqttCliente:
    def __init__(self, host, porta):
        self.host = host
        self.porta = porta
        self.socket = None
        self.topicos = []
        self.contador_pacote = 1
        self._conectar()

    def _conectar(self):
        self.socket = socket.create_connection(
            (self.host, self.porta), timeout=2
        )
        self.socket.settimeout(2)
        corpo_connect = (
            self._utf8("MQTT")
            + b"\x04\x02\x00\x3c"
            + self._utf8("ATR-tela")
        )
        self.socket.sendall(self._pacote(0x10, corpo_connect))
        cabecalho, corpo = self._receber_pacote()
        if cabecalho != 0x20 or corpo != b"\x00\x00":
            self.socket.close()
            self.socket = None
            raise RuntimeError("broker MQTT recusou a tela")
        self.socket.setblocking(False)
        self.buffer = bytearray()
        self.ultimo_envio = time.monotonic()
        for topico in self.topicos:
            self._enviar_assinatura(topico)

    def _reconectar(self):
        try:
            if self.socket is not None:
                self.socket.close()
        except OSError:
            pass
        self.socket = None
        try:
            self._conectar()
            return True
        except (OSError, RuntimeError):
            return False

    @staticmethod
    def _utf8(valor):
        dados = valor.encode()
        return len(dados).to_bytes(2, "big") + dados

    @staticmethod
    def _tamanho_mqtt(tamanho):
        dados = bytearray()
        while True:
            byte = tamanho % 128
            tamanho //= 128
            if tamanho:
                byte |= 0x80
            dados.append(byte)
            if not tamanho:
                return bytes(dados)

    @classmethod
    def _pacote(cls, tipo, corpo):
        return bytes([tipo]) + cls._tamanho_mqtt(len(corpo)) + corpo

    def _receber_pacote(self):
        cabecalho_bytes = self.socket.recv(1)
        if not cabecalho_bytes:
            raise RuntimeError("broker MQTT encerrou a conexao durante o CONNECT")
        cabecalho = cabecalho_bytes[0]
        multiplicador = 1
        tamanho = 0
        while True:
            byte_bytes = self.socket.recv(1)
            if not byte_bytes:
                raise RuntimeError("broker MQTT enviou um pacote incompleto")
            byte = byte_bytes[0]
            tamanho += (byte & 127) * multiplicador
            if not byte & 128:
                break
            multiplicador *= 128

        corpo = bytearray()
        while len(corpo) < tamanho:
            parte = self.socket.recv(tamanho - len(corpo))
            if not parte:
                raise RuntimeError("broker MQTT enviou um pacote incompleto")
            corpo.extend(parte)
        return cabecalho, bytes(corpo)

    def publicar(self, topico, mensagem):
        pacote = self._pacote(0x30, self._utf8(topico) + mensagem.encode())
        if self.socket is None and not self._reconectar():
            raise OSError("broker MQTT indisponivel")
        try:
            self.socket.sendall(pacote)
        except (BrokenPipeError, ConnectionResetError, OSError):
            if not self._reconectar():
                raise
            self.socket.sendall(pacote)
        self.ultimo_envio = time.monotonic()

    def _enviar_assinatura(self, topico):
        corpo = (
            self.contador_pacote.to_bytes(2, "big")
            + self._utf8(topico)
            + b"\x00"
        )
        self.contador_pacote += 1
        self.socket.sendall(self._pacote(0x82, corpo))
        self.ultimo_envio = time.monotonic()

    def assinar(self, topico):
        if topico not in self.topicos:
            self.topicos.append(topico)
        if self.socket is None and not self._reconectar():
            raise OSError("broker MQTT indisponivel")
        try:
            self._enviar_assinatura(topico)
        except (BrokenPipeError, ConnectionResetError, OSError):
            if not self._reconectar():
                raise

    def receber(self):
        mensagens = []
        if self.socket is None and not self._reconectar():
            return mensagens
        if time.monotonic() - self.ultimo_envio >= 30:
            try:
                self.socket.sendall(b"\xc0\x00")
            except (BrokenPipeError, ConnectionResetError, OSError):
                self._reconectar()
                return mensagens
            self.ultimo_envio = time.monotonic()
        while True:
            try:
                self.buffer.extend(self.socket.recv(4096))
            except BlockingIOError:
                break
            except (ConnectionResetError, OSError):
                self._reconectar()
                return mensagens

        while len(self.buffer) >= 2:
            indice = 1
            multiplicador = 1
            tamanho = 0
            while True:
                if indice >= len(self.buffer):
                    return mensagens
                byte = self.buffer[indice]
                indice += 1
                tamanho += (byte & 127) * multiplicador
                if not byte & 128:
                    break
                multiplicador *= 128
            if len(self.buffer) < indice + tamanho:
                break
            corpo = self.buffer[indice:indice + tamanho]
            if self.buffer[0] >> 4 == 3 and len(corpo) >= 2:
                tamanho_topico = int.from_bytes(corpo[:2], "big")
                inicio = 2 + tamanho_topico
                mensagens.append(bytes(corpo[inicio:]).decode())
            del self.buffer[:indice + tamanho]
        return mensagens

    def fechar(self):
        try:
            if self.socket is not None:
                self.socket.sendall(b"\xe0\x00")
                self.socket.close()
                self.socket = None
        except OSError:
            pass


def iniciar_backend(usar_mqtt=False):
    if not BINARIO.is_file():
        sys.exit(
            "Executavel build/atr_telemetria nao encontrado. "
            "Compile com CMake primeiro."
        )

    return subprocess.Popen(
        [
            str(BINARIO),
            "--robos",
            "3",
            *(["--mqtt"] if usar_mqtt else [])
        ],
        cwd=str(RAIZ),
        stdout=subprocess.PIPE,
        stdin=subprocess.PIPE,
        stderr=None,
        text=True,
        bufsize=1,
        start_new_session=True
    )


def enviar_comando(proc, comando, mqtt=None):
    if mqtt is not None:
        try:
            mqtt.publicar("atr/comandos", comando)
            return
        except OSError as erro:
            print(f"ERRO_MQTT_COMANDO: {erro}", file=sys.stderr)
    if proc.poll() is not None or proc.stdin is None:
        return
    try:
        proc.stdin.write(comando + "\n")
        proc.stdin.flush()
    except BrokenPipeError:
        pass


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
    spritesheet_lixo = pygame.image.load(str(BLOCOS_LIXO)).convert_alpha()
    sprites_lixo = [
        spritesheet_lixo.subsurface(
            (coluna * 20, linha * 20, 20, 20)
        ).copy()
        for linha in range(2)
        for coluna in range(3)
    ]
    sprite_cargas = {}

    try:
        mqtt = MqttCliente(MQTT_HOST, MQTT_PORTA)
        mqtt.assinar("atr/telemetria")
    except (OSError, RuntimeError):
        mqtt = None

    proc = iniciar_backend(mqtt is not None)
    atexit.register(encerrar_backend, proc)

    def encerrar_por_interrupcao(signum, frame):
        encerrar_backend(proc)
        if mqtt is not None:
            mqtt.fechar()
        pygame.quit()
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, encerrar_por_interrupcao)

    fila = queue.Queue(maxsize=6)

    if mqtt is None:
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
    modo_manual = False
    robo_selecionado = 0
    proxima_baia = 1
    proxima_zona = 1
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

                if evento.key == pygame.K_m:
                    modo_manual = not modo_manual
                    if not modo_manual:
                        enviar_comando(
                            proc,
                            f"automatico {robo_selecionado}",
                            mqtt
                        )

                # Sem Ctrl, as teclas 1 a 9 selecionam os robos.
                if (pygame.K_1 <= evento.key <= pygame.K_9
                        and not (evento.mod & pygame.KMOD_CTRL)):
                    candidato = evento.key - pygame.K_1
                    if any(
                        item.get("id") == candidato
                        for item in estado.get("robos", [])
                    ):
                        robo_selecionado = candidato

                if evento.key == pygame.K_f:
                    robo = next(
                        (
                            item for item in estado.get("robos", [])
                            if item.get("id") == robo_selecionado
                        ),
                        None
                    )
                    falha = robo.get("falha", False) if robo else False
                    enviar_comando(
                        proc,
                        f"falha {robo_selecionado} {'off' if falha else 'on'}",
                        mqtt
                    )

                if evento.key == pygame.K_b:
                    enviar_comando(
                        proc,
                        f"bateria {robo_selecionado} 30",
                        mqtt
                    )

                # C cria uma carga simples; Ctrl+1..4 define o tamanho.
                tamanho_carga = None
                if evento.key == pygame.K_c:
                    tamanho_carga = 1
                elif ((evento.mod & pygame.KMOD_CTRL)
                      and pygame.K_1 <= evento.key <= pygame.K_4):
                    tamanho_carga = evento.key - pygame.K_1 + 1

                if tamanho_carga is not None:
                    # Escolhe uma zona acessivel pelo portao com
                    # capacidade nominal para o tamanho do grupo.
                    if tamanho_carga == 2:
                        zona_destino = 0  # A: capacidade 2
                    elif tamanho_carga == 3:
                        zona_destino = 2  # C: capacidade 3
                    elif tamanho_carga == 4:
                        zona_destino = 3  # D: capacidade 4
                    else:
                        zona_destino = proxima_zona

                    enviar_comando(
                        proc,
                        f"caixa {proxima_baia} {zona_destino} {tamanho_carga}",
                        mqtt
                    )
                    print(
                        f"Nova carga: baia {proxima_baia + 1}, "
                        f"zona {chr(65 + zona_destino)}, "
                        f"tamanho {tamanho_carga}"
                    )
                    proxima_baia = (proxima_baia + 1) % 4
                    proxima_zona = (proxima_zona + 1) % 4

                if evento.key == pygame.K_n:
                    enviar_comando(proc, "robo", mqtt)

                if evento.key == pygame.K_r:
                    encerrar_backend(proc)

                    proc = iniciar_backend(mqtt is not None)
                    fila = queue.Queue(maxsize=6)

                    if mqtt is None:
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

            elif evento.type == pygame.MOUSEBUTTONDOWN and evento.button == 1:
                x_tela, y_tela = evento.pos
                dentro_mapa = (
                    16 <= x_tela < 16 + LARGURA_MAPA
                    and 49 <= y_tela < 49 + ALTURA_MAPA
                )
                if modo_manual and dentro_mapa:
                    x = (x_tela - 16) // TAM_CELULA
                    y = (y_tela - 49) // TAM_CELULA
                    enviar_comando(
                        proc,
                        f"destino {robo_selecionado} {x} {y}",
                        mqtt
                    )

        if mqtt is not None:
            for mensagem in mqtt.receber():
                try:
                    fila.put_nowait(json.loads(mensagem))
                except (ValueError, queue.Full):
                    pass

        # Recebe as atualizacoes do C++
        try:
            while True:
                estado = fila.get_nowait()
        except queue.Empty:
            pass

        ids_ativos = [
            item.get("id") for item in estado.get("robos", [])
        ]
        if ids_ativos and robo_selecionado not in ids_ativos:
            robo_selecionado = ids_ativos[0]

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

        posicoes_robos = {
            robo.get("id"): (robo.get("x", -1), robo.get("y", -1))
            for robo in estado.get("robos", [])
        }
        for carga in estado.get("cargas", []):
            carga_id = carga.get("id")
            if carga_id not in sprite_cargas:
                sprite_cargas[carga_id] = random.choice(sprites_lixo)

            estado_carga = carga.get("estado")
            if estado_carga == "concluida":
                x = 27
                y = 1 + 5 * carga.get("zona", 0)
            elif estado_carga == "transportando":
                posicao = next(
                    (
                        posicoes_robos[robo_id]
                        for robo_id in posicoes_robos
                        if carga.get("robos", 0) & (1 << robo_id)
                    ),
                    None
                )
                if posicao is None:
                    continue
                x, y = posicao
            else:
                x = 7
                y = 1 + 5 * carga.get("baia", 0)

            if 0 <= x < 32 and 0 <= y < 21:
                tela.blit(
                    sprite_cargas[carga_id],
                    (16 + x * TAM_CELULA + 6, 49 + y * TAM_CELULA + 6)
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

        texto(
            tela, fonte_peq,
            f"Robo selecionado: {robo_selecionado} "
            f"(tecla {robo_selecionado + 1})",
            1066, 114, SUAVE
        )

        texto(
            tela, fonte_peq,
            "Modo manual: " + ("ATIVO" if modo_manual else "desativado"),
            1066, 129,
            AMARELO if modo_manual else SUAVE
        )

        pronto = estado.get("carga_concluida", False)

        rotulo = (
            "ULTIMA CARGA ENTREGUE"
            if pronto
            else (
                "MISSAO EM EXECUCAO"
                if estado.get("pendentes", 0) > 0
                else "AGUARDANDO NOVA CARGA"
            )
        )

        texto(
            tela, fonte, rotulo,
            1066, 148,
            VERDE if pronto else AMARELO
        )

        texto(
            tela, fonte_peq,
            f"Cargas pendentes: {estado.get('pendentes', 0)}",
            1066, 171, SUAVE
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

            if robo.get("id") == robo_selecionado:
                pygame.draw.rect(
                    tela,
                    AMARELO,
                    (1063, yi, 295, 94),
                    2,
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
            "1-9: selecionar | Ctrl+1..4: carga 1..4 | C: caixa simples | "
            "M: manual | clique: destino | F: falha | B: bateria 30% | "
            "N: robo | G: grade | R: repetir | ESC: fechar",
            17, 727, SUAVE
        )

        pygame.display.flip()
        relogio.tick(30)

    encerrar_backend(proc)
    if mqtt is not None:
        mqtt.fechar()
    pygame.quit()


if __name__ == "__main__":
    executar()
