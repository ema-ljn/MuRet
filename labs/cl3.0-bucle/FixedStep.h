#pragma once

#include <chrono>

// Misión de la CL3.0 (docs/learning/ola-cl3.0-el-bucle-de-juego.md, actividad 4): el paso fijo con
// acumulador, en milisegundos enteros como el original, y uno con std::chrono para comparar
// (actividad 9).
// Contrato: Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El ritmo".
//
// Se suma lo que tardó el frame y corren pasos de 40 ms mientras alcance. Lo que no llega a un paso
// queda para el frame siguiente: la lógica avanza 25 pasos por segundo con cualquier FPS.
namespace Labs
{
// El paso de la lógica del original: 0x28, 40 ms (CMP [0x6d5ee4],0x28 en 0x637791 y SUB EAX,0x28
// en 0x638167).
inline constexpr int StepMs = 40;

// El mismo paso como duración, para el ChronoFixedStep.
inline constexpr std::chrono::nanoseconds Step = std::chrono::milliseconds{StepMs};

class FixedStep
{
public:
    // Suma un frame de `frameMs` y devuelve cuántos pasos de StepMs corren. Un frame negativo
    // cuenta 0: un reloj que vuelve para atrás no puede dejar el acumulador debiendo. La suma se
    // hace en 64 bits, así que ningún int se pasa de rango.
    [[nodiscard]] int Advance(int frameMs);

    // Lo que quedó sin llegar a un paso: siempre en [0, StepMs).
    [[nodiscard]] int RestMs() const;

private:
    int m_restMs = 0;
};

// Lo mismo con std::chrono::nanoseconds: no hay que truncar el frame a ms antes de sumarlo.
class ChronoFixedStep
{
public:
    // Como FixedStep::Advance: un frame negativo cuenta 0. Un frame de más de INT_MAX pasos (unos
    // 994 días) cuenta INT_MAX pasos, así que la cantidad entra en un int y la suma con el resto no
    // se pasa de los 64 bits, ni con nanoseconds::max().
    [[nodiscard]] int Advance(std::chrono::nanoseconds frame);

    // Siempre en [0, Step).
    [[nodiscard]] std::chrono::nanoseconds Rest() const;

private:
    std::chrono::nanoseconds m_rest{0};
};
} // namespace Labs
