#include "Scenes/GameScene.h"

#include "Kernel/FaithfulPacer.h"
#include "Session/ServerSession.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Input/MouseMessageState.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorPickingBox.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Chat/GameChat.h"
#include "Tables/GameData.h"
#include "Hud/Dialogs/GameDialogDrawing.h"
#include "Messenger/GameFriends.h"
#include "Hud/GameHover.h"
#include "Hud/GameHud.h"
#include "Hud/GameInventoryHud.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Chat/GameMessages.h"
#include "Kernel/MouseState.h"
#include "Hud/GamePanels.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Social/GameSocialDrawing.h"
#include "Tooltips/GameTooltips.h"
#include "Hud/GameUi.h"
#include "Guild/GuildDrawing.h"
#include "Items/InventoryMoves.h"
#include "Hud/MapBanner.h"
#include "Npc/NpcWindows.h"
#include "Messenger/LetterPortrait.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Items/UiItemDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Hud/WorldTexts.h"
#include "Chat/ChatInput.h"
#include "Ui/GameCursor.h"
#include "Ui/GameUiState.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
class ProjectionTintRestore
{
public:
    ProjectionTintRestore(std::span<Kernel::ActorState> actors, bool active) : m_actors(actors), m_active(active)
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
    std::span<Kernel::ActorState> m_actors;
    std::array<std::uint8_t, Kernel::ActorPoolSize> m_byteE8{};
    std::array<glm::vec3, Kernel::ActorPoolSize> m_light{};
    bool m_active = false;
};

// The WndProc: WM_RBUTTONDOWN clears 0x7d36840 and sets 0x7d36880 (with 0x7d3685c at 0, as the press always sees it:
// the previous WM_RBUTTONUP cleared it) and 0x7d3685c; WM_RBUTTONUP clears both and, if 0x7d3685c was set, sets
// 0x7d36840. 0x7d36849 is the left double click and is published separately.
void LatchTheRightButton(Ui::GameUiState& ui, const Input::InputState& input)
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

// 0x467b35..0x467b67: GetAsyncKeyState of 0x26, 0x28, 0x25 and 0x27 (the high byte at 0x80), which the EDIT subclass
// checks with each message.
bool AnArrowIsDown(const Input::InputState& input)
{
    return input.IsDown(Input::Key::Up) || input.IsDown(Input::Key::Down) || input.IsDown(Input::Key::Left) ||
           input.IsDown(Input::Key::Right);
}

// What the game dialogs ask for: the F3 30 and the F1 02 through the session (without a server, a warning to the
// log), Data\Macro.txt and the log.
class SceneUiRequests final : public Hud::IGameUiRequests
{
public:
    SceneUiRequests(App::ServerSession* session, Core::Log& log, const std::filesystem::path& dataRoot)
        : m_session(session), m_log(log), m_dataRoot(dataRoot)
    {
    }

    void SendOptionData(const Protocol::Player::OptionBody& body) override
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

