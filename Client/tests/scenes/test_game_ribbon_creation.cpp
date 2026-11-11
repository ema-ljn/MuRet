#include "Scenes/GameRibbonCreation.h"
#include "Scenes/GameDrawTail.h"
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

TEST(GameRibbonCreationTest, ExistingOwnerWithNonPositiveKeyReusesItsLifeAndShiftsItsHistory)
{
    std::array<Client::Scenes::RibbonState, 2> ribbons{};
    std::uint16_t equipment = 0;
    const Client::Scenes::RibbonCreateRequest request{&equipment,         &equipment, {1, 2, 3}, {4, 5, 6},
                                                      {0.2f, 0.3f, 0.4f}, 3,          false,     7};
    auto created = Client::Scenes::CreateRibbon(ribbons, request);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(*created.Value(), 0u);
    EXPECT_EQ(ribbons[0].life, 30);
    EXPECT_EQ(ribbons[0].count, 1);
    ribbons[0].life = 9;
    auto next = request;
    next.key = 0;
    next.first = {10, 20, 30};
    next.type = 1;
    created = Client::Scenes::CreateRibbon(ribbons, next);
    ASSERT_TRUE(created.HasValue());
    EXPECT_EQ(*created.Value(), 0u);
    EXPECT_EQ(ribbons[0].life, 9);
    EXPECT_EQ(ribbons[0].key, 7);
    EXPECT_EQ(ribbons[0].type, 1);
    EXPECT_EQ(ribbons[0].count, 2);
    EXPECT_EQ(ribbons[0].first[0], next.first);
    EXPECT_EQ(ribbons[0].first[1], request.first);
    EXPECT_EQ(ribbons[0].second[1], request.second);
}

TEST(GameRibbonCreationTest, PositiveKeysSplitAnOwnerAndAFullPoolDoesNotOverwrite)
{
    std::array<Client::Scenes::RibbonState, 2> ribbons{};
    std::uint16_t equipment = 1;
    Client::Scenes::RibbonCreateRequest request{&equipment, &equipment, {}, {}, {}, 0, true, 1};
    ASSERT_TRUE(Client::Scenes::CreateRibbon(ribbons, request).HasValue());
    request.key = 2;
    const auto second = Client::Scenes::CreateRibbon(ribbons, request);
    ASSERT_TRUE(second.HasValue());
    ASSERT_TRUE(second.Value().has_value());
    EXPECT_EQ(*second.Value(), 1u);
    EXPECT_EQ(ribbons[1].life, 15);
    request.key = 3;
    const auto full = Client::Scenes::CreateRibbon(ribbons, request);
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(ribbons[1].key, 2);
}

TEST(GameRibbonCreationTest, StepDuplicatesTheHeadAndExpiresOnlyAfterItsExtraCountDecrement)
{
    std::array<Client::Scenes::RibbonState, 1> ribbons{};
    auto& ribbon = ribbons[0];
    ribbon.alive = true;
    ribbon.life = 1;
    ribbon.count = 4;
    ribbon.first[0] = {1, 2, 3};
    ribbon.first[1] = {4, 5, 6};
    ASSERT_TRUE(Client::Scenes::StepRibbons(ribbons).HasValue());
    EXPECT_EQ(ribbon.life, 0);
    EXPECT_EQ(ribbon.count, 2);
    EXPECT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.first[1], ribbon.first[0]);
    EXPECT_EQ(ribbon.first[2], (glm::vec3{4, 5, 6}));
    ASSERT_TRUE(Client::Scenes::StepRibbons(ribbons).HasValue());
    EXPECT_FALSE(ribbon.alive);
    EXPECT_EQ(ribbon.count, 0);
    EXPECT_EQ(ribbon.first[0], (glm::vec3{1, 2, 3}));
}

TEST(GameRibbonCreationTest, ReemissionAfterTwoTicksReplacesBothHeadsWithoutShiftingTheRetainedPayload)
{
    std::array<Client::Scenes::RibbonState, 1> ribbons{};
    std::uint16_t equipment = 7;
    const Client::Scenes::RibbonCreateRequest request{&equipment,         &equipment, {1, 2, 3}, {4, 5, 6},
                                                      {0.2f, 0.3f, 0.4f}, 3,          false,     7};
    ASSERT_TRUE(Client::Scenes::CreateRibbon(ribbons, request).HasValue());
    auto& ribbon = ribbons[0];
    for (std::size_t index = 1; index < Client::Scenes::RibbonPointCount; ++index)
    {
        ribbon.first[index] = glm::vec3(static_cast<float>(index));
        ribbon.second[index] = -ribbon.first[index];
    }
    const auto firstHistory = ribbon.first;
    const auto secondHistory = ribbon.second;
    ASSERT_TRUE(Client::Scenes::StepRibbons(ribbons).HasValue());
    ASSERT_TRUE(Client::Scenes::StepRibbons(ribbons).HasValue());
    ASSERT_EQ(ribbon.count, -1);
    ASSERT_TRUE(ribbon.alive);
    auto next = request;
    next.key = 0;
    next.shortTrail = true;
    next.first = {10, 20, 30};
    next.second = {40, 50, 60};
    next.type = 1;
    next.light = {0.7f, 0.8f, 0.9f};
    const auto created = Client::Scenes::CreateRibbon(ribbons, next);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(*created.Value(), 0u);
    EXPECT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.count, 0);
    EXPECT_EQ(ribbon.life, 28);
    EXPECT_EQ(ribbon.key, 7);
    EXPECT_EQ(ribbon.ownerContext, &equipment);
    EXPECT_EQ(ribbon.ownerEquipment, &equipment);
    EXPECT_EQ(ribbon.type, next.type);
    EXPECT_EQ(ribbon.light, next.light);
    EXPECT_EQ(ribbon.first[0], next.first);
    EXPECT_EQ(ribbon.second[0], next.second);
    for (std::size_t index = 1; index < Client::Scenes::RibbonPointCount; ++index)
    {
        EXPECT_EQ(ribbon.first[index], firstHistory[index]);
        EXPECT_EQ(ribbon.second[index], secondHistory[index]);
    }
}

TEST(GameRibbonCreationTest, OversizedPositiveHistoryFailsAfterTypeAndLightWithoutChangingItsHeads)
{
    std::array<Client::Scenes::RibbonState, 1> ribbons{};
    std::uint16_t equipment = 7;
    const Client::Scenes::RibbonCreateRequest request{&equipment, &equipment, {1, 2, 3}, {4, 5, 6}, {}, 3, false, 7};
    ASSERT_TRUE(Client::Scenes::CreateRibbon(ribbons, request).HasValue());
    auto& ribbon = ribbons[0];
    ribbon.count = static_cast<std::int32_t>(Client::Scenes::RibbonPointCount);
    const auto first = ribbon.first;
    const auto second = ribbon.second;
    auto next = request;
    next.type = 1;
    next.light = {0.7f, 0.8f, 0.9f};
    const auto result = Client::Scenes::CreateRibbon(ribbons, next);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x52e3d5"), std::string::npos);
    EXPECT_EQ(ribbon.first, first);
    EXPECT_EQ(ribbon.second, second);
    EXPECT_EQ(ribbon.count, static_cast<std::int32_t>(Client::Scenes::RibbonPointCount));
    EXPECT_EQ(ribbon.life, 30);
    EXPECT_EQ(ribbon.type, next.type);
    EXPECT_EQ(ribbon.light, next.light);
}
