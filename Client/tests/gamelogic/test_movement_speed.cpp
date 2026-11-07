#include "GameLogic/MovementSpeed.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <source_location>

// 06 § Caminar, velocidad 0x508940: expectativas analíticas del ASM, sin ejecutar el original.
namespace
{
using Client::GameLogic::MovementSpeedInput;
using Client::GameLogic::MovementSpeedSharedState;
using Client::GameLogic::UpdateMovementSpeed;

MovementSpeedInput PlayerInput()
{
    MovementSpeedInput input;
    input.type = 0x020a;
    input.movementMode = 1;
    input.baseSpeed = 321;
    input.map = 0;
    return input;
}

void ExpectOutcome(const MovementSpeedInput& input, std::uint8_t run, MovementSpeedSharedState shared,
                   float expectedSpeed, std::uint8_t expectedRun, std::uint32_t expectedLatch,
                   std::uint32_t expectedCounter, std::source_location location = std::source_location::current())
{
    // El argumento por defecto señala la llamada, no esta línea del helper.
    const ::testing::ScopedTrace trace(location.file_name(), static_cast<int>(location.line()), "ExpectOutcome");
    EXPECT_EQ(UpdateMovementSpeed(run, shared, input), expectedSpeed);
    EXPECT_EQ(run, expectedRun);
    EXPECT_EQ(shared.itemLatch, expectedLatch);
    EXPECT_EQ(shared.itemCounterBits, expectedCounter);
}

TEST(MovementSpeedTest, OtherTypesUseTheEntireUnsignedBaseSpeed)
{
    struct BaseCase
    {
        std::uint16_t base;
        float speed;
    };
    constexpr std::array<BaseCase, 5> Bases = {
        {{0, 0.0f}, {1, 1.0f}, {32767, 32767.0f}, {32768, 32768.0f}, {65535, 65535.0f}}};
    constexpr std::array<std::uint16_t, 9> Types = {0, 0x000a, 0x0209, 0x020b, 0x120a, 0x7fff, 0x8000, 0x820a, 0xffff};
    for (const auto type : Types)
    {
        for (const auto& [base, speed] : Bases)
        {
            SCOPED_TRACE(::testing::Message() << "type=" << type << " base=" << base);
            auto input = PlayerInput();
            input.type = type;
            input.baseSpeed = base;
            input.item = 0x1c37;
            ExpectOutcome(input, 255, {0, 100}, speed, 255, 0, 100);
        }
    }
}

TEST(MovementSpeedTest, SpecialPolicyRequiresMovementModeExactlyOne)
{
    constexpr std::array<std::uint8_t, 4> Modes = {0, 2, 0x80, 0xff};
    for (const auto mode : Modes)
    {
        SCOPED_TRACE(static_cast<unsigned>(mode));
        auto input = PlayerInput();
        input.movementMode = mode;
        input.item = 0x1c37;
        ExpectOutcome(input, 7, {0, 100}, 321.0f, 7, 0, 100);
    }
}

TEST(MovementSpeedTest, FirstSharedItemUseInitializesGlobalsAndPreservesRun)
{
    auto input = PlayerInput();
    input.item = 0x1c37;
    input.wings = 0x1a17;
    input.itemVariant = 0xff;
    ExpectOutcome(input, 7, {0, 100}, 15.0f, 7, 1, 0);
    ExpectOutcome(input, 255, {0, 0xffffffff}, 15.0f, 255, 1, 0);
}

TEST(MovementSpeedTest, SharedCounterUsesSignedThresholdsWithoutChangingAnyState)
{
    struct CounterCase
    {
        std::uint32_t bits;
        float speed;
    };
    constexpr std::array<CounterCase, 8> Cases = {{{0x80000000, 15.0f},
                                                   {0xffffffff, 15.0f},
                                                   {0, 15.0f},
                                                   {9, 15.0f},
                                                   {10, 16.0f},
                                                   {19, 16.0f},
                                                   {20, 17.0f},
                                                   {0x7fffffff, 17.0f}}};
    constexpr std::array<std::uint32_t, 3> Latches = {1, 0x80000000, 0xffffffff};
    auto input = PlayerInput();
    input.item = 0x1c37;
    for (const auto latch : Latches)
    {
        for (const auto& [bits, speed] : Cases)
        {
            SCOPED_TRACE(::testing::Message() << "latch=" << latch << " counter=" << bits);
            ExpectOutcome(input, 7, {latch, bits}, speed, 7, latch, bits);
        }
    }
}

TEST(MovementSpeedTest, HighSharedCounterTestsVariantForZeroRatherThanItsSign)
{
    struct VariantCase
    {
        std::uint8_t variant;
        float speed;
    };
    constexpr std::array<VariantCase, 4> Cases = {{{0, 17.0f}, {1, 19.0f}, {0x80, 19.0f}, {0xff, 19.0f}}};
    auto input = PlayerInput();
    input.item = 0x1c37;
    for (const auto& [variant, speed] : Cases)
    {
        SCOPED_TRACE(static_cast<unsigned>(variant));
        input.itemVariant = variant;
        ExpectOutcome(input, 7, {0xffffffff, 20}, speed, 7, 0xffffffff, 20);
    }
}

TEST(MovementSpeedTest, NonzeroVariantCannotOverrideTheLowOrMiddleCounterBands)
{
    auto input = PlayerInput();
    input.item = 0x1c37;
    input.itemVariant = 0xff;
    ExpectOutcome(input, 7, {1, 0xffffffff}, 15.0f, 7, 1, 0xffffffff);
    ExpectOutcome(input, 7, {1, 9}, 15.0f, 7, 1, 9);
    ExpectOutcome(input, 7, {1, 10}, 16.0f, 7, 1, 10);
    ExpectOutcome(input, 7, {1, 19}, 16.0f, 7, 1, 19);
}

TEST(MovementSpeedTest, SafeZoneBypassesSpecialItemsAndNeverResetsRunOrGlobals)
{
    constexpr std::array<std::uint16_t, 2> Items = {0x1c37, 0x1c16};
    constexpr std::array<std::uint8_t, 3> SafeZones = {1, 0x80, 0xff};
    constexpr std::array<std::uint16_t, 2> Wings = {0xffff, 0x1a17};
    for (const auto item : Items)
    {
        for (const auto safeZone : SafeZones)
        {
            SCOPED_TRACE(::testing::Message() << "item=" << item << " safe=" << static_cast<unsigned>(safeZone));
            auto input = PlayerInput();
            input.item = item;
            input.safeZone = safeZone;
            for (const auto wings : Wings)
            {
                SCOPED_TRACE(wings);
                input.wings = wings;
                ExpectOutcome(input, 39, {0, 100}, 12.0f, 39, 0, 100);
                ExpectOutcome(input, 40, {0xffffffff, 20}, 15.0f, 40, 0xffffffff, 20);
                ExpectOutcome(input, 255, {0xffffffff, 20}, 15.0f, 255, 0xffffffff, 20);
            }
        }
    }
}

TEST(MovementSpeedTest, Item1c16WritesRunEvenWithoutWingsAndOverridesTheirSpeed)
{
    auto input = PlayerInput();
    input.item = 0x1c16;
    ExpectOutcome(input, 0, {0, 100}, 17.0f, 40, 0, 100);
    input.wings = 0x1a17;
    ExpectOutcome(input, 255, {0xffffffff, 20}, 17.0f, 40, 0xffffffff, 20);
}

TEST(MovementSpeedTest, OnlyTheInclusiveItemPairQualifiesWithoutWings)
{
    struct ItemCase
    {
        std::uint16_t item;
        float speed;
        std::uint8_t run;
    };
    constexpr std::array<ItemCase, 19> Cases = {{{0, 12.0f, 39},
                                                 {0x1c13, 12.0f, 39},
                                                 {0x1c14, 15.0f, 40},
                                                 {0x1c15, 15.0f, 40},
                                                 {0x1c17, 12.0f, 39},
                                                 {0x7fff, 12.0f, 39},
                                                 {0x8000, 12.0f, 39},
                                                 {0xfffe, 12.0f, 39},
                                                 {0xffff, 12.0f, 39},
                                                 {0x1c36, 12.0f, 39},
                                                 {0x1c38, 12.0f, 39},
                                                 {0x9c37, 12.0f, 39},
                                                 {0x9c16, 12.0f, 39},
                                                 {0x0016, 12.0f, 39},
                                                 {0x9c14, 12.0f, 39},
                                                 {0x9c15, 12.0f, 39},
                                                 {0x0014, 12.0f, 39},
                                                 {0x0015, 12.0f, 39},
                                                 {0x0037, 12.0f, 39}}};
    for (const auto& [item, speed, run] : Cases)
    {
        SCOPED_TRACE(item);
        auto input = PlayerInput();
        input.item = item;
        ExpectOutcome(input, 39, {0xffffffff, 20}, speed, run, 0xffffffff, 20);
    }
}

TEST(MovementSpeedTest, FasterWingsSelectSixteenForEitherItemInTheInclusivePair)
{
    constexpr std::array<std::uint16_t, 2> Items = {0x1c14, 0x1c15};
    auto input = PlayerInput();
    input.wings = 0x1a17;
    for (const auto item : Items)
    {
        SCOPED_TRACE(item);
        input.item = item;
        ExpectOutcome(input, 0, {0xffffffff, 20}, 16.0f, 40, 0xffffffff, 20);
    }
}

TEST(MovementSpeedTest, EveryNonSentinelWingsValueWritesRunWithOnly1a17SelectingSixteen)
{
    struct WingsCase
    {
        std::uint16_t wings;
        float speed;
    };
    constexpr std::array<WingsCase, 9> Cases = {{{0, 15.0f},
                                                 {0x1a16, 15.0f},
                                                 {0x1a17, 16.0f},
                                                 {0x1a18, 15.0f},
                                                 {0x7fff, 15.0f},
                                                 {0x8000, 15.0f},
                                                 {0xfffe, 15.0f},
                                                 {0x9a17, 15.0f},
                                                 {0x0017, 15.0f}}};
    for (const auto& [wings, speed] : Cases)
    {
        SCOPED_TRACE(wings);
        auto input = PlayerInput();
        input.wings = wings;
        ExpectOutcome(input, 255, {0xffffffff, 20}, speed, 40, 0xffffffff, 20);
    }
}

TEST(MovementSpeedTest, SafeZoneAlsoBlocksTheInclusiveItemPair)
{
    auto input = PlayerInput();
    input.item = 0x1c14;
    input.safeZone = 0xff;
    ExpectOutcome(input, 39, {0, 100}, 12.0f, 39, 0, 100);
    input.item = 0x1c15;
    ExpectOutcome(input, 255, {0, 100}, 15.0f, 255, 0, 100);
}

TEST(MovementSpeedTest, FallbackComparesRunAsUnsignedAndDoesNotClampIt)
{
    struct RunCase
    {
        std::uint8_t run;
        float speed;
    };
    constexpr std::array<RunCase, 6> Cases = {
        {{0, 12.0f}, {39, 12.0f}, {40, 15.0f}, {127, 15.0f}, {128, 15.0f}, {255, 15.0f}}};
    for (const auto& [run, speed] : Cases)
    {
        SCOPED_TRACE(static_cast<unsigned>(run));
        ExpectOutcome(PlayerInput(), run, {0xffffffff, 20}, speed, run, 0xffffffff, 20);
    }
}

TEST(MovementSpeedTest, MapOverrideUsesTheFullWidthAndInclusiveRangeForOtherTypes)
{
    struct MapCase
    {
        std::int32_t map;
        float speed;
        std::uint8_t run;
    };
    constexpr std::array<MapCase, 15> Cases = {{{std::numeric_limits<std::int32_t>::min(), 160.5f, 7},
                                                {-1, 160.5f, 7},
                                                {17, 160.5f, 7},
                                                {18, 7.5f, 40},
                                                {19, 7.5f, 40},
                                                {20, 7.5f, 40},
                                                {21, 7.5f, 40},
                                                {22, 7.5f, 40},
                                                {23, 7.5f, 40},
                                                {24, 160.5f, 7},
                                                {std::numeric_limits<std::int32_t>::max(), 160.5f, 7},
                                                {0x112, 160.5f, 7},
                                                {0x10012, 160.5f, 7},
                                                {-238, 160.5f, 7},
                                                {std::numeric_limits<std::int32_t>::min() + 18, 160.5f, 7}}};
    for (const auto& [map, speed, run] : Cases)
    {
        SCOPED_TRACE(map);
        auto input = PlayerInput();
        input.type = 0xffff;
        input.item = 0x1c37;
        input.flags = 2;
        input.map = map;
        ExpectOutcome(input, 7, {0, 100}, speed, run, 0, 100);
    }
    auto input = PlayerInput();
    input.item = 0x1c37;
    input.movementMode = 2;
    input.map = 23;
    ExpectOutcome(input, 7, {0, 100}, 15.0f, 40, 0, 100);
}

TEST(MovementSpeedTest, MapOverrideWritesRunInsideSafeZonesWithoutInitializingItems)
{
    auto input = PlayerInput();
    input.safeZone = 0xff;
    input.map = 18;
    input.item = 0x1c37;
    ExpectOutcome(input, 7, {0, 100}, 15.0f, 40, 0, 100);
    input.item = 0x1c16;
    ExpectOutcome(input, 7, {0, 100}, 15.0f, 40, 0, 100);
    input.item = 0xffff;
    input.wings = 0x1a17;
    ExpectOutcome(input, 7, {0, 100}, 15.0f, 40, 0, 100);
}

TEST(MovementSpeedTest, MapOverridePreservesEarlierItemEffectsBeforeFinalHalving)
{
    auto input = PlayerInput();
    input.item = 0x1c37;
    input.map = 18;
    input.flags = 2;
    ExpectOutcome(input, 7, {0, 100}, 7.5f, 40, 1, 0);
    input.itemVariant = 0x80;
    ExpectOutcome(input, 7, {0xffffffff, 20}, 7.5f, 40, 0xffffffff, 20);
    input.item = 0x1c16;
    ExpectOutcome(input, 255, {0, 100}, 7.5f, 40, 0, 100);
}

TEST(MovementSpeedTest, OnlyTheHalfSpeedMaskHalvesTheFullUnsignedBase)
{
    struct FlagCase
    {
        std::uint32_t flags;
        float speed;
    };
    // 0x5089ce: AND EDX, 2; máscara 0x2, no índice de bit 2.
    constexpr std::array<FlagCase, 9> Cases = {{{0, 65535.0f},
                                                {1, 65535.0f},
                                                {2, 32767.5f},
                                                {3, 32767.5f},
                                                {0x80000000, 65535.0f},
                                                {0x80000002, 32767.5f},
                                                {4, 65535.0f},
                                                {0xfffffffd, 65535.0f},
                                                {0xffffffff, 32767.5f}}};
    for (const auto& [flags, speed] : Cases)
    {
        SCOPED_TRACE(flags);
        MovementSpeedInput input;
        input.baseSpeed = 65535;
        input.flags = flags;
        ExpectOutcome(input, 255, {0xffffffff, 20}, speed, 255, 0xffffffff, 20);
    }
}

TEST(MovementSpeedTest, FinalFlagHalvesEverySpecialSpeedWithoutHalvingState)
{
    auto input = PlayerInput();
    input.flags = 2;
    ExpectOutcome(input, 39, {0, 100}, 6.0f, 39, 0, 100);
    input.wings = 0;
    ExpectOutcome(input, 39, {0, 100}, 7.5f, 40, 0, 100);
    input.wings = 0x1a17;
    ExpectOutcome(input, 39, {0, 100}, 8.0f, 40, 0, 100);
    input.item = 0x1c16;
    ExpectOutcome(input, 39, {0, 100}, 8.5f, 40, 0, 100);
    input.item = 0x1c37;
    ExpectOutcome(input, 7, {0, 100}, 7.5f, 7, 1, 0);
    ExpectOutcome(input, 7, {1, 10}, 8.0f, 7, 1, 10);
    ExpectOutcome(input, 7, {1, 20}, 8.5f, 7, 1, 20);
    input.itemVariant = 0xff;
    ExpectOutcome(input, 7, {1, 20}, 9.5f, 7, 1, 20);
}

TEST(MovementSpeedTest, ZeroBaseRemainsPositiveZeroWithAndWithoutHalving)
{
    MovementSpeedInput input;
    std::uint8_t run = 7;
    MovementSpeedSharedState shared{0xffffffff, 20};
    EXPECT_EQ(std::bit_cast<std::uint32_t>(UpdateMovementSpeed(run, shared, input)), 0u);
    input.flags = 2;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(UpdateMovementSpeed(run, shared, input)), 0u);
    EXPECT_EQ(run, 7);
    EXPECT_EQ(shared.itemLatch, 0xffffffff);
    EXPECT_EQ(shared.itemCounterBits, 20u);
}

