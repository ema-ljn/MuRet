#include "Scenes/CharacterSelectScene.h"

#include "App/LoopTiming.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/RayPicking.h"
#include "Render/Models/Skeleton.h"
#include "Render/OriginalQuad.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
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
#include "UI/Notices.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
// El esqueleto de los personajes y la mascota que se dibuja.
constexpr std::uint16_t PlayerModel = 0x20a;

// La acción de parado de la selección. La elige 0x4f2250 por la mascota, las alas y el arma; la de
// la traza es la 3, con un arma de una mano. Las otras, sin leer (P-C14 c): hasta entonces, la 3.
constexpr std::uint8_t StandingAction = 3;

// La luz de cada personaje y la del elegido (0x50ba30 y 0x634680).
constexpr float CharacterLight = 0.4f;
constexpr float ChosenLight = 1.4f;

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
      m_notice([this](Render::TextureSlot slot) { return m_windowTextures.BufferSize(slot); }, m_texts)
{
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

void CharacterSelectScene::TakeTheList()
{
    if (m_tookTheList || m_session == nullptr || !m_session->Characters())
    {
        return;
    }
    m_tookTheList = true;
    const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(PlayerModel);
    for (const Protocol::Character::CharacterEntry& entry : m_session->Characters()->characters)
    {
        Character character;
        character.slot = entry.slot;
        character.appearance =
            GameLogic::AppearanceOf(Protocol::Character::CharacterClassOf(entry.classByte), 0, entry.equipment);
        if (skeleton != nullptr)
        {
            Render::Models::SetAction(character.body, *skeleton, StandingAction);
        }
        m_characters.push_back(character);
    }
    m_context.log.Info("la selección tiene {} personajes", m_characters.size());
}

// El cuerpo por paso con la velocidad de su acción; la mascota por tiempo (Q-C20).
void CharacterSelectScene::StepCharacters()
{
    const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(PlayerModel);
    for (Character& character : m_characters)
    {
        if (skeleton != nullptr && character.body.action < skeleton->actions.size())
        {
            const Render::Models::ActionInfo& action = skeleton->actions[character.body.action];
            Render::Models::AdvanceAnimation(character.body, action, action.speed,
                                             Render::Models::AnimationScene::CharacterSelect);
        }
        if (character.appearance.pet)
        {
            const Render::Models::Skeleton* pet = m_models.SkeletonOf(*character.appearance.pet);
            if (pet != nullptr && !pet->actions.empty())
            {
                Render::Models::AdvanceByTime(character.pet, pet->actions[0], Render::Models::PetKeysPerOriginalFrame,
                                              App::LogicStep);
            }
        }
    }
}

void CharacterSelectScene::Step(const Input::InputState& input)
{
    if (WaitingForTheList())
    {
        return;
    }
    ++m_steps;
    TakeTheList();
    StepCharacters();
    if (m_notice.IsOpen())
    {
        m_notice.Step(input, m_screen);
        return;
    }
    const auto now = App::LogicStep * static_cast<std::int64_t>(m_steps);
    const std::optional<std::uint8_t> hovered = UnderTheMouse(input);
    if (m_doubleClick.Step(input, now) && hovered)
    {
        // El doble clic elige y entra como Enter, sin el sonido (0x6342c0).
        Choose(hovered);
        TryToEnter();
        return;
    }
    if (input.WasPressed(Input::MouseButton::Left))
    {
        Choose(hovered);
    }
    // El original pide GetAsyncKeyState(VK_RETURN) == 0x8001, y pierde un Enter corto entre dos
    // lecturas; MuRet lo ve, como en el login (Q-C45).
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
    const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(PlayerModel);
    const Protocol::Character::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    for (const Character& character : m_characters)
    {
        DrawnCharacter drawn;
        drawn.appearance = character.appearance;
        drawn.body = character.body;
        if (skeleton != nullptr && character.body.action < skeleton->actions.size())
        {
            const Render::Models::ActionInfo& action = skeleton->actions[character.body.action];
            drawn.body = Render::Models::Interpolated(character.body, action, action.speed, alpha);
        }
        drawn.pet = character.pet.state;
        drawn.object = ObjectOf(character);
        drawn.light = chosen != nullptr && chosen->slot == character.slot ? ChosenLight : CharacterLight;
        DrawCharacter(canvas.models, m_models, drawn, canvas.frameClock);
    }
    canvas.flushModels();
    m_notice.Draw(sprites, canvas.text, canvas.screen);
}

void CharacterSelectScene::Leave()
{
    m_textures.Release();
    m_windowTextures.Release();
}

std::optional<SceneId> CharacterSelectScene::Next() const
{
    if (m_session != nullptr && m_session->State() == App::SessionState::Failed)
    {
        return SceneId::Login;
    }
    return m_next;
}

bool CharacterSelectScene::WantsToQuit() const
{
    return false;
}

bool CharacterSelectScene::WantsTextInput() const
{
    return false;
}

bool CharacterSelectScene::HoldsTheLastFrame() const
{
    return false;
}
} // namespace Client::Scenes
