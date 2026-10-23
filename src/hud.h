#pragma once
#include "framebuffer.h"

// Interfaz 2D dibujada directo en el framebuffer, despues de la escena 3D.

// Rectangulo relleno, recortado a la pantalla.
void fillRect(Framebuffer& fb, int x, int y, int w, int h, uint32_t argb);

// Barra horizontal con marco: fraction (0..1) indica cuanto esta llena.
void drawBar(Framebuffer& fb, int x, int y, int w, int h, float fraction, uint32_t argb);