TEST(MovementSpeedTest, RepeatedCallsAndTwoActorsUseOneSharedStateWithoutIncrementingIt)
{
    auto firstInput = PlayerInput();
    firstInput.item = 0x1c37;
    auto secondInput = firstInput;
    secondInput.itemVariant = 0x80;
    std::uint8_t firstRun = 7;
    std::uint8_t secondRun = 255;
    MovementSpeedSharedState shared{0, 100};

    EXPECT_EQ(UpdateMovementSpeed(firstRun, shared, firstInput), 15.0f);
    EXPECT_EQ(shared.itemLatch, 1u);
    EXPECT_EQ(shared.itemCounterBits, 0u);
    EXPECT_EQ(UpdateMovementSpeed(secondRun, shared, secondInput), 15.0f);
    EXPECT_EQ(shared.itemLatch, 1u);
    EXPECT_EQ(shared.itemCounterBits, 0u);

    // Un productor externo puede cambiar D4; no se simula Walk ni se le atribuye ese store aquí.
    shared.itemCounterBits = 20;
    EXPECT_EQ(UpdateMovementSpeed(secondRun, shared, secondInput), 19.0f);
    EXPECT_EQ(UpdateMovementSpeed(firstRun, shared, firstInput), 17.0f);
    EXPECT_EQ(firstRun, 7);
    EXPECT_EQ(secondRun, 255);
    EXPECT_EQ(shared.itemLatch, 1u);
    EXPECT_EQ(shared.itemCounterBits, 20u);
}
} // namespace
