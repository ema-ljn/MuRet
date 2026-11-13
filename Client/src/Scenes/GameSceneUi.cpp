#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Core/PathText.h"
#include "Data/DataPath.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Inventory.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameData.h"
#include "Scenes/GameDialogDrawing.h"
#include "Scenes/GameHover.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTexts.h"
#include "UI/GameCursor.h"
#include "UI/GameUiState.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <system_error>
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

// Lo que piden los diálogos del juego: el F3 30 y el F1 02 por la sesión (sin servidor, un aviso al log),
// Data\Macro.txt y el log.
class SceneUiRequests final : public IGameUiRequests
{
public:
    SceneUiRequests(App::ServerSession* session, Core::Log& log, const std::filesystem::path& dataRoot)
        : m_session(session), m_log(log), m_dataRoot(dataRoot)
    {
    }

    void SendOptionData(const Protocol::OptionData::OptionBody& body) override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se manda el F3 30: no hay servidor");
            return;
        }
        if (auto sent = m_session->SendOptionData(body); !sent.HasValue())
            m_log.Warning("no se manda el F3 30: {}", sent.Error());
    }

    void RequestLogout(std::uint8_t kind) override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se pide la salida (F1 02 con {}): no hay servidor", kind);
            return;
        }
        m_session->RequestLogout(kind);
    }

    void RequestPartyList() override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se pide la lista de la party (C1 03 42): no hay servidor");
            return;
        }
        m_session->RequestPartyList();
    }

    // 0x61c170: fopen("wt") crea el archivo o lo vacía. Lo que guardan las macros no va al log.
    Core::Expected<void, std::string> SaveMacros(std::string_view content) override
    {
        std::error_code error;
        std::optional<std::filesystem::path> path = Data::ResolveDataPath(m_dataRoot, MacroFile, error);
        if (error)
            return Core::Unexpected(
                std::format("no se pudo buscar {} en {}: {}", MacroFile, Core::PathText(m_dataRoot), error.message()));
        if (!path)
            path = m_dataRoot / MacroFile;
        std::ofstream file(*path, std::ios::binary | std::ios::trunc);
        if (!file)
            return Core::Unexpected(std::format("0x61c17c: no se pudo abrir {} para escribir (el original sigue con "
                                                "fprintf sobre NULL)",
                                                Core::PathText(*path)));
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file)
            return Core::Unexpected(std::format("no se pudo escribir {}", Core::PathText(*path)));
        return {};
    }

    void Log(std::string_view line) override
    {
        m_log.Info("{}", line);
    }

private:
    App::ServerSession* m_session;
    Core::Log& m_log;
    const std::filesystem::path& m_dataRoot;
};

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
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    LatchTheLeftRelease(runtime.movement, input.WasPressed(Input::MouseButton::Left),
                        input.WasReleased(Input::MouseButton::Left), input.IsDown(Input::MouseButton::Left), mouse.x,
                        mouse.y);
    LatchTheLeftButton(runtime.movement, input.WasPressed(Input::MouseButton::Left),
                       input.IsDown(Input::MouseButton::Left));
    LatchTheRightButton(runtime.ui, input);
    LatchTheWheel(runtime.ui, input);
    // Y los WM_CHAR, en su orden: Tab y Enter al WndProc (la entrada del chat) y lo demás al EDIT con el foco.
    if (auto typed = TypeInTheGameChat(ChatContext(), input.Text()); !typed.HasValue())
        return typed;
    // 0x637c4a..0x637c5c: con 0x5762eb4 en 0 (nadie agarró el mouse: el registro del chat o una ventana vieja) y la
    // escena 5, 0x623e70 antes de la lógica de la escena (0x636290), con el mundo cargado o no.
    if (runtime.ui.capture != 0)
        return {};
    SceneUiRequests requests(m_session, m_context.log, m_context.dataRoot);
    std::optional<std::uint8_t> chosenSlot;
    if (m_session != nullptr)
    {
        if (const Protocol::Character::CharacterEntry* chosen = m_session->ChosenCharacter(); chosen != nullptr)
            chosenSlot = chosen->slot;
    }
    const InventoryServices inventory = InventoryContext();
    return StepTheGameDialogs(GameUiContext{.world = *m_context.world,
                                            .random = m_context.cloth->random,
                                            .input = input,
                                            .mouseX = mouse.x,
                                            .mouseY = mouse.y,
                                            .measure = m_context.textMeasure,
                                            .screen = m_screen,
                                            .texts = m_texts,
                                            .chosenSlot = chosenSlot,
                                            .requests = &requests,
                                            .inventory = &inventory});
}

