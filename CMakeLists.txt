cmake_minimum_required(VERSION 3.16)
project(atr CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wall -Wextra)
find_package(Threads REQUIRED)

add_library(atrlib STATIC 
    src/main.cpp
)

target_include_directories(atrlib PUBLIC include)
target_link_libraries(atrlib PUBLIC Threads::Threads rt)

add_executable(atr src/main.cpp)
target_link_libraries(atr PRIVATE atrlib)