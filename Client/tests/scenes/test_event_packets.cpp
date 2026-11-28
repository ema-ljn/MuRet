#include "Models/IModelLibrary.h"
#include "Events/EventPackets.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Effects/ModelEffects.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Events/EventMapObject.h"
#include "Hud/GamePanels.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/MessageArea.h"
#include "Crt/OriginalFormat.h"
#include "Ui/PanelRegistry.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"
#include "support/Bytes.h"
#include "support/FileBytes.h"
#include "support/SameBytes.h"
#include "support/TableFiles.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// CL9 milestone 11 (04 § Los eventos): what the Devil Square, Blood Castle, Chaos Castle, Kalima and Crywolf packets do
// on arrival (Events::ApplyEventPacket), the 0x40bab0 countdown and the object collapse, against the ASM of c32bbd5f
// without running the original. Text.bmd is made up, with the specifiers of the original's texts.
namespace
{
namespace Events = Client::Protocol::Events;
using Client::Crt::CrtRandom;
using Client::Events::EventMapKind;
using Client::Events::EventMapObject;
using Client::Events::EventPacketContext;
using Client::Events::EventPacketResult;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;

constexpr std::size_t BloodCastleNameText = 1146;
constexpr std::size_t ChaosCastleNameText = 1147;

std::map<std::size_t, std::string> EventTexts()
{
    return {
        {176, "+Suerte"},
        {177, "+Habil"},
        {178, "+Opcion"},
        {423, "aviso cc siete"},
        {449, "aviso fijo"},
        {639 + 1, "ds uno en %d"},
        {639 + 3, "ds tres en %d"},
        {639 + 10, "otro diez en %d"},
        {643, "ds abierto"},
        {644, "ds en% d minutos"},
        {650 + 5, "aviso bajo"},
        {677, "ds sin entrada"},
        {678, "ds tarde"},
        {679, "ds lleno"},
        {686, "muy fuerte"},
        {687, "muy debil"},
        {710 + 3, "mensaje tres"},
        {810 + 0x14, "aviso alto"},
        {820 + 4, "%s cierra en %d"},
        {820 + 5, "% s invasion (en% d segundos)"},
        {820 + 6, "%s termina en %d"},
        {820 + 7, "%s sigue %d"},
        {828, "%s abre en %d"},
        {850, "entra en% s ahora"},
        {851, "entra en% d minutos"},
        {852, "se acabo% s"},
        {853, "lleno y% d"},
        {854, "otra capa"},
        {867, "no mas que% d"},
        {868, "permitida% d veces"},
        {918, "obtuvo"},
        {886, "serie cero"},
        {890, "serie cuatro"},
        {899, "serie ya"},
        {620, "Excelente"},
        {1089, "Conjunto"},
        {BloodCastleNameText, "Blood Castle"},
        {ChaosCastleNameText, "Chaos Castle"},
        {1156, "%s abre %d de %d"},
        {1164, "Cuando %d"},
        {1776 + 0x2f, "comando 2f"},
        {1776 + 0x31, "comando 31"},
        {1800, "deshace"},
        {1801, "mezcla"},
        {1811, "sin zen"},
        {1812, "no sirve"},
        {1815, "sin lugar"},
        {1816, "para"},
        {1817, "faltan"},
        {1826, "aviso gremio"},
        {1953, "contrato firmado"},
        {1954, "altar tomado"},
        {2004, "contrato fallido"},
        {2005, "otra vez"},
        {2008, "nivel alto"},
        {2020, "castillo cero"},
        {2021, "castillo uno"},
    };
}

// Item.bmd with two names: a sword (type 1) and a shield (0x0c05); the other records, empty.
Client::Tables::ItemTable EventItems()
{
    using Client::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    const std::array<std::pair<std::size_t, std::string_view>, 2> names{{{0x0001, "Espada"}, {0x0c05, "Escudo"}}};
    for (const auto& [type, name] : names)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            plain[type * ItemRecordSize + i] = static_cast<std::byte>(name[i]);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// The models by type of 0x4ea030: Player.bmd and a transformed one with two actions.
class EventModels final : public Client::Models::IModelLibrary
{
public:
    explicit EventModels(const Client::Models::Skeleton& player) : m_player(player)
    {
        transformed.actions.resize(2);
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }

    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Client::Tables::PlayerSkeletonModel)
            return &m_player;
        return model.Value() == TransformedModel ? &transformed : nullptr;
    }

    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }

    static constexpr std::uint16_t TransformedModel = 0x45;
    static constexpr std::uint16_t MissingModel = 0x46;
    Client::Models::Skeleton transformed;

private:
    const Client::Models::Skeleton& m_player;
};

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * letterWidth, 13};
    }

    int letterWidth = 6;
};

// The maps of each event and the outside ones (Lorencia, Devias, Kalima and Crywolf).
constexpr std::array<std::int32_t, 7> BloodCastleMaps{11, 12, 13, 14, 15, 16, 17};
constexpr std::array<std::int32_t, 6> ChaosCastleMaps{18, 19, 20, 21, 22, 23};
constexpr std::array<std::int32_t, 2> DevilSquareMaps{9, 32};
constexpr std::array<std::int32_t, 11> OutsideMaps{0, 2, 24, 25, 26, 27, 28, 29, 36, 34, 30};

class EventPacketsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        // 0x4ea030 with action 0xc0 of 0x5111a0 and BD 03's 0xca: Player.bmd has them.
        player.actions.resize(0xcb);
        for (auto& action : player.actions)
            action.keyCount = 4;
        Runtime().hero = 0;
        Hero().alive = true;
        Hero().type = Client::Tables::PlayerSkeletonModel;
        Hero().key = 0x0123;
    }

    WorldRuntime& Runtime()
    {
        return world.Runtime();
    }

    Client::Kernel::ActorState& Hero()
    {
        return world.Actors()[0];
    }

    EventPacketContext Context()
    {
        return EventPacketContext{.world = world,
                                  .random = random,
                                  .texts = &texts,
                                  .items = &items,
                                  .measure = &letters,
                                  .screen = {640, 480},
                                  .tickCount = tickCount,
                                  .playerSkeleton = &player,
                                  .models = &models,
                                  .send = [this](Client::Protocol::Outgoing packet)
                                  { worldPackets.push_back(std::move(packet)); },
                                  .dataRoot = dataRoot,
                                  .terrain = &terrain};
    }

    Client::Core::Expected<EventPacketResult, std::string> Apply(const Events::EventPacket& packet)
    {
        return Client::Events::ApplyEventPacket(packet, Context());
    }

    EventPacketResult Applied(const Events::EventPacket& packet)
    {
        auto applied = Apply(packet);
        EXPECT_TRUE(applied.HasValue()) << applied.Error();
        return applied.HasValue() ? std::move(applied).Value() : EventPacketResult{};
    }

    std::string Cut(const Events::EventPacket& packet)
    {
        const auto applied = Apply(packet);
        EXPECT_FALSE(applied.HasValue());
        return applied.HasValue() ? std::string{} : applied.Error();
    }

    // What the original does outside its tables: the session goes on and the rest of the packet is dropped, with the
    // why.
    std::string Discarded(const Events::EventPacket& packet)
    {
        const EventPacketResult result = Applied(packet);
        EXPECT_TRUE(result.discarded.has_value());
        return result.discarded.value_or(std::string{});
    }

    // The notice of 0x632e30 with `text`, as Ui::OpenMessageBox leaves it in an empty box.
    void ExpectMessageBox(std::string_view text)
    {
        Client::Ui::DialogBoxState expected;
        Client::Ui::DialogState dialogs;
        Client::Ui::OpenMessageBox(expected, dialogs, text);
        EXPECT_EQ(Runtime().dialogs.current, Client::Ui::MessageBoxDialog);
        EXPECT_EQ(Runtime().dialogBox.lineCount, expected.lineCount);
        EXPECT_EQ(std::string(Runtime().dialogBox.rows.data()), std::string(expected.rows.data())) << text;
    }

    void ResetTheBox()
    {
        Runtime().dialogs = {};
        Runtime().dialogBox = {};
    }

    // The CRT rand() after 0x657a90 (close everything) on another world like this one.
    std::uint32_t AfterClosingAll()
    {
        auto other = std::make_unique<WorldState>();
        CrtRandom expected = random;
        Client::Hud::GamePanels panels(*other, expected);
        EXPECT_TRUE(panels.Registry().CloseAll().HasValue());
        return expected.State();
    }

    std::uint16_t& Cell(std::uint8_t x, std::uint8_t y)
    {
        return terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // The map's terrain with the repo's Data, as the load leaves it (0x61a560).
    void LoadTheTerrain(std::uint8_t map)
    {
        terrain = {};
        std::optional<std::string> fatal;
        const auto layout = Client::Terrain::LoadWorldTerrainLayout(dataRoot, map, terrain, fatal);
        ASSERT_TRUE(layout.HasValue()) << layout.Error();
        const auto surface = Client::Terrain::LoadWorldTerrainSurface(dataRoot, map, terrain, fatal);
        ASSERT_TRUE(surface.HasValue()) << surface.Error();
    }

    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> AttributesOf(const std::string& relative) const
    {
        const auto bytes = Client::Tests::ReadFileBytes(dataRoot / relative);
        EXPECT_TRUE(bytes.has_value()) << relative;
        auto loaded = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*bytes)), 35);
        EXPECT_TRUE(loaded.HasValue()) << relative;
        return std::move(loaded).Value();
    }

    // The light of `relative` shaded with the loaded heights of 34.
    std::unique_ptr<Client::Terrain::TerrainLighting> Shaded(const std::string& relative) const
    {
        const auto bytes = Client::Tests::ReadFileBytes(dataRoot / relative);
        EXPECT_TRUE(bytes.has_value()) << relative;
        const auto light = Client::Data::Terrain::LoadTerrainLight(std::as_bytes(std::span(*bytes)));
        EXPECT_TRUE(light.HasValue()) << relative;
        return Client::Terrain::ShadeTerrainLight(*light.Value(), *terrain.heights, 34);
    }

    // On the heap: WorldState is over 340 KB (the Windows stack is 1 MB).
    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    std::filesystem::path dataRoot{MU_CLIENT_REPO_DATA_DIR};
    CrtRandom random{1};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(EventTexts());
    LetterMeasure letters;
    Client::Tables::ItemTable items = EventItems();
    Client::Models::Skeleton player;
    EventModels models{player};
    std::uint32_t tickCount = 0;
    // What the world send() sends (BD 03's C1 05 18).
    std::vector<Client::Protocol::Outgoing> worldPackets;
};

