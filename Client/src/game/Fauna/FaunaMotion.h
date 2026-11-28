#pragma once

#include "Fauna/FaunaObject.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Models
{
struct Skeleton;
}
namespace Client::Fauna
{
struct FaunaStepContext;
[[nodiscard]] double FaunaHeight(const FaunaStepContext& context, float x, float y);
[[nodiscard]] float FaunaSine(float phase);
[[nodiscard]] double FaunaWave(float sine);
[[nodiscard]] std::int32_t TurnFaunaAngle(std::int32_t from, std::int32_t target, std::int32_t step);
void SteerFauna(FaunaObject& object, std::size_t index, std::span<FaunaObject> pool);
void SetFaunaAction(FaunaObject& object, const Models::Skeleton& skeleton, std::uint8_t action);
void AdvanceFaunaAnimation(FaunaObject& object, const Models::Skeleton& skeleton, float speed);
void FinalizeFauna(FaunaObject& object);
} // namespace Client::Fauna
