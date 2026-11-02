#pragma once

#include "App/FaithfulPacer.h"
#include "App/FixedStep.h"
#include "App/ILoopClient.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Input/InputState.h"

#include <cstdint>
#include <optional>

// El bucle de juego (CL3.2): por frame, los pasos de la lógica que tocan y el dibujo. El reloj va
// inyectado, así que el mismo bucle corre con el de SDL en Main y con uno falso en los tests.
//
// En Decoupled, el frame se mide de un Now() al siguiente, al empezar RunFrame, y va a un
// FixedStep: el dibujo se interpola con su alfa. El primer frame solo fija el inicio. Con un tope
// de FPS (maxFps, el FpsCap de MuMain), después de dibujar duerme lo que le falta al frame para
// durar 1 s / maxFps desde su Now(). En Faithful, va a un FaithfulPacer: BeginFrame, esos pasos,
// StartMeasure, el dibujo con alfa 0 y EndFrame; el tope no se usa (Resolve no lo deja).
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
    // maxFps: el tope de FPS del modo normal, de 1 a MaxFpsLimit, o 0 (sin tope).
    GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode, int maxFps = 0);
    // Un frame: los pasos que tocan y el dibujo si `state.minimized` es falso. Minimizada, en
    // Decoupled duerme hasta el próximo paso en vez de girar; en Faithful, la espera corre siempre.
    // Cada paso recibe `input`, y después del primero GameLoop le llama a ConsumeEdges: un
    // apretón lo ve un solo paso, y en un frame sin pasos `input` no se toca y el apretón espera
    // al próximo (CL3.3, Q-C45). Devuelve cuántos pasos corrió.
    int RunFrame(const WindowState& state, Input::InputState& input);
    [[nodiscard]] const FrameStats& Stats() const;
    [[nodiscard]] float LastAlpha() const;

private:
    int RunDecoupled(const WindowState& state, Input::InputState& input);
    int RunFaithful(const WindowState& state, Input::InputState& input);
    void RunSteps(int steps, Input::InputState& input);

    Core::IClock& m_clock;
    ILoopClient& m_client;
    PacingMode m_mode;
    // 1 s / maxFps, o 0 sin tope.
    Core::Duration m_minFrameTime{};
    FixedStep m_fixedStep;
    FaithfulPacer m_pacer;
    // El Now() del frame anterior; vacío antes del primero.
    std::optional<Core::Duration> m_lastNow;
    FrameStats m_stats;
    float m_lastAlpha = 0.0f;
};
} // namespace Client::App
