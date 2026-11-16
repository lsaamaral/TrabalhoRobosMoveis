#include "atr/mqtt.hpp"

#include <netdb.h>
#include <fcntl.h>
#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace atr {

    namespace {
        void enviar_tudo(int socket, const void* dados, std::size_t tamanho) {
            const auto* bytes = static_cast<const unsigned char*>(dados);
            while (tamanho > 0) {
                const ssize_t enviados = send(socket, bytes, tamanho, MSG_NOSIGNAL);
                if (enviados < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    pollfd pronto{socket, POLLOUT, 0};
                    if (poll(&pronto, 1, 1000) <= 0) {
                        throw std::runtime_error("timeout ao enviar pacote MQTT");
                    }
                    continue;
                }
                if (enviados < 0 && errno == EINTR) continue;
                if (enviados <= 0) {
                    throw std::runtime_error("falha ao enviar pacote MQTT");
                }
                bytes += enviados;
                tamanho -= static_cast<std::size_t>(enviados);
            }
        }

        void receber_tudo(int socket, void* dados, std::size_t tamanho) {
            auto* bytes = static_cast<unsigned char*>(dados);
            while (tamanho > 0) {
                const ssize_t recebidos = recv(socket, bytes, tamanho, 0);
                if (recebidos <= 0) {
                    throw std::runtime_error(
                        "broker MQTT encerrou a conexao durante o CONNECT");
                }
                bytes += recebidos;
                tamanho -= static_cast<std::size_t>(recebidos);
            }
        }

        std::vector<unsigned char> comprimento_mqtt(std::size_t tamanho) {
            std::vector<unsigned char> resultado;
            do {
                unsigned char byte = static_cast<unsigned char>(tamanho % 128);
                tamanho /= 128;
                if (tamanho > 0) byte |= 0x80;
                resultado.push_back(byte);
            } while (tamanho > 0);
            return resultado;
        }

        void adicionar_utf8(std::vector<unsigned char>& pacote,
                            const std::string& texto) {
            pacote.push_back(static_cast<unsigned char>((texto.size() >> 8) & 0xff));
            pacote.push_back(static_cast<unsigned char>(texto.size() & 0xff));
            pacote.insert(pacote.end(), texto.begin(), texto.end());
        }
    }

    MqttPublicador::MqttPublicador(
        std::string host, uint16_t port, std::string topico)
        : host_(std::move(host)), port_(port), topico_(std::move(topico)) {
        conectar();
    }

    void MqttPublicador::fechar_socket() {
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
        }
        recebimento_.clear();
    }

    void MqttPublicador::conectar() {
        addrinfo criterios{};
        criterios.ai_socktype = SOCK_STREAM;
        criterios.ai_family = AF_UNSPEC;
        addrinfo* enderecos = nullptr;
        const std::string porta = std::to_string(port_);
        if (getaddrinfo(host_.c_str(), porta.c_str(), &criterios, &enderecos) != 0) {
            throw std::runtime_error("nao foi possivel resolver o host MQTT");
        }

        for (addrinfo* endereco = enderecos; endereco != nullptr;
             endereco = endereco->ai_next) {
            socket_ = socket(endereco->ai_family, endereco->ai_socktype,
                             endereco->ai_protocol);
            if (socket_ >= 0 && connect(socket_, endereco->ai_addr,
                                        endereco->ai_addrlen) == 0) {
                break;
            }
            if (socket_ >= 0) {
                close(socket_);
                socket_ = -1;
            }
        }
        freeaddrinfo(enderecos);
        if (socket_ < 0) {
            throw std::runtime_error("nao foi possivel conectar ao broker MQTT");
        }

        std::vector<unsigned char> variaveis;
        adicionar_utf8(variaveis, "MQTT");
        variaveis.push_back(4);
        variaveis.push_back(2);
        variaveis.push_back(0);
        variaveis.push_back(60);
        adicionar_utf8(variaveis, "atr-telemetria");

        const auto comprimento = comprimento_mqtt(variaveis.size());
        const unsigned char cabecalho = 0x10;
        enviar_tudo(socket_, &cabecalho, 1);
        enviar_tudo(socket_, comprimento.data(), comprimento.size());
        enviar_tudo(socket_, variaveis.data(), variaveis.size());

        unsigned char resposta[4]{};
        try {
            receber_tudo(socket_, resposta, sizeof(resposta));
        } catch (const std::runtime_error&) {
            close(socket_);
            socket_ = -1;
            throw;
        }
        if (resposta[0] != 0x20 || resposta[1] != 0x02 ||
            resposta[3] != 0) {
            close(socket_);
            socket_ = -1;
            throw std::runtime_error("broker MQTT recusou a conexao");
        }
        const int flags = fcntl(socket_, F_GETFL, 0);
        fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
    }

    void MqttPublicador::reconectar() {
        fechar_socket();
        conectar();
        for (const auto& topico : topicos_) {
            enviar_assinatura(topico);
        }
    }

    MqttPublicador::~MqttPublicador() {
        if (socket_ >= 0) {
            const unsigned char desconectar[2] = {0xe0, 0x00};
            send(socket_, desconectar, sizeof(desconectar), MSG_NOSIGNAL);
            close(socket_);
        }
    }

    void MqttPublicador::publicar(const std::string& payload) {
        if (socket_ < 0) {
            throw std::runtime_error("publicador MQTT nao conectado");
        }

        std::vector<unsigned char> corpo;
        adicionar_utf8(corpo, topico_);
        corpo.insert(corpo.end(), payload.begin(), payload.end());
        const auto comprimento = comprimento_mqtt(corpo.size());
        const unsigned char cabecalho = 0x30;
        enviar_tudo(socket_, &cabecalho, 1);
        enviar_tudo(socket_, comprimento.data(), comprimento.size());
        enviar_tudo(socket_, corpo.data(), corpo.size());
    }

    void MqttPublicador::assinar(const std::string& topico) {
        if (std::find(topicos_.begin(), topicos_.end(), topico) == topicos_.end()) {
            topicos_.push_back(topico);
        }
        enviar_assinatura(topico);
    }

    void MqttPublicador::enviar_assinatura(const std::string& topico) {
        std::vector<unsigned char> corpo{0, 1};
        adicionar_utf8(corpo, topico);
        corpo.push_back(0);
        const auto comprimento = comprimento_mqtt(corpo.size());
        const unsigned char cabecalho = 0x82;
        enviar_tudo(socket_, &cabecalho, 1);
        enviar_tudo(socket_, comprimento.data(), comprimento.size());
        enviar_tudo(socket_, corpo.data(), corpo.size());
    }

    std::vector<std::string> MqttPublicador::receber() {
        if (socket_ < 0) return {};
        std::vector<unsigned char> bloco(4096);
        while (true) {
            const ssize_t recebidos = recv(socket_, bloco.data(), bloco.size(), 0);
            if (recebidos < 0 &&
                (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                break;
            }
            if (recebidos == 0) {
                throw std::runtime_error("broker MQTT encerrou a conexao");
            }
            if (recebidos < 0) {
                throw std::runtime_error("falha ao receber pacote MQTT");
            }
            recebimento_.insert(recebimento_.end(), bloco.begin(),
                                bloco.begin() + recebidos);
        }

        std::vector<std::string> mensagens;
        while (recebimento_.size() >= 2) {
            std::size_t multiplicador = 1;
            std::size_t tamanho = 0;
            std::size_t indice = 1;
            unsigned char byte = 0;
            do {
                if (indice >= recebimento_.size() || multiplicador > 128 * 128 * 128) {
                    return mensagens;
                }
                byte = recebimento_[indice++];
                tamanho += (byte & 127) * multiplicador;
                multiplicador *= 128;
            } while (byte & 128);

            if (recebimento_.size() < indice + tamanho) break;
            const unsigned char tipo = recebimento_[0] >> 4;
            if (tipo == 3 && tamanho >= 2) {
                const std::size_t tamanho_topico =
                    (recebimento_[indice] << 8) | recebimento_[indice + 1];
                const std::size_t inicio_payload = indice + 2 + tamanho_topico;
                if (inicio_payload <= indice + tamanho) {
                    mensagens.emplace_back(
                        recebimento_.begin() + inicio_payload,
                        recebimento_.begin() + indice + tamanho);
                }
            }
            recebimento_.erase(recebimento_.begin(),
                               recebimento_.begin() + indice + tamanho);
        }
        return mensagens;
    }

}
