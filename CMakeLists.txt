cmake_minimum_required(VERSION 3.10)
project(Galaxia CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# El renderizador por software necesita las optimizaciones del compilador.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
# Hilos para hornear las texturas de los planetas en paralelo.
find_package(Threads REQUIRED)
# zlib solo descomprime los datos de los PNG de los mapas de normales del
# carro (el resto del lector de PNG es propio). Estatica para no depender de su DLL.
set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)

add_executable(galaxia
    src/main.cpp
    src/camera.cpp
    src/font.cpp
    src/framebuffer.cpp
    src/geometry.cpp
    src/hud.cpp
    src/mesh.cpp
    src/noise.cpp
    src/obj_loader.cpp
    src/parallel.cpp
    src/planets.cpp
    src/props.cpp
    src/renderer.cpp
    src/scene.cpp
    src/shaders.cpp
    src/ship.cpp
    src/spacekit.cpp
    src/texture.cpp
)

target_include_directories(galaxia PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(galaxia PRIVATE Threads::Threads ZLIB::ZLIB)

if(TARGET SDL2::SDL2main)
    target_link_libraries(galaxia PRIVATE SDL2::SDL2main)
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(galaxia PRIVATE SDL2::SDL2)
else()
    target_link_libraries(galaxia PRIVATE ${SDL2_LIBRARIES})
endif()

# Copia los modelos junto al ejecutable para que la ruta relativa
# "assets/..." funcione al ejecutar desde la carpeta build.
add_custom_command(TARGET galaxia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:galaxia>/assets"
)

# En Windows con MinGW: las bibliotecas de C++ y de hilos se enlazan dentro
# del ejecutable y SDL2.dll se copia a su lado, asi se puede abrir con doble
# clic o desde PowerShell sin tener MSYS2 en el PATH.
if(MINGW)
    target_link_options(galaxia PRIVATE -static-libgcc -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()
if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET galaxia POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:galaxia>"
    )
endif()
