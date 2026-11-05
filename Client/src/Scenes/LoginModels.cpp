#include "Scenes/LoginModels.h"

#include "Scenes/DarkSpirit.h"

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x606660 con el 0xa2 y su caso de 0x6068d5 (0x400ccccd, y +0x64 en 1).
constexpr float LogoScale = 2.2f;
constexpr std::int16_t LogoBlendMesh = 1;
// +0xd8 de 0x606660 (0x3e23d70a).
constexpr float ObjectSpeed = 0.16f;

// 0x633920: la fase 0 dura 40 pasos, y en la 1 la cámara cambia de ruta cada 128.
constexpr int FirstPhaseSteps = 40;
constexpr int RouteSteps = 128;
// 0x604ab0: 0x6b8b90 (0,02) y 0x6b8b94 (1,5).
constexpr float LogoLightPerStep = 0.02f;
constexpr float LogoLightAfterTheFade = 1.5f;

// 0x5ffce0: 0x6b87b8.
constexpr float TerrainLightShare = 0.1f;

// 0x517270 en la escena 2.
constexpr std::uint8_t LoginClass = 4;
constexpr std::uint8_t PartLevel = 7;
constexpr std::uint8_t WeaponLevel = 13;
constexpr std::array<std::uint16_t, GameLogic::BodyPartCount> SetParts{0x102e, 0x122e, 0x142e, 0x162e, 0x182e};
constexpr std::uint16_t Mace = 0x620;
constexpr std::uint16_t Cape = 0x1c30;
constexpr std::uint16_t DarkHorse = 0x1c16;

// 0x634c60: ftol(T) % 2300 (0x8fc) y el resto por 0,01 (0x6b88d4) en la x87.
constexpr std::int64_t ActionPeriodMs = 2300;
constexpr float WindowScale = 0.01f;
constexpr std::uint_fast32_t RandRange = 32768;
constexpr int RandChoices = 6;
constexpr int FirstChoice = 1;
constexpr int SecondChoice = 2;
constexpr std::uint8_t FirstAction = 0x54;
constexpr std::uint8_t SecondAction = 0x55;
} // namespace

void SingleModelLibrary::Set(std::uint16_t model, Data::Models::Model data, Render::Models::Skeleton skeleton,
                             std::vector<Render::TextureSlot> meshTextures)
{
    m_model = model;
    m_data = std::move(data);
    m_skeleton = std::move(skeleton);
    m_meshTextures = std::move(meshTextures);
}

void SingleModelLibrary::Clear()
{
    m_model.reset();
    m_data = {};
    m_skeleton = {};
    m_meshTextures.clear();
}

const Data::Models::Model* SingleModelLibrary::Find(std::uint16_t model) const
{
    return m_model == model ? &m_data : nullptr;
}

const Render::Models::Skeleton* SingleModelLibrary::SkeletonOf(std::uint16_t model) const
{
    return m_model == model ? &m_skeleton : nullptr;
}

std::span<const Render::TextureSlot> SingleModelLibrary::MeshTextures(std::uint16_t model) const
{
    return m_model == model ? std::span<const Render::TextureSlot>(m_meshTextures)
                            : std::span<const Render::TextureSlot>{};
}

ModelObject LoginLogo()
{
    ModelObject logo;
    logo.model = LoginLogoModel;
    logo.transform.position = {10680.0f, 15550.0f, 1710.0f};
    logo.transform.angles = {10.0f, 0.0f, 36.0f};
    logo.transform.scale = LogoScale;
    logo.state.blendMesh = LogoBlendMesh;
    logo.speed = ObjectSpeed;
    return logo;
}

void AdvanceRoutes(LoginRoutes& routes)
{
    ++routes.count;
    const int limit = routes.phase == 0 ? FirstPhaseSteps : RouteSteps;
    if (routes.count < limit)
    {
        return;
    }
    routes.count = 0;
    if (routes.phase == 0)
    {
        routes.phase = 1;
    }
}

float LoginLogoLight(const LoginRoutes& routes)
{
    return routes.phase == 0 ? static_cast<float>(routes.count) * LogoLightPerStep : LogoLightAfterTheFade;
}

glm::vec3 UnlitObjectColor(const glm::vec3& terrainLight, const glm::vec3& objectLight)
{
    return (terrainLight * TerrainLightShare) + objectLight;
}

GameLogic::CharacterAppearance LoginCharacter()
{
    GameLogic::CharacterAppearance character;
    character.characterClass = LoginClass;
    for (std::size_t part = 0; part < GameLogic::BodyPartCount; ++part)
    {
        character.bodyParts[part] = GameLogic::WornItem{.model = SetParts[part], .level = PartLevel};
    }
    character.rightHand = GameLogic::WornItem{.model = Mace, .level = WeaponLevel};
    character.leftHand = GameLogic::WornItem{.model = LoginRavenItem};
    character.wings = Cape;
    character.pet = DarkHorse;
    return character;
}

Render::Models::ObjectTransform LoginCharacterTransform()
{
    return Render::Models::ObjectTransform{{10603.0f, 14112.0f, 580.0f}, {0.0f, 0.0f, 188.0f}, 1.6f, 0.0f};
}

DarkSpiritOwner LoginRavenOwner()
{
    return DarkSpiritOwner{.position = LoginCharacterTransform().position, .flags = 0};
}

namespace
{
// La ventana: ftol(T % 2300 · 0,01) en 0, con el producto en la x87, sin redondear a float antes del ftol.
bool InTheActionWindow(Core::Duration frameClock)
{
    const std::int64_t remainder =
        std::chrono::duration_cast<std::chrono::milliseconds>(frameClock).count() % ActionPeriodMs;
    return static_cast<std::int64_t>(static_cast<double>(remainder) * static_cast<double>(WindowScale)) == 0;
}

// rand() % 6: 1 la 0x54 y 2 la 0x55.
std::optional<std::uint8_t> ActionOfRand(int rand)
{
    const int choice = rand % RandChoices;
    if (choice == FirstChoice)
    {
        return FirstAction;
    }
    if (choice == SecondChoice)
    {
        return SecondAction;
    }
    return std::nullopt;
}
} // namespace

std::optional<std::uint8_t> LoginCharacterActionAt(Core::Duration frameClock, std::mt19937& random)
{
    if (!InTheActionWindow(frameClock))
    {
        return std::nullopt;
    }
    return ActionOfRand(static_cast<int>(random() % RandRange));
}

std::optional<std::uint8_t> LoginCharacterActionAt(Core::Duration frameClock, Render::Models::CrtRandom& random)
{
    if (!InTheActionWindow(frameClock))
    {
        return std::nullopt;
    }
    return ActionOfRand(random.Next());
}
} // namespace Client::Scenes
