#pragma once

#include "Assets/ModelTable.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Textures/Sampling.h"
#include "Kernel/CharacterAppearance.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/ModelId.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Pets/DarkSpirit.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <vector>

// What the login draws in its 3D pass (CL6.4): the logo, a world object (0x606660), and the
// scene 2 character (0x517270(2, 4, …)), with what the logic changes on them each step
// (0x634c60). Contract: Client/docs/research/06-render-escenas-y-ui.md, "El login, al entrar", "El
// logo en el binario" and "La luz del terreno en un punto", and the CL6 plan (CL6.4).
namespace Client::FrontEnd
{
// A loose world object, as 0x606660 builds it: the model, where it goes, what it passes to
// RenderBody (+0x64, +0x68, +0x6c, +0x70 and +0x58), its light (+0xf4..+0xfc, the one added to the
// terrain's) and how much its animation advances each step (+0xd8, with 0x4ec200 in 0x604ab0).
struct ModelObject
{
    std::uint16_t model = 0;
    Models::ObjectTransform transform;
    Models::ObjectState state;
    glm::vec3 light{};
    float speed = 0.0f;
};

// The logo: Logo03.bmd at 0xa2 (0x61c7c0: 0x60da30(0xa2, "Data\\Logo\\", "Logo", 3)), with its
// textures from Logo\ through 0x60d5f0(0xa2, "Logo\\", 0x2901, 0x2601, 1): repeating and with GL_LINEAR.
inline constexpr std::uint16_t LoginLogoModel = 0xa2;
// The 0x1c17 (Item\SpiritBill.bmd) the login character holds in the left hand: the raven's item
// (06 § El cuervo).
inline constexpr std::uint16_t LoginRavenItem = 0x1c17;
inline constexpr Data::Models::ModelFile LoginLogoFile{LoginLogoModel, "Logo\\", "Logo", 3};
inline constexpr Data::Textures::Sampling LoginLogoSampling{Data::Textures::Filter::Linear,
                                                            Data::Textures::Wrap::Repeat};
inline constexpr std::string_view LoginLogoTextureFolder = "Logo\\";
// Without a 0x60d530 before: its textures go to the next slot world 55 left, 0x41c (0x61a0e8) plus
// the 57 new ones of Object56\ (Q-C92; *inferred* until the capture of P-C15).
inline constexpr Render::TextureSlot LoginLogoFirstSlot{0x455};

// A loose model with its skeleton and each mesh's slot, as the original's table keeps them for the
// logo: what DrawObject asks of a library. Empty, it has none.
class SingleModelLibrary final : public Models::IModelLibrary
{
public:
    void Set(std::uint16_t model, Data::Models::Model data, Models::Skeleton skeleton,
             std::vector<Render::TextureSlot> meshTextures);
    void Clear();

    [[nodiscard]] const Data::Models::Model* Find(Models::ModelId model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(Models::ModelId model) const override;

private:
    std::optional<std::uint16_t> m_model;
    Assets::LoadedModel m_loaded;
};

// 0x606660(0xa2, (10680; 15550; 1710), (10, 0, 36), 1) and its case in scenes 2 and 4
// (0x6068d5): scale 2.2, blendMesh at 1 (the mesh of texture 1 goes additive) and no per-vertex
// light. The blendMesh light at 1 and the object's at 0 until 0x604ab0 writes them, and the
// advance of 0.16 (0x3e23d70a) per step.
[[nodiscard]] ModelObject LoginLogo();

// The count (0x6d5e30) and the phase (0x7d3a0cc) of the camera routes, which 0x633920 keeps each
// step: the count starts at −1 (0x6349a0 on entering and the .data).
struct LoginRoutes
{
    int count = -1;
    int phase = 0;
};

// 0x633920: the count goes up by 1; with phase 0 and the count at 40, the count goes back to 0 and the phase
// goes to 1, and with phase 1, every 128 the count goes back to 0 (and the camera picks another route, which
// 0x635ae0 overwrites: SceneCameras.h). The step to phase 2 belongs to the fade, which does not draw (Q-C66).
void AdvanceRoutes(LoginRoutes& routes);

// 0x604ab0 with the 0xa2: with phase 0, the count · 0.02 (0x6b8b90); with another, 1.5 (0x6b8b94).
// It goes to the object's light and the blendMesh's (Q-C105). It runs before AdvanceRoutes in the same
// step, and only with the logo already drawn: the first step does not write it, and from the second it gives
// (step − 2) · 0.02, 0.78 at step 41.
[[nodiscard]] float LoginLogoLight(const LoginRoutes& routes);

// 0x5ffce0 with an object without per-vertex light: the model's color is the terrain light at its place
// times 0.1 (0x6b87b8) plus the object's.
[[nodiscard]] glm::vec3 UnlitObjectColor(const glm::vec3& terrainLight, const glm::vec3& objectLight);

// The scene 2 character (0x517270(2, 4, 0, 0, 0, 0)): class 4, the helm, armor, pants, gloves and boots
// of set 29 (0x102e to 0x182e) at +7, the mace 0x620 at +13 in the right hand, the raven 0x1c17 in the
// left, the cape 0x1c30 and the dark horse 0x1c16 as pet, without a head (0x512930 leaves it at −1).
[[nodiscard]] Kernel::CharacterAppearance LoginCharacter();

// 0x6349a0 after 0x517270: (10603; 14112; 580), turned 188° in z and with scale 1.6.
[[nodiscard]] Models::ObjectTransform LoginCharacterTransform();

// What the raven's step (0x416a20) reads from its owner at the login: the character at its place and its
// flags (+0x78) at 0.
[[nodiscard]] Pets::DarkSpiritOwner LoginRavenOwner();

// The light 0x517270 gives it (+0xf4..+0xfc, 0x3e99999a): 0x50ba30, outside scene 4, adds it to the
// terrain's at its place.
inline constexpr float LoginCharacterLight = 0.3f;

// The standing action 0x4f2250 gives it with the dark horse as pet (+0x318 at 0x1c16).
inline constexpr std::uint8_t LoginCharacterStandingAction = 0x4a;

// 0x634c60 on each step: with the frame clock (DAT_05767b88, in ms) from 0 to 100 ms in each 2300
// (ftol(T % 2300 · 0.01) at 0: the product goes on the x87, and 100 · 0.01 gives 0.99999998, Q-C110),
// rand() % 6 at 1 sets 0x54 and at 2 0x55 (0x4ea030); with another value, or outside that window,
// none, and outside the window it does not roll. `random` stands in for rand(): 0 to 32767.
[[nodiscard]] std::optional<std::uint8_t> LoginCharacterActionAt(Core::Duration frameClock, std::mt19937& random);
// With the CRT's rand() (Models/Cloth.h), the original's one (0x634cca): the one the login uses with the
// loop.
[[nodiscard]] std::optional<std::uint8_t> LoginCharacterActionAt(Core::Duration frameClock, Crt::CrtRandom& random);
} // namespace Client::FrontEnd