TEST_F(EventPacketsTest, ClosingForAnEventKeepsTheSeniorBusyWarningAsALocalContinuation)
{
    texts = Client::Tests::MakeTextTable({{1641, "la caja sigue ocupada"}});
    Runtime().panels.delegated.castleSiegeList = 1;
    Runtime().npcWindows.chaosBox[0].id = 1;

    auto result = Apply(Events::BloodCastleEntryResult{0});

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(Runtime().panels.delegated.castleSiegeList, 1);
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "la caja sigue ocupada");
    EXPECT_TRUE(worldPackets.empty());
    CrtRandom expected(1);
    expected.Discard(2);
    EXPECT_EQ(random.State(), expected.State());
}

// ----- 0B (0x4e3594).

// 0x4e3621..0x4e3663: 1 turns on the storm (1) and 3 the strong one (3), with pkt[3] at 0 they turn it off; another
// event leaves it alone. It always puts away the 40 birds (0x4455c0). The same in every map.
TEST_F(EventPacketsTest, TheEventStateSetsTheStormAndPutsTheBirdsAway)
{
    for (const std::int32_t map : {0, 2, 9, 11, 18, 24, 34})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().storm = 7;
        world.Fauna().Birds()[3].active = 1;
        world.Fauna().Birds()[39].active = 1;
        Applied(Events::EventState{.state = 1, .event = 2});
        EXPECT_EQ(Runtime().storm, 7);
        EXPECT_EQ(world.Fauna().Birds()[3].active, 0);
        EXPECT_EQ(world.Fauna().Birds()[39].active, 0);
        Applied(Events::EventState{.state = 1, .event = 1});
        EXPECT_EQ(Runtime().storm, 1);
        Applied(Events::EventState{.state = 5, .event = 3});
        EXPECT_EQ(Runtime().storm, 3);
        Applied(Events::EventState{.state = 0, .event = 3});
        EXPECT_EQ(Runtime().storm, 0);
    }
}

// ----- 46 (0x4dab90).

// Type 1: each cell with 0x5f8710 (OR) if pkt[5] is 0 and with 0x5f8730 (AND with the complement) otherwise.
TEST_F(EventPacketsTest, TheCellAttributesAddWithModeZeroAndRemoveOtherwise)
{
    for (const std::int32_t map : {0, 2, 11, 24})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Cell(10, 11) = 0x0001;
        Cell(12, 13) = 0x0005;
        Applied(Events::MapAttributes{.type = 1, .attribute = 4, .mode = 0, .count = 2, .cells = {{10, 11}, {12, 13}}});
        EXPECT_EQ(Cell(10, 11), 0x0005);
        EXPECT_EQ(Cell(12, 13), 0x0005);
        Applied(Events::MapAttributes{.type = 1, .attribute = 4, .mode = 1, .count = 1, .cells = {{12, 13}}});
        EXPECT_EQ(Cell(12, 13), 0x0001);
        EXPECT_EQ(Runtime().events.objectCollapse.map, -1);
    }
}

// Type 0: rectangles from (x1, y1) to (x2, y2) with 0x5f8750 and 1 − pkt[5] in a byte: 0 adds, 1 removes and 2 (0xff)
// also adds (Q-C1320). In Blood Castle, attribute 8 first requests the collapse (map, 0x24, 0, 1).
TEST_F(EventPacketsTest, TheAreaAttributesMarkRectanglesAndTheBloodCastleGateCollapses)
{
    for (const std::int32_t map : {0, 2, 9, 11, 14, 17, 18, 24, 36})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().events.objectCollapse = {};
        Applied(Events::MapAttributes{.type = 0, .attribute = 8, .mode = 0, .count = 1, .areas = {{13, 15, 14, 17}}});
        EXPECT_EQ(Cell(13, 15), 8);
        EXPECT_EQ(Cell(14, 17), 8);
        EXPECT_EQ(Cell(15, 15), 0);
        EXPECT_EQ(Cell(13, 18), 0);
        const bool bloodCastle = map >= 11 && map <= 17;
        EXPECT_EQ(Runtime().events.objectCollapse.map, bloodCastle ? map : -1);
        EXPECT_EQ(Runtime().events.objectCollapse.type, bloodCastle ? 0x24 : -1);
        EXPECT_EQ(Runtime().events.objectCollapse.counter, bloodCastle ? 0 : -1);
        EXPECT_EQ(Runtime().events.objectCollapse.delay, bloodCastle ? 1.0f : -1.0f);

        Applied(Events::MapAttributes{.type = 0, .attribute = 8, .mode = 1, .count = 1, .areas = {{13, 15, 13, 15}}});
        EXPECT_EQ(Cell(13, 15), 0);
        EXPECT_EQ(Cell(14, 15), 8);
        Applied(Events::MapAttributes{.type = 0, .attribute = 2, .mode = 2, .count = 1, .areas = {{13, 15, 13, 15}}});
        EXPECT_EQ(Cell(13, 15), 2);
        Cell(13, 15) = 0;
        Cell(14, 15) = 0;
        Cell(13, 16) = 0;
        Cell(14, 16) = 0;
        Cell(13, 17) = 0;
        Cell(14, 17) = 0;
    }
    Runtime().map = 12;
    Runtime().events.objectCollapse = {};
    Applied(Events::MapAttributes{.type = 0, .attribute = 4, .mode = 0, .count = 1, .areas = {{1, 1, 1, 1}}});
    EXPECT_EQ(Runtime().events.objectCollapse.map, -1);
}

// Without the map's .att it stops; another type does nothing.
TEST_F(EventPacketsTest, TheAttributesNeedTheMapAttributes)
{
    Applied(Events::MapAttributes{.type = 2, .attribute = 8, .mode = 0, .count = 3});
    terrain.attributes.reset();
    const std::string cut =
        Cut(Events::MapAttributes{.type = 1, .attribute = 4, .mode = 0, .count = 1, .cells = {{1, 2}}});
    EXPECT_NE(cut.find("0x4dabd0"), std::string::npos) << cut;
}

// ----- 47 (0x4d6990).

// With the hero's key, nothing. With another, the message of 0x555190 with the owner's name (+0x1d9): the prefix of
// 0x10000 (620) or, without it, that of 0x20000 (1089) with "%s ", the name from 0x5b1c70 (the low 13 bits and
// pkt[0xc]), what 0x2000, 0x8000 and 0x4000 add in that order (176, 177 and 178) and "%s %s" with 918. With nobody
// holding the key, 400.
TEST_F(EventPacketsTest, TheItemAcquiredNoticeNamesTheItemAndItsOwner)
{
    // One-pixel letters: the notice fits whole in the first 180-wide line of 0x45bb20.
    letters.letterWidth = 1;
    Client::Kernel::ActorState& other = world.Actors()[5];
    other.alive = true;
    other.key = 0x0456;
    const std::string_view name = "Brezal";
    for (std::size_t i = 0; i < name.size(); ++i)
        other.name[i] = static_cast<std::byte>(name[i]);

    Applied(Events::ItemAcquired{.key = 0x0123, .item = 0x1fffe001, .level = 1});
    EXPECT_TRUE(Runtime().chat.log.entries.empty());

    struct Case
    {
        std::uint32_t item;
        std::uint8_t level;
        std::string_view text;
    };
    const std::array<Case, 5> cases{{
        {0x0001, 0, "Espada obtuvo"},
        {0x10000 | 0x2000 | 0x4000 | 0x8000 | 0x0001, 3, "Excelente Espada +3+Suerte+Habil+Opcion obtuvo"},
        {0x20000 | 0x4000 | 0x0c05, 0, "Conjunto Escudo+Opcion obtuvo"},
        {0x30000 | 0x0c05, 0, "Excelente Escudo obtuvo"},
        {0xa000 | 0x0001, 2, "Espada +2+Suerte+Habil obtuvo"},
    }};
    for (const Case& test : cases)
    {
        SCOPED_TRACE(test.item);
        Applied(Events::ItemAcquired{.key = 0x0456, .item = test.item, .level = test.level});
        ASSERT_FALSE(Runtime().chat.log.entries.empty());
        EXPECT_EQ(Runtime().chat.log.entries.front().name, "Brezal");
        EXPECT_EQ(Runtime().chat.log.entries.front().text, test.text);
        EXPECT_EQ(Runtime().chat.log.entries.front().type, Client::Ui::SystemMessage);
    }
    // 0x5110d0 without the key (and with one from 0x8000, by the MOVSX) gives 400, with no name.
    for (const std::uint16_t key : {std::uint16_t{0x0789}, std::uint16_t{0x8456}})
    {
        Applied(Events::ItemAcquired{.key = key, .item = 0x0001, .level = 0});
        EXPECT_EQ(Runtime().chat.log.entries.front().name, "");
        EXPECT_EQ(Runtime().chat.log.entries.front().text, "Espada obtuvo");
    }
}

// Without Item.bmd it stops at 0x5b1c70; so does what does not fit with its '\0' in the 100-byte stack buffers.
TEST_F(EventPacketsTest, TheItemAcquiredNoticeCutsWithoutTheItemsOrOutsideItsBuffers)
{
    letters.letterWidth = 1;
    auto context = Context();
    context.items = nullptr;
    const auto withoutItems = Client::Events::ApplyEventPacket(Events::ItemAcquired{.key = 0x0456, .item = 1}, context);
    ASSERT_FALSE(withoutItems.HasValue());
    EXPECT_NE(withoutItems.Error().find("0x4d6a54"), std::string::npos) << withoutItems.Error();

    auto longTexts = EventTexts();
    longTexts[176] = std::string(86, 'x');
    texts = Client::Tests::MakeTextTable(longTexts);
    // "Espada" and 86: 92 in the first, and with " obtuvo" 99 and the '\0' in the second: they fit exactly.
    Applied(Events::ItemAcquired{.key = 0x0456, .item = 0x2001, .level = 0});
    EXPECT_EQ(Runtime().chat.log.entries.front().text.size(), 99u);
    // With "+Habil" and "+Opcion", 105 do not fit in the first.
    const std::string strcat = Cut(Events::ItemAcquired{.key = 0x0456, .item = 0xe001, .level = 0});
    EXPECT_NE(strcat.find("0x4d6b1f"), std::string::npos) << strcat;
    // With "+Habil", 98 fit in the first and 105 do not fit in the second.
    const std::string sprintf = Cut(Events::ItemAcquired{.key = 0x0456, .item = 0xa001, .level = 0});
    EXPECT_NE(sprintf.find("0x4d6b36"), std::string::npos) << sprintf;
}

// ----- 90 (0x4e4870).

// Closes the event NPC window (0x65cae0(0x1000)) and, with pkt[3] from 1 to 5, notice 677, 678, 686, 687 or 679.
TEST_F(EventPacketsTest, TheDevilSquareEntryShowsItsNotice)
{
    const std::array<std::string_view, 5> notices{"ds sin entrada", "ds tarde", "muy fuerte", "muy debil", "ds lleno"};
    for (const std::int32_t map : {0, 2, 9, 32})
    {
        Runtime().map = map;
        for (std::uint8_t result = 1; result <= 5; ++result)
        {
            SCOPED_TRACE(testing::Message() << map << " " << int{result});
            ResetTheBox();
            Applied(Events::DevilSquareEntryResult{result});
            ExpectMessageBox(notices[result - 1U]);
        }
        ResetTheBox();
        Applied(Events::DevilSquareEntryResult{0});
        Applied(Events::DevilSquareEntryResult{6});
        EXPECT_EQ(Runtime().dialogs.current, 0);
    }
}

// With the NPC window open, 0x65cae0(0x1000) sends C1 03 31 (0x658a04) and also closes the inventory; without the
// session's send, closing it stops.
TEST_F(EventPacketsTest, TheDevilSquareEntryClosesTheOpenWindow)
{
    Runtime().panels.eventNpc = 1;
    Runtime().panels.inventory = 1;
    const std::string cut = Cut(Events::DevilSquareEntryResult{3});
    EXPECT_NE(cut.find("0x657ab0(0x1000)"), std::string::npos) << cut;

    int sent = 0;
    auto context = Context();
    context.sendWindowClose = [&sent]() { ++sent; };
    const auto applied = Client::Events::ApplyEventPacket(Events::DevilSquareEntryResult{3}, context);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(sent, 1);
    EXPECT_EQ(Runtime().panels.eventNpc, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    ExpectMessageBox("muy fuerte");
}

// 0x4da78f → 0x657a90 → 0x657ab0(0x10000): the 9A that arrives with an NPC shop open closes it with C1 03 31 in the
// clear through the windows' session (0x659063), empties its grid and closes the inventory; without that send, closing
// it stops. The same for the warehouse (C1 03 82) and the empty chaos box (C1 03 87).
TEST_F(EventPacketsTest, TheBloodCastleEntryClosesAnOpenNpcWindow)
{
    using Client::Ui::PanelState;
    struct NpcWindowCase
    {
        std::uint8_t PanelState::*open;
        std::byte closeCode;
    };
    constexpr std::array<NpcWindowCase, 3> windows{{{&PanelState::npcShop, std::byte{0x31}},
                                                    {&PanelState::warehouse, std::byte{0x82}},
                                                    {&PanelState::chaosBox, std::byte{0x87}}}};
    for (const NpcWindowCase& window : windows)
    {
        SCOPED_TRACE(std::to_integer<int>(window.closeCode));
        Runtime().panels.*window.open = 1;
        Runtime().panels.inventory = 1;
        const std::string cut = Cut(Events::BloodCastleEntryResult{0});
        EXPECT_NE(cut.find("sin la sesión"), std::string::npos) << cut;

        Runtime().panels.*window.open = 1;
        Runtime().panels.inventory = 1;
        Runtime().npcWindows.grid[5].id = 0x0c01;
        Runtime().npcWindows.grid[5].occupied = 1;
        std::vector<std::vector<std::byte>> sent;
        auto context = Context();
        context.sendNpcWindow =
            [&sent](Client::Protocol::Outgoing outgoing) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(outgoing.delivery, Client::Network::Delivery::Clear);
            sent.emplace_back(outgoing.packet.Bytes().begin(), outgoing.packet.Bytes().end());
            return {};
        };
        const auto applied = Client::Events::ApplyEventPacket(Events::BloodCastleEntryResult{0}, context);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0], (std::vector<std::byte>{std::byte{0xc1}, std::byte{0x03}, window.closeCode}));
        EXPECT_EQ(Runtime().panels.*window.open, 0);
        EXPECT_EQ(Runtime().panels.inventory, 0);
        if (window.open != &PanelState::chaosBox)
        {
            EXPECT_EQ(Runtime().npcWindows.grid[5].id, 0xffff);
            EXPECT_EQ(Runtime().npcWindows.grid[5].occupied, 0u);
        }
        EXPECT_TRUE(worldPackets.empty());
    }
}

// ----- 91 (0x4da8c0), with wsprintfA.

// 1: 643 with pkt[4] at 0 and otherwise 644 with the minutes, which wsprintfA's "% d" does not print (P-C290). 2: 850
// with the Blood Castle name or 851 with (minutes, name). 3, nothing.
TEST_F(EventPacketsTest, TheOpenTimeOfDevilSquareAndBloodCastle)
{
    Applied(Events::EventOpenTime{.kind = 1});
    ExpectMessageBox("ds abierto");
    ResetTheBox();
    Applied(Events::EventOpenTime{.kind = 1, .value4 = 15});
    ExpectMessageBox("ds en d minutos");
    ResetTheBox();
    Applied(Events::EventOpenTime{.kind = 2});
    ExpectMessageBox("entra en s ahora");
    ResetTheBox();
    Applied(Events::EventOpenTime{.kind = 2, .value4 = 5});
    ExpectMessageBox("entra en d minutos");
    ResetTheBox();
    Applied(Events::EventOpenTime{.kind = 3, .value4 = 5});
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 4: with the word pkt[4]:pkt[6] at 0, the 0x9a with 850 (name) and 1156 (name, pkt[5], 100); otherwise, 1164 with
// the hours and 851 with (minutes, name) joined by the strcat.
TEST_F(EventPacketsTest, TheOpenTimeOfChaosCastleOpensTheChaosCastleDialog)
{
    Applied(Events::EventOpenTime{.kind = 4, .value4 = 0, .value5 = 7, .value6 = 0});
    EXPECT_EQ(Runtime().dialogs.current, Client::Ui::ChaosCastleDialog);
    {
        Client::Ui::DialogBoxState expected;
        Client::Ui::DialogState dialogs;
        const std::array<std::string_view, 2> lines{"entra en s ahora", "Chaos Castle abre 7 de 100"};
        ASSERT_TRUE(Client::Ui::OpenTheChaosCastleDialog(expected, dialogs, lines).HasValue());
        EXPECT_EQ(Runtime().dialogBox.rows, expected.rows);
        EXPECT_EQ(Runtime().dialogBox.lineCount, expected.lineCount);
    }
    ResetTheBox();
    // 1 · 256 + 10 = 266 minutes: 4 hours and 26.
    Applied(Events::EventOpenTime{.kind = 4, .value4 = 1, .value5 = 0, .value6 = 10});
    Client::Ui::DialogBoxState expected;
    Client::Ui::DialogState dialogs;
    const std::array<std::string_view, 1> lines{"Cuando 4entra en d minutos"};
    ASSERT_TRUE(Client::Ui::OpenTheChaosCastleDialog(expected, dialogs, lines).HasValue());
    EXPECT_EQ(Runtime().dialogBox.rows, expected.rows);
    EXPECT_EQ(Runtime().dialogs.current, Client::Ui::ChaosCastleDialog);
}

// 0x4daa3b..0x4daa73: the strcat of 851 onto record 1154 (300 bytes) that does not fit with its '\0' would overwrite
// 1155 and the original opens 0x9a anyway. MuRet drops the rest of 91 without 0x9a, the session goes on and the
// notice carries the length, not the texts (Q-C1109). With 299 characters and its '\0' it fits exactly and opens 0x9a.
TEST_F(EventPacketsTest, TheChaosCastleTimeThatOverflowsItsRecordIsDiscarded)
{
    // "Cuando 4" (1164 with the hours) and "entra en d minutos" (851): 8 and 18 characters, plus the padding.
    const auto withHours = [this](std::size_t padding)
    {
        auto longer = EventTexts();
        longer[1164] = std::string(padding, 'h') + "Cuando %d";
        texts = Client::Tests::MakeTextTable(longer);
    };
    // 1 · 256 + 10 = 266 minutes: 4 hours and 26.
    const Events::EventOpenTime packet{.kind = 4, .value4 = 1, .value5 = 0, .value6 = 10};
    withHours(300 - 8 - 18);
    const std::string reason = Discarded(packet);
    EXPECT_NE(reason.find("0x4daa73"), std::string::npos) << reason;
    EXPECT_NE(reason.find("301 bytes"), std::string::npos) << reason;
    EXPECT_EQ(reason.find("hhh"), std::string::npos) << reason;
    EXPECT_EQ(reason.find("minutos"), std::string::npos) << reason;
    EXPECT_EQ(Runtime().dialogs.current, 0);

    ResetTheBox();
    withHours(299 - 8 - 18);
    EXPECT_FALSE(Applied(packet).discarded.has_value());
    EXPECT_EQ(Runtime().dialogs.current, Client::Ui::ChaosCastleDialog);
}

// ----- 92 (0x4e4930) and the countdown (0x40bab0).

// With the event object, +8 = pkt[3] + 1 and +0xc = GetTickCount; without it (Lorencia, Devias, Kalima, Crywolf),
// nothing.
TEST_F(EventPacketsTest, TheCountdownStartsOnlyWithTheEventObject)
{
    tickCount = 123456;
    for (const std::int32_t map : OutsideMaps)
    {
        Runtime().map = map;
        Runtime().eventObject.reset();
        Applied(Events::EventCountdown{.type = 3});
        EXPECT_FALSE(Runtime().eventObject.has_value());
    }
    for (const auto& [map, kind] : {std::pair{9, EventMapKind::DevilSquare}, std::pair{32, EventMapKind::DevilSquare},
                                    std::pair{11, EventMapKind::BloodCastle}, std::pair{18, EventMapKind::ChaosCastle}})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = kind};
        Applied(Events::EventCountdown{.type = 3});
        EXPECT_EQ(Runtime().eventObject->field08, 4);
        EXPECT_EQ(Runtime().eventObject->field0c, 123456);
    }
}

