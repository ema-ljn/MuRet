#include "Scenes/CharacterSelectScene.h"

#include "App/LoopTiming.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/TextureSet.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterName.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/RayPicking.h"
#include "Render/Models/Skeleton.h"
#include "Render/OriginalQuad.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/GameData.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/TextureSets.h"
#include "UI/CreateCharacterWindow.h"
#include "UI/DeleteChecks.h"
#include "UI/GameText.h"
#include "UI/NameChecks.h"
#include "UI/Notices.h"
#include "UI/SelectionBar.h"
#include "UI/Widget.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{

// La acción de parado de la selección. La elige 0x4f2250 por la mascota, las alas y el arma; la de
// la traza es la 3, con un arma de una mano. Las otras, sin leer (P-C14 c): hasta entonces, la 3.
constexpr std::uint8_t StandingAction = 3;

// La luz de 0x50ba30 en la escena 4: 0,4, y 0,5 para el "héroe" (DAT_0743a170, el objeto 0, que es
// el del lugar 0: 0x50d219), más el +0xf4 que pone 0x634680, 1 para el elegido.
constexpr float CharacterLight = 0.4f;
constexpr float HeroLight = 0.5f;
constexpr float ChosenExtraLight = 1.0f;
constexpr std::uint8_t HeroSlot = 0;

// Los textos que 0x622c70 busca en el nombre de uno nuevo.
constexpr std::size_t OperationText = 457;
constexpr std::size_t ServerText = 458;

// El bit de las banderas (+0x1d8) con el que el armado de la barra muestra los textos 436 y 437.
constexpr std::uint8_t BlockedAccountFlag = 0x10;

// Si la entrada es la misma que dio el personaje: un crear en un lugar ocupado lo pisa.
bool SameEntry(const Protocol::Character::CharacterEntry& one, const Protocol::Character::CharacterEntry& other)
{
    return one.slot == other.slot && one.name == other.name && one.classByte == other.classByte &&
           one.equipment == other.equipment && one.withoutEquipment == other.withoutEquipment;
}

// Los dos fondos, 0x621b60(0x717, 0, 25, 320, 320, …) y el 0x718 en x 320, escalados desde 640 × 480.
Render::OriginalQuad Background(std::uint16_t slot, float x)
{
    return Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                .x = x,
                                .y = 25.0f,
                                .width = 320.0f,
                                .height = 320.0f,
                                .scaleSize = true,
                                .scalePosition = true};
}
} // namespace

