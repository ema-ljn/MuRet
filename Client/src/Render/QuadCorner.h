#pragma once

#include <array>

// Una esquina de un quad del original, como la manda a GL entre glBegin(GL_TRIANGLE_FAN) y glEnd:
// la posición en píxeles de la pantalla, con la y para arriba (la de gluOrtho2D(0, W, 0, H) que
// pone 0x6219b0), y su UV. Sin color: el color lo pone el SpriteBatch, que sabe cuál es el de GL en
// ese momento.
namespace Client::Render
{
struct QuadCorner
{
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Las cuatro, en el orden del fan del original: arriba a la izquierda, abajo a la izquierda, abajo
// a la derecha y arriba a la derecha.
using QuadCorners = std::array<QuadCorner, 4>;
} // namespace Client::Render
