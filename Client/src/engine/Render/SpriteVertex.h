#pragma once

// Gl::GlRenderer uploads it to the VBO as is, in this order (its VAO describes it so).
namespace Client::Render
{
// A GL color in floats, like glColor4f's, which the shader multiplies by the texture (the fixed function's
// GL_MODULATE).
struct Color
{
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;

    friend bool operator==(const Color&, const Color&) = default;
};

struct SpriteVertex
{
    // In screen pixels with y up, as gluOrtho2D(0, W, 0, H) leaves them (06 § Primitivas 2D): the original flips its
    // own, H − y, at each vertex.
    float x = 0.0f;
    float y = 0.0f;
    // In the texture: (0, 0) is the start of buffer row 0, the first one glTexImage2D uploads, and v grows toward the
    // following rows.
    float u = 0.0f;
    float v = 0.0f;
    Color color{};
};
} // namespace Client::Render
