#include "Fauna/FaunaState.h"

#include "Fauna/FaunaObject.h"
#include "Fauna/IBirdStepServices.h"
#include "Fauna/IFishStepServices.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace Client::Fauna
{
namespace
{
Fauna::BirdRecord BirdView(FaunaObject& slot)
{
    return std::as_writable_bytes(std::span<FaunaObject, 1>(&slot, 1));
}
Fauna::FishStepView FishView(FaunaObject& slot)
{
    return {slot.active,
            slot.model,
            slot.counter,
            slot.bodyScale,
            slot.position,
            slot.angles,
            slot.previousPosition,
            slot.hiddenMesh,
            slot.timer,
            slot.blendMesh,
            slot.ceiling,
            slot.phase,
            slot.flag8f,
            slot.matrix,
            slot.aheadX,
            slot.aheadY,
            slot.scale,
            slot.turnRate,
            slot.vertexLight,
            slot.light,
            slot.action,
            slot.previousAction,
            slot.frame,
            slot.previousFrame,
            slot.alphaMode,
            slot.alphaTarget,
            slot.alpha,
            &slot};
}
template <std::size_t... I> auto BirdViews(std::array<FaunaObject, BirdPoolSize>& slots, std::index_sequence<I...>)
{
    return std::array<Fauna::BirdRecord, BirdPoolSize>{BirdView(slots[I])...};
}
template <std::size_t... I>
auto FishViewsOf(std::array<FaunaObject, Fauna::FishPoolSize>& slots, std::index_sequence<I...>)
{
    return std::array<Fauna::FishStepView, Fauna::FishPoolSize>{FishView(slots[I])...};
}
} // namespace
std::span<FaunaObject, BirdPoolSize> FaunaState::Birds()
{
    return m_birds;
}
std::span<const FaunaObject, BirdPoolSize> FaunaState::Birds() const
{
    return m_birds;
}
std::span<FaunaObject, Fauna::FishPoolSize> FaunaState::Fish()
{
    return m_fish;
}
std::span<const FaunaObject, Fauna::FishPoolSize> FaunaState::Fish() const
{
    return m_fish;
}
std::array<Fauna::BirdRecord, BirdPoolSize> FaunaState::BirdRecords()
{
    return BirdViews(m_birds, std::make_index_sequence<BirdPoolSize>{});
}
std::array<Fauna::FishStepView, Fauna::FishPoolSize> FaunaState::FishViews()
{
    return FishViewsOf(m_fish, std::make_index_sequence<Fauna::FishPoolSize>{});
}
void FaunaState::Clear()
{
    for (auto& bird : m_birds)
        bird.active = 0;
    for (auto& fish : m_fish)
        fish.active = 0;
}
void FaunaState::Sound(std::int32_t id, const void* owner)
{
    m_sounds.push_back({id, owner});
}
std::span<const FaunaSound> FaunaState::Sounds() const
{
    return m_sounds;
}
void FaunaState::ClearSounds()
{
    m_sounds.clear();
}
} // namespace Client::Fauna