    // 0x61c170: fopen("wt") creates the file or empties it. What the macros save does not go to the log.
    Core::Expected<void, std::string> SaveMacros(std::string_view content) override
    {
        return Tables::WriteMacroFile(m_dataRoot, content);
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

// WM_MOUSEWHEEL (0x4b4c19) writes the wheel of the last message; without messages, the previous one stays.
void LatchTheWheel(Ui::GameUiState& ui, const Input::InputState& input)
{
    if (const float notches = input.LastWheel(); notches != 0.0f)
        ui.wheel = static_cast<std::int32_t>(notches);
}
} // namespace

Core::Expected<void, std::string> GameScene::StepTheDialogs(const Input::InputState& input)
{
    // Enter does not let the scene in without both.
    if (m_context.world == nullptr || m_context.cloth == nullptr)
        return {};
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    // The mouse messages the WndProc handled before this turn of the WinMain loop.
    const Ui::Point mouse = Ui::MouseIn640x480(input, m_screen);
    const Input::MouseMessageState& mouseMessages = input.MouseMessages();
    Kernel::LatchTheLeftRelease(runtime.mouse, mouseMessages.leftPress, mouseMessages.leftRelease,
                                mouseMessages.leftDown, mouse.x, mouse.y);
    Kernel::LatchTheLeftButton(runtime.mouse, mouseMessages.leftPress, mouseMessages.leftDown);
    if (const std::optional<bool> doubleClick = input.TakeDoubleClickMessage(); doubleClick.has_value())
        runtime.ui.doubleClick = *doubleClick;
    LatchTheRightButton(runtime.ui, input);
    LatchTheWheel(runtime.ui, input);
    // The text, editing and composition events, in the order InputState handled them.
    if (auto typed = Chat::TypeTheGameInput(ChatContext(), input.TextInputEvents()); !typed.HasValue())
        return typed;
    // 0x637c4a..0x637c5c: with 0x5762eb4 at 0 (nobody grabbed the mouse: the chat record or an old window) and scene 5,
    // 0x623e70 before the scene's logic (0x636290), with the world loaded or not.
    if (runtime.ui.capture != 0)
        return {};
    SceneUiRequests requests(m_session, m_context.log, m_context.dataRoot);
    std::optional<std::uint8_t> chosenSlot;
    if (m_session != nullptr)
    {
        if (const Protocol::FrontEnd::CharacterEntry* chosen = m_session->ChosenCharacter(); chosen != nullptr)
            chosenSlot = chosen->slot;
    }
    const Items::InventoryServices inventory = InventoryContext();
    return Hud::StepTheGameDialogs(Hud::GameUiContext{.world = *m_context.world,
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
                                                      .worldEntry = m_context.worldEntry,
                                                      .sendNpcWindow = NpcWindowSenderOf(m_session),
                                                      .terrain = &MapTerrain()});
}

Core::Expected<void, std::string> GameScene::StepTheUi(const Input::InputState& input)
{
    const Ui::Point mouse = Ui::MouseIn640x480(input, m_screen);
    SceneUiRequests requests(m_session, m_context.log, m_context.dataRoot);
    const Chat::GameChatContext chat = ChatContext();
    const Items::InventoryServices inventory = InventoryContext();
    // timeGetTime() of the banner's seconds (0x46ada0): the scene clock in ms.
    const auto systemTime =
        m_context.clock != nullptr
            ? static_cast<std::uint32_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
            : 0U;
    return Hud::StepTheGameUi(Hud::GameUiContext{.world = *m_context.world,
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
                                                 .worldEntry = m_context.worldEntry,
                                                 .sendNpcWindow = NpcWindowSenderOf(m_session),
                                                 .terrain = &MapTerrain()});
}

Npc::NpcWindowSender GameScene::NpcWindowSenderOf(App::ServerSession* session)
{
    if (session == nullptr)
        return {};
    return [session](Protocol::Outgoing packet) { return session->SendNpcWindowPacket(std::move(packet)); };
}

Chat::GameChatContext GameScene::ChatContext()
{
    App::ServerSession* session = m_session;
    return Chat::GameChatContext{
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
                    ? Chat::ChatSender([session](Protocol::Outgoing packet) { session->SendChat(std::move(packet)); })
                    : Chat::ChatSender{},
        .sendWindowClose = session != nullptr ? std::function<void()>([session] { session->SendWindowClose(); })
                                              : std::function<void()>{},
        .clipboard = m_context.clipboard,
        .models = m_models,
        .faunaModels = &LoadedFaunaModels()};
}

Items::InventoryServices GameScene::InventoryContext()
{
    Items::InventoryServices services = m_context.worldEntry != nullptr
                                            ? Items::InventoryServicesOf(*m_context.worldEntry)
                                            : Items::InventoryServices{};
    if (m_context.worldEntry == nullptr)
    {
        services.items = m_items;
        services.texts = m_texts;
    }
    if (App::ServerSession* session = m_session; session != nullptr)
    {
        services.sendItemMove = [session](const Protocol::Items::ItemMove& move)
        { return session->SendItemMove(move); };
        // The 23, the 22 and the 26.
        services.sendDrop = [session](const Protocol::Items::Drop& drop) { return session->SendDrop(drop); };
        services.sendPickUp = [session](std::uint16_t index) { return session->SendPickUp(index); };
        services.sendUseItem = [session](const Protocol::Items::UseItem& use) { return session->SendUseItem(use); };
        // The trade's 3C, sent by picking up and dropping in its grid.
        services.sendGamePacket = [session](Protocol::Outgoing packet) { session->SendGamePacket(std::move(packet)); };
    }
    services.pickTheGround = [this] { return GroundUnderTheMouse(); };
    return services;
}

Core::Expected<void, std::string> GameScene::DrawTheInterface(Render::Frame::SceneCanvas& canvas,
                                                              const GameProjection* projection)
{
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    const Input::InputState noInput;
    const Input::InputState& input = m_context.input != nullptr ? *m_context.input : noInput;
    // 0x637641: 0x59ec80, the frame's picking. A MuRet intermediate draw runs it on the copies: the rand(), the own
    // record's depth and the chosen ones.
    std::array<std::int32_t, 5> projectedTargets = runtime.selectedTargets;
    ProjectionTintRestore tintRestore(m_context.world->Actors(), m_projectingFrame);
    auto picked = Hud::PickInTheFrame(Hud::FramePickingContext{
        .world = *m_context.world,
        .random = FrameCloth().random,
        .ownRecordDepth = m_projectingFrame ? m_projection->ownRecordDepth : runtime.ownRecordDepth,
        .targets = m_projectingFrame ? projectedTargets : runtime.selectedTargets,
        .altDown = input.IsDown(Input::Key::Alt),
        .ray = m_mouseRay,
        .mouse = Ui::MouseIn640x480(input, canvas.screen),
        .cameraPosition = m_drawnCamera.position,
        .pickingBoxes =
            m_projectingFrame ? std::span(m_projection->actorPickingBoxes) : std::span<Actors::ActorPickingBox>{},
        .floorItems = FrameTail().floorItems,
        .terrain = &MapTerrain()});
    tintRestore.Restore();
    if (!picked.HasValue())
        return picked;
    // The image 0x65dc30 loaded into slot 0x66a on reaching the map (0x63bd00): the packet has no GPU, so the first
    // draw that follows uploads it, on the real banner and not on an intermediate draw's copy.
    if (auto uploaded = Hud::UploadTheMapBanner(runtime.hud.mapBanner, m_context.dataRoot, m_context.textures);
        !uploaded.HasValue())
        return uploaded;
    // 0x637646..0x6376e6: the 2D pass, in this order (06 § El dibujo del juego). A MuRet intermediate draw draws with
    // the copies: the rand(), the access counts, the HUD globals, the Crywolf smoke and the sound.
    Kernel::ActorState& hero = m_context.world->Hero();
    Hud::GameHudContext hud{
        .world = *m_context.world,
        .random = FrameCloth().random,
        .ownRecordDepth = m_projectingFrame ? m_projection->ownRecordDepth : runtime.ownRecordDepth,
        .cellXDepth = m_projectingFrame ? m_projection->heroXDepth : hero.cellXAccesses,
        .cellYDepth = m_projectingFrame ? m_projection->heroYDepth : hero.cellYAccesses,
        .hud = m_projectingFrame ? m_projection->hud : runtime.hud,
        .crywolfSandOverlay = m_projectingFrame ? m_projection->crywolfSandOverlay : runtime.crywolf.sandOverlay,
        .crywolfInterface = m_projectingFrame ? &m_projection->crywolfInterface : &runtime.crywolf.ui,
        .sound = m_projectingFrame ? m_projection->sound : runtime.lastSoundId,
        .soundRequest = !m_projectingFrame ? std::function<void(Audio::SoundId)>{[&runtime](Audio::SoundId sound)
                                                                                 { runtime.RequestSound(sound); }}
                                           : std::function<void(Audio::SoundId)>{},
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
        .tickCount = Kernel::TickCountMs(canvas.frameClock),
        .mouse = Ui::MouseIn640x480(input, canvas.screen),
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
    // 0x5ac120: the 3D pass of the UI items, with the item models, the global pose buffer and the frame's 0x60b3d0
    // contexts (or their copies in an intermediate draw). Without the library, no draw.
    Actors::Drawing::ItemSpecialDrawContext itemSpecial;
    Actors::Drawing::ItemDrawContext itemContext;
    std::optional<Items::UiItemResources> uiItems;
    if (m_models != nullptr)
    {
        auto pass = Items::MakeUiItemPass(canvas.screen);
        if (!pass.HasValue())
            return Core::Unexpected(pass.Error());
        FillTheItemContexts(itemSpecial, itemContext, canvas.frameClock, m_projectingFrame ? 0 : 1);
        uiItems.emplace(Items::UiItemResources{.batch = canvas.models,
                                               .models = *m_models,
                                               .poseCarry = FramePoseCarry(),
                                               .itemContext = itemContext,
                                               .frameClock = canvas.frameClock,
                                               .flushSprites = canvas.flushSprites,
                                               .flushModels = canvas.flushModels,
                                               .pass = pass.Value()});
        hud.uiItems = &*uiItems;
    }
    // 0x42403d: the pet tooltip's A9 goes through the game session, like the pet button's.
    if (App::ServerSession* session = m_session; session != nullptr && !m_projectingFrame)
        hud.send = [session](Protocol::Outgoing packet) { session->SendChat(std::move(packet)); };
    // 0x637646: 0x6219b0 starts the 2D (gluOrtho2D and identity MODELVIEW, with push): SceneCanvas's batch.
    // 0x63764b: 0x4383f0, the 3D texts.
    Hud::WorldTextDrawContext worldTexts{.queue = runtime.worldTexts,
                                         .projection = projection,
                                         .sprites = canvas.sprites,
                                         .text = canvas.text,
                                         .hud = hud.hud,
                                         .projecting = m_projectingFrame};
    if (auto drawn = Hud::DrawTheWorldTexts(worldTexts); !drawn.HasValue())
        return drawn;
    // 0x637650: 0x5bbbc0, the hover and Alt.
    Hud::GameHoverContext hover{.world = *m_context.world,
                                .tail = FrameTail(),
                                .random = FrameCloth().random,
                                .targets = m_projectingFrame ? projectedTargets : runtime.selectedTargets,
                                .hud = hud.hud,
                                .altLatch = m_projectingFrame ? m_projection->altLatch
                                                              : runtime.ui.keyLatches[Ui::AltVirtualKey],
                                .altNames = m_projectingFrame ? m_projection->altNames : runtime.ui.altNames,
                                .altDown = input.IsDown(Input::Key::Alt),
                                .sprites = canvas.sprites,
                                .text = canvas.text,
                                .projecting = m_projectingFrame,
                                .texts = m_texts,
                                .names = &hud};
    if (auto drawn = Hud::DrawTheHover(hover); !drawn.HasValue())
        return drawn;
    // 0x637660: 0x5a7320(1), the HUD, with 0x7d36899 at 0.
    if (auto drawn = Hud::DrawTheHud(hud); !drawn.HasValue())
        return drawn;
    // 0x637668: 0x5a7eb0, the time and the F3 24 outcome, even with the HUD hidden.
    if (auto drawn = Guild::DrawTheEventTimeAndOutcome(hud); !drawn.HasValue())
        return drawn;
    // 0x637673: 0x490390, the old windows, and 0x637680: the window list, always. A MuRet intermediate draw draws them
    // on its copy.
    Ui::OldWindowManager& oldWindows = m_projectingFrame ? m_projection->oldWindows : runtime.oldWindows;
    const Ui::OldWindowDrawContext windowContext{.sprites = canvas.sprites,
                                                 .text = canvas.text,
                                                 .dcFont = hud.hud.dcFont,
                                                 .textColors = hud.hud.textColors,
                                                 .arrowHeld = hud.arrowHeld};
    // 0x659290(1) takes no rand(): it can be checked before the window that asks for it.
    Hud::GamePanels panels(*m_context.world, FrameCloth().random);
    // The friends window's texts, the list with the keyboard arrows (0x5762ec0), the EDIT with the Windows focus
    // (GetFocus() of 0x468320: the old windows' one, with ChatFocus::OldWindow) and where the chat record goes
    // (0x6c7a88).
    const Ui::OldWindowTexts friendTexts = Messenger::OldWindowTextsOf(m_texts);
    const bool editFocused = runtime.chat.input.focus == Chat::ChatFocus::OldWindow;
    // 0x49a1d0 and 0x498800 → 0x497c10: the letters' portrait, with SceneContext::worldEntry's set, the models (the
    // character's and, for the helper, the world's) and the frame's things, or their copies in an intermediate draw.
    // Without the set or the models, the portrait in view stops.
    std::optional<std::string> portraitPoseFailure;
    std::optional<Messenger::LetterPortraitDraw> portraitDraw;
    if (m_models != nullptr && m_context.worldEntry != nullptr)
    {
        const bool originalFrame = !m_projectingFrame;
        portraitDraw.emplace(Messenger::LetterPortraitDraw{
            .hud = hud,
            .batch = canvas.models,
            .flushSprites = canvas.flushSprites,
            .flushModels = canvas.flushModels,
            .characters =
                Actors::Drawing::GameCharacterPass{
                    .dynamicLighting = &FrameLighting(),
                    .clothWorld = &FrameCloth(),
                    .frameClock = canvas.frameClock,
                    .originalFrames = originalFrame ? 1 : 0,
                    .sprites = &FrameSpritesForDraw(),
                    .poseCarry = &FramePoseCarry(),
                    .joints = FrameTail().joints,
                    .particles = &FrameParticles(),
                    .effects = &FrameEffects(),
                    .itemContext = &itemContext,
                    .pace =
                        originalFrame ? Models::OriginalFramePace::OncePerDraw : OriginalFramePaceOf(m_context.pacing),
                    .onPose =
                        [&](std::uint16_t model, std::span<const Math::BoneTransform> bones,
                            const Models::ObjectTransform& transform, bool translated)
                    {
                        const auto published = Actors::Drawing::PublishPoseRaw(FramePoseCarry(), *m_models, model,
                                                                               bones, transform, translated);
                        if (!published.HasValue() && !portraitPoseFailure)
                            portraitPoseFailure = published.Error();
                    },
                    .publishProjectedPose = true,
                    .poseBones = [&](std::uint16_t)
                    { return std::span<const Math::BoneTransform>(FramePoseCarry().bones); },
                    .worldModels = m_worldModelsMap ? &m_worldModels : nullptr,
                    .fixedHeight = CameraState().fixedHeight,
                    .terrain = &MapTerrain(),
                    .faunaModels = &LoadedFaunaModels(),
                    .drawBuffers = &m_characterBuffers,
                    .hooks = &m_characterDrawHooks,
                    .effectHooks = &m_effectHooks},
            .helperModels = &m_worldModels,
            .onHelperPose = [&](const Actors::Drawing::DrawnObject& object)
            { return Actors::Drawing::PublishPose(FramePoseCarry(), m_worldModels, object, true); },
            .posedMeshes = [&]() { return std::span<const Models::SkinnedMesh>(FramePoseCarry().meshes); },
            // timeGetTime (0x6b8544), from the Fenrirs' decal 0x14e: the draw clock in ms.
            .systemTime = [systemTime = static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count())]()
            { return systemTime; },
            .globalPose = [&]() { return std::span<const Math::BoneTransform>(FramePoseCarry().bones); },
            .originalFrame = originalFrame});
    }
    std::function<Core::Expected<void, std::string>(Ui::OldPortraitPart&)> drawThePortrait;
    if (portraitDraw)
        drawThePortrait = [&](Ui::OldPortraitPart& part) -> Core::Expected<void, std::string>
        {
            auto drawn = Messenger::DrawTheLetterPortrait(part, *portraitDraw, *m_context.worldEntry);
            if (drawn.HasValue() && portraitPoseFailure)
                return Core::Unexpected(*portraitPoseFailure);
            return drawn;
        };
    // An intermediate pass starts from the same state as its copy of the manager and publishes no network effects.
    Ui::GameUiState projectedUi = runtime.ui;
    Kernel::MouseState projectedMouse = runtime.mouse;
    Chat::ChatFocus projectedFocus = runtime.chat.input.focus;
    Ui::GameUiState& drawUi = m_projectingFrame ? projectedUi : runtime.ui;
    Kernel::MouseState& drawMouse = m_projectingFrame ? projectedMouse : runtime.mouse;
    Chat::ChatFocus& drawFocus = m_projectingFrame ? projectedFocus : runtime.chat.input.focus;
    const Ui::OldWindowGlobals drawGlobals{
        .leftHeld = drawMouse.leftHeld,
        .leftClick = drawMouse.pendingClick,
        .releasedClick = drawMouse.releasedClick,
        .mouseOnUi = drawUi.mouseOnUi,
        .hovered = drawUi.hovered,
        .capture = drawUi.capture,
        .pressedButton = drawUi.pressedButton,
        .keyboardFocus = drawFocus,
        .sound = m_projectingFrame ? m_projection->sound : runtime.lastSoundId,
        .focusedList = drawUi.focusedList,
        .wheel = drawUi.wheel,
        .rightClick = drawUi.rightClick,
        .doubleClick = drawUi.doubleClick,
        .mouseUpdateTime = &drawMouse.updateTime,
        .mouseUpdateTimeMax = &drawMouse.updateTimeMax,
        .soundRequest = !m_projectingFrame ? std::function<void(Audio::SoundId)>{[&runtime](Audio::SoundId sound)
                                                                                 { runtime.RequestSound(sound); }}
                                           : std::function<void(Audio::SoundId)>{}};
    const Ui::OldWindowMeasure drawMeasure{.measure = canvas.text, .dcFont = hud.hud.dcFont, .screen = canvas.screen};
    SceneUiRequests drawRequests(m_session, m_context.log, m_context.dataRoot);
    Ui::OldWindowStepView drawStep{.mouseX = hud.mouse.x,
                                   .mouseY = hud.mouse.y,
                                   .keyEdge = [&drawUi, &input](std::uint8_t key)
                                   { return Hud::OldWindowKeyEdge(drawUi, input, key); },
                                   .chatOpen = drawUi.chatOpen,
                                   .texts = &friendTexts,
                                   .measure = &drawMeasure,
                                   .send =
                                       [this, &drawRequests](const Ui::FriendSend& send)
                                   {
                                       if (!m_projectingFrame)
                                           drawRequests.SendFriendPacket(Messenger::FriendPacketOf(send));
                                   }};
    // The rest the step reads from the game, like the UI's (the portrait, the saved letter, the filter): a command that
    // arrives in the draw (0x45aa50) cannot find less than in the step.
    const Chat::GameChatContext drawChat = ChatContext();
    std::optional<std::uint8_t> drawChosenSlot;
    if (m_session != nullptr)
    {
        if (const Protocol::FrontEnd::CharacterEntry* chosen = m_session->ChosenCharacter(); chosen != nullptr)
            drawChosenSlot = chosen->slot;
    }
    Hud::LendTheGameHooks(drawStep,
                          Hud::GameUiContext{.world = *m_context.world,
                                             .random = m_context.cloth->random,
                                             .input = input,
                                             .mouseX = hud.mouse.x,
                                             .mouseY = hud.mouse.y,
                                             .measure = m_context.textMeasure,
                                             .screen = m_screen,
                                             .chat = &drawChat,
                                             .texts = m_texts,
                                             .chosenSlot = drawChosenSlot,
                                             .requests = &drawRequests,
                                             .worldEntry = m_context.worldEntry,
                                             .terrain = &MapTerrain()},
                          !m_projectingFrame);
    drawStep.field78abaac = drawUi.field78abaac;
    const Ui::OldWindowDrawView windowView{.mouseX = hud.mouse.x,
                                           .mouseY = hud.mouse.y,
                                           .leftClick = runtime.mouse.pendingClick,
                                           .friendsOpen = panels.Registry().IsOpen(Ui::FriendsPanel),
                                           .texts = &friendTexts,
                                           .focusedList = runtime.ui.focusedList,
                                           .focusedEdit = editFocused ? oldWindows.focusedEdit : 0,
                                           .chatPlacement = runtime.ui.chatModes.placement,
                                           .portrait = drawThePortrait,
                                           .globals = &drawGlobals,
                                           .stepView = &drawStep};
    if (auto windows = Ui::DrawTheOldWindows(oldWindows, windowContext, windowView); !windows.HasValue())
        return windows;
    if (auto list = Ui::DrawTheWindowList(oldWindows, windowContext, windowView); !list.HasValue())
        return list;
    // 0x637683: 0x5f37f0, the duel request, with 0x78abad9, which the AC sets.
    if (auto duel = Social::DrawTheDuelRequest(hud); !duel.HasValue())
        return duel;
    // 0x637696: 0x62db60, the dialogs, in scene 5.
    if (auto dialogs = Hud::Dialogs::DrawTheGameDialogs(hud); !dialogs.HasValue())
        return dialogs;
    // 0x63769b: 0x5f7bc0, the tooltips.
    if (auto tooltips = Tooltips::DrawTheTooltips(hud); !tooltips.HasValue())
        return tooltips;
    // 0x6376a0..0x6376b6: the notices and, with the chat record in its window, the old area.
    Chat::DrawTheMessages(*m_context.world, hud.hud, canvas.sprites, canvas.text, hud.mouse, true);
    // 0x6376bb: 0x5b0550, the overlays.
    if (auto overlays = Hud::DrawTheOverlays(hud); !overlays.HasValue())
        return overlays;
    // 0x6376c6: 0x40eca0, the set label, with +0x9305 (the chat opens it with its name).
    if (auto label = Tooltips::DrawTheSetLabel(hud); !label.HasValue())
        return label;
    // 0x6376cb: 0x65ee60 → 0x65fe00, the new UI.
    // 0x6376d7: 0x5ac2d0, the cursor, 0x6376dc: 0x633110, the dialog's 3D item, with this frame's world far
    // (0x6d5d44), and 0x6376e1: the cursor again, the same quad.
    if (auto cursor = Hud::DrawTheGameCursor(hud); !cursor.HasValue())
        return cursor;
    if (auto item = Hud::DrawTheDialogItem(hud, m_drawnCamera.farPlane); !item.HasValue())
        return item;
    if (auto cursor = Hud::DrawTheGameCursor(hud); !cursor.HasValue())
        return cursor;
    // 0x6376e6: 0x621a90 and 0x620fc0 close the 2D: whoever presents the frame draws the batch.
    return {};
}
} // namespace Client::Scenes