// 0x40bab0: 639 + n with 1 to 3 and 8 to 10 (the numbers), 820 + n with 4 to 7 and the Blood Castle name, 813 + n with
// 11 to 14 and the Chaos Castle one (with 828 instead of 825). The seconds are 30 − elapsed / 1000.
TEST_F(EventPacketsTest, TheCountdownTextsByKind)
{
    struct Case
    {
        std::int32_t kind;
        std::string_view text;
    };
    const std::array<Case, 9> cases{{
        {1, "ds uno en 30"},
        {3, "ds tres en 30"},
        {10, "otro diez en 30"},
        {4, "Blood Castle cierra en 30"},
        // P-C290: wsprintfA's "% s" and "% d" come out as text, without arguments.
        {5, " s invasion (en d segundos)"},
        {7, "Blood Castle sigue 30"},
        {11, "Chaos Castle cierra en 30"},
        {12, "Chaos Castle abre en 30"},
        {13, "Chaos Castle termina en 30"},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.kind);
        EventMapObject event{.field08 = probe.kind, .field0c = 5000};
        const auto text = Client::Events::EventCountdownText(event, 5000, &texts, true);
        ASSERT_TRUE(text.HasValue()) << text.Error();
        ASSERT_TRUE(text.Value().has_value());
        EXPECT_EQ(*text.Value(), probe.text);
    }
}

// The seconds left by the unsigned subtraction: at 12.345 s, 18 are left; at exactly 30 s, 0; past it, nothing and +8
// at 0 if it writes. A GetTickCount that wrapped around still counts right. With +8 outside 1..14, nothing.
TEST_F(EventPacketsTest, TheCountdownExpiresAfterThirtySeconds)
{
    EventMapObject event{.field08 = 1, .field0c = 1000};
    EXPECT_EQ(*Client::Events::EventCountdownText(event, 1000 + 12345, &texts, true).Value(), "ds uno en 18");
    EXPECT_EQ(*Client::Events::EventCountdownText(event, 1000 + 30000, &texts, true).Value(), "ds uno en 0");
    EXPECT_FALSE(Client::Events::EventCountdownText(event, 1000 + 30001, &texts, false).Value().has_value());
    EXPECT_EQ(event.field08, 1);
    EXPECT_FALSE(Client::Events::EventCountdownText(event, 1000 + 30001, &texts, true).Value().has_value());
    EXPECT_EQ(event.field08, 0);

    EventMapObject wrapped{.field08 = 1, .field0c = static_cast<std::int32_t>(0xfffffc18u)};
    EXPECT_EQ(*Client::Events::EventCountdownText(wrapped, 2000, &texts, true).Value(), "ds uno en 27");

    for (const std::int32_t kind : {0, 15, -1})
    {
        EventMapObject other{.field08 = kind, .field0c = 0};
        EXPECT_FALSE(Client::Events::EventCountdownText(other, 0, &texts, true).Value().has_value());
    }
    EventMapObject missing{.field08 = 2, .field0c = 0};
    const auto cut = Client::Events::EventCountdownText(missing, 0, nullptr, true);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x40bab0"), std::string::npos) << cut.Error();
}

// ----- 93 (0x4e4959).

// Devil Square (0x40bd20): with pkt[4] < 200, the buttons, +0x24 = count, +0x28 = rank, the 24-byte copy per record to
// +0x2c and the 0x8b. Blood Castle (0x40c0c0) with 0xff and Chaos Castle (0x404da0) with 0xfe: one record, +0x24 =
// rank and the 0x97 or the 0x9b.
TEST_F(EventPacketsTest, TheResultsFillTheEventObjectAndPushTheirDialog)
{
    std::vector<std::byte> scores(48);
    for (std::size_t index = 0; index < scores.size(); ++index)
        scores[index] = static_cast<std::byte>(index + 1);
    for (const std::int32_t map : DevilSquareMaps)
    {
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::DevilSquare};
        Applied(Events::EventResult{.rank = 3, .count = 2, .scores = scores});
        const EventMapObject& event = *Runtime().eventObject;
        EXPECT_EQ(event.field24, 2);
        EXPECT_EQ(event.field28, 3);
        EXPECT_EQ(event.field2c[0], 0x04030201);
        EXPECT_EQ(event.field2c[11], 0x302f2e2d);
        EXPECT_EQ(event.field2c[12], 0);
        EXPECT_EQ(Runtime().dialogs.current, 0x8b);
        EXPECT_EQ(Runtime().dialogBox.buttons[0].y, 0x104);
        Runtime().dialogs = {};
        Applied(Events::EventResult{.rank = 3, .count = 200, .scores = scores});
        EXPECT_EQ(Runtime().dialogs.current, 0);
    }
    for (const auto& [kind, count, dialog, maps] :
         {std::tuple{EventMapKind::BloodCastle, std::uint8_t{0xff}, 0x97, std::vector<int>{11, 17}},
          std::tuple{EventMapKind::ChaosCastle, std::uint8_t{0xfe}, 0x9b, std::vector<int>{18, 23}}})
    {
        for (const int map : maps)
        {
            SCOPED_TRACE(map);
            Runtime() = WorldRuntime{};
            Runtime().map = map;
            Runtime().eventObject = EventMapObject{.kind = kind};
            Applied(Events::EventResult{.rank = 5, .count = 1, .scores = scores});
            EXPECT_EQ(Runtime().dialogs.current, 0);
            Applied(Events::EventResult{.rank = 5, .count = count, .scores = scores});
            EXPECT_EQ(Runtime().eventObject->field24, 5);
            EXPECT_EQ(Runtime().eventObject->field2c[5], 0x18171615);
            EXPECT_EQ(Runtime().eventObject->field2c[6], 0);
            EXPECT_EQ(Runtime().dialogs.current, dialog);
            EXPECT_EQ(Runtime().dialogBox.buttons[0].y, 0x82);
        }
    }
}

// Q-C1324: more than 11 records pass the 0x108 bytes of +0x2c; fewer bytes than it copies come from the receive
// buffer. In both the rest is dropped, after the buttons, +0x24 and +0x28 (0x40bd34..0x40bd81): neither the copy nor
// the 0x8b. Blood Castle (0x40c12a) and Chaos Castle (0x404e0a) without the 24 bytes, the same. The session goes on.
TEST_F(EventPacketsTest, TheResultsThatOverflowTheObjectAreDiscarded)
{
    for (const std::int32_t map : DevilSquareMaps)
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::DevilSquare};
        Runtime().eventObject->field2c[0] = 0x55;
        const std::string overflow = Discarded(
            Events::EventResult{.rank = 1, .count = 12, .scores = std::vector<std::byte>(12 * 24, std::byte{7})});
        EXPECT_NE(overflow.find("Q-C1324"), std::string::npos) << overflow;
        EXPECT_EQ(Runtime().eventObject->field24, 12);
        EXPECT_EQ(Runtime().eventObject->field28, 1);
        EXPECT_EQ(Runtime().eventObject->field2c[0], 0x55);
        EXPECT_EQ(Runtime().dialogBox.buttons[0].y, 0x104);
        EXPECT_EQ(Runtime().dialogs.current, 0);
        const std::string shorter =
            Discarded(Events::EventResult{.rank = 1, .count = 2, .scores = std::vector<std::byte>(24, std::byte{7})});
        EXPECT_NE(shorter.find("0x40bd97"), std::string::npos) << shorter;
        EXPECT_EQ(Runtime().eventObject->field24, 2);
        EXPECT_EQ(Runtime().eventObject->field2c[0], 0x55);
        EXPECT_EQ(Runtime().dialogs.current, 0);
    }
    for (const auto& [kind, count, where, map] :
         {std::tuple{EventMapKind::BloodCastle, std::uint8_t{0xff}, "0x40c12a", 11},
          std::tuple{EventMapKind::ChaosCastle, std::uint8_t{0xfe}, "0x404e0a", 18}})
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = kind};
        const std::string shorter =
            Discarded(Events::EventResult{.rank = 4, .count = count, .scores = std::vector<std::byte>(23)});
        EXPECT_NE(shorter.find(where), std::string::npos) << shorter;
        EXPECT_EQ(Runtime().eventObject->field24, 4);
        EXPECT_EQ(Runtime().dialogs.current, 0);
    }
    for (const std::int32_t map : {0, 2})
    {
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        const EventPacketResult result =
            Applied(Events::EventResult{.rank = 1, .count = 12, .scores = std::vector<std::byte>(5)});
        EXPECT_FALSE(result.discarded.has_value());
    }
}

// ----- 94, 95 and 96.

// 0x4dd8a0: closes everything (with its rand()), 0x78abb04 = the signed count and 0x78abb00 = type + 1; with 1, the
// Muto numbers if none is 0xffff; with 3 (0x4dd8ce..0x4dd920), the serial number zeroed, the chat buffers empty, the
// field closed, a single field for Tab and 0x77f3b6b at 1.
TEST_F(EventPacketsTest, TheChipInfoOpensTheChipWindow)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        const std::uint32_t closed = AfterClosingAll();
        Applied(Events::EventChipInfo{.type = 0, .count = 0xfffe, .muto = std::array<std::uint16_t, 3>{1, 2, 3}});
        EXPECT_EQ(random.State(), closed);
        EXPECT_EQ(Runtime().panels.field78abb00, 1);
        EXPECT_EQ(Runtime().panels.field78abb04, -2);
        EXPECT_EQ(Runtime().events.mutoNumbers, (std::array<std::uint16_t, 3>{1, 2, 3}));
        Applied(Events::EventChipInfo{.type = 0, .count = 4, .muto = std::array<std::uint16_t, 3>{4, 0xffff, 6}});
        EXPECT_EQ(Runtime().events.mutoNumbers, (std::array<std::uint16_t, 3>{1, 2, 3}));
        Applied(Events::EventChipInfo{.type = 1, .count = 9});
        EXPECT_EQ(Runtime().panels.field78abb00, 2);
        EXPECT_EQ(Runtime().panels.field78abb04, 9);

        Runtime().events.serial[0] = std::byte{0x55};
        Runtime().ui.chatOpen = true;
        Runtime().chat.input.messageBuffer[0] = 'a';
        Runtime().chat.input.messageLength = 1;
        Runtime().chat.input.fieldCount = 2;
        const std::uint32_t closedAgain = AfterClosingAll();
        Applied(Events::EventChipInfo{.type = 2, .count = 1});
        EXPECT_EQ(Runtime().panels.field78abb00, 3);
        EXPECT_EQ(Runtime().events.serial[0], std::byte{0});
        EXPECT_FALSE(Runtime().ui.chatOpen);
        EXPECT_EQ(Runtime().chat.input.messageBuffer[0], '\0');
        EXPECT_EQ(Runtime().chat.input.messageLength, 0);
        EXPECT_EQ(Runtime().chat.input.fieldCount, 1);
        EXPECT_TRUE(Runtime().events.serialEntry);
        EXPECT_EQ(random.State(), closedAgain);
    }
}

