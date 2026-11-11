#pragma once

#include "Render/Models/Animation.h"

#include <cstdint>

// Los frames del original de una escena, contados por los pasos de lógica que corrió y no por el reloj del
// dibujo (#307). El original corre los pasos que debe y después dibuja (02 § El ritmo), así que cada frame
// ve 1 o 2 pasos: 75 cada 64 frames. Contados por el reloj del dibujo, a 60 FPS, el dibujo que cruza la marca
// de 46,875 ms a veces llega antes del paso que le toca y a veces después: unos 40 frames por minuto no
// veían ningún paso y el siguiente veía 2, y la capa del que camina daba un tirón. Con el reloj de los pasos
// (LogicStep cada uno), un frame del original es el dibujo que viene después de sus pasos, a cualquier FPS.
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

    // Cuántos frames del original corre este dibujo: OriginalFrameCarry con el reloj de los pasos. El primero
    // corre uno; en el modo fiel (OncePerDraw), uno por dibujo.
    [[nodiscard]] int FramesAtDraw(Render::Models::OriginalFramePace pace);

private:
    Render::Models::OriginalFrameCarry m_frames;
    std::uint64_t m_steps = 0;
};
} // namespace Client::Scenes
