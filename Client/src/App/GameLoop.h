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

// El bucle de juego (CL3.2): por frame, la red (el Poll de la CL4.1), los pasos de la lógica que
// tocan y el dibujo. El reloj va inyectado, así que el mismo bucle corre con el de SDL en Main y
// con uno falso en los tests.
//
// En Decoupled, el frame se mide de un Now() al siguiente, al empezar RunFrame después del Poll,
// y va a un FixedStep: el dibujo se interpola con su alfa. El primer frame solo fija el inicio.
// Con un tope de FPS (maxFps, el FpsCap de MuMain), después de dibujar duerme hasta la meta del
// frame: 1 s / maxFps después de la del anterior, o de donde terminó el anterior si se pasó
// (SleepRestOfFrame). Así lo que corre entre dos RunFrame no se suma, y el tope nunca da menos FPS
// que no tenerlo. En Faithful, va a un FaithfulPacer: el Poll, BeginFrame, esos pasos,
// StartMeasure, el dibujo con alfa 0 y EndFrame; el Poll queda fuera de la medición, y el tope no
// se usa (Resolve no lo deja).
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
    // maxFps: el tope de FPS del modo normal; 0 o menos es sin tope. Main pasa 0 o un valor de
    // MinFpsLimit a MaxFpsLimit (Resolve y EffectiveMaxFps).
    GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode, int maxFps = 0);
    // Un frame: un Poll, los pasos que tocan y el dibujo si `state.minimized` es falso y el
    // cliente no retiene el último frame (ILoopClient::HoldsTheLastFrame). El Poll corre siempre,
    // también minimizada y en un frame sin pasos. Sin dibujo, en Decoupled duerme hasta el próximo
    // paso en vez de girar; en Faithful, la espera corre siempre.
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
    // El tope de FPS, después de dibujar el frame que empezó en `frameStart`.
    void SleepRestOfFrame(Core::Duration frameStart);

    Core::IClock& m_clock;
    ILoopClient& m_client;
    PacingMode m_mode;
    // 1 s / maxFps, o 0 sin tope.
    Core::Duration m_minFrameTime{};
    // Desde dónde cuenta la meta del próximo frame: hasta dónde durmió el último, o dónde terminó si
    // se pasó. Vacía antes del primero, al minimizar y si el reloj volvió durante el frame.
    std::optional<Core::Duration> m_frameDeadline;
    FixedStep m_fixedStep;
    FaithfulPacer m_pacer;
    // El Now() del frame anterior; vacío antes del primero.
    std::optional<Core::Duration> m_lastNow;
    FrameStats m_stats;
    float m_lastAlpha = 0.0f;
};
} // namespace Client::App
