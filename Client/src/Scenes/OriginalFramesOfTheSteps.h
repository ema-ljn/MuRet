#pragma once

#include "Core/IClock.h"
#include "Render/Models/Animation.h"

#include <cstdint>

// Los frames del original de una escena, contados por los pasos de lógica que corrió y no por el reloj del
// dibujo (#307). El original corre los pasos que debe y después dibuja (02 § El ritmo, 0x637791..0x6381a5):
// cada dibujo es un frame, que a 21,33 FPS ve 1 o 2 pasos (75 cada 64 frames) y en una máquina más lenta, más.
// Nunca corre dos frames en un dibujo ni un frame sin pasos. Contados por el reloj del dibujo, a 60 FPS, el
// dibujo que cruza la marca de 46,875 ms a veces llega antes del paso que le toca y a veces después: unos 40
// frames por minuto no veían ningún paso y el siguiente veía 2, y la capa del que camina daba un tirón.
namespace Client::Scenes
{
class OriginalFramesOfTheSteps
{
public:
    // Un paso de lógica de la escena.
    void Step()
    {
        ++m_steps;
    }

    // Si este dibujo es un frame del original (1) o uno intermedio (0). El primero de la escena lo es; en el modo
    // fiel (OncePerDraw), todos. Si no, lo es el dibujo que trae al menos un paso nuevo y completa 46,875 ms de
    // pasos desde el frame anterior. Como mucho uno por dibujo: lo que sobra de un frame más se descarta, y por
    // debajo de 21,33 FPS cada dibujo es un frame, con más pasos, como el original en una máquina lenta.
    [[nodiscard]] int FramesAtDraw(Render::Models::OriginalFramePace pace);

private:
    std::uint64_t m_steps = 0;
    std::uint64_t m_stepsAtTheLastDraw = 0;
    Core::Duration m_carry{};
    bool m_started = false;
};
} // namespace Client::Scenes
