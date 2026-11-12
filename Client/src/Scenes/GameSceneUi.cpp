#include "Scenes/GameScene.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Scenes/GameUi.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameCursor.h"
#include "UI/GameUiState.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
// El WndProc: WM_RBUTTONDOWN prende 0x7d36880 y WM_RBUTTONUP lo apaga. MuRet ve los dos mensajes del paso juntos:
// apretado y soltado en el mismo paso, queda apagado, como después de los dos mensajes.
void LatchTheRightButton(UI::GameUiState& ui, const Input::InputState& input)
{
    ui.rightClick =
        input.IsDown(Input::MouseButton::Right) && (ui.rightClick || input.WasPressed(Input::MouseButton::Right));
}
} // namespace

Core::Expected<void, std::string> GameScene::StepTheDialogs(const Input::InputState& input)
{
    // Enter no deja entrar sin los dos.
    if (m_context.world == nullptr || m_context.cloth == nullptr)
        return {};
    WorldRuntime& runtime = m_context.world->Runtime();
    // Los mensajes del mouse que el WndProc atendió antes de esta vuelta de WinMain.
    LatchTheLeftButton(runtime.movement, input.WasPressed(Input::MouseButton::Left),
                       input.IsDown(Input::MouseButton::Left));
    LatchTheRightButton(runtime.ui, input);
    // 0x637c4a..0x637c5c: con 0x5762eb4 en 0 (la modal de las ventanas viejas, del hito 7) y la escena 5, 0x623e70
    // antes de la lógica de la escena (0x636290), con el mundo cargado o no.
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    return StepTheGameDialogs(GameUiContext{.world = *m_context.world,
                                            .random = m_context.cloth->random,
                                            .input = input,
                                            .mouseX = mouse.x,
                                            .mouseY = mouse.y});
}

Core::Expected<void, std::string> GameScene::StepTheUi(const Input::InputState& input)
{
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    return StepTheGameUi(GameUiContext{.world = *m_context.world,
                                       .random = m_context.cloth->random,
                                       .input = input,
                                       .mouseX = mouse.x,
                                       .mouseY = mouse.y});
}

Core::Expected<void, std::string> GameScene::DrawTheInterface()
{
    WorldRuntime& runtime = m_context.world->Runtime();
    const Input::InputState noInput;
    const Input::InputState& input = m_context.input != nullptr ? *m_context.input : noInput;
    // 0x637641: 0x59ec80, el picking del frame. Un dibujo intermedio de MuRet lo corre sobre las copias: el rand(), la
    // profundidad del registro propio y los elegidos.
    std::array<std::int32_t, 5> projectedTargets = runtime.selectedTargets;
    auto picked = PickInTheFrame(
        FramePickingContext{.world = *m_context.world,
                            .random = FrameCloth().random,
                            .ownRecordDepth = m_projectingFrame ? m_projection->ownRecordDepth : runtime.ownRecordDepth,
                            .targets = m_projectingFrame ? projectedTargets : runtime.selectedTargets,
                            .altDown = input.IsDown(Input::Key::Alt)});
    if (!picked.HasValue())
        return picked;
    // 0x637646..0x6376e6: la pasada 2D, en este orden (06 § El dibujo del juego). Ninguna dibuja en MuRet hasta su
    // hito, ni saca rand(): el HUD (0x5a7320) y lo que cuelga de él, con los rand() del estado base, son del hito 2.
    // 0x637646: 0x6219b0 empieza el 2D (gluOrtho2D y MODELVIEW identidad, con push).
    // 0x63764b: 0x4383f0, los textos 3D (hito 3).
    // 0x637650: 0x5bbbc0, el hover y Alt (hito 4).
    // 0x637660: 0x5a7320(1), el HUD, con 0x7d36899 en 0 (hito 2).
    // 0x637668: 0x5a7eb0, la cuenta y los resultados de los eventos (CL9.6).
    // 0x637673: 0x490390, las ventanas con clase, y 0x637680: el marco común (hito 7).
    // 0x637683: 0x5f37f0, el pedido de duelo, con 0x78abad9 (hito 4).
    // 0x637696: 0x62db60, las cajas viejas, en la escena 5 (hito 6).
    // 0x63769b: 0x5f7bc0, los tooltips (hito 4).
    // 0x6376a0: 0x554810, los avisos (hito 3).
    // 0x6376b6: 0x5554e0, el área de mensajes, con 0x6c7a88 en 1: en 0 en el juego (hito 3).
    // 0x6376bb: 0x5b0550, las superposiciones (hito 2).
    // 0x6376c6: 0x40eca0, el cartel del set, con +0x9305 (hito 5).
    // 0x6376cb: 0x65ee60 → 0x65fe00, la UI nueva (hito 6).
    // 0x6376d7: 0x5ac2d0, el cursor (hito 2), 0x6376dc: 0x633110, el ítem 3D del diálogo (hito 6), y 0x6376e1: el
    // cursor otra vez.
    // 0x6376e6: 0x621a90 y 0x620fc0 cierran el 2D.
    return {};
}
} // namespace Client::Scenes
