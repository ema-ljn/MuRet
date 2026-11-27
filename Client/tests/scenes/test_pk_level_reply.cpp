#include "Actors/PkLevelReply.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Protocol/Player/PkLevel.h"
#include "Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Kernel/ActorState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "UI/MessageArea.h"
#include "Camera/GameCamera.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A character's PK state (F3 08), 0x4d3b50: the character with the key (0x5110d0) keeps the state at +0x375 and at
// +0x1d6 if it is 6 or more; from 1 to 6, a notice to the chat (0x555190) with its name and its state's text.
namespace
{
using Client::Crt::CrtRandom;
using Client::Protocol::Player::PkLevel;
using Client::Scenes::WorldEntryResources;

constexpr std::size_t ChosenSlot = 9;
constexpr std::uint16_t ChosenKey = 0x0123;

struct Stage
{
    Stage()
    {
        Client::Kernel::ActorState& actor = world.Actors()[ChosenSlot];
        actor.alive = true;
        actor.key = ChosenKey;
        actor.level = 77;
        constexpr std::string_view Name = "Rival01";
        std::ranges::transform(Name, actor.name.begin(), [](char c) { return static_cast<std::byte>(c); });
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = std::filesystem::path{},
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = &items,
                                   .itemSetTables = &sets,
                                   .log = log,
                                   .textMeasure = &text,
                                   .screen = [] { return Client::Core::PixelSize{640, 480}; },
                                   .texts = &texts};
    }

    Client::Kernel::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth{.random = CrtRandom(0x1357)};
    Client::Scenes::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemTableOf({});
    Client::Tables::ItemSetTables sets = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    // Made-up texts 487 to 491 (0x76e0c90, 0x76e0dbc, 0x76e0ee8, 0x76e1014 and 0x76e1140).
    Client::Tables::TextTable texts =
        Client::Tests::MakeTextTable({{487, "heroe"}, {488, "civil"}, {489, "cuidado"}, {490, "asesino"}, {491, "!"}});
};

// 0x4d3b86..0x4d3c9c: +0x375 with the state; +0x1d6 (the word the list and the F3 03 write as the level) at 1 from 6
// (JC) and at 0 otherwise; from 1 to 4, 0x555190(name, text, type) with 487 (1 and 2), 488 (3) and 489 (4), in type 1
// or 2; 5 and 6, "%s %d%s" (0x6c843c, 0x6c8444) with 490, 1 or 2 and 491, in type 2. 0 and from 7 on, nothing.
TEST(PkLevelReplyTest, EachLevelKeepsItsStateAndNamesItInTheChat)
{
    struct Case
    {
        std::uint8_t level = 0;
        std::uint16_t word1d6 = 0;
        std::string text;
        std::int32_t type = 0;
    };
    const std::array<Case, 8> cases{{{0, 0, "", -1},
                                     {1, 0, "heroe", Client::UI::SystemMessage},
                                     {2, 0, "heroe", Client::UI::SystemMessage},
                                     {3, 0, "civil", Client::UI::SystemMessage},
                                     {4, 0, "cuidado", Client::UI::ErrorMessage},
                                     {5, 0, "asesino 1!", Client::UI::ErrorMessage},
                                     {6, 1, "asesino 2!", Client::UI::ErrorMessage},
                                     {7, 1, "", -1}}};
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(int{tested.level});
        auto stage = std::make_unique<Stage>();
        const auto applied =
            Client::Actors::ApplyPkLevel(PkLevel{.key = ChosenKey, .level = tested.level}, stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const Client::Kernel::ActorState& actor = stage->world.Actors()[ChosenSlot];
        EXPECT_EQ(actor.pk, tested.level);
        EXPECT_EQ(actor.level, tested.word1d6);
        const auto& entries = stage->world.Runtime().chat.log.entries;
        if (tested.type < 0)
        {
            EXPECT_TRUE(entries.empty());
            continue;
        }
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].name, "Rival01");
        EXPECT_EQ(entries[0].text, tested.text);
        EXPECT_EQ(entries[0].type, tested.type);
    }
}

// 0x5110d0 with nobody holding the key gives 400, the phantom record: the state goes there, and the notice with its
// name.
TEST(PkLevelReplyTest, AnUnknownKeyWritesThePhantomRecord)
{
    auto stage = std::make_unique<Stage>();
    const auto applied = Client::Actors::ApplyPkLevel(PkLevel{.key = 0x0456, .level = 3}, stage->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(stage->world.Actors()[ChosenSlot].pk, 0);
    EXPECT_EQ(stage->world.Actors()[400].pk, 3);
    ASSERT_EQ(stage->world.Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(stage->world.Runtime().chat.log.entries[0].text, "civil");
}

// 0x4d3c19 and 0x4d3c6a: the sprintf of 5 and 6 goes to a 0x20-byte stack buffer ([EBP − 0x20]): one that does not fit
// overwrites the stack, and it stops.
TEST(PkLevelReplyTest, AMurdererTextThatOverflowsTheStackBufferIsCut)
{
    auto stage = std::make_unique<Stage>();
    stage->texts = Client::Tests::MakeTextTable({{490, std::string(30, 'a')}, {491, "!"}});
    const auto applied = Client::Actors::ApplyPkLevel(PkLevel{.key = ChosenKey, .level = 5}, stage->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4d3c19"), std::string::npos) << applied.Error();
}
} // namespace
