cmake_minimum_required(VERSION 3.16)
project(atr CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wall -Wextra)
find_package(Threads REQUIRED)

add_library(atrlib STATIC 
    src/mapa.cpp
    src/hardware_falso.cpp
    src/robo.cpp
)

target_include_directories(atrlib PUBLIC include)
target_link_libraries(atrlib PUBLIC Threads::Threads rt)

add_executable(atr src/main.cpp)
target_link_libraries(atr PRIVATE atrlib)

enable_testing()

add_executable(teste_mapa testes/teste_mapa.cpp)
target_link_libraries(teste_mapa PRIVATE atrlib)
add_test(NAME TesteMapa COMMAND teste_mapa)

add_executable(teste_util testes/teste_fila.cpp)
target_link_libraries(teste_util PRIVATE atrlib Threads::Threads)
add_test(NAME TesteFila COMMAND teste_util)

add_executable(teste_timer testes/teste_temporizador.cpp)
target_link_libraries(teste_timer PRIVATE atrlib Threads::Threads)
add_test(NAME TesteTimer COMMAND teste_timer)

add_executable(teste_hardware testes/teste_hardware.cpp)
target_link_libraries(teste_hardware PRIVATE atrlib Threads::Threads)
add_test(NAME TesteHardware COMMAND teste_hardware)

add_executable(teste_robo testes/teste_robo.cpp)
target_link_libraries(teste_robo PRIVATE atrlib Threads::Threads)
add_test(NAME TesteRobo COMMAND teste_robo)