// 0x4dd926..0x4dd92c: the short 94 with type 0 reads the Muto numbers from the receive buffer. They are dropped, after
// closing everything (with its rand()) and 0x78abb00/04; the session goes on.
TEST_F(EventPacketsTest, TheChipInfoWithoutTheMutoNumbersIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().events.mutoNumbers = {4, 5, 6};
        const std::uint32_t closed = AfterClosingAll();
        const std::string discarded = Discarded(Events::EventChipInfo{.type = 0, .count = 7});
        EXPECT_NE(discarded.find("0x4dd92c"), std::string::npos) << discarded;
        EXPECT_EQ(random.State(), closed);
        EXPECT_EQ(Runtime().panels.field78abb00, 1);
        EXPECT_EQ(Runtime().panels.field78abb04, 7);
        EXPECT_EQ(Runtime().events.mutoNumbers, (std::array<std::uint16_t, 3>{4, 5, 6}));
    }
}

// 0x4e49d4: the count except 0xffff; 0x4e49fb: the three numbers except with one at 0xffff.
TEST_F(EventPacketsTest, TheChipCountAndTheMutoNumbers)
{
    Runtime().panels.field78abb04 = 3;
    Applied(Events::EventChipCount{0xffff});
    EXPECT_EQ(Runtime().panels.field78abb04, 3);
    Applied(Events::EventChipCount{0x7fff});
    EXPECT_EQ(Runtime().panels.field78abb04, 0x7fff);
    Applied(Events::MutoNumbers{{7, 8, 9}});
    EXPECT_EQ(Runtime().events.mutoNumbers, (std::array<std::uint16_t, 3>{7, 8, 9}));
    Applied(Events::MutoNumbers{{0xffff, 8, 9}});
    EXPECT_EQ(Runtime().events.mutoNumbers, (std::array<std::uint16_t, 3>{7, 8, 9}));
}

// ----- 99 (0x4e4a40).

TEST_F(EventPacketsTest, TheEventDialogPushesItsDialog)
{
    Applied(Events::EventDialog{0});
    EXPECT_EQ(Runtime().dialogs.current, 0x8f);
    Applied(Events::EventDialog{1});
    EXPECT_EQ(Runtime().dialogs.pending, 0x90);
    Runtime().dialogs = {};
    Applied(Events::EventDialog{2});
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// ----- 9A (0x4da780) and AF 01 (0x4daa90), with sprintf.

// They close everything and show the notice. "% s" with the name comes out with the name; the "% d" of 853 takes the
// pointer to the name and prints its address (Q-C1321); 867 with 6.
TEST_F(EventPacketsTest, TheCastleEntryResultsShowTheirNotice)
{
    const std::string bloodAddress =
        std::to_string(Client::Crt::TextTableAddress + BloodCastleNameText * Client::Crt::TextRecordBytes);
    const std::string chaosAddress =
        std::to_string(Client::Crt::TextTableAddress + ChaosCastleNameText * Client::Crt::TextRecordBytes);
    struct Case
    {
        bool chaos;
        std::uint8_t result;
        std::string text;
    };
    const std::vector<Case> cases{
        {false, 1, "otra capa"},
        {false, 2, "se acaboBlood Castle"},
        {false, 3, "muy fuerte"},
        {false, 4, "muy debil"},
        {false, 5, "lleno y " + bloodAddress},
        {false, 6, "no mas que 6"},
        {false, 8, "se acaboChaos Castle"},
        {false, 9, "lleno y " + chaosAddress},
        {true, 1, "otra capa"},
        {true, 2, "se acaboChaos Castle"},
        {true, 3, "muy fuerte"},
        {true, 4, "muy debil"},
        {true, 5, "lleno y " + chaosAddress},
        {true, 6, "no mas que 6"},
        {true, 7, "aviso cc siete"},
    };
    for (const std::int32_t map : {0, 2, 11, 18})
    {
        Runtime().map = map;
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(testing::Message() << map << (probe.chaos ? " AF " : " 9A ") << int{probe.result});
            ResetTheBox();
            const std::uint32_t closed = AfterClosingAll();
            if (probe.chaos)
                Applied(Events::ChaosCastleEntryResult{probe.result});
            else
                Applied(Events::BloodCastleEntryResult{probe.result});
            EXPECT_EQ(random.State(), closed);
            ExpectMessageBox(probe.text);
        }
    }
    ResetTheBox();
    Applied(Events::BloodCastleEntryResult{7});
    Applied(Events::ChaosCastleEntryResult{8});
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// ----- 9B (0x4e4999).

// Blood Castle (0x40c140), in maps 11 to 17: 0 turns the players toward the gate (0x5111a0(0xc0)), requests music 0x8b
// and falls through to 1: +0x10 = state + 1, +0x14 = (900, pkt[4], pkt[6], pkt[8]) and the owner's weapon (without the
// key's high bit) in +0x360, with the others' at 0. 2 goes back to 0x40ba20 and turns off the music; 3 requests the
// collapse (map, 0x24, 0x14, 1).
TEST_F(EventPacketsTest, TheBloodCastlePlayStateTurnsThePlayersAndGivesTheWeapon)
{
    for (const std::int32_t map : BloodCastleMaps)
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
        Client::Kernel::ActorState& other = world.Actors()[7];
        other.alive = true;
        other.type = Client::Tables::PlayerSkeletonModel;
        other.key = 0x0456;
        other.byte360 = 3;
        Client::Kernel::ActorState& monster = world.Actors()[8];
        monster.alive = true;
        monster.type = 0x20;
        Hero().angles = {};
        Hero().animation.action = 1;

        const EventPacketResult started = Applied(Events::EventPlayState{
            .state = 0, .remaining = 300, .maxKill = 40, .currentKill = 2, .weaponOwner = 0x8123, .weapon = 2});
        EXPECT_EQ(Hero().angles.z, 180.0f);
        EXPECT_EQ(Hero().animation.action, 0xc0);
        EXPECT_EQ(other.animation.action, 0xc0);
        EXPECT_EQ(monster.angles.z, 0.0f);
        EXPECT_EQ(started.sounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x8b}});
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x8b});
        const EventMapObject& event = *Runtime().eventObject;
        EXPECT_EQ(event.field10, 1);
        EXPECT_EQ(event.field14, (std::array<std::int32_t, 4>{900, 300, 40, 2}));
        EXPECT_EQ(Hero().byte360, 2);
        EXPECT_EQ(other.byte360, 0);

        Applied(Events::EventPlayState{.state = 4, .weaponOwner = 0xffff, .weapon = 1});
        EXPECT_EQ(Runtime().eventObject->field10, 5);
        EXPECT_EQ(Hero().byte360, 2);

        const EventPacketResult reset = Applied(Events::EventPlayState{.state = 2});
        EXPECT_EQ(Runtime().eventObject->field10, 0);
        EXPECT_EQ(Runtime().eventObject->field14, (std::array<std::int32_t, 4>{-1, -1, -1, -1}));
        EXPECT_EQ(reset.stoppedSounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x8b}});

        Applied(Events::EventPlayState{.state = 3});
        EXPECT_EQ(Runtime().events.objectCollapse.map, map);
        EXPECT_EQ(Runtime().events.objectCollapse.type, 0x24);
        EXPECT_EQ(Runtime().events.objectCollapse.counter, 0x14);
        Applied(Events::EventPlayState{.state = 5});
        EXPECT_EQ(Runtime().eventObject->field10, 0);
    }
}

// Q-C1326: 0x511150 with nobody holding the key writes the weapon into 400 (outside those it walked).
TEST_F(EventPacketsTest, TheWeaponWithoutItsOwnerGoesToTheFourHundredth)
{
    Runtime().map = 11;
    Runtime().eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
    Hero().byte360 = 1;
    Applied(Events::EventPlayState{.state = 1, .weaponOwner = 0x0777, .weapon = 3});
    EXPECT_EQ(Hero().byte360, 0);
    EXPECT_EQ(world.Actors()[400].byte360, 3);
}

// Without Player.bmd, the turn of 0x5111a0 stops.
TEST_F(EventPacketsTest, TheGateTurnNeedsThePlayerSkeleton)
{
    Runtime().map = 11;
    Runtime().eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
    auto context = Context();
    context.playerSkeleton = nullptr;
    const auto applied = Client::Events::ApplyEventPacket(Events::EventPlayState{.state = 0}, context);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x40c16a"), std::string::npos) << applied.Error();
}

// Chaos Castle (0x404e20), in maps 18 to 23: 5 arms the trap, the area at 0xff, closes the chat, turns the players and
// changes the music (0x9e for 0x99); 6, +0x10 = 7 without a weapon; 7 goes back and, in 18..23, 0x9e and 0x9f. 8, 9
// and 10 with the area at 0, 3 and 6: the next one, the collapse (map, 1, 0x28, 1), four rectangles with 8 and the
// 0x61; with another area, nothing.
TEST_F(EventPacketsTest, TheChaosCastlePlayStateCollapsesItsRings)
{
    for (const std::int32_t map : ChaosCastleMaps)
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::ChaosCastle};
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Runtime().chaosCastleTrapArmed = 0;
        Runtime().chaosCastleArea = 0;
        Runtime().ui.chatOpen = true;
        Hero().byte360 = 0;

        const EventPacketResult started = Applied(Events::EventPlayState{.state = 5});
        EXPECT_EQ(Runtime().chaosCastleTrapArmed, 1);
        EXPECT_EQ(Runtime().chaosCastleArea, 0xff);
        EXPECT_FALSE(Runtime().ui.chatOpen);
        EXPECT_EQ(Hero().animation.action, 0xc0);
        EXPECT_EQ(started.stoppedSounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x9e}});
        EXPECT_EQ(started.sounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x99}});

        Applied(Events::EventPlayState{.state = 6, .remaining = 50, .weaponOwner = 0x0123, .weapon = 2});
        EXPECT_EQ(Runtime().eventObject->field10, 7);
        EXPECT_EQ(Runtime().eventObject->field14[1], 50);
        EXPECT_EQ(Hero().byte360, 0);

        Applied(Events::EventPlayState{.state = 8});
        EXPECT_EQ(Runtime().events.objectCollapse.map, -1);
        const std::array<std::uint8_t, 3> areas{0, 3, 6};
        for (std::uint8_t step = 0; step < 3; ++step)
        {
            Runtime().chaosCastleArea = areas[step];
            const EventPacketResult collapsed =
                Applied(Events::EventPlayState{.state = static_cast<std::uint8_t>(8 + step)});
            EXPECT_EQ(Runtime().chaosCastleArea, areas[step] + 1);
            EXPECT_EQ(Runtime().events.objectCollapse.map, map);
            EXPECT_EQ(Runtime().events.objectCollapse.type, 1);
            EXPECT_EQ(Runtime().events.objectCollapse.counter, 0x28);
            EXPECT_EQ(collapsed.sounds, (std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x61}}));
        }
        // The first ring: (0x17, 0x4b) of 0x16 × 2, (0x2b, 0x4d) of 2 × 0x20, ...
        EXPECT_EQ(Cell(0x17, 0x4b), 8);
        EXPECT_EQ(Cell(0x2c, 0x4c), 8);
        EXPECT_EQ(Cell(0x2d, 0x4c), 0);
        EXPECT_EQ(Cell(0x2c, 0x6c), 8);
        EXPECT_EQ(Cell(0x2c, 0x6d), 0);
        // The third: (0x1b, 0x4f) of 0xe × 2.
        EXPECT_EQ(Cell(0x28, 0x50), 8);

        const EventPacketResult ended = Applied(Events::EventPlayState{.state = 7});
        EXPECT_EQ(Runtime().eventObject->field10, 0);
        EXPECT_EQ(ended.stoppedSounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x99}});
        EXPECT_EQ(ended.sounds,
                  (std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x9e}, Client::Audio::SoundId{0x9f}}));
    }
}

