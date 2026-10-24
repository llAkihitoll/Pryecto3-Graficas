#pragma once
#include <string>
#include "framebuffer.h"

// Fuente propia de 5x7 pixeles para el HUD (sin bibliotecas de texto).
// Solo ASCII: las minusculas se dibujan como mayusculas, '^' es una flecha
// hacia arriba y '~' una flecha hacia abajo.
const int GLYPH_WIDTH = 6;  // 5 de la letra + 1 de espacio
const int GLYPH_HEIGHT = 8; // 7 de la letra + 1 de espacio

// Dibuja el texto con su esquina superior izquierda en (x, y). scale
// agranda cada pixel de la letra. Lleva una sombra oscura para leerse sobre
// cualquier fondo.
void drawText(Framebuffer& fb, int x, int y, const std::string& text, uint32_t argb, int scale = 1);

int textWidth(const std::string& text, int scale = 1);