CharacterSelectScene::CharacterSelectScene(const SceneContext& context, const GameData& data,
                                           const IModelLibrary& models, App::ServerSession* session)
    : m_context(context), m_texts(data.text ? &*data.text : nullptr), m_models(models), m_session(session),
      m_textures(context.textures, SelectionTextureSet), m_windowTextures(context.textures, WindowTextureSet),
      m_notice([this](Render::TextureSlot slot) { return m_windowTextures.BufferSize(slot); }, m_texts),
      m_bar([this](Render::TextureSlot slot) { return m_textures.BufferSize(slot); }, m_texts),
      m_nameFilters{.filterName = data.filterName,
                    .filter = data.filter,
                    .operation = UI::TextAt(m_texts, OperationText).value_or(std::string_view{}),
                    .server = UI::TextAt(m_texts, ServerText).value_or(std::string_view{})},
      m_create([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts)
{
}

Scenes::TextureBufferSize CharacterSelectScene::BufferSize(Render::TextureSlot slot) const
{
    const bool inTheWindows = std::ranges::any_of(WindowTextureSet, [slot](const Data::Textures::TextureSetEntry& entry)
                                                  { return entry.slot == slot.value; });
    return inTheWindows ? m_windowTextures.BufferSize(slot) : m_textures.BufferSize(slot);
}

Core::Expected<void, std::string> CharacterSelectScene::Enter()
{
    if (auto loaded = m_textures.Load(m_context.dataRoot); !loaded.HasValue())
    {
        return loaded;
    }
    if (auto loaded = m_windowTextures.Load(m_context.dataRoot); !loaded.HasValue())
    {
        return loaded;
    }
    if (m_session != nullptr)
    {
        m_session->RequestCharacterList();
    }
    return {};
}

bool CharacterSelectScene::WaitingForTheList() const
{
    return m_session != nullptr && !m_session->Characters().has_value();
}

CharacterSelectScene::Character
CharacterSelectScene::MakeCharacter(const Protocol::Character::CharacterEntry& entry) const
{
    Character character;
    character.slot = entry.slot;
    character.entry = entry;
    const std::uint8_t characterClass = Protocol::Character::CharacterClassOf(entry.classByte);
    // El que armó la respuesta de crear sale sin equipo (0x517270 sin 0x5139f0, Q-C103).
    character.appearance = entry.withoutEquipment ? GameLogic::ClassAppearance(characterClass, 0)
                                                  : GameLogic::AppearanceOf(characterClass, 0, entry.equipment);
    if (const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(Data::Models::PlayerSkeletonModel))
    {
        Render::Models::SetAction(character.animation.body, *skeleton, StandingAction);
    }
    return character;
}

void CharacterSelectScene::SyncTheCharacters()
{
    if (m_session == nullptr || !m_session->Characters())
    {
        return;
    }
    const std::vector<Protocol::Character::CharacterEntry>& entries = m_session->Characters()->characters;
    bool changed = !m_tookTheList || entries.size() != m_characters.size();
    std::vector<Character> synced;
    synced.reserve(entries.size());
    for (const Protocol::Character::CharacterEntry& entry : entries)
    {
        const auto same = std::ranges::find_if(m_characters, [&entry](const Character& character)
                                               { return SameEntry(character.entry, entry); });
        if (same != m_characters.end())
        {
            synced.push_back(std::move(*same));
            continue;
        }
        synced.push_back(MakeCharacter(entry));
        changed = true;
    }
    m_characters = std::move(synced);
    if (!m_tookTheList)
    {
        m_tookTheList = true;
        // 0x63e820, una vez: algún personaje de los cinco lugares con el bit 0x10 de las banderas.
        m_bar.SetBlockedAccount(std::ranges::any_of(entries, [](const Protocol::Character::CharacterEntry& entry)
                                                    { return (entry.flags & BlockedAccountFlag) != 0; }));
        // 0x4c20a0 → 0x622b20 → 0x63d7c0: las clases de la 0 a pkt[4].
        m_create.EnableClasses(m_session->Characters()->creatableClasses);
        m_context.log.Info("la selección tiene {} personajes", m_characters.size());
    }
    else if (changed)
    {
        m_context.log.Info("la selección tiene ahora {} personajes", m_characters.size());
    }
}

// El cuerpo, la mascota y las alas de cada uno (StepCharacterAnimation), con la regla de la escena 4.
void CharacterSelectScene::StepCharacters()
{
    for (Character& character : m_characters)
    {
        // Lo que termina vuelve a la de parado (0x4fcb10), que en la selección es la 3: como la 3 repite,
        // es la de ahora, y SetAction no cambia nada.
        static_cast<void>(StepCharacterAnimation(character.animation, character.appearance, m_models,
                                                 Render::Models::AnimationScene::CharacterSelect));
    }
}

// Q-C70: en la selección, un FD_CLOSE abre el aviso 0x70, como en el login, una vez. MuRet lo abre
// también sin respuesta (NoReply), como el login (Q-C69).
bool CharacterSelectScene::ShowTheFailure()
{
    if (m_failureShown || m_session == nullptr || m_session->State() != App::SessionState::Failed)
    {
        return false;
    }
    m_failureShown = true;
    m_context.log.Info("la sesión falló en la selección: se abre el aviso {:#04x}",
                       static_cast<int>(UI::ConnectFailedNotice));
    m_notice.Open(UI::ConnectFailedNotice);
    return true;
}

bool CharacterSelectScene::TakeTheCreationResult()
{
    if (!m_waitingForTheCreation || m_session == nullptr ||
        m_session->State() == App::SessionState::WaitingCreateResult)
    {
        return false;
    }
    m_waitingForTheCreation = false;
    const std::optional<std::uint8_t> result = m_session->CreateResult();
    if (!result || m_failureShown)
    {
        return false;
    }
    const std::optional<UI::NoticeId> notice = UI::NoticeOfCreateResult(*result);
    if (!notice)
    {
        // Creado: 0x65f6c0(+0x8) cierra el 0x5d; el nuevo ya está en la lista.
        if (m_notice.Notice() == UI::PleaseWaitNotice)
        {
            m_notice.Close();
        }
        return false;
    }
    // La ventana antes que el aviso, que queda arriba (0x6431b0), con el campo vacío.
    m_create.Open();
    m_notice.Open(*notice);
    return true;
}

void CharacterSelectScene::StepTheCreateWindow(const Input::InputState& input)
{
    if (m_create.Step(input, m_screen) == UI::CreateWindowEvent::Submitted)
    {
        SubmitTheCreation();
    }
}

void CharacterSelectScene::SubmitTheCreation()
{
    if (const std::optional<UI::NoticeId> refused =
            UI::NoticeOfNewName(m_create.Name(), m_create.Buffer(), m_nameFilters))
    {
        // El nombre no va al log: solo el aviso. La ventana queda abajo, con el nombre.
        m_context.log.Info("el nombre del personaje nuevo no pasa: el aviso {:#04x}", static_cast<int>(*refused));
        m_notice.Open(*refused);
        return;
    }
    if (m_session == nullptr)
    {
        return;
    }
    m_session->CreateCharacter(m_create.Name(), m_create.ChosenClass());
    if (m_session->State() == App::SessionState::WaitingCreateResult)
    {
        m_waitingForTheCreation = true;
        m_create.Close();
        m_notice.Open(UI::PleaseWaitNotice);
    }
}

bool CharacterSelectScene::TakeTheDeletionResult()
{
    if (!m_waitingForTheDeletion || m_session == nullptr ||
        m_session->State() == App::SessionState::WaitingDeleteResult)
    {
        return false;
    }
    m_waitingForTheDeletion = false;
    const std::optional<std::uint8_t> result = m_session->DeleteResult();
    // Con la sesión caída, el 0x70 de la falla queda arriba aunque la respuesta haya llegado antes.
    if (!result || m_failureShown)
    {
        return false;
    }
    m_notice.Open(UI::NoticeOfDeleteResult(*result));
    return true;
}

// El aviso abierto tiene el mouse (Q-C84) y 0x634680 no llama a 0x59ec80 (+0x8de8): el contorno queda
// en el último que estuvo bajo el mouse. El OK de uno que cierra el juego (el 0x70, Q-C69) lo dice en
// WantsToQuit; el de la confirmación del borrado abre el del código, y el del código manda el F3 02.
// Los demás solo se cierran.
void CharacterSelectScene::StepTheNotice(const Input::InputState& input)
{
    const std::optional<UI::NoticeId> closed = m_notice.Step(input, m_screen);
    if (!closed)
    {
        return;
    }
    switch (UI::FollowUpOf(*closed))
    {
    case UI::NoticeFollowUp::Quit:
        m_context.log.Info("el OK del aviso cierra el juego (el {:#04x})", static_cast<int>(*closed));
        m_quit = true;
        break;
    case UI::NoticeFollowUp::AskForTheCode:
        m_notice.Open(UI::DeletionCodeNotice);
        break;
    case UI::NoticeFollowUp::SendTheDeletion:
        SendTheDeletion();
        break;
    case UI::NoticeFollowUp::Close:
    case UI::NoticeFollowUp::ReopenAtAccount:
    case UI::NoticeFollowUp::ReopenAtPassword:
        break;
    }
}

void CharacterSelectScene::EnableTheBar()
{
    bool aFreeSlot = true;
    if (m_session != nullptr && m_session->Characters())
    {
        const std::vector<Protocol::Character::CharacterEntry>& entries = m_session->Characters()->characters;
        aFreeSlot = false;
        for (std::uint8_t slot = 0; slot <= Protocol::Character::MaxCharacterSlot; ++slot)
        {
            if (std::ranges::find(entries, slot, &Protocol::Character::CharacterEntry::slot) == entries.end())
            {
                aFreeSlot = true;
                break;
            }
        }
    }
    m_bar.Enable(aFreeSlot, m_session != nullptr && m_session->ChosenCharacter() != nullptr);
}

void CharacterSelectScene::OnTheBar(UI::SelectionBarClick click)
{
    switch (click)
    {
    case UI::SelectionBarClick::Connect:
        TryToEnter();
        break;
    case UI::SelectionBarClick::Menu:
        // El menú (+0x3d0) es de la CL8, como el del login (P-C13).
        m_context.log.Info("el menú de la selección todavía no está");
        break;
    case UI::SelectionBarClick::Create:
        m_create.Open();
        break;
    case UI::SelectionBarClick::Delete:
        AskToDelete();
        break;
    }
}

void CharacterSelectScene::AskToDelete()
{
    const Protocol::Character::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    if (chosen == nullptr)
    {
        return;
    }
    const UI::NoticeId notice =
        UI::NoticeOfDeleteButton(*chosen, UI::FreeDeletionGroup(m_texts, m_session->ChosenServer()));
    m_context.log.Info("borrar el personaje del lugar {} abre el aviso {:#04x}", chosen->slot,
                       static_cast<int>(notice));
    // El nombre va en el %s del texto 1712 del 0x5e (0x6431b0); no va al log.
    m_notice.Open(notice, Protocol::Character::NameAsText(chosen->name));
}

void CharacterSelectScene::SendTheDeletion()
{
    if (m_session == nullptr)
    {
        return;
    }
    // El código, como la clave, no va al log, y no se queda en la ventana.
    m_session->DeleteCharacter(Protocol::Character::DeleteCodeFromText(m_notice.TakeCode()));
    if (m_session->State() == App::SessionState::WaitingDeleteResult)
    {
        m_waitingForTheDeletion = true;
        m_notice.Open(UI::PleaseWaitNotice);
    }
}

void CharacterSelectScene::Step(const Input::InputState& input)
{
    // Un aviso que se abre en este paso se atiende desde el siguiente: lo que se apretó en este no
    // lo cierra antes de que se vea.
    const bool failureOpened = ShowTheFailure();
    if (WaitingForTheList())
    {
        if (m_notice.IsOpen() && !failureOpened)
        {
            StepTheNotice(input);
        }
        return;
    }
    ++m_steps;
    SyncTheCharacters();
    StepCharacters();
    const bool deletionOpened = TakeTheDeletionResult();
    const bool creationOpened = TakeTheCreationResult();
    EnableTheBar();
    if (m_notice.IsOpen())
    {
        m_bar.StepWithoutTheMouse(input);
        if (m_create.IsOpen())
        {
            m_create.StepUnderANotice(input);
        }
        if (!failureOpened && !deletionOpened && !creationOpened)
        {
            StepTheNotice(input);
        }
        return;
    }
    // La ventana de crear es modal (la base de toda la pantalla): tiene el mouse y el teclado.
    if (m_create.IsOpen())
    {
        m_bar.StepWithoutTheMouse(input);
        StepTheCreateWindow(input);
        return;
    }
    if (const std::optional<UI::SelectionBarClick> click = m_bar.Step(input, m_screen))
    {
        OnTheBar(*click);
        return;
    }
    const auto now = App::LogicStep * static_cast<std::int64_t>(m_steps);
    const bool doubleClick = m_doubleClick.Step(input, now);
    // Con el mouse sobre la barra, la UI lo tiene (+0x8de8): la escena no elige, no hace el doble clic
    // y no busca quién está debajo, así que el contorno queda en el último.
    if (!m_bar.Covers(UI::MouseInPixels(input, m_screen), m_screen))
    {
        m_hovered = UnderTheMouse(input);
        if (doubleClick && m_hovered)
        {
            // El doble clic elige y entra como Enter, sin el sonido (0x6342c0).
            Choose(m_hovered);
            TryToEnter();
            return;
        }
        if (input.WasPressed(Input::MouseButton::Left))
        {
            Choose(m_hovered);
        }
    }
    // El original pide GetAsyncKeyState(VK_RETURN) == 0x8001, y pierde un Enter corto entre dos
    // lecturas; MuRet lo ve, como en el login (Q-C45). La barra no lo traba.
    if (input.WasPressed(Input::Key::Enter))
    {
        TryToEnter();
    }
}

std::optional<std::uint8_t> CharacterSelectScene::UnderTheMouse(const Input::InputState& input) const
{
    if (!m_lastPass || m_characters.empty())
    {
        return std::nullopt;
    }
    const auto [origin, end] = m_lastPass->RayThrough(input.MouseX(), input.MouseY());
    std::vector<glm::vec3> positions;
    positions.reserve(m_characters.size());
    for (const Character& character : m_characters)
    {
        positions.push_back(ObjectOf(character).position);
    }
    const std::optional<std::size_t> nearest = Render::Models::NearestHit(origin, end, positions);
    return nearest ? std::optional<std::uint8_t>{m_characters[*nearest].slot} : std::nullopt;
}

void CharacterSelectScene::Choose(std::optional<std::uint8_t> slot)
{
    if (m_session != nullptr)
    {
        m_session->ChooseCharacter(slot);
    }
}

void CharacterSelectScene::TryToEnter()
{
    if (m_session == nullptr)
    {
        return;
    }
    const Protocol::Character::CharacterEntry* chosen = m_session->ChosenCharacter();
    if (chosen == nullptr)
    {
        return;
    }
    if (Protocol::Character::IsLocked(*chosen))
    {
        m_notice.Open(UI::LockedCharacterNotice);
        return;
    }
    m_context.log.Info("se elige el personaje del lugar {} para entrar al mundo", chosen->slot);
    m_next = SceneId::WorldLoading;
}

Render::Models::ObjectTransform CharacterSelectScene::ObjectOf(const Character& character) const
{
    const CharacterSlot& slot = CharacterSlots[character.slot];
    return Render::Models::ObjectTransform{{slot.position.x, slot.position.y, CharacterSlotHeight},
                                           {0.0f, 0.0f, slot.angle},
                                           character.appearance.scale,
                                           0.0f};
}

void CharacterSelectScene::Render(SceneCanvas& canvas, float alpha)
{
    m_screen = canvas.screen;
    if (WaitingForTheList())
    {
        // Sin la lista, 0x634680 no dibuja; el aviso es de la UI, que sí.
        m_notice.Draw(canvas.sprites, canvas.text, canvas.screen);
        return;
    }
    Render::SpriteBatch& sprites = canvas.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    sprites.Quad(Background(0x717, 0.0f));
    sprites.Quad(Background(0x718, 320.0f));
    canvas.flushSprites();

    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, WorldViewport, SelectionCamera());
    m_lastPass = pass;
    canvas.models.Begin(pass);
    if (const std::optional<SceneWorld> world = WorldOf(SceneId::CharacterSelect))
    {
        canvas.world.Draw(*world, pass, alpha);
    }
    const Protocol::Character::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    for (const Character& character : m_characters)
    {
        DrawnCharacter drawn;
        drawn.appearance = character.appearance;
        drawn.body = BodyBetweenSteps(character.animation, m_models, alpha);
        drawn.pet = character.animation.pet.state;
        drawn.wings = character.animation.wings.state;
        drawn.object = ObjectOf(character);
        const bool isChosen = chosen != nullptr && chosen->slot == character.slot;
        drawn.light =
            glm::vec3((character.slot == HeroSlot ? HeroLight : CharacterLight) + (isChosen ? ChosenExtraLight : 0.0f));
        drawn.outlined = m_hovered == character.slot;
        DrawCharacter(canvas.models, m_models, drawn, canvas.frameClock);
    }
    canvas.flushModels();
    // La UI del modo 4 (0x65fe00): la barra, la ventana de crear y el aviso arriba.
    m_bar.Draw(sprites, canvas.text, canvas.screen);
    m_create.DrawBase(sprites, canvas.screen);
    m_create.Draw(sprites, canvas.text, canvas.screen);
    m_notice.Draw(sprites, canvas.text, canvas.screen);
}

void CharacterSelectScene::Leave()
{
    m_textures.Release();
    m_windowTextures.Release();
}

std::optional<SceneId> CharacterSelectScene::Next() const
{
    return m_next;
}

bool CharacterSelectScene::WantsToQuit() const
{
    return m_quit;
}

bool CharacterSelectScene::WantsTextInput() const
{
    return m_notice.WantsTextInput() || m_create.WantsTextInput();
}

bool CharacterSelectScene::HoldsTheLastFrame() const
{
    return false;
}
} // namespace Client::Scenes