// Devil Square (0x40bdc0) does nothing with 9B; without the object (Lorencia, Devias, Kalima, Crywolf), neither.
TEST_F(EventPacketsTest, ThePlayStateOutsideTheCastlesDoesNothing)
{
    for (const std::int32_t map : DevilSquareMaps)
    {
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::DevilSquare};
        const EventPacketResult result =
            Applied(Events::EventPlayState{.state = 0, .weaponOwner = 0x0123, .weapon = 1});
        EXPECT_TRUE(result.sounds.empty());
        EXPECT_EQ(Runtime().eventObject->field10, 0);
    }
    for (const std::int32_t map : OutsideMaps)
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().eventObject.reset();
        for (std::uint8_t state = 0; state <= 10; ++state)
        {
            const EventPacketResult result = Applied(Events::EventPlayState{.state = state});
            EXPECT_TRUE(result.sounds.empty());
        }
        EXPECT_EQ(Hero().animation.action, 0);
        EXPECT_EQ(Runtime().events.objectCollapse.map, -1);
        EXPECT_EQ(Runtime().chaosCastleTrapArmed, 1);
    }
}

// ----- 9D and 9E.

// 0x4e4a86: notice 886 + n (from 0 to 4) or 899 (5), and the 0x40 bytes always.
TEST_F(EventPacketsTest, TheSerialResultShowsItsNoticeAndKeepsTheData)
{
    Events::SerialResult serial{.result = 4, .data = {}};
    serial.data[0x3f] = std::byte{0x77};
    Applied(serial);
    ExpectMessageBox("serie cuatro");
    EXPECT_EQ(Runtime().events.serial[0x3f], std::byte{0x77});
    ResetTheBox();
    serial.result = 5;
    Applied(serial);
    ExpectMessageBox("serie ya");
    ResetTheBox();
    serial.result = 6;
    serial.data[0] = std::byte{0x11};
    Applied(serial);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().events.serial[0], std::byte{0x11});
}

// 0x4e4ad4: 0x5b, 0x5c and 0x5d with 0, 1 and 2.
TEST_F(EventPacketsTest, TheEventSoundPlaysItsSound)
{
    EXPECT_EQ(Applied(Events::EventSound{0}).sounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x5b}});
    EXPECT_EQ(Applied(Events::EventSound{2}).sounds, std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x5d}});
    EXPECT_TRUE(Applied(Events::EventSound{3}).sounds.empty());
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x5d});
}

// ----- BC 00 and BC 01.

// The message "%s%s %s" (mix or unmix, 1816 and 868 as they are) or the notice, and then the mix window (0x402e50,
// ChaosMix/JewelMix.h, milestone 6) goes back to the choice: with mode 0, the 0xa2, and with 1, the 0xa4. 1 opens
// dialogs 0 and 0xa6. The whole tables, in test_jewel_mix_answers.
TEST_F(EventPacketsTest, TheJewelResultsSayTheirMessageAndReturnToTheChoice)
{
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::MixSubcode, .result = 0});
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "mezclapara permitida% d veces");
    EXPECT_EQ(Runtime().dialogs.current, 0xa2);
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::MixSubcode, .result = 4});
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "faltan");
    Runtime().npcWindows.jewels.mode = 1;
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::UnmixSubcode, .result = 5});
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "deshacepara permitida% d veces");
    EXPECT_EQ(Runtime().dialogs.current, 0xa4);
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::UnmixSubcode, .result = 7});
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "sin lugar");
    EXPECT_TRUE(worldPackets.empty());

    Runtime().dialogs = {};
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::UnmixSubcode, .result = 1});
    EXPECT_EQ(Runtime().dialogs.current, 0xa6);
    // 0x6236f0(0) moves to the waiting one and the 0xa6 stays waiting behind.
    Runtime().dialogs = {.current = 0, .pending = 0x55};
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::MixSubcode, .result = 1});
    EXPECT_EQ(Runtime().dialogs.current, 0x55);
    EXPECT_EQ(Runtime().dialogs.pending, 0xa6);
    const std::size_t entries = Runtime().chat.log.entries.size();
    Applied(Events::JewelMixResult{.subcode = Client::Protocol::JewelMix::MixSubcode, .result = 9});
    EXPECT_EQ(Runtime().chat.log.entries.size(), entries);
}

// ----- BD 00 (0x42fe80), in Crywolf (34) and against the ASM only.

// The same stage and the same state, nothing. 4 sets 0x723bec. 5 sets 0x6c73e4..0x6c73f4 to −1 and, except with state
// 2, the notice (with 0, 0x6c73dc at 0xb). 6 is 0x42fdc0. Without a state change it does not stop, and the stage stays
// in the camera tour (0x4033b0).
TEST_F(EventPacketsTest, TheCrywolfStageUpdatesTheInterface)
{
    Runtime().map = 34;
    Client::Kernel::CrywolfInterface& ui = Runtime().crywolf.ui;
    Applied(Events::CrywolfState{.state = 0, .stage = 0});
    EXPECT_EQ(Runtime().events.cameraTour.stage, 0);

    Applied(Events::CrywolfState{.state = 0, .stage = 4});
    EXPECT_EQ(Runtime().crywolf.stage, 4);
    EXPECT_EQ(ui.byte723bec, 1);

    ui.dwords6c73e0 = {5, 5, 5, 5, 5, 5};
    ui.byte723a3d = 1;
    ui.dword6c73f8 = 0;
    Applied(Events::CrywolfState{.state = 0, .stage = 5});
    EXPECT_EQ(ui.dwords6c73e0, (std::array<std::int32_t, 6>{5, -1, -1, -1, -1, -1}));
    EXPECT_EQ(ui.byte723a3d, 0);
    EXPECT_EQ(ui.message, 1);
    EXPECT_EQ(ui.dword6c73dc, 0xb);
    EXPECT_EQ(ui.dword6c73f8, 0x186);
    EXPECT_EQ(Runtime().events.cameraTour.stage, 5);

    Runtime().selectedTargets[1] = 9;
    Applied(Events::CrywolfState{.state = 0, .stage = 6});
    EXPECT_EQ(Runtime().crywolf.stage, 0);
    EXPECT_EQ(ui.message, -1);
    EXPECT_EQ(ui.byte723bec, 0);
    EXPECT_EQ(ui.dword6c73dc, 0x0a);
    EXPECT_EQ(Runtime().selectedTargets[1], -1);
    EXPECT_EQ(Runtime().events.cameraTour.stage, 6);
}

// With another state, 0x42ff81 and 0x42ffe8 reload the light and the .att from the map's folder + 1, after the stage
// part: in Crywolf, TerrainLight1/2 and EncTerrain351/352; with 0 again, the usual ones. With state 2, stage 5 does not
// set the notice; above 2, the light buffer unwritten (Q-C1328).
TEST_F(EventPacketsTest, TheCrywolfStateReloadsTheLightAndTheAttributes)
{
    Runtime().map = 34;
    LoadTheTerrain(34);
    const auto plain = AttributesOf("world35/EncTerrain35.att");
    ASSERT_EQ(terrain.attributes->cells, plain->cells);
    Client::Kernel::CrywolfInterface& ui = Runtime().crywolf.ui;
    ui.message = -1;
    const auto first = Shaded("world35/TerrainLight1.OZJ");
    Applied(Events::CrywolfState{.state = 1, .stage = 1});
    EXPECT_EQ(Runtime().crywolf.state, 1);
    EXPECT_EQ(terrain.lighting->cells, first->cells);
    EXPECT_EQ(terrain.attributes->cells, AttributesOf("world35/EncTerrain351.att")->cells);
    EXPECT_EQ(Runtime().events.cameraTour.stage, 1);

    Applied(Events::CrywolfState{.state = 2, .stage = 5});
    EXPECT_EQ(Runtime().crywolf.state, 2);
    EXPECT_EQ(Runtime().crywolf.stage, 5);
    EXPECT_EQ(ui.message, -1);
    EXPECT_EQ(terrain.lighting->cells, Shaded("world35/TerrainLight2.OZJ")->cells);
    EXPECT_EQ(terrain.attributes->cells, AttributesOf("world35/EncTerrain352.att")->cells);

    Applied(Events::CrywolfState{.state = 0, .stage = 5});
    EXPECT_EQ(terrain.lighting->cells, Shaded("world35/TerrainLight.OZJ")->cells);
    EXPECT_EQ(terrain.attributes->cells, plain->cells);
    EXPECT_FALSE(Runtime().fatalDataMessage.has_value());

    // Q-C1328: with the state already written, the load and 0x4033b0 are dropped; the session goes on.
    const std::string broken = Discarded(Events::CrywolfState{.state = 3, .stage = 2});
    EXPECT_NE(broken.find("Q-C1328"), std::string::npos) << broken;
    EXPECT_EQ(Runtime().crywolf.state, 3);
    EXPECT_EQ(Runtime().crywolf.stage, 2);
    EXPECT_EQ(terrain.lighting->cells, Shaded("world35/TerrainLight.OZJ")->cells);
    EXPECT_EQ(terrain.attributes->cells, plain->cells);
    EXPECT_EQ(Runtime().events.cameraTour.stage, 5);
}

