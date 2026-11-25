#include "Scenes/PkLevelReply.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Protocol/Character/PkLevel.h"
#include "Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActorState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "World/GameCamera.h"
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

// El estado de PK de un personaje (F3 08), 0x4d3b50: el personaje de la clave (0x5110d0) guarda el estado en +0x375 y
// en +0x1d6 si es 6 o más; del 1 al 6, un aviso al chat (0x555190) con su nombre y el texto de su estado.
namespace
{
using Client::Crt::CrtRandom;
using Client::Protocol::Character::PkLevel;
using Client::Scenes::WorldEntryResources;

constexpr std::size_t ChosenSlot = 9;
constexpr std::uint16_t ChosenKey = 0x0123;

struct Stage
{
    Stage()
    {
        Client::Scenes::ActorState& actor = world.Actors()[ChosenSlot];
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

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(0x1357)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemTableOf({});
    Client::Data::Tables::ItemSetTables sets = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    // Los textos inventados de 487 a 491 (0x76e0c90, 0x76e0dbc, 0x76e0ee8, 0x76e1014 y 0x76e1140).
    Client::Data::Text::TextTable texts =
        Client::Tests::MakeTextTable({{487, "heroe"}, {488, "civil"}, {489, "cuidado"}, {490, "asesino"}, {491, "!"}});
};

// 0x4d3b86..0x4d3c9c: +0x375 con el estado; +0x1d6 (el word que la lista y el F3 03 escriben como nivel) en 1 desde el
// 6 (JC) y en 0 si no; del 1 al 4, 0x555190(nombre, texto, tipo) con el 487 (1 y 2), el 488 (3) y el 489 (4), en el
// tipo 1 o 2; el 5 y el 6, "%s %d%s" (0x6c843c, 0x6c8444) con el 490, 1 o 2 y el 491, en el tipo 2. El 0 y desde el 7,
// nada.
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
            Client::Scenes::ApplyPkLevel(PkLevel{.key = ChosenKey, .level = tested.level}, stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const Client::Scenes::ActorState& actor = stage->world.Actors()[ChosenSlot];
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

// 0x5110d0 sin nadie con la clave da el 400, el registro fantasma: el estado va ahí, y el aviso con su nombre.
TEST(PkLevelReplyTest, AnUnknownKeyWritesThePhantomRecord)
{
    auto stage = std::make_unique<Stage>();
    const auto applied = Client::Scenes::ApplyPkLevel(PkLevel{.key = 0x0456, .level = 3}, stage->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(stage->world.Actors()[ChosenSlot].pk, 0);
    EXPECT_EQ(stage->world.Actors()[400].pk, 3);
    ASSERT_EQ(stage->world.Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(stage->world.Runtime().chat.log.entries[0].text, "civil");
}

// 0x4d3c19 y 0x4d3c6a: el sprintf del 5 y del 6 va a un buffer de 0x20 bytes de la pila ([EBP − 0x20]): uno que no
// entra la pisa, y corta.
TEST(PkLevelReplyTest, AMurdererTextThatOverflowsTheStackBufferIsCut)
{
    auto stage = std::make_unique<Stage>();
    stage->texts = Client::Tests::MakeTextTable({{490, std::string(30, 'a')}, {491, "!"}});
    const auto applied = Client::Scenes::ApplyPkLevel(PkLevel{.key = ChosenKey, .level = 5}, stage->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4d3c19"), std::string::npos) << applied.Error();
}
} // namespace
