#pragma once
#include "Core/Expected.h"
#include "Render/SpriteVertex.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <system_error>
namespace Client::Render
{
// Fronteras obligatorias del contexto, sin estado inicial ni implementaciones vacías.
// D3 debe aportar caches/binder raw32 y envío real: esta interfaz no acredita GL productivo.
class IParticleDrawBackend
{
public:
    using Result = Core::Expected<void, std::error_code>;
    virtual ~IParticleDrawBackend() = default;
    [[nodiscard]] virtual Result Additive() = 0;          // 620a90 completo.
    [[nodiscard]] virtual Result AlphaInheritDepth() = 0; // 620a00(raw0), sin forzar máscara.
    // Cuerpo propio620b10: prefijo modo4/blend; dos helpers OPACOS, en orden; cola alpha/textura/fog.
    // Prefix/Tail deben releer su estado en el momento acreditado, no usar snapshot entre helpers.
    // Prefix: si modo32!=4, store4 antes de enableBLEND y factoresZERO/ONE_MINUS_SRC_COLOR;
    // un hit omite ambos calls. Tail: alpha no0→store0/disable, textura0→store1/enable,
    // fog no0→enable sin store; cada lectura ocurre después de las fronteras anteriores.
    // Los dos helpers siguientes siguen opacos: no sustituirlos por efectos supuestos.

    [[nodiscard]] virtual Result SubtractivePrefix() = 0;
    [[nodiscard]] virtual Result Helper6208f0() = 0;
    [[nodiscard]] virtual Result Helper6208b0() = 0;
    [[nodiscard]] virtual Result SubtractiveTail() = 0;
    // 0x620c10: el mismo cuerpo con el modo 6 y los factores SRC_ALPHA / ONE_MINUS_SRC_ALPHA; la cola es la de
    // 0x620b10.
    [[nodiscard]] virtual Result BlendedPrefix() = 0;
    [[nodiscard]] virtual Result BlendedTail() = 0;
    [[nodiscard]] virtual Result Type620850() = 0;
    [[nodiscard]] virtual Result Type620870() = 0;
    // 620780: selector32 es la clave; hit/miss/nombre/NEG32 pertenecen al contexto durable D3.
    [[nodiscard]] virtual Result BindTexture(std::int32_t selector) = 0;
    [[nodiscard]] virtual Result Begin() = 0;
    [[nodiscard]] virtual Result Color3(const glm::vec3& light) = 0;
    [[nodiscard]] virtual Result Color4(const Color& color) = 0;
    [[nodiscard]] virtual Result TexCoord(glm::vec2 uv) = 0;
    [[nodiscard]] virtual Result Vertex(const glm::vec3& position) = 0;
    // No publicar un quad incompleto si una frontera anterior falló; tampoco simular rollback.
    [[nodiscard]] virtual Result End() = 0;
};
} // namespace Client::Render