// In Lorencia and Devias BD 00 also reloads, from World1 and World3 (Q-C1340): TerrainLight1 is missing and leaves the
// fatal notice; .att 11 or 31 is missing too. What was loaded stays as it was. With 0 again it reloads its own.
TEST_F(EventPacketsTest, TheCrywolfStateReloadsTheCurrentMapToo)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().crywolf.state = 0;
        Runtime().fatalDataMessage.reset();
        LoadTheTerrain(static_cast<std::uint8_t>(map));
        // The copies, on the heap: the light takes 768 KB and the Windows stack is 1 MB.
        const auto lighting = std::make_unique<Client::Terrain::TerrainLighting>(*terrain.lighting);
        const auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>(*terrain.attributes);
        Applied(Events::CrywolfState{.state = 1, .stage = 0});
        const std::string folder = map == 0 ? "World1" : "World3";
        ASSERT_TRUE(Runtime().fatalDataMessage.has_value());
        EXPECT_EQ(*Runtime().fatalDataMessage, "Data\\" + folder + "\\TerrainLight1.OZJ - File not exist.");
        EXPECT_EQ(terrain.lighting->cells, lighting->cells);
        EXPECT_EQ(terrain.attributes->cells, attributes->cells);

        terrain.attributes->cells[0] = 0x55;
        Applied(Events::CrywolfState{.state = 0, .stage = 0});
        EXPECT_EQ(terrain.attributes->cells, attributes->cells);
    }
    Runtime().crywolf.state = 0;
    dataRoot.clear();
    const std::string cut = Cut(Events::CrywolfState{.state = 1, .stage = 0});
    EXPECT_NE(cut.find("0x42ff81"), std::string::npos) << cut;
}

// 0x42fdc0 (also the return to the selection): the interface globals to their initial value.
TEST_F(EventPacketsTest, TheCrywolfInterfaceResetMatchesTheData)
{
    Client::Kernel::CrywolfInterface& ui = Runtime().crywolf.ui;
    ui.dwords6c73e0 = {1, 2, 3, 4, 5, 6};
    ui.dword6c73d4 = 0;
    ui.light = 3;
    Runtime().crywolf.state = 2;
    Runtime().crywolf.stage = 3;
    Client::Events::ResetTheCrywolfInterface(Runtime());
    EXPECT_EQ(ui.dwords6c73e0, (std::array<std::int32_t, 6>{-1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(ui.dword6c73d4, 0x64);
    EXPECT_EQ(ui.light, 0);
    EXPECT_EQ(Runtime().crywolf.state, 0);
    EXPECT_EQ(Runtime().crywolf.stage, 0);
}

// ----- BD 02 to 0C: the Crywolf interface (GS 0.97 does not send them; the oracle is the client's ASM).

// BD 02, 07, 08 and 09 write in any map and stage: the light and the altars (0x430000), the rank and the experience
// (0x437600), the results with their window (0x437620) and 0x7d2dc9d. A place of 5 or more would write outside the
// tables (Q-C1341): it is dropped from that record on, and the earlier ones stay.
TEST_F(EventPacketsTest, TheCrywolfInterfacePacketsWriteOnAnyMap)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().crywolf = {};
        Client::Kernel::CrywolfInterface& ui = Runtime().crywolf.ui;
        EXPECT_EQ(ui.altars, (std::array<std::uint8_t, 5>{2, 2, 2, 2, 2}));
        Applied(Events::CrywolfAltars{.light = 0x60, .altars = {0x11, 0x12, 1, 0x13, 0}});
        EXPECT_EQ(ui.light, 0x60);
        EXPECT_EQ(ui.altars, (std::array<std::uint8_t, 5>{0x11, 0x12, 1, 0x13, 0}));
        Applied(Events::CrywolfRank{.rank = 3, .experience = 4567});
        EXPECT_EQ(ui.byte723bed, 3);
        EXPECT_EQ(ui.dword723bf0, 4567);
        Events::CrywolfRanking ranking;
        ranking.entries.push_back(
            {.place = 2, .name = {'L', 'o', 'b', 'o', 'G', 'r', 'i', 's', '0', '1'}, .score = 90, .classByte = 0x08});
        Applied(ranking);
        EXPECT_EQ(ui.byte723bf9, 1);
        EXPECT_EQ(ui.dwords6c73e0, (std::array<std::int32_t, 6>{-1, -1, -1, 90, -1, -1}));
        EXPECT_EQ(ui.rankingClasses[2], 0x08);
        EXPECT_EQ(std::string(ui.rankingNames[2].data()), "LoboGris01");
        Applied(Events::CrywolfChaosBox{.value = 7});
        EXPECT_EQ(ui.byte7d2dc9d, 7);
        ranking.entries.front().place = 0;
        ranking.entries.front().score = 70;
        ranking.entries.push_back({.place = 5, .name = {'Z'}, .score = 60, .classByte = 0x10});
        ranking.entries.push_back({.place = 1, .name = {'Y'}, .score = 50, .classByte = 0x20});
        ui.byte723bf9 = 0;
        const std::string discarded = Discarded(ranking);
        EXPECT_NE(discarded.find("0x437632"), std::string::npos) << discarded;
        EXPECT_EQ(ui.byte723bf9, 1);
        EXPECT_EQ(ui.dwords6c73e0, (std::array<std::int32_t, 6>{-1, 70, -1, 90, -1, -1}));
        EXPECT_EQ(ui.rankingClasses[1], 0);
    }
}

// BD 04 only with stage 4 in 34 (0x4359f0 and 0x4303c0): pkt[4], the minutes, GetTickCount and 0x723bf8. BD 05 with
// stage 3 or 4 in 34: 0x723bd8, the bar on with the dword above 0 and off with 0 or less, and 0x6c73d4.
TEST_F(EventPacketsTest, TheCrywolfTimerAndBossNeedTheDefenseInCrywolf)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        for (const std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
        {
            SCOPED_TRACE(static_cast<int>(stage));
            Runtime().map = map;
            Runtime().crywolf = {};
            Runtime().crywolf.stage = stage;
            Client::Kernel::CrywolfInterface& ui = Runtime().crywolf.ui;
            tickCount = 0x12345;
            Applied(Events::CrywolfTimer{.value4 = 1, .minutes = 15});
            const bool timer = map == 34 && stage == 4;
            EXPECT_EQ(ui.dwords723bdc[2], timer ? 1 : 0);
            EXPECT_EQ(ui.dwords723bdc[3], timer ? 15 : 0);
            EXPECT_EQ(ui.dword7239f4, timer ? 0x12345 : 0);
            EXPECT_EQ(ui.byte723bf8, timer ? 1 : 0);
            Applied(Events::CrywolfBoss{.life = 80, .count = 4});
            const bool boss = map == 34 && stage >= 3;
            EXPECT_EQ(ui.dword723bd8, boss ? 4 : 0);
            EXPECT_EQ(ui.byte723a3d, boss ? 1 : 0);
            EXPECT_EQ(ui.dword6c73d4, boss ? 80 : 0x64);
            Applied(Events::CrywolfBoss{.life = 0, .count = 0});
            EXPECT_EQ(ui.byte723a3d, 0);
            EXPECT_EQ(ui.dword6c73d4, boss ? 0 : 0x64);
        }
    }
}

// BD 03 (0x4e1010) with 0: the hero stands up and, with the own record open (0xf08 rand() when closing it), under 350
// gives 0x36 and 0x37 and 350 or more 0x3a, which only show with the defense in 34. With a result other than 0 and 1,
// nothing.
TEST_F(EventPacketsTest, TheFailedCrywolfContractStandsTheHeroUp)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        for (const std::uint16_t level : {std::uint16_t{349}, std::uint16_t{350}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(level);
            Runtime() = WorldRuntime{};
            Runtime().hero = 0;
            Runtime().map = map;
            Runtime().crywolf.stage = 3;
            world.OwnCharacter().level = level;
            Hero().flags7c = 0x200001;
            const CrtRandom before = random;
            Applied(Events::CrywolfContract{.result = 0, .value = 0x11, .key = 0x13c});
            CrtRandom expected = before;
            expected.Discard(Client::Models::OwnRecordCloseRandoms);
            EXPECT_EQ(random.State(), expected.State());
            EXPECT_EQ(Runtime().ownRecordDepth, 0);
            EXPECT_EQ(Hero().flags7c, 1u);
            const auto& ui = Runtime().crywolf.ui;
            const bool shown = map == 34;
            EXPECT_EQ(ui.byte723a44, shown ? 2 : 0);
            EXPECT_EQ(std::string(ui.contractLines[0].data()),
                      !shown ? "" : (level < 350 ? "contrato fallido" : "nivel alto"));
            EXPECT_EQ(std::string(ui.contractLines[1].data()), shown && level < 350 ? "otra vez" : "");
            EXPECT_EQ(ui.altars, (std::array<std::uint8_t, 5>{2, 2, 2, 2, 2}));
            EXPECT_TRUE(worldPackets.empty());
        }
    }
    const CrtRandom before = random;
    Hero().flags7c = 0x200000;
    Applied(Events::CrywolfContract{.result = 2, .value = 0x11, .key = 0x13c});
    EXPECT_EQ(random.State(), before.State());
    EXPECT_EQ(Hero().flags7c, 0x200000u);
    EXPECT_TRUE(worldPackets.empty());
}

// BD 03 with 1: 0x435b00(3) and (4), the altar of the key − 0x13c (0x435ad0) only with the defense in 34, and in any
// map the hero seated (0x200000 in +0x7c and action 0xca) and C1 05 18 with the heading of +0x24 and 0x82, with
// XOR32. No rand(). With the key outside the five altars, in 34 (Q-C1347) the rest is dropped after the two lines:
// neither the altar nor the seated hero nor the 18; outside 34 it goes on whole.
TEST_F(EventPacketsTest, TheSignedCrywolfContractSitsTheHero)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().hero = 0;
        Runtime().map = map;
        Runtime().crywolf.stage = 4;
        Hero().flags7c = 1;
        Hero().angles.z = 90.0f;
        Hero().animation = {};
        worldPackets.clear();
        const CrtRandom before = random;
        Applied(Events::CrywolfContract{.result = 1, .value = 0x13, .key = 0x140});
        EXPECT_EQ(random.State(), before.State());
        const auto& ui = Runtime().crywolf.ui;
        const bool shown = map == 34;
        EXPECT_EQ(ui.byte723a44, shown ? 2 : 0);
        EXPECT_EQ(std::string(ui.contractLines[0].data()), shown ? "contrato firmado" : "");
        EXPECT_EQ(std::string(ui.contractLines[1].data()), shown ? "altar tomado" : "");
        EXPECT_EQ(ui.altars[4], shown ? 0x13 : 0x02);
        EXPECT_EQ(Hero().flags7c, 0x200001u);
        EXPECT_EQ(Hero().animation.action, 0xca);
        ASSERT_EQ(worldPackets.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(worldPackets[0].packet.Bytes(),
                                             Client::Tests::Bytes({0xC1, 0x05, 0x18, 0x92, 0xAC})));
    }
    for (const std::uint16_t key : {std::uint16_t{0x141}, std::uint16_t{0x13b}})
    {
        SCOPED_TRACE(key);
        Runtime() = WorldRuntime{};
        Runtime().map = 34;
        Runtime().crywolf.stage = 3;
        Hero().flags7c = 1;
        Hero().animation = {};
        worldPackets.clear();
        const std::string discarded = Discarded(Events::CrywolfContract{.result = 1, .value = 0x13, .key = key});
        EXPECT_NE(discarded.find("0x435aeb"), std::string::npos) << discarded;
        const auto& ui = Runtime().crywolf.ui;
        EXPECT_EQ(std::string(ui.contractLines[0].data()), "contrato firmado");
        EXPECT_EQ(std::string(ui.contractLines[1].data()), "altar tomado");
        EXPECT_EQ(ui.altars, (std::array<std::uint8_t, 5>{2, 2, 2, 2, 2}));
        EXPECT_EQ(Hero().flags7c, 1u);
        EXPECT_EQ(Hero().animation.action, 0);
        EXPECT_TRUE(worldPackets.empty());
    }
    Runtime().map = 0;
    worldPackets.clear();
    const EventPacketResult outside = Applied(Events::CrywolfContract{.result = 1, .value = 0x13, .key = 0x141});
    EXPECT_FALSE(outside.discarded.has_value());
    EXPECT_EQ(worldPackets.size(), 1u);
}

