#pragma once

#include "App/FixedStep.h"
#include "App/ILoopClient.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Core/IClock.h"

#include <cstdint>
#include <optional>

// El bucle de juego (CL3.2): por frame, los pasos de la lógica que tocan y el dibujo. El reloj va
// inyectado, así que el mismo bucle corre con el de SDL en Main y con uno falso en los tests.
//
// En Decoupled, el frame se mide de un Now() al siguiente, al empezar RunFrame, y va a un
// FixedStep: el dibujo se interpola con su alfa. El primer frame solo fija el inicio.
namespace Client::App
{
// Lo que llevó el bucle hasta ahora, para --stats y para los tests.
struct FrameStats
{
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    // Solo Decoupled: los frames que pasaron MaxFrameTime y se recortaron.
    std::uint64_t clampedFrames = 0;
    // Decoupled: FixedStep::Accumulated(); Faithful: AccumulatedMs().
    Core::Duration accumulated{};
    // Solo Faithful: lo que sumó el último frame.
    std::int32_t lastMeasuredMs = 0;
};

class GameLoop
{
public:
    GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode);
    // Un frame: los pasos que tocan y el dibujo si `state.minimized` es falso. Minimizada, duerme
    // hasta el próximo paso en vez de girar. Devuelve cuántos pasos corrió.
    int RunFrame(const WindowState& state);
    [[nodiscard]] const FrameStats& Stats() const;
    [[nodiscard]] float LastAlpha() const;

private:
    int RunDecoupled(const WindowState& state);
    void RunSteps(int steps);

    Core::IClock& m_clock;
    ILoopClient& m_client;
    PacingMode m_mode;
    FixedStep m_fixedStep;
    // El Now() del frame anterior; vacío antes del primero.
    std::optional<Core::Duration> m_lastNow;
    FrameStats m_stats;
    float m_lastAlpha = 0.0f;
};
} // namespace Client::App
