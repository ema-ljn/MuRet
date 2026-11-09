#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace Client::World
{
inline constexpr std::size_t FishPoolSize = 10;

// Referencias a almacenamiento canónico. Ningún campo se inicializa al construir la vista.
struct FishStepView
{
    std::uint8_t& active;
    std::int16_t& type;
    std::int32_t& counter;
    float& scale;
    glm::vec3& position;
    glm::vec3& angles;
    glm::vec3& previousPosition;
    std::int32_t& field58;
    std::int32_t& timer;
    std::int32_t& field64;
    float& oscillation;
    float& phase;
    std::uint8_t& field8f;
    Render::Models::OriginalMatrix& matrix;
    float& aheadX;
    float& aheadY;
    float& speed;
    float& turnRate;
    std::uint8_t& fieldE8;
    glm::vec3& light;
    std::uint8_t& action;
    std::uint8_t& previousAction;
    float& frame;
    float& previousFrame;
    std::uint8_t& field16d;
    float& field170;
    float& field174;
    // Identidad estable del slot/subsistema canónico, válida durante toda retención en efectos/audio.
    // Core la provee: nunca es la dirección de esta vista ni de una solicitud temporal.
    void* context;
};

struct FishHeroView
{
    const glm::vec3& position;
    const std::uint8_t& flag;
};

struct FishStepWorld
{
    const std::int32_t& map;
    const FishHeroView* const& hero;
    std::span<const std::uint8_t> surfaces;
    std::span<const std::uint16_t> attributes;
};

struct FishModelStepView
{
    std::uint8_t& action;
    void* identity; // Identidad opaca del modelo prestado, nunca propietario.
};

struct FishEffectRequest
{
    std::int32_t type;
    glm::vec3& firstPosition;
    glm::vec3& secondPosition;
    glm::vec3& angles;
    std::int32_t subtype;
    void* ownerContext; // Se puede retener; los otros argumentos sólo se prestan durante la llamada.
    float scale;
    std::int32_t key;
    std::int32_t skill;
    std::int32_t skillSerial;
    std::int32_t target;
};

// Todos los servicios son obligatorios al alcanzarse. Expected transporta fallos del binding;
// los retornos nativos ignorados siguen ignorados, sin convertir AL/EAX en un éxito universal.
// PC53/RN condicionado: sin traps/status x87. El backend matemático debe reutilizar
// OriginalRetainedMath; esta interfaz no incorpora algoritmo, RNG, pool ni reloj alternativos.
class IFishStepServices
{
public:
    virtual ~IFishStepServices() = default;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Random() = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Truncate(double value) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint8_t, std::error_code> EventGate() = 0;
    [[nodiscard]] virtual Core::Expected<std::uint32_t, std::error_code>
    CreateEffect(const FishEffectRequest& request) = 0;
    [[nodiscard]] virtual Core::Expected<double, std::error_code> Height(float x, float y) = 0;
    [[nodiscard]] virtual Core::Expected<FishModelStepView, std::error_code> LookupModel(std::int16_t type) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint8_t, std::error_code>
    AdvanceModel(FishModelStepView model, FishStepView& fish, float speed) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Move(FishStepView& fish, std::int32_t mode) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Steer(FishStepView& fish, std::size_t index,
                                                                      std::span<FishStepView, FishPoolSize> pool) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> AngleMatrix(FishStepView& fish) = 0;
    [[nodiscard]] virtual Core::Expected<glm::vec3, std::error_code>
    Rotate(const glm::vec3& input, const Render::Models::OriginalMatrix& matrix) = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Sound(std::int32_t id, void* context,
                                                                              std::int32_t mode) = 0;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Finalize(FishStepView& fish) = 0;
    // Frontera pura sin mutaciones: SineProductPC53(double(phase), F32bits3ecccccd).
    // Devuelve producto retenido antes de sumar0.5PC53 y almacenarF32; errores del perfil al alcanzarse.
    [[nodiscard]] virtual Core::Expected<double, std::error_code> OscillationProductPC53(float phase) = 0;
};
} // namespace Client::World
