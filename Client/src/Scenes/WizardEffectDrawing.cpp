#include "Scenes/WizardEffectDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
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

Core::Expected<bool, std::string> DrawTheDarkBody(Render::ModelBatch& batch, const IModelLibrary& models,
                                                  const DrawnObject& object, Core::Duration frameClock)
{
    std::vector<Render::Models::SkinnedMesh> posed;
    std::span<const Render::Models::SkinnedMesh> meshes = object.retainedMeshes;
    if (meshes.empty())
    {
        // Sin el scratch publicado, el de 0x5ffdf0 con el segundo argumento en 0 (0x52da99 → 0x602e90(efecto, 0, …)):
        // WorldScratchOf, la normal girada por R · s. Nada sin el modelo, su esqueleto o la pose.
        if (object.alpha < VisibleAlpha)
            return true;
        posed = WorldScratchOf(models, object);
        if (posed.empty())
            return true;
        meshes = posed;
    }
    if (auto drawn = DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, DarkBodyFlags); !drawn.HasValue())
        return Core::Unexpected("0x6004d7: " + drawn.Error());
    return true;
}
} // namespace

Core::Expected<bool, std::string> DrawTheWizardEffectBody(Render::ModelBatch& batch, const IModelLibrary& models,
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
    return false;
}
} // namespace Client::Scenes
