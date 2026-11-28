#pragma once

#include "Fauna/FaunaObject.h"
#include "Fauna/IBirdStepServices.h"
#include "Fauna/IFishStepServices.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Fauna
{
inline constexpr std::size_t BirdPoolSize = 40;
struct FaunaSound
{
    std::int32_t id;
    const void* owner;
};
// Los 50 slots físicos sobreviven a las escenas; Clear sólo escribe active (60712a..607150).
class FaunaState
{
public:
    FaunaState() = default;
    FaunaState(const FaunaState&) = delete;
    FaunaState& operator=(const FaunaState&) = delete;
    FaunaState(FaunaState&&) = delete;
    FaunaState& operator=(FaunaState&&) = delete;
    [[nodiscard]] std::span<FaunaObject, BirdPoolSize> Birds();
    [[nodiscard]] std::span<const FaunaObject, BirdPoolSize> Birds() const;
    [[nodiscard]] std::span<FaunaObject, Fauna::FishPoolSize> Fish();
    [[nodiscard]] std::span<const FaunaObject, Fauna::FishPoolSize> Fish() const;
    [[nodiscard]] std::array<Fauna::BirdRecord, BirdPoolSize> BirdRecords();
    [[nodiscard]] std::array<Fauna::FishStepView, Fauna::FishPoolSize> FishViews();
    void Clear();
    // 41ed10: solicitudes lógicas con identidad durable; el consumidor de audio las drena.
    void Sound(std::int32_t id, const void* owner);
    [[nodiscard]] std::span<const FaunaSound> Sounds() const;
    void ClearSounds();

private:
    std::array<FaunaObject, BirdPoolSize> m_birds{};
    std::array<FaunaObject, Fauna::FishPoolSize> m_fish{};
    std::vector<FaunaSound> m_sounds;
};
} // namespace Client::Fauna
