
#include "atr/trafego.hpp"

#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    using namespace atr;

    ControleTrafego controlador;

    assert(controlador.registrar(0, { 8, 19 }));
    assert(controlador.registrar(1, { 10, 19 }));

    // Nao permite registrar dois robos na mesma celula.
    assert(!controlador.registrar(2, { 8, 19 }));

    // Pai e filho disputam a mesma celula.
    pid_t filho = fork();
    assert(filho >= 0);

    if (filho == 0) {
        bool moveu =
            controlador.mover(1, { 9, 19 }) ==
            ResultadoMovimento::Aprovado;

        _exit(moveu ? 0 : 1);
    }

    auto pai = controlador.mover(0, { 9, 19 });

    int st = 0;
    assert(waitpid(filho, &st, 0) == filho);
    assert(WIFEXITED(st));

    bool moveu_filho = WEXITSTATUS(st) == 0;

    // Somente um processo pode conseguir mover.
    assert(
        (pai == ResultadoMovimento::Aprovado) !=
        moveu_filho
    );

    Ponto a{}, b{};

    assert(controlador.posicao(0, a));
    assert(controlador.posicao(1, b));
    assert(a != b);

    controlador.remover(0);
    controlador.remover(1);

    // Corredor 1 possui capacidade para um robo.
    assert(controlador.registrar(2, { 23, 7 }));
    assert(controlador.registrar(3, { 21, 7 }));

    assert(
        controlador.mover(3, { 22, 7 }) ==
        ResultadoMovimento::CorredorCheio
    );

    // O primeiro robo sai do corredor.
    assert(
        controlador.mover(2, { 24, 7 }) ==
        ResultadoMovimento::Aprovado
    );

    // Agora o segundo consegue entrar.
    assert(
        controlador.mover(3, { 22, 7 }) ==
        ResultadoMovimento::Aprovado
    );

    std::cout << "TesteTrafego passou\n";
}
