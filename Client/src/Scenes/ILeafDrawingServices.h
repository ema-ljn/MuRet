#pragma once

#include "Core/Expected.h"
#include "Scenes/WorldFaunaDrawing.h"

#include <cstdint>
#include <system_error>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::World
{
enum class LeafBlend : std::uint8_t;
struct LeafQuad;
} // namespace Client::World
namespace Client::Scenes
{
// Binding al renderer/contexto existente, biblioteca/metadata, clock/RNG prestados y cachés canónicas deCore. Nunca un
// success/no-op al faltar estado, modelo, pose, textura o binder. Los spies NO son ese backend.
class ILeafDrawingServices
{
public:
    virtual ~ILeafDrawingServices() = default;
    // Alpha corresponde620a00(1); Additive620a90, incluidas sus guardas/dependencias reales.
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Blend(World::LeafBlend blend) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> WhiteColor() = 0;
    // 620780: selector signed32, negativos y caché persistente. No convertir aTextureSlot16.
    [[nodiscard]] virtual Core::Expected<void, std::error_code> BindTexture(std::int32_t selector) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> PushMatrix() = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Translate(float x, float y, float z) = 0;
    // 621320: quad ya transformado en perfil legado. Append al MISMO ModelBatch con material/color
    //  vigentes; no forzarblanco porquad. UVcorriente queda(0,0), sin restaurar color/binder/blend.
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Quad(Render::ModelBatch& batch,
                                                                     const World::LeafQuad& quad) = 0;
    // Frontera621400 completa: el caller ya cruzó su binder externo; Billboard debe cruzar también
    // el binder interno con el selector32 conservado, consultar metadata después de glBegin y dejar
    // color/UV/binding persistentes. No estrechar el selector ni sustituirlo por MakeBillboard solo.
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Billboard(Render::ModelBatch& batch,
                                                                          const LeafBillboardRequest& request) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> PopMatrix() = 0;
};
} // namespace Client::Scenes
