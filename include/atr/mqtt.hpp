#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace atr {

    class MqttPublicador {
    public:
        MqttPublicador(std::string host, uint16_t port, std::string topico);
        ~MqttPublicador();

        MqttPublicador(const MqttPublicador&) = delete;
        MqttPublicador& operator=(const MqttPublicador&) = delete;

        void publicar(const std::string& payload);
        void assinar(const std::string& topico);
        std::vector<std::string> receber();

    private:
        void conectar();
        void reconectar();
        void fechar_socket();
        void enviar_assinatura(const std::string& topico);

        int socket_ = -1;
        std::string host_;
        uint16_t port_;
        std::string topico_;
        std::vector<std::string> topicos_;
        uint16_t contador_pacote_ = 1;
        std::vector<unsigned char> recebimento_;
    };

}
