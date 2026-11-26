#pragma once

#include <array>

// A corner of an original quad as sent to GL between glBegin(GL_TRIANGLE_FAN) and glEnd: the position in screen pixels
// with y up (gluOrtho2D(0, W, 0, H), set by 0x6219b0), and its UV. No color: the SpriteBatch sets it, since it knows
// GL's current one.
namespace Client::Render
{
struct QuadCorner
{
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// In the original's fan order: top left, bottom left, bottom right and top right.
using QuadCorners = std::array<QuadCorner, 4>;
} // namespace Client::Render
