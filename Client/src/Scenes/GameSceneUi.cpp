#include "Scenes/GameScene.h"

#include "App/FaithfulPacer.h"
#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Core/PathText.h"
#include "Data/DataPath.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Input/MouseMessageState.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/FloorItems.h"
#include "Protocol/Inventory.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Animation.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorPickingBox.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/EventDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameData.h"
#include "Scenes/GameDialogDrawing.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameHover.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Scenes/GameSocialDrawing.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/LetterPortrait.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTexts.h"
#include "UI/ChatInput.h"
#include "UI/GameCursor.h"
#include "UI/GameUiState.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <span>
#include <system_error>
#include <utility>

namespace Client::Scenes
{
namespace
{
class ProjectionTintRestore
{
public:
    ProjectionTintRestore(std::span<ActorState> actors, bool active) : m_actors(actors), m_active(active)
    {
        if (!m_active)
            return;
        for (std::size_t slot = 0; slot < m_actors.size(); ++slot)
        {
            m_byteE8[slot] = m_actors[slot].byteE8;
            m_light[slot] = m_actors[slot].light;
        }
    }

    ~ProjectionTintRestore()
    {
        Restore();
    }

    void Restore()
    {
        if (!m_active)
            return;
        for (std::size_t slot = 0; slot < m_actors.size(); ++slot)
        {
            m_actors[slot].byteE8 = m_byteE8[slot];
            m_actors[slot].light = m_light[slot];
        }
        m_active = false;
    }

private:
    std::span<ActorState> m_actors;
    std::array<std::uint8_t, ActorPoolSize> m_byteE8{};
    std::array<glm::vec3, ActorPoolSize> m_light{};
    bool m_active = false;
};

// El WndProc: WM_RBUTTONDOWN apaga 0x7d36840 y prende 0x7d36880 (con 0x7d3685c en 0, que el apretón siempre ve así:
// lo apagó el WM_RBUTTONUP de antes) y 0x7d3685c, y WM_RBUTTONUP apaga los dos y, si 0x7d3685c estaba prendido, prende
// 0x7d36840. MuRet ve los mensajes del paso juntos: apretado y soltado en el mismo paso, quedan apagados y el soltado
// prendido, como después de los dos.
void LatchTheRightButton(UI::GameUiState& ui, const Input::InputState& input)
{
    const bool down = input.IsDown(Input::MouseButton::Right);
    const bool pressed = input.WasPressed(Input::MouseButton::Right);
    if (pressed)
        ui.rightReleased = false;
    if (!down && (ui.rightDown || pressed))
        ui.rightReleased = true;
    ui.rightClick = down && (ui.rightClick || pressed);
    ui.rightDown = down && (ui.rightDown || pressed);
}

// 0x467b35..0x467b67: GetAsyncKeyState de 0x26, 0x28, 0x25 y 0x27 (el byte alto en 0x80), que la subclase de los EDIT
// mira con cada mensaje.
bool AnArrowIsDown(const Input::InputState& input)
{
    return input.IsDown(Input::Key::Up) || input.IsDown(Input::Key::Down) || input.IsDown(Input::Key::Left) ||
           input.IsDown(Input::Key::Right);
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

    void SendGuildPacket(Protocol::Outgoing packet) override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se manda el paquete del gremio: no hay servidor");
            return;
        }
        m_session->SendGuildPacket(std::move(packet));
    }