// BD 03 with 1: 0x4ea030 reads the actions of the model of the hero's type. Transformed into a model without the 0xca,
// the action does not change and the rest goes on; without that model loaded, it stops.
TEST_F(EventPacketsTest, TheSignedCrywolfContractUsesTheActionsOfTheHeroModel)
{
    Runtime().map = 0;
    Hero().type = EventModels::TransformedModel;
    Hero().animation = {};
    Hero().animation.action = 1;
    Hero().flags7c = 0;
    Applied(Events::CrywolfContract{.result = 1, .value = 0x13, .key = 0x140});
    EXPECT_EQ(Hero().animation.action, 1);
    EXPECT_EQ(Hero().flags7c, 0x200000u);
    EXPECT_EQ(worldPackets.size(), 1u);

    Hero().type = EventModels::MissingModel;
    worldPackets.clear();
    const std::string cut = Cut(Events::CrywolfContract{.result = 1, .value = 0x13, .key = 0x140});
    EXPECT_NE(cut.find("0x4e11d9"), std::string::npos) << cut;
    EXPECT_TRUE(worldPackets.empty());
}

// 0x4e15f0 hands pkt6..9 to 0x430040; a truncated packet does not have those fields.
TEST_F(EventPacketsTest, TheTruncatedCrywolfEffectIsDiscardedBeforeReadingItsDirection)
{
    const std::string effects = Discarded(Events::CrywolfUnported{.subcode = 0xc, .bytes = {}});
    EXPECT_NE(effects.find("0x4e15f0"), std::string::npos) << effects;
}

// ----- F3 22, F3 23 and F3 40.

TEST_F(EventPacketsTest, TheEventTimeIsKept)
{
    Applied(Events::EventTime{.value4 = 2, .value6 = 0x0e10});
    EXPECT_EQ(Runtime().events.timeKind, 2);
    EXPECT_EQ(Runtime().events.timeValue, 0x0e10);
}

// 0x4d9f00: the two names with their '\0', the points and the active match with pkt[0xc] other than 0xff; then the
// effect 0x60e (CL11) stops.
TEST_F(EventPacketsTest, TheSoccerScoreSharesTheWarPointsAndEmitsOnTheRawHeroCell)
{
    const Events::SoccerScore score{.firstName = {'A', 'z', 'u', 'l', 'e', 's', 'x', 'y'},
                                    .firstScore = 2,
                                    .secondName = {'R', 'o', 'j', 'o', 0, 0, 0, 0},
                                    .secondScore = 1};
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Hero().cellXLeft = 12;
    Hero().cellYLeft = 34;
    Hero().cellX = 99;
    Hero().cellY = 88;
    const auto before = random.State();
    Applied(score);
    const auto& effect = world.Map().effects.Effects()[0];
    EXPECT_EQ(effect.type, 0x60e);
    EXPECT_EQ(effect.transform.position, (glm::vec3{1250, 3450, 100}));
    EXPECT_EQ(effect.life, 75);
    EXPECT_EQ(effect.light, glm::vec3(1));
    EXPECT_EQ(random.State(), before);
    const auto& soccer = Runtime().events.soccer;
    EXPECT_EQ(std::string(soccer.firstName.data()), "Azulesxy");
    EXPECT_EQ(std::string(soccer.secondName.data()), "Rojo");
    EXPECT_TRUE(soccer.active);
    EXPECT_EQ(Runtime().guild.ourScore, 2);
    EXPECT_EQ(Runtime().guild.theirScore, 1);
    Events::SoccerScore none = score;
    none.firstScore = 0xff;
    Applied(none);
    EXPECT_FALSE(soccer.active);
}

TEST_F(EventPacketsTest, TheGuildOutcomePublishesOnlyTheNativeThreeKinds)
{
    const Events::GuildWarOutcome outcome{.present = 2,
                                          .firstName = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'},
                                          .secondName = {'O', 'm', 'b', 'u'},
                                          .firstScore = 0x1234,
                                          .secondScore = 0x5678};
    const auto before = random.State();
    Applied(outcome);
    const auto& kept = Runtime().events.guildWarOutcome;
    EXPECT_EQ(kept.present, 2);
    EXPECT_EQ(kept.firstName, outcome.firstName);
    EXPECT_EQ(kept.secondName, outcome.secondName);
    EXPECT_EQ(kept.firstScore, 0x1234);
    EXPECT_EQ(kept.secondScore, 0x5678);
    for (const auto kind : {std::uint8_t{3}, std::uint8_t{0xff}})
        Applied(Events::GuildWarOutcome{.present = kind});
    EXPECT_EQ(kept.firstName, outcome.firstName);
    EXPECT_EQ(random.State(), before);
}

// 0x4da070: the notices by command and sound 0x5a; the effect (CL11) stops.
TEST_F(EventPacketsTest, TheServerCommandsShowTheirNotices)
{
    struct Case
    {
        std::uint8_t command;
        std::uint8_t value5;
        std::string_view text;
    };
    const std::array<Case, 8> cases{{
        {0x2f, 0, "comando 2f"},
        {0x31, 0, "comando 31"},
        {1, 5, "aviso bajo"},
        {1, 0x14, "aviso alto"},
        {3, 3, "mensaje tres"},
        {6, 0, "aviso fijo"},
        {7, 1, "castillo uno"},
        {0xd, 0, "aviso gremio"},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(int{probe.command});
        ResetTheBox();
        Applied(Events::ServerCommand{.command = probe.command, .value5 = probe.value5, .value6 = 0});
        ExpectMessageBox(probe.text);
    }
    ResetTheBox();
    Applied(Events::ServerCommand{.command = 7, .value5 = 0, .value6 = 0});
    EXPECT_EQ(Runtime().dialogs.current, 0xaa);
    EXPECT_EQ(Applied(Events::ServerCommand{.command = 2}).sounds,
              std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x5a}});
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Applied(Events::ServerCommand{.command = 0, .value5 = 4, .value6 = 7});
    EXPECT_EQ(world.Map().effects.Effects()[0].transform.position, (glm::vec3{450, 750, 100}));
    ResetTheBox();
    Applied(Events::ServerCommand{.command = 4});
    Applied(Events::ServerCommand{.command = 0x32});
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x4da18f..0x4da195: command 5 is 0x632f70(pkt[5]), the Dialog.bmd dialog with its answers in the 0x8d (or waiting,
// with another box open). From 200 on the original reads outside the 200 of 0x7688ac0 (Q-C1325): it is dropped.
// Without Dialog.bmd it stops, like 0x632f70 from the answers.
TEST_F(EventPacketsTest, TheServerCommandFiveOpensTheAnswersDialog)
{
    Client::Tables::DialogTable table(Client::Tables::DialogCount);
    std::ranges::copy(std::string_view("un dialogo inventado"), table[0xc7].text.begin());
    table[0xc7].answerCount = 1;
    std::ranges::copy(std::string_view("seguir"), table[0xc7].answers[0].begin());
    std::ranges::copy(std::string_view("sin respuestas"), table[3].text.begin());
    texts = Client::Tests::MakeTextTable({{Client::Ui::NoAnswersText, "nada mas"}});
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().dialogTable = table;
        for (const std::uint8_t index : {std::uint8_t{0xc7}, std::uint8_t{3}})
        {
            SCOPED_TRACE(int{index});
            ResetTheBox();
            Client::Ui::DialogBoxState expected;
            Client::Ui::DialogState expectedDialogs;
            ASSERT_TRUE(Client::Ui::OpenTheAnswersDialog(expected, expectedDialogs, &table, &texts, index).HasValue());
            const EventPacketResult result = Applied(Events::ServerCommand{.command = 5, .value5 = index});
            EXPECT_FALSE(result.discarded.has_value());
            EXPECT_EQ(Runtime().dialogs.current, Client::Ui::AnswersDialog);
            EXPECT_EQ(Runtime().dialogBox.dialogIndex, index);
            EXPECT_EQ(Runtime().dialogBox.lineCount, expected.lineCount);
            EXPECT_EQ(Runtime().dialogBox.answerCount, expected.answerCount);
            EXPECT_EQ(Runtime().dialogBox.rows, expected.rows);
            EXPECT_EQ(Runtime().dialogBox.answers, expected.answers);
        }
        ResetTheBox();
        Runtime().dialogs.current = Client::Ui::MessageBoxDialog;
        Applied(Events::ServerCommand{.command = 5, .value5 = 3});
        EXPECT_EQ(Runtime().dialogs.current, Client::Ui::MessageBoxDialog);
        EXPECT_EQ(Runtime().dialogs.pending, Client::Ui::AnswersDialog);

        ResetTheBox();
        const std::string discarded = Discarded(Events::ServerCommand{.command = 5, .value5 = 200});
        EXPECT_NE(discarded.find("Q-C1325"), std::string::npos) << discarded;
        EXPECT_EQ(Runtime().dialogs.current, 0);
        EXPECT_EQ(Runtime().dialogBox.dialogIndex, -1);
    }
    Runtime().dialogTable.clear();
    EXPECT_NE(Cut(Events::ServerCommand{.command = 5, .value5 = 3}).find("Dialog.bmd"), std::string::npos);
}
} // namespace
