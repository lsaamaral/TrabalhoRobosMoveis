#pragma once
#include <cstdio>
#include <cstdlib>
#define VERIFICA(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FALHOU (%s:%d): %s\n", __FILE__, __LINE__, msg); std::exit(1); } } while (0)

