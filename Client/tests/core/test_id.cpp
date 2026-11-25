#include "Core/Id.h"

#include "support/CorePrinters.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace
{
using Client::Core::Id;
using Client::Core::IdRepresentation;

using SoundId = Id<struct SoundIdTag, std::int32_t>;
using ModelId = Id<struct ModelIdTag, std::int32_t>;
using SlotId = Id<struct SlotIdTag, std::uint8_t>;

template <typename Built, typename From>
concept BuiltWithBracesFrom = requires(From value) { Built{value}; };

template <typename Left, typename Right>
concept ComparedWith = requires(Left left, Right right) { left == right; };

static_assert(IdRepresentation<std::int8_t> && IdRepresentation<std::uint8_t>);
static_assert(IdRepresentation<std::int16_t> && IdRepresentation<std::uint16_t>);
static_assert(IdRepresentation<std::int32_t> && IdRepresentation<std::uint32_t>);
static_assert(!IdRepresentation<bool>);
static_assert(!IdRepresentation<char>);
static_assert(!IdRepresentation<const std::int32_t>);
static_assert(!IdRepresentation<std::uint64_t>);

static_assert(sizeof(SoundId) == sizeof(std::int32_t));
static_assert(sizeof(SlotId) == sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<SoundId>);
static_assert(std::is_trivially_copyable_v<SlotId>);

static_assert(!std::is_default_constructible_v<SoundId>);
static_assert(std::is_constructible_v<SoundId, std::int32_t>);
static_assert(!std::is_convertible_v<std::int32_t, SoundId>);
static_assert(!std::is_convertible_v<SoundId, std::int32_t>);
static_assert(!std::is_constructible_v<ModelId, SoundId>);
static_assert(!std::is_constructible_v<SoundId, ModelId>);

static_assert(BuiltWithBracesFrom<SlotId, std::uint8_t>);
static_assert(!BuiltWithBracesFrom<SlotId, std::int32_t>);

static_assert(ComparedWith<SoundId, SoundId>);
static_assert(!ComparedWith<SoundId, ModelId>);
static_assert(!ComparedWith<SoundId, std::int32_t>);

static_assert(SoundId{25} == SoundId{25});
static_assert(SoundId{25}.Value() == 25);

TEST(IdTest, ReturnsTheValueItWasGiven)
{
    EXPECT_EQ(SoundId{-1}.Value(), -1);
    EXPECT_EQ(SlotId{0xff}.Value(), 0xff);
}

TEST(IdTest, TwoIdsAreEqualExactlyWhenTheirValuesAre)
{
    EXPECT_EQ(SoundId{25}, SoundId{25});
    EXPECT_NE(SoundId{25}, SoundId{26});
}
} // namespace