    void SendGamePacket(Protocol::Outgoing outgoing) override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no sale el paquete del comercio, la party o el duelo: no hay servidor");
            return;
        }
        m_session->SendGamePacket(std::move(outgoing));
    }

    void SendWindowClose() override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se cierra la ventana (C1 03 31): no hay servidor");
            return;
        }
        m_session->SendWindowClose();
    }

    void SendSetQuest(std::uint8_t quest, std::uint8_t state) override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se manda el A2 de la misión {}: no hay servidor", quest);
            return;
        }
        m_session->SendSetQuest(quest, state);
    }

    void SendFriendPacket(Protocol::Outgoing packet) override
    {
        if (m_session == nullptr)
        {
            m_log.Warning("no se manda el paquete de los amigos: no hay servidor");
            return;
        }
        m_session->SendFriendPacket(std::move(packet));
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
    const Input::MouseMessageState& mouseMessages = input.MouseMessages();
    LatchTheLeftRelease(runtime.movement, mouseMessages.leftPress, mouseMessages.leftRelease, mouseMessages.leftDown,
                        mouse.x, mouse.y);
    LatchTheLeftButton(runtime.movement, mouseMessages.leftPress, mouseMessages.leftDown);
    if (const std::optional<bool> doubleClick = input.TakeDoubleClickMessage(); doubleClick.has_value())
        runtime.ui.doubleClick = *doubleClick;
    LatchTheRightButton(runtime.ui, input);
    LatchTheWheel(runtime.ui, input);
    // Los eventos de texto, edición y composición, en el orden en que los atendió InputState.
    if (auto typed = TypeTheGameInput(ChatContext(), input.TextInputEvents()); !typed.HasValue())
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
                                            .inventory = &inventory,
                                            .worldEntry = m_context.worldEntry});
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
                                       .texts = m_texts,
                                       .requests = &requests,
                                       .systemTime = systemTime,
                                       .inventory = &inventory,
                                       .itemSetTables = m_chatTables.itemSetTables,
                                       .skills = m_skills,
                                       .worldEntry = m_context.worldEntry});
}

GameChatContext GameScene::ChatContext()
{
    App::ServerSession* session = m_session;
    return GameChatContext{
        .world = *m_context.world,
        .random = m_context.cloth->random,
        .texts = m_texts,
        .items = m_items,
        .setOptions = m_chatTables.setOptions,
        .filter = m_chatTables.filter,
        .filterName = m_chatTables.filterName,
        .measure = m_context.textMeasure,
        .screen = m_screen,
        .send = session != nullptr
                    ? ChatSender([session](Protocol::Outgoing packet) { session->SendChat(std::move(packet)); })
                    : ChatSender{},
        .sendWindowClose = session != nullptr ? std::function<void()>([session] { session->SendWindowClose(); })
                                              : std::function<void()>{},
        .clipboard = m_context.clipboard,
        .models = m_models};
}

