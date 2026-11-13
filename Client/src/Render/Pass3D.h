#pragma once

#include "App/WindowState.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>

#include <optional>
#include <utility>

// Un pase 3D como lo arma 0x620db0 (06 § Cámara): el viewport, la perspectiva y la vista de una
// cámara, sin GL. Lo dibuja Gl::GlModelRenderer con el ModelBatch de la escena, y lo recibe el
// IWorldView (CL7).
namespace Client::Render
{
// Lo que 0x620db0 recibe: un rectángulo en 640 × 480, con la y desde arriba.
struct ViewportRect
{
    int x = 0;
    int y = 0;
    int width = 640;
    int height = 480;
};

struct Camera3D
{
    glm::vec3 position{};
    // En grados: los de los tres glRotatef, x el pitch y z el yaw (DAT_07d36868, 6c y 70).
    glm::vec3 angles{};
    // DAT_07d36899: sin el glRotatef en X.
    bool skipXRotation = false;
    float fieldOfView = 35.0f; // grados, el vertical
    float nearPlane = 20.0f;   // `near` y `far` son macros de Windows
    // Sin el × 1,4: MakePass3D lo multiplica para la perspectiva, y el rayo del mouse llega hasta acá.
    float farPlane = 0.0f;
};

struct Pass3D
{
    App::PixelSize screen;
    // x, y desde abajo, ancho y alto, en píxeles de GL: lo de glViewport.
    glm::ivec4 viewport{};
    glm::mat4 projection{1.0f};
    glm::mat4 view{1.0f};
    // El de la cámara, sin el × 1,4.
    float farPlane = 0.0f;
    // glClear(GL_DEPTH_BUFFER_BIT) al empezar el pase con el material (ModelBatch::Begin con initializeMaterial):
    // solo el del retrato de las cartas (0x495949), que borra la profundidad del mundo antes de su modelo.
    bool clearDepth = false;

    // El segmento del mouse de 0x620480: de la posición de la cámara al punto del píxel (x, y) de la
    // pantalla, con la y desde arriba, que está a farPlane delante de la cámara, sobre su eje.
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> RayThrough(float x, float y) const;

    // Dónde cae un punto del mundo, en píxeles con la y desde arriba; std::nullopt si queda detrás
    // de la cámara.
    [[nodiscard]] std::optional<glm::vec2> Project(const glm::vec3& world) const;
};

// 0x620db0 en una pantalla de `screen` píxeles: el rectángulo escalado en enteros (x · W / 640,
// y · H / 480), la y de GL desde abajo (0x620d10: H − y − h), gluPerspective(fov, w / h, near,
// far · 1,4) con el ancho y el alto del viewport (0x6203c0, 0x6b8c00), y la vista de glRotatef en
// Y, en X (salvo skipXRotation) y en Z, y glTranslatef(−posición).
[[nodiscard]] Pass3D MakePass3D(App::PixelSize screen, const ViewportRect& rect, const Camera3D& camera);
} // namespace Client::Render
