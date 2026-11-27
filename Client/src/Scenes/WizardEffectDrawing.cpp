#include "Scenes/WizardEffectDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Models/IModelLibrary.h"
#include "Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t DarkBodyFlags = 0x82;      // 0x6004d0
constexpr float VisibleAlpha = 0.01f;              // 0x6b88d4, el de 0x5ffdf0
constexpr Render::TextureSlot MagicTexture{0x6e9}; // Effect\Magic_b.jpg (0x61d632)

// 0x601dbb..0x601dc7: (0x3dcccccd, 0x3dcccccd, 0x41200000).
const glm::vec3 DeepBlue{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 10.0f};
// 0x601e09..0x601e14: (0x3f000000, 0x3f000000, edi = 0x3f800000 de 0x60043c).
constexpr glm::vec3 PaleBlue{0.5f, 0.5f, 1.0f};
// 0x60213e..0x60216b: el 0x109 (Protect2) con +0x4 en 1 (el edx de 0x601095), RenderBody(0x1040) sin la malla 1.
constexpr std::int16_t ShieldEffect = 0x109;
constexpr std::int32_t ShieldPassSubtype = 1;
constexpr std::uint32_t ShieldPassFlags = 0x1040; // 0x602164
constexpr std::int16_t ShieldHiddenMesh = 1;      // 0x602154

// Las mallas posadas por el llamador o, sin ellas, las que posa acá como 0x5ffdf0 con el segundo argumento en 0
// (0x52da99 → 0x602e90(efecto, 0, …)): WorldScratchOf, la normal girada por R · s. Vacío por debajo de 0,01 de alpha
// o sin el modelo, su esqueleto o la pose.
std::span<const Models::SkinnedMesh> PosedOrScratch(const Models::IModelLibrary& models, const DrawnObject& object,
                                                    std::vector<Models::SkinnedMesh>& scratch)
{
    if (!object.retainedMeshes.empty())
        return object.retainedMeshes;
    if (object.alpha < VisibleAlpha)
        return {};
    scratch = WorldScratchOf(models, object);
    return scratch;
}

Core::Expected<bool, std::string> DrawTheDarkBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                  const DrawnObject& object, Core::Duration frameClock)
{
    std::vector<Models::SkinnedMesh> posed;
    const std::span<const Models::SkinnedMesh> meshes = PosedOrScratch(models, object, posed);
    if (meshes.empty())
        return true;
    if (auto drawn = DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, DarkBodyFlags); !drawn.HasValue())
        return Core::Unexpected("0x6004d7: " + drawn.Error());
    return true;
}

// 0x60213e..0x60216b: RenderBody(0x1040, +0x174, +0x64, +0x68, +0x6c, +0x70, 1, −1), el cromo 0x1000 (Chrome4, la
// textura del pase) con la 0x40, aditivo, sin la malla 1 y con lo demás del efecto; después, el pase común (0x602170).
Core::Expected<void, std::string> DrawTheShieldChrome(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                      const DrawnObject& object, Core::Duration frameClock)
{
    std::vector<Models::SkinnedMesh> posed;
    const std::span<const Models::SkinnedMesh> meshes = PosedOrScratch(models, object, posed);
    if (meshes.empty())
        return {};
    DrawnObject chromed = object;
    chromed.state.hiddenMesh = ShieldHiddenMesh;
    if (auto drawn = DrawItemMeshPass(batch, models, chromed, frameClock, meshes, -1, ShieldPassFlags);
        !drawn.HasValue())
        return Core::Unexpected("0x60216b: " + drawn.Error());
    return {};
}
} // namespace

Core::Expected<bool, std::string> DrawTheWizardEffectBody(Render::ModelBatch& batch,
                                                          const Models::IModelLibrary& models,
                                                          const World::ModelEffect& effect, DrawnObject& object,
                                                          Core::Duration frameClock)
{
    if (effect.renderFlags == World::CrackRenderFlags)
        return DrawTheDarkBody(batch, models, object, frameClock);
    if (effect.type == World::OuterCircleEffect && (effect.subtype == 3 || effect.subtype == 4))
    {
        object.color = DeepBlue;
        return false;
    }
    if (effect.type == World::InnerCircleEffect && (effect.subtype == 2 || effect.subtype == 3))
    {
        object.color = PaleBlue;
        DrawObjectWithTexture(batch, models, object, frameClock, MagicTexture);
        return true;
    }
    if (effect.type == ShieldEffect && effect.subtype == ShieldPassSubtype)
    {
        if (auto drawn = DrawTheShieldChrome(batch, models, object, frameClock); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return false;
    }
    return false;
}
} // namespace Client::Scenes
