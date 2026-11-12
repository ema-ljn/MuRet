#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Scenes/GameUi.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameCursor.h"
#include "UI/GameUiState.h"
#include "UI/Widget.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <ratio>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// El WndProc: WM_RBUTTONDOWN prende 0x7d36880 (con 0x7d3685c en 0, que el apretón siempre ve así: lo apagó el
// WM_RBUTTONUP de antes) y 0x7d3685c, y WM_RBUTTONUP apaga los dos. MuRet ve los mensajes del paso juntos: apretado y
// soltado en el mismo paso, quedan apagados, como después de los dos.
void LatchTheRightButton(UI::GameUiState& ui, const Input::InputState& input)
{
    const bool down = input.IsDown(Input::MouseButton::Right);
    const bool pressed = input.WasPressed(Input::MouseButton::Right);
    ui.rightClick = down && (ui.rightClick || pressed);
    ui.rightDown = down && (ui.rightDown || pressed);
}

// WM_MOUSEWHEEL (0x4b4c19) escribe la rueda del último mensaje; sin mensajes, queda la de antes.
void LatchTheWheel(UI::GameUiState& ui, const Input::InputState& input)
{
    if (const float notches = input.LastWheel(); notches != 0.0f)
        ui.wheel = static_cast<std::int32_t>(notches);
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
    LatchTheWheel(runtime.ui, input);
    // Y los WM_CHAR, en su orden: Tab y Enter al WndProc (la entrada del chat) y lo demás al EDIT con el foco.
    if (auto typed = TypeInTheGameChat(ChatContext(), input.Text()); !typed.HasValue())
        return typed;
    // 0x637c4a..0x637c5c: con 0x5762eb4 en 0 (nadie agarró el mouse: el registro del chat o, en el hito 7, las
    // ventanas viejas) y la escena 5, 0x623e70 antes de la lógica de la escena (0x636290), con el mundo cargado o no.
    if (runtime.ui.capture != 0)
        return {};
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
    const GameChatContext chat = ChatContext();
    // timeGetTime() de los segundos del cartel (0x46ada0): el reloj de la escena en ms.
    const auto systemTime =
        m_context.clock != nullptr
            ? static_cast<std::uint32_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
            : 0U;
    return StepTheGameUi(GameUiContext{.world = *m_context.world,
                                       .random = m_context.cloth->random,
                                       .input = input,
                                       .mouseX = mouse.x,
                                       .mouseY = mouse.y,
                                       .measure = m_context.textMeasure,
                                       .screen = m_screen,
                                       .chat = &chat,
                                       .systemTime = systemTime});
}

GameChatContext GameScene::ChatContext()
{
    App::ServerSession* session = m_session;
    return GameChatContext{.world = *m_context.world,
                           .random = m_context.cloth->random,
                           .texts = m_texts,
                           .items = m_items,
                           .setOptions = m_chatTables.setOptions,
                           .filter = m_chatTables.filter,
                           .measure = m_context.textMeasure,
                           .screen = m_screen,
                           .send = session != nullptr ? ChatSender([session](Protocol::Outgoing packet)
                                                                   { session->SendChat(std::move(packet)); })
                                                      : ChatSender{}};
}

Core::Expected<void, std::string> GameScene::DrawTheInterface(SceneCanvas& canvas)
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
    // 0x637646..0x6376e6: la pasada 2D, en este orden (06 § El dibujo del juego). Un dibujo intermedio de MuRet dibuja
    // con las copias: el rand(), las cuentas de accesos, los globales del HUD, el humo de Crywolf y el sonido.
    ActorState& hero = m_context.world->Actors()[runtime.hero];
    GameHudContext hud{
        .world = *m_context.world,
        .random = FrameCloth().random,
        .ownRecordDepth = m_projectingFrame ? m_projection->ownRecordDepth : runtime.ownRecordDepth,
        .cellXDepth = m_projectingFrame ? m_projection->heroXDepth : hero.cellXAccesses,
        .cellYDepth = m_projectingFrame ? m_projection->heroYDepth : hero.cellYAccesses,
        .hud = m_projectingFrame ? m_projection->hud : runtime.hud,
        .crywolfSandOverlay = m_projectingFrame ? m_projection->crywolfSandOverlay : runtime.crywolf.sandOverlay,
        .sound = m_projectingFrame ? m_projection->sound : runtime.lastSoundId,
        .projecting = m_projectingFrame,
        .sprites = canvas.sprites,
        .text = canvas.text,
        .texts = m_texts,
        .items = m_items,
        .skills = m_skills,
        .worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
        .mouse = UI::MouseIn640x480(input, canvas.screen),
        .leftDown = input.IsDown(Input::MouseButton::Left)};
    // 0x637646: 0x6219b0 empieza el 2D (gluOrtho2D y MODELVIEW identidad, con push): el lote de SceneCanvas.
    // 0x63764b: 0x4383f0, los textos 3D (hito 3).
    // 0x637650: 0x5bbbc0, el hover y Alt (hito 4).
    // 0x637660: 0x5a7320(1), el HUD, con 0x7d36899 en 0.
    if (auto drawn = DrawTheHud(hud); !drawn.HasValue())
        return drawn;
    // 0x637668: 0x5a7eb0, la cuenta y los resultados de los eventos (CL9.6).
    // 0x637673: 0x490390, las ventanas con clase, y 0x637680: el marco común (hito 7).
    // 0x637683: 0x5f37f0, el pedido de duelo, con 0x78abad9 (hito 4).
    // 0x637696: 0x62db60, las cajas viejas, en la escena 5 (hito 6).
    // 0x63769b: 0x5f7bc0, los tooltips (hito 4).
    // 0x6376a0..0x6376b6: los avisos y, con el registro del chat en su ventana, el área vieja.
    DrawTheMessages(*m_context.world, hud.hud, canvas.sprites, canvas.text, hud.mouse, true);
    // 0x6376bb: 0x5b0550, las superposiciones.
    if (auto overlays = DrawTheOverlays(hud); !overlays.HasValue())
        return overlays;
    // 0x6376c6: 0x40eca0, el cartel del set, con +0x9305 (hito 5).
    // 0x6376cb: 0x65ee60 → 0x65fe00, la UI nueva (hito 6).
    // 0x6376d7: 0x5ac2d0, el cursor, 0x6376dc: 0x633110, el ítem 3D del diálogo (hito 6), y 0x6376e1: el cursor otra
    // vez, el mismo quad.
    if (auto cursor = DrawTheGameCursor(hud); !cursor.HasValue())
        return cursor;
    if (auto cursor = DrawTheGameCursor(hud); !cursor.HasValue())
        return cursor;
    // 0x6376e6: 0x621a90 y 0x620fc0 cierran el 2D: el lote lo dibuja quien presenta el frame.
    return {};
}
} // namespace Client::Scenes
