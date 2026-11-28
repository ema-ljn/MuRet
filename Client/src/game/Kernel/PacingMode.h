#pragma once

#include <cstdint>

// El ritmo del bucle (CL3.2). Está en su propio header porque lo leen el bucle, la línea de
// comandos y la config (CL3.4), y ninguno de ellos necesita el resto del bucle.
namespace Client::App
{
enum class PacingMode : std::uint8_t
{
    // El de MuRet: el frame entero va al acumulador, con MaxFrameTime de tope, y el dibujo se
    // interpola entre pasos (D-C4).
    Decoupled,
    // El del original (0x637700): solo el dibujo, el swap y la espera de 40 ms de GetTickCount van
    // al acumulador, sin tope y sin interpolar.
    Faithful,
};
} // namespace Client::App