InventoryServices GameScene::InventoryContext()
{
    InventoryServices services =
        m_context.worldEntry != nullptr ? InventoryServicesOf(*m_context.worldEntry) : InventoryServices{};
    if (m_context.worldEntry == nullptr)
    {
        services.items = m_items;
        services.texts = m_texts;
    }
    if (App::ServerSession* session = m_session; session != nullptr)
    {
        services.sendItemMove = [session](const Protocol::Inventory::ItemMove& move)
        { return session->SendItemMove(move); };
        // CL9 hito 5: el 23, el 22 y el 26.
        services.sendDrop = [session](const Protocol::FloorItems::Drop& drop) { return session->SendDrop(drop); };
        services.sendPickUp = [session](std::uint16_t index) { return session->SendPickUp(index); };
        services.sendUseItem = [session](const Protocol::FloorItems::UseItem& use)
        { return session->SendUseItem(use); };
        // CL9 hito 7: el 3C del comercio que mandan levantar y soltar en su grilla.
        services.sendGamePacket = [session](Protocol::Outgoing packet) { session->SendGamePacket(std::move(packet)); };
    }
    services.pickTheGround = [this] { return GroundUnderTheMouse(); };
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
    ProjectionTintRestore tintRestore(m_context.world->Actors(), m_projectingFrame);
    auto picked = PickInTheFrame(FramePickingContext{
        .world = *m_context.world,
        .random = FrameCloth().random,
        .ownRecordDepth = m_projectingFrame ? m_projection->ownRecordDepth : runtime.ownRecordDepth,
        .targets = m_projectingFrame ? projectedTargets : runtime.selectedTargets,
        .altDown = input.IsDown(Input::Key::Alt),
        .ray = m_mouseRay,
        .mouse = UI::MouseIn640x480(input, canvas.screen),
        .cameraPosition = m_drawnCamera.position,
        .pickingBoxes = m_projectingFrame ? std::span(m_projection->actorPickingBoxes) : std::span<ActorPickingBox>{},
        .floorItems = FrameTail().floorItems});
    tintRestore.Restore();
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
        .crywolfInterface = m_projectingFrame ? &m_projection->crywolfInterface : &runtime.crywolf.ui,
        .sound = m_projectingFrame ? m_projection->sound : runtime.lastSoundId,
        .oldWindows = m_projectingFrame ? &m_projection->oldWindows : &runtime.oldWindows,
        .projecting = m_projectingFrame,
        .projection = projection,
        .sprites = canvas.sprites,
        .text = canvas.text,
        .texts = m_texts,
        .items = m_items,
        .skills = m_skills,
        .setOptions = m_chatTables.setOptions,
        .setTypes = m_chatTables.itemSetTables != nullptr ? &m_chatTables.itemSetTables->types : nullptr,
        .worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
        .tickCount = App::TickCountMs(canvas.frameClock),
        .mouse = UI::MouseIn640x480(input, canvas.screen),
        .leftDown = input.MouseMessages().leftDown,
        .arrowHeld = AnArrowIsDown(input),
        .controlDown = input.IsDown(Input::Key::Ctrl),
        .chosenServer = m_session != nullptr ? m_session->ChosenServer() : std::nullopt,
        .guildMarks = &m_guildMarks};
    if (m_session != nullptr)
    {
        if (const auto* chosen = m_session->ChosenCharacter(); chosen != nullptr)
            hud.chosenSlot = chosen->slot;
    }
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
    // 0x42403d: el A9 del tooltip de las mascotas sale por la sesión del juego, como el del botón de la mascota.
    if (App::ServerSession* session = m_session; session != nullptr && !m_projectingFrame)
        hud.send = [session](Protocol::Outgoing packet) { session->SendChat(std::move(packet)); };
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
                           .projecting = m_projectingFrame,
                           .texts = m_texts,
                           .names = &hud};
    if (auto drawn = DrawTheHover(hover); !drawn.HasValue())
        return drawn;
    // 0x637660: 0x5a7320(1), el HUD, con 0x7d36899 en 0.
    if (auto drawn = DrawTheHud(hud); !drawn.HasValue())
        return drawn;
    // 0x637668: 0x5a7eb0, el tiempo del F3 22 (Scenes/EventDrawing).
    if (auto drawn = DrawTheEventTime(hud); !drawn.HasValue())
        return drawn;
    // 0x637673: 0x490390, las ventanas viejas, y 0x637680: la lista de ventanas, siempre. Un dibujo intermedio de MuRet
    // las dibuja sobre su copia.
    UI::OldWindowManager& oldWindows = m_projectingFrame ? m_projection->oldWindows : runtime.oldWindows;
    const UI::OldWindowDrawContext windowContext{.sprites = canvas.sprites,
                                                 .text = canvas.text,
                                                 .dcFont = hud.hud.dcFont,
                                                 .textColors = hud.hud.textColors,
                                                 .arrowHeld = hud.arrowHeld};
    // 0x659290(1) no saca rand(): se puede mirar antes de la ventana que la pregunta.
    GamePanels panels(*m_context.world, FrameCloth().random);
    // Los textos de la ventana de amigos, la lista con las flechas del teclado (0x5762ec0), el EDIT con el foco de
    // Windows (GetFocus() de 0x468320: el de las ventanas viejas, con ChatFocus::OldWindow) y dónde va el registro del
    // chat (0x6c7a88).
    const UI::OldWindowTexts friendTexts = OldWindowTextsOf(m_texts);
    const bool editFocused = runtime.chat.input.focus == UI::ChatFocus::OldWindow;
    // 0x49a1d0 y 0x498800 → 0x497c10: el retrato de las cartas, con el juego de SceneContext::worldEntry, los modelos
    // (los del personaje y, para el ayudante, los del mundo) y lo del frame, o sus copias en un dibujo intermedio. Sin
    // el juego o sin los modelos, el retrato a la vista corta.
    std::optional<std::string> portraitPoseFailure;
    std::optional<LetterPortraitDraw> portraitDraw;
    if (m_models != nullptr && m_context.worldEntry != nullptr)
    {
        const bool originalFrame = !m_projectingFrame;
        portraitDraw.emplace(LetterPortraitDraw{
            .hud = hud,
            .batch = canvas.models,
            .flushSprites = canvas.flushSprites,
            .flushModels = canvas.flushModels,
            .characters =
                GameCharacterPass{.dynamicLighting = &FrameLighting(),
                                  .clothWorld = &FrameCloth(),
                                  .frameClock = canvas.frameClock,
                                  .originalFrames = originalFrame ? 1 : 0,
                                  .sprites = &FrameSpritesForDraw(),
                                  .poseCarry = &FramePoseCarry(),
                                  .joints = FrameTail().joints,
                                  .particles = &FrameParticles(),
                                  .effects = &FrameEffects(),
                                  .itemContext = &itemContext,
                                  .pace = originalFrame ? Render::Models::OriginalFramePace::OncePerDraw
                                                        : OriginalFramePaceOf(m_context.pacing),
                                  .onPose =
                                      [&](std::uint16_t model, std::span<const Render::Models::BoneTransform> bones,
                                          const Render::Models::ObjectTransform& transform, bool translated)
                                  {
                                      const auto published = PublishFaunaPoseRaw(FramePoseCarry(), *m_models, model,
                                                                                 bones, transform, translated);
                                      if (!published.HasValue() && !portraitPoseFailure)
                                          portraitPoseFailure = published.Error();
                                  },
                                  .publishProjectedPose = true,
                                  .poseBones = [&](std::uint16_t)
                                  { return std::span<const Render::Models::BoneTransform>(FramePoseCarry().bones); },
                                  .worldModels = m_worldModelsMap ? &m_worldModels : nullptr,
                                  .fixedHeight = CameraState().fixedHeight},
            .helperModels = &m_worldModels,
            .onHelperPose = [&](const DrawnObject& object)
            { return PublishFaunaPose(FramePoseCarry(), m_worldModels, object, true); },
            .posedMeshes = [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); },
            // timeGetTime (0x6b8544), del calco 0x14e de los Fenrir: el reloj del dibujo en ms.
            .systemTime = [systemTime = static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count())]()
            { return systemTime; },
            .globalPose = [&]() { return std::span<const Render::Models::BoneTransform>(FramePoseCarry().bones); },
            .originalFrame = originalFrame});
    }
    std::function<Core::Expected<void, std::string>(UI::OldPortraitPart&)> drawThePortrait;
    if (portraitDraw)
        drawThePortrait = [&](UI::OldPortraitPart& part) -> Core::Expected<void, std::string>
        {
            auto drawn = DrawTheLetterPortrait(part, *portraitDraw, *m_context.worldEntry);
            if (drawn.HasValue() && portraitPoseFailure)
                return Core::Unexpected(*portraitPoseFailure);
            return drawn;
        };
    // Una pasada intermedia parte del mismo estado que su copia del administrador y no publica efectos de red.
    UI::GameUiState projectedUi = runtime.ui;
    GameMovementState projectedMovement = runtime.movement;
    UI::ChatFocus projectedFocus = runtime.chat.input.focus;
    UI::GameUiState& drawUi = m_projectingFrame ? projectedUi : runtime.ui;
    GameMovementState& drawMovement = m_projectingFrame ? projectedMovement : runtime.movement;
    UI::ChatFocus& drawFocus = m_projectingFrame ? projectedFocus : runtime.chat.input.focus;
    const UI::OldWindowGlobals drawGlobals{.leftHeld = drawMovement.leftHeld,
                                           .leftClick = drawMovement.pendingClick,
                                           .releasedClick = drawMovement.releasedClick,
                                           .mouseOnUi = drawUi.mouseOnUi,
                                           .hovered = drawUi.hovered,
                                           .capture = drawUi.capture,
                                           .pressedButton = drawUi.pressedButton,
                                           .keyboardFocus = drawFocus,
                                           .sound = m_projectingFrame ? m_projection->sound : runtime.lastSoundId,
                                           .focusedList = drawUi.focusedList,
                                           .wheel = drawUi.wheel,
                                           .rightClick = drawUi.rightClick,
                                           .doubleClick = drawUi.doubleClick};
    const UI::OldWindowMeasure drawMeasure{.measure = canvas.text, .dcFont = hud.hud.dcFont, .screen = canvas.screen};
    SceneUiRequests drawRequests(m_session, m_context.log, m_context.dataRoot);
    const UI::OldWindowStepView drawStep{.mouseX = hud.mouse.x,
                                         .mouseY = hud.mouse.y,
                                         .keyEdge = [&drawUi, &input](std::uint8_t key)
                                         { return OldWindowKeyEdge(drawUi, input, key); },
                                         .chatOpen = drawUi.chatOpen,
                                         .texts = &friendTexts,
                                         .measure = &drawMeasure,
                                         .send =
                                             [this, &drawRequests](const UI::FriendSend& send)
                                         {
                                             if (!m_projectingFrame)
                                                 drawRequests.SendFriendPacket(FriendPacketOf(send));
                                         }};
    const UI::OldWindowDrawView windowView{.mouseX = hud.mouse.x,
                                           .mouseY = hud.mouse.y,
                                           .leftClick = runtime.movement.pendingClick,
                                           .friendsOpen = panels.Registry().IsOpen(UI::FriendsPanel),
                                           .texts = &friendTexts,
                                           .focusedList = runtime.ui.focusedList,
                                           .focusedEdit = editFocused ? oldWindows.focusedEdit : 0,
                                           .chatPlacement = runtime.ui.chatModes.placement,
                                           .portrait = drawThePortrait,
                                           .globals = &drawGlobals,
                                           .stepView = &drawStep};
    if (auto windows = UI::DrawTheOldWindows(oldWindows, windowContext, windowView); !windows.HasValue())
        return windows;
    if (auto list = UI::DrawTheWindowList(oldWindows, windowContext, windowView); !list.HasValue())
        return list;
    // 0x637683: 0x5f37f0, el pedido de duelo, con 0x78abad9, que prende el AC (CL9 hito 7).
    if (auto duel = DrawTheDuelRequest(hud); !duel.HasValue())
        return duel;
    // 0x637696: 0x62db60, los diálogos, en la escena 5.
    if (auto dialogs = DrawTheGameDialogs(hud); !dialogs.HasValue())
        return dialogs;
    // 0x63769b: 0x5f7bc0, los tooltips.
    if (auto tooltips = DrawTheTooltips(hud); !tooltips.HasValue())
        return tooltips;
    // 0x6376a0..0x6376b6: los avisos y, con el registro del chat en su ventana, el área vieja.
    DrawTheMessages(*m_context.world, hud.hud, canvas.sprites, canvas.text, hud.mouse, true);
    // 0x6376bb: 0x5b0550, las superposiciones.
    if (auto overlays = DrawTheOverlays(hud); !overlays.HasValue())
        return overlays;
    // 0x6376c6: 0x40eca0, el cartel del set, con +0x9305 (lo abre el chat con su nombre).
    if (auto label = DrawTheSetLabel(hud); !label.HasValue())
        return label;
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