Core::Expected<void, std::string> GameScene::StepTheUi(const Input::InputState& input)
{
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    SceneUiRequests requests(m_session, m_context.log, m_context.dataRoot);
    const GameChatContext chat = ChatContext();
    const InventoryServices inventory = InventoryContext();
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
                                       .requests = &requests,
                                       .systemTime = systemTime,
                                       .inventory = &inventory});
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

InventoryServices GameScene::InventoryContext() const
{
    InventoryServices services =
        m_context.worldEntry != nullptr ? InventoryServicesOf(*m_context.worldEntry) : InventoryServices{};
    if (m_context.worldEntry == nullptr)
        services.items = m_items;
    if (App::ServerSession* session = m_session; session != nullptr)
        services.sendItemMove = [session](const Protocol::Inventory::ItemMove& move)
        { return session->SendItemMove(move); };
    return services;
}

Core::Expected<void, std::string> GameScene::DrawTheInterface(SceneCanvas& canvas, const GameProjection* projection)
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
        .projection = projection,
        .sprites = canvas.sprites,
        .text = canvas.text,
        .texts = m_texts,
        .items = m_items,
        .skills = m_skills,
        .worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
        .mouse = UI::MouseIn640x480(input, canvas.screen),
        .leftDown = input.IsDown(Input::MouseButton::Left)};
    // 0x5ac120: el pase 3D de los ítems de la UI, con los modelos de los ítems, el buffer global de la pose y los
    // contextos de 0x60b3d0 del frame (o sus copias en un dibujo intermedio). Sin la biblioteca, sin dibujo.
    ItemSpecialDrawContext itemSpecial;
    ItemDrawContext itemContext;
    std::optional<UiItemResources> uiItems;
    if (m_models != nullptr)
    {
        auto pass = MakeUiItemPass(canvas.screen);
        if (!pass.HasValue())
            return Core::Unexpected(pass.Error());
        FillTheItemContexts(itemSpecial, itemContext, canvas.frameClock, m_projectingFrame ? 0 : 1);
        uiItems.emplace(UiItemResources{.batch = canvas.models,
                                        .models = *m_models,
                                        .poseCarry = FramePoseCarry(),
                                        .itemContext = itemContext,
                                        .frameClock = canvas.frameClock,
                                        .flushSprites = canvas.flushSprites,
                                        .flushModels = canvas.flushModels,
                                        .pass = pass.Value()});
        hud.uiItems = &*uiItems;
    }
    // 0x637646: 0x6219b0 empieza el 2D (gluOrtho2D y MODELVIEW identidad, con push): el lote de SceneCanvas.
    // 0x63764b: 0x4383f0, los textos 3D.
    WorldTextDrawContext worldTexts{.queue = runtime.worldTexts,
                                    .projection = projection,
                                    .sprites = canvas.sprites,
                                    .text = canvas.text,
                                    .hud = hud.hud,
                                    .projecting = m_projectingFrame};
    if (auto drawn = DrawTheWorldTexts(worldTexts); !drawn.HasValue())
        return drawn;
    // 0x637650: 0x5bbbc0, el hover y Alt.
    GameHoverContext hover{.world = *m_context.world,
                           .tail = FrameTail(),
                           .random = FrameCloth().random,
                           .targets = m_projectingFrame ? projectedTargets : runtime.selectedTargets,
                           .hud = hud.hud,
                           .altLatch =
                               m_projectingFrame ? m_projection->altLatch : runtime.ui.keyLatches[UI::AltVirtualKey],
                           .altNames = m_projectingFrame ? m_projection->altNames : runtime.ui.altNames,
                           .altDown = input.IsDown(Input::Key::Alt),
                           .sprites = canvas.sprites,
                           .text = canvas.text,
                           .projecting = m_projectingFrame};
    if (auto drawn = DrawTheHover(hover); !drawn.HasValue())
        return drawn;
    // 0x637660: 0x5a7320(1), el HUD, con 0x7d36899 en 0.
    if (auto drawn = DrawTheHud(hud); !drawn.HasValue())
        return drawn;
    // 0x637668: 0x5a7eb0, la cuenta y los resultados de los eventos (CL9.6).
    // 0x637673: 0x490390, las ventanas viejas, y 0x637680: la lista de ventanas, siempre. Un dibujo intermedio de MuRet
    // las dibuja sobre su copia.
    UI::OldWindowManager& oldWindows = m_projectingFrame ? m_projection->oldWindows : runtime.oldWindows;
    const UI::OldWindowDrawContext windowContext{
        .sprites = canvas.sprites, .text = canvas.text, .dcFont = hud.hud.dcFont, .textColors = hud.hud.textColors};
    // 0x659290(1) no saca rand(): se puede mirar antes de la ventana que la pregunta.
    GamePanels panels(*m_context.world, FrameCloth().random);
    const UI::OldWindowDrawView windowView{.mouseX = hud.mouse.x,
                                           .mouseY = hud.mouse.y,
                                           .leftClick = runtime.movement.pendingClick,
                                           .friendsOpen = panels.Registry().IsOpen(UI::FriendsPanel)};
    if (auto windows = UI::DrawTheOldWindows(oldWindows, windowContext, windowView); !windows.HasValue())
        return windows;
    if (auto list = UI::DrawTheWindowList(oldWindows, windowContext, windowView); !list.HasValue())
        return list;
    // 0x637683: 0x5f37f0, el pedido de duelo, con 0x78abad9 (hito 4).
    // 0x637696: 0x62db60, los diálogos, en la escena 5.
    if (auto dialogs = DrawTheGameDialogs(hud); !dialogs.HasValue())
        return dialogs;
    // 0x63769b: 0x5f7bc0, los tooltips (hito 4).
    // 0x6376a0..0x6376b6: los avisos y, con el registro del chat en su ventana, el área vieja.
    DrawTheMessages(*m_context.world, hud.hud, canvas.sprites, canvas.text, hud.mouse, true);
    // 0x6376bb: 0x5b0550, las superposiciones.
    if (auto overlays = DrawTheOverlays(hud); !overlays.HasValue())
        return overlays;
    // 0x6376c6: 0x40eca0, el cartel del conjunto, con +0x9305 de [0x6eed3c] (lo abre el chat con su nombre).
    if (const std::uint8_t setHelp = m_context.world->ItemSets().setHelp; setHelp != 0)
        return Core::Unexpected(std::format(
            "0x6376c6: el cartel del conjunto {} (0x40eca0, con 0x40d530 y 0x5af090) es del hito 5", setHelp));
    // 0x6376cb: 0x65ee60 → 0x65fe00, la UI nueva (hito 6).
    // 0x6376d7: 0x5ac2d0, el cursor, 0x6376dc: 0x633110, el ítem 3D del diálogo (P-C90), y 0x6376e1: el cursor otra
    // vez, el mismo quad.
    if (auto cursor = DrawTheGameCursor(hud); !cursor.HasValue())
        return cursor;
    if (auto cursor = DrawTheGameCursor(hud); !cursor.HasValue())
        return cursor;
    // 0x6376e6: 0x621a90 y 0x620fc0 cierran el 2D: el lote lo dibuja quien presenta el frame.
    return {};
}
} // namespace Client::Scenes
