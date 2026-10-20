#include <chrono>
#include <iostream>
#include <thread>
#include "atr/robo.hpp"

int main() {
    atr::Robo robo(0, atr::obter_mapa().ponto_inicial(0));
    robo.enviar_destino(atr::obter_mapa().pega_baia(0, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto status = robo.ler_status();
    std::cout << "robo " << robo.id() << " estado="
              << atr::to_string(status.estado)
              << " posicao=(" << status.pos.x << "," << status.pos.y
              << ") bateria=" << status.bateria << "%\n";
    return 0;
}