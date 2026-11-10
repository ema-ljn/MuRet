#pragma once

#include "Scenes/FaunaObject.h"
#include "World/IBirdStepServices.h"
#include "World/IFishStepServices.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Scenes
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
    [[nodiscard]] std::span<FaunaObject, World::FishPoolSize> Fish();
    [[nodiscard]] std::span<const FaunaObject, World::FishPoolSize> Fish() const;
    [[nodiscard]] std::array<World::BirdRecord, BirdPoolSize> BirdRecords();
    [[nodiscard]] std::array<World::FishStepView, World::FishPoolSize> FishViews();
    void Clear();
    // 41ed10: solicitudes lógicas con identidad durable; el consumidor de audio las drena.
    void Sound(std::int32_t id, const void* owner);
    [[nodiscard]] std::span<const FaunaSound> Sounds() const;
    void ClearSounds();

private:
    std::array<FaunaObject, BirdPoolSize> m_birds{};
    std::array<FaunaObject, World::FishPoolSize> m_fish{};
    std::vector<FaunaSound> m_sounds;
};
} // namespace Client::Scenes
