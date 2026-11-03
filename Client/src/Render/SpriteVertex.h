#pragma once

// Un vértice del 2D: lo que el SpriteBatch (CL5.2) arma por cada esquina de un quad del original, y
// lo que Gl::GlRenderer sube al VBO tal cual, en este orden (su VAO lo describe así).
namespace Client::Render
{
// Un color de GL en floats, como el de glColor4f, que el shader multiplica por la textura (el
// GL_MODULATE de la función fija).
struct Color
{
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
};

struct SpriteVertex
{
    // En píxeles de la pantalla, con la y hacia arriba, como los deja gluOrtho2D(0, W, 0, H) (06 §
    // Primitivas 2D): el original da vuelta la suya, H − y, en cada vértice.
    float x = 0.0f;
    float y = 0.0f;
    // En la textura: (0, 0) es el principio de la fila 0 del buffer, la primera que sube
    // glTexImage2D, y v crece hacia las filas siguientes.
    float u = 0.0f;
    float v = 0.0f;
    Color color{};
};
} // namespace Client::Render
