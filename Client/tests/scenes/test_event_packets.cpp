#include "Scenes/EventPackets.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Events.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/OriginalFormat.h"
#include "UI/PanelRegistry.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"
#include "support/Bytes.h"
#include "support/FileBytes.h"
#include "support/SameBytes.h"

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

// CL9 hito 11 (04 § Los eventos): lo que los paquetes de Devil Square, Blood Castle, Chaos Castle, Kalima y Crywolf
// hacen al llegar (Scenes::ApplyEventPacket), la cuenta de 0x40bab0 y el derrumbe de los objetos, contra el ASM de
// c32bbd5f sin ejecutar el original. Text.bmd es inventado, con los especificadores de los textos del original.
namespace
{
namespace Events = Client::Protocol::Events;
using Client::Render::Models::CrtRandom;
using Client::Scenes::EventMapKind;
using Client::Scenes::EventMapObject;
using Client::Scenes::EventPacketContext;
using Client::Scenes::EventPacketResult;
using Client::Scenes::WorldRuntime;
using Client::Scenes::WorldState;

constexpr std::size_t BloodCastleNameText = 1146;
constexpr std::size_t ChaosCastleNameText = 1147;

std::map<std::size_t, std::string> EventTexts()
{
    return {
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
        {886, "serie cero"},
        {890, "serie cuatro"},
        {899, "serie ya"},
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

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// Los mapas de cada evento y los de afuera (Lorencia, Devias, Kalima y Crywolf).
constexpr std::array<std::int32_t, 7> BloodCastleMaps{11, 12, 13, 14, 15, 16, 17};
constexpr std::array<std::int32_t, 6> ChaosCastleMaps{18, 19, 20, 21, 22, 23};
constexpr std::array<std::int32_t, 2> DevilSquareMaps{9, 32};
constexpr std::array<std::int32_t, 11> OutsideMaps{0, 2, 24, 25, 26, 27, 28, 29, 36, 34, 30};

class EventPacketsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        // 0x4ea030 con la acción 0xc0 de 0x5111a0 y la 0xca del BD 03: Player.bmd las tiene.
        player.actions.resize(0xcb);
        for (auto& action : player.actions)
            action.keyCount = 4;
        Runtime().hero = 0;
        Hero().alive = true;
        Hero().type = Client::Data::Models::PlayerSkeletonModel;
        Hero().key = 0x0123;
    }

    WorldRuntime& Runtime()
    {
        return world.Runtime();
    }

    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[0];
    }

    EventPacketContext Context()
    {
        return EventPacketContext{.world = world,
                                  .random = random,
                                  .texts = &texts,
                                  .measure = &letters,
                                  .screen = {640, 480},
                                  .tickCount = tickCount,
                                  .playerSkeleton = &player,
                                  .send = [this](Client::Protocol::Outgoing packet)
                                  { worldPackets.push_back(std::move(packet)); },
                                  .dataRoot = dataRoot};
    }

    Client::Core::Expected<EventPacketResult, std::string> Apply(const Events::EventPacket& packet)
    {
        return Client::Scenes::ApplyEventPacket(packet, Context());
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

    // Lo que el original hace fuera de sus tablas: la sesión sigue y el resto del paquete se descarta, con el porqué.
    std::string Discarded(const Events::EventPacket& packet)
    {
        const EventPacketResult result = Applied(packet);
        EXPECT_TRUE(result.discarded.has_value());
        return result.discarded.value_or(std::string{});
    }

    // El aviso de 0x632e30 con `text`, como lo deja UI::OpenMessageBox en una caja vacía.
    void ExpectMessageBox(std::string_view text)
    {
        Client::UI::DialogBoxState expected;
        Client::UI::DialogState dialogs;
        Client::UI::OpenMessageBox(expected, dialogs, text);
        EXPECT_EQ(Runtime().dialogs.current, Client::UI::MessageBoxDialog);
        EXPECT_EQ(Runtime().dialogBox.lineCount, expected.lineCount);
        EXPECT_EQ(std::string(Runtime().dialogBox.rows.data()), std::string(expected.rows.data())) << text;
    }

    void ResetTheBox()
    {
        Runtime().dialogs = {};
        Runtime().dialogBox = {};
    }

    // El rand() de la CRT después de 0x657a90 (cerrar todo) sobre otro mundo como este.
    std::uint32_t AfterClosingAll()
    {
        auto other = std::make_unique<WorldState>();
        CrtRandom expected = random;
        Client::Scenes::GamePanels panels(*other, expected);
        EXPECT_TRUE(panels.Registry().CloseAll().HasValue());
        return expected.State();
    }

    std::uint16_t& Cell(std::uint8_t x, std::uint8_t y)
    {
        return world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // El terreno del mapa con la Data del repo, como lo deja la carga (0x61a560).
    void LoadTheTerrain(std::uint8_t map)
    {
        world.Map().terrain = {};
        std::optional<std::string> fatal;
        const auto layout = Client::World::LoadWorldTerrainLayout(dataRoot, map, world.Map().terrain, fatal);
        ASSERT_TRUE(layout.HasValue()) << layout.Error();
        const auto surface = Client::World::LoadWorldTerrainSurface(dataRoot, map, world.Map().terrain, fatal);
        ASSERT_TRUE(surface.HasValue()) << surface.Error();
    }

    auto AttributesOf(const std::string& relative) const
    {
        const auto bytes = Client::Tests::ReadFileBytes(dataRoot / relative);
        EXPECT_TRUE(bytes.has_value()) << relative;
        auto loaded = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*bytes)), 35);
        EXPECT_TRUE(loaded.HasValue()) << relative;
        return loaded.Value()->cells;
    }

    // La luz de `relative` sombreada con las alturas cargadas del 34.
    auto Shaded(const std::string& relative) const
    {
        const auto bytes = Client::Tests::ReadFileBytes(dataRoot / relative);
        EXPECT_TRUE(bytes.has_value()) << relative;
        const auto light = Client::Data::Terrain::LoadTerrainLight(std::as_bytes(std::span(*bytes)));
        EXPECT_TRUE(light.HasValue()) << relative;
        return Client::World::ShadeTerrainLight(*light.Value(), *world.Map().terrain.heights, 34)->cells;
    }

    // En el heap: el WorldState pasa de 340 KB (el stack de Windows es de 1 MB).
    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    std::filesystem::path dataRoot{MURET_CLIENT_REPO_DATA_DIR};
    CrtRandom random{1};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(EventTexts());
    LetterMeasure letters;
    Client::Render::Models::Skeleton player;
    std::uint32_t tickCount = 0;
    // Lo que manda el send() de mundo (el C1 05 18 del BD 03).
    std::vector<Client::Protocol::Outgoing> worldPackets;
};

// ----- 0B (0x4e3594).

// 0x4e3621..0x4e3663: el 1 prende la tormenta (1) y el 3 la fuerte (3), con pkt[3] en 0 la apagan; otro evento no la
// toca. Siempre apaga los 40 pájaros (0x4455c0). En todos los mapas igual.
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

// El tipo 1: cada celda con 0x5f8710 (OR) si pkt[5] es 0 y con 0x5f8730 (AND con el complemento) si no.
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

// El tipo 0: rectángulos de (x1, y1) a (x2, y2) con 0x5f8750 y 1 − pkt[5] en un byte: 0 agrega, 1 quita y 2 (0xff)
// también agrega (Q-C1320). En Blood Castle, el atributo 8 pide antes el derrumbe (mapa, 0x24, 0, 1).
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
    // Otro atributo no pide el derrumbe.
    Runtime().map = 12;
    Runtime().events.objectCollapse = {};
    Applied(Events::MapAttributes{.type = 0, .attribute = 4, .mode = 0, .count = 1, .areas = {{1, 1, 1, 1}}});
    EXPECT_EQ(Runtime().events.objectCollapse.map, -1);
}

// Sin el .att del mapa corta; otro tipo no hace nada.
TEST_F(EventPacketsTest, TheAttributesNeedTheMapAttributes)
{
    Applied(Events::MapAttributes{.type = 2, .attribute = 8, .mode = 0, .count = 3});
    world.Map().terrain.attributes.reset();
    const std::string cut =
        Cut(Events::MapAttributes{.type = 1, .attribute = 4, .mode = 0, .count = 1, .cells = {{1, 2}}});
    EXPECT_NE(cut.find("0x4dabd0"), std::string::npos) << cut;
}

// ----- 47 (0x4d6990).

// Con la clave del héroe, nada; con otra, el aviso lleva el nombre del ítem (0x5b1c70, CL8) y corta.
TEST_F(EventPacketsTest, TheItemAcquiredNoticeIsCutForOthers)
{
    Applied(Events::ItemAcquired{.key = 0x0123, .item = 0x1234, .level = 1});
    const std::string cut = Cut(Events::ItemAcquired{.key = 0x0456, .item = 0x1234, .level = 1});
    EXPECT_NE(cut.find("0x4d6a54"), std::string::npos) << cut;
}

// ----- 90 (0x4e4870).

// Cierra la ventana del NPC del evento (0x65cae0(0x1000)) y, con pkt[3] de 1 a 5, el aviso 677, 678, 686, 687 o 679.
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

// Con la ventana del NPC abierta, 0x65cae0(0x1000) manda el C1 03 31 (0x658a04) y cierra también el inventario; sin
// el envío de la sesión, cerrarla corta.
TEST_F(EventPacketsTest, TheDevilSquareEntryClosesTheOpenWindow)
{
    Runtime().panels.eventNpc = 1;
    Runtime().panels.inventory = 1;
    const std::string cut = Cut(Events::DevilSquareEntryResult{3});
    EXPECT_NE(cut.find("0x657ab0(0x1000)"), std::string::npos) << cut;

    int sent = 0;
    auto context = Context();
    context.sendWindowClose = [&sent]() { ++sent; };
    const auto applied = Client::Scenes::ApplyEventPacket(Events::DevilSquareEntryResult{3}, context);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(sent, 1);
    EXPECT_EQ(Runtime().panels.eventNpc, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    ExpectMessageBox("muy fuerte");
}

// ----- 91 (0x4da8c0), con wsprintfA.

// El 1: 643 con pkt[4] en 0 y si no 644 con los minutos, que "% d" de wsprintfA no imprime (P-C290). El 2: 850 con
// el nombre de Blood Castle o 851 con (minutos, nombre). El 3, nada.
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

// El 4: con la palabra pkt[4]:pkt[6] en 0, el 0x9a con 850 (nombre) y 1156 (nombre, pkt[5], 100); si no, 1164 con
// las horas y 851 con (minutos, nombre) pegados por el strcat.
TEST_F(EventPacketsTest, TheOpenTimeOfChaosCastleOpensTheChaosCastleDialog)
{
    Applied(Events::EventOpenTime{.kind = 4, .value4 = 0, .value5 = 7, .value6 = 0});
    EXPECT_EQ(Runtime().dialogs.current, Client::UI::ChaosCastleDialog);
    {
        Client::UI::DialogBoxState expected;
        Client::UI::DialogState dialogs;
        const std::array<std::string_view, 2> lines{"entra en s ahora", "Chaos Castle abre 7 de 100"};
        ASSERT_TRUE(Client::UI::OpenTheChaosCastleDialog(expected, dialogs, lines).HasValue());
        EXPECT_EQ(Runtime().dialogBox.rows, expected.rows);
        EXPECT_EQ(Runtime().dialogBox.lineCount, expected.lineCount);
    }
    ResetTheBox();
    // 1 · 256 + 10 = 266 minutos: 4 horas y 26.
    Applied(Events::EventOpenTime{.kind = 4, .value4 = 1, .value5 = 0, .value6 = 10});
    Client::UI::DialogBoxState expected;
    Client::UI::DialogState dialogs;
    const std::array<std::string_view, 1> lines{"Cuando 4entra en d minutos"};
    ASSERT_TRUE(Client::UI::OpenTheChaosCastleDialog(expected, dialogs, lines).HasValue());
    EXPECT_EQ(Runtime().dialogBox.rows, expected.rows);
    EXPECT_EQ(Runtime().dialogs.current, Client::UI::ChaosCastleDialog);
}

// ----- 92 (0x4e4930) y la cuenta (0x40bab0).

// Con el objeto del evento, +8 = pkt[3] + 1 y +0xc = GetTickCount; sin él (Lorencia, Devias, Kalima, Crywolf), nada.
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

// 0x40bab0: 639 + n con 1 a 3 y 8 a 10 (los números), 820 + n con 4 a 7 y el nombre de Blood Castle, 813 + n con 11
// a 14 y el de Chaos Castle (con el 828 en lugar del 825). Los segundos son 30 − transcurrido / 1000.
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
        // P-C290: "% s" y "% d" de wsprintfA salen como texto, sin argumentos.
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
        const auto text = Client::Scenes::EventCountdownText(event, 5000, &texts, true);
        ASSERT_TRUE(text.HasValue()) << text.Error();
        ASSERT_TRUE(text.Value().has_value());
        EXPECT_EQ(*text.Value(), probe.text);
    }
}

// Los segundos que faltan con la resta sin signo: a los 12,345 s quedan 18; a los 30 s justos, 0; pasados, nada y
// +8 en 0 si escribe. GetTickCount que dio la vuelta sigue contando bien. Con +8 fuera de 1..14, nada.
TEST_F(EventPacketsTest, TheCountdownExpiresAfterThirtySeconds)
{
    EventMapObject event{.field08 = 1, .field0c = 1000};
    EXPECT_EQ(*Client::Scenes::EventCountdownText(event, 1000 + 12345, &texts, true).Value(), "ds uno en 18");
    EXPECT_EQ(*Client::Scenes::EventCountdownText(event, 1000 + 30000, &texts, true).Value(), "ds uno en 0");
    EXPECT_FALSE(Client::Scenes::EventCountdownText(event, 1000 + 30001, &texts, false).Value().has_value());
    EXPECT_EQ(event.field08, 1);
    EXPECT_FALSE(Client::Scenes::EventCountdownText(event, 1000 + 30001, &texts, true).Value().has_value());
    EXPECT_EQ(event.field08, 0);

    EventMapObject wrapped{.field08 = 1, .field0c = static_cast<std::int32_t>(0xfffffc18u)};
    EXPECT_EQ(*Client::Scenes::EventCountdownText(wrapped, 2000, &texts, true).Value(), "ds uno en 27");

    for (const std::int32_t kind : {0, 15, -1})
    {
        EventMapObject other{.field08 = kind, .field0c = 0};
        EXPECT_FALSE(Client::Scenes::EventCountdownText(other, 0, &texts, true).Value().has_value());
    }
    EventMapObject missing{.field08 = 2, .field0c = 0};
    const auto cut = Client::Scenes::EventCountdownText(missing, 0, nullptr, true);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x40bab0"), std::string::npos) << cut.Error();
}

// ----- 93 (0x4e4959).

// Devil Square (0x40bd20): con pkt[4] < 200, los botones, +0x24 = cuenta, +0x28 = puesto, la copia de 24 bytes por
// registro a +0x2c y el 0x8b. Blood Castle (0x40c0c0) con 0xff y Chaos Castle (0x404da0) con 0xfe: un registro,
// +0x24 = puesto y el 0x97 o el 0x9b.
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

// Q-C1324: más de 11 registros pasan los 0x108 bytes de +0x2c; menos bytes que los que copia, del buffer de la
// recepción. En los dos se descarta el resto, después de los botones, +0x24 y +0x28 (0x40bd34..0x40bd81): ni la copia
// ni el 0x8b. Blood Castle (0x40c12a) y Chaos Castle (0x404e0a) sin los 24 bytes, ídem. La sesión sigue.
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

// ----- 94, 95 y 96.

// 0x4dd8a0: cierra todo (con sus rand()), 0x78abb04 = la cuenta con signo y 0x78abb00 = tipo + 1; con 1, los números
// del Muto si ninguno es 0xffff; con 3 (0x4dd8ce..0x4dd920), el número de serie en cero, los buffers del chat vacíos,
// el campo cerrado, un solo campo para el Tab y 0x77f3b6b en 1.
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

// 0x4dd926..0x4dd92c: el 94 con el tipo 0 corto lee los números del Muto del buffer de la recepción. Se descartan,
// después del cierre de todo (con sus rand()) y de 0x78abb00/04; la sesión sigue.
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

// 0x4e49d4: la cuenta salvo 0xffff; 0x4e49fb: los tres números salvo con alguno en 0xffff.
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

// ----- 9A (0x4da780) y AF 01 (0x4daa90), con sprintf.

// Cierran todo y muestran el aviso. "% s" con el nombre sale con el nombre; el "% d" de 853 toma el puntero al nombre
// e imprime su dirección (Q-C1321); 867 con 6.
TEST_F(EventPacketsTest, TheCastleEntryResultsShowTheirNotice)
{
    const std::string bloodAddress =
        std::to_string(Client::UI::TextTableAddress + BloodCastleNameText * Client::UI::TextRecordBytes);
    const std::string chaosAddress =
        std::to_string(Client::UI::TextTableAddress + ChaosCastleNameText * Client::UI::TextRecordBytes);
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

// Blood Castle (0x40c140), en los mapas 11 a 17: el 0 gira a los jugadores hacia la puerta (0x5111a0(0xc0)), pide la
// música 0x8b y sigue en el 1: +0x10 = estado + 1, +0x14 = (900, pkt[4], pkt[6], pkt[8]) y el arma del dueño (sin
// el bit alto de la clave) en +0x360, con las de los demás en 0. El 2 vuelve a 0x40ba20 y apaga la música; el 3 pide
// el derrumbe (mapa, 0x24, 0x14, 1).
TEST_F(EventPacketsTest, TheBloodCastlePlayStateTurnsThePlayersAndGivesTheWeapon)
{
    for (const std::int32_t map : BloodCastleMaps)
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
        Client::Scenes::ActorState& other = world.Actors()[7];
        other.alive = true;
        other.type = Client::Data::Models::PlayerSkeletonModel;
        other.key = 0x0456;
        other.byte360 = 3;
        Client::Scenes::ActorState& monster = world.Actors()[8];
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
        EXPECT_EQ(started.sounds, std::vector<std::int32_t>{0x8b});
        EXPECT_EQ(Runtime().lastSoundId, 0x8b);
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
        EXPECT_EQ(reset.stoppedSounds, std::vector<std::int32_t>{0x8b});

        Applied(Events::EventPlayState{.state = 3});
        EXPECT_EQ(Runtime().events.objectCollapse.map, map);
        EXPECT_EQ(Runtime().events.objectCollapse.type, 0x24);
        EXPECT_EQ(Runtime().events.objectCollapse.counter, 0x14);
        Applied(Events::EventPlayState{.state = 5});
        EXPECT_EQ(Runtime().eventObject->field10, 0);
    }
}

// Q-C1326: 0x511150 sin nadie con la clave escribe el arma en el 400 (fuera de los que recorrió).
TEST_F(EventPacketsTest, TheWeaponWithoutItsOwnerGoesToTheFourHundredth)
{
    Runtime().map = 11;
    Runtime().eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
    Hero().byte360 = 1;
    Applied(Events::EventPlayState{.state = 1, .weaponOwner = 0x0777, .weapon = 3});
    EXPECT_EQ(Hero().byte360, 0);
    EXPECT_EQ(world.Actors()[400].byte360, 3);
}

// Sin Player.bmd, el giro de 0x5111a0 corta.
TEST_F(EventPacketsTest, TheGateTurnNeedsThePlayerSkeleton)
{
    Runtime().map = 11;
    Runtime().eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
    auto context = Context();
    context.playerSkeleton = nullptr;
    const auto applied = Client::Scenes::ApplyEventPacket(Events::EventPlayState{.state = 0}, context);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x40c16a"), std::string::npos) << applied.Error();
}

// Chaos Castle (0x404e20), en los mapas 18 a 23: el 5 arma la trampa, el área en 0xff, cierra el chat, gira a los
// jugadores y cambia la música (0x9e por 0x99); el 6, +0x10 = 7 sin arma; el 7 vuelve y, en 18..23, 0x9e y 0x9f.
// Los 8, 9 y 10 con el área en 0, 3 y 6: la siguiente, el derrumbe (mapa, 1, 0x28, 1), cuatro rectángulos con el 8
// y el 0x61; con otra área, nada.
TEST_F(EventPacketsTest, TheChaosCastlePlayStateCollapsesItsRings)
{
    for (const std::int32_t map : ChaosCastleMaps)
    {
        SCOPED_TRACE(map);
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().eventObject = EventMapObject{.kind = EventMapKind::ChaosCastle};
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Runtime().chaosCastleTrapArmed = 0;
        Runtime().chaosCastleArea = 0;
        Runtime().ui.chatOpen = true;
        Hero().byte360 = 0;

        const EventPacketResult started = Applied(Events::EventPlayState{.state = 5});
        EXPECT_EQ(Runtime().chaosCastleTrapArmed, 1);
        EXPECT_EQ(Runtime().chaosCastleArea, 0xff);
        EXPECT_FALSE(Runtime().ui.chatOpen);
        EXPECT_EQ(Hero().animation.action, 0xc0);
        EXPECT_EQ(started.stoppedSounds, std::vector<std::int32_t>{0x9e});
        EXPECT_EQ(started.sounds, std::vector<std::int32_t>{0x99});

        Applied(Events::EventPlayState{.state = 6, .remaining = 50, .weaponOwner = 0x0123, .weapon = 2});
        EXPECT_EQ(Runtime().eventObject->field10, 7);
        EXPECT_EQ(Runtime().eventObject->field14[1], 50);
        EXPECT_EQ(Hero().byte360, 0);

        // Con el área en 0xff, los derrumbes no hacen nada.
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
            EXPECT_EQ(collapsed.sounds, std::vector<std::int32_t>{0x61});
        }
        // El primer anillo: (0x17, 0x4b) de 0x16 × 2, (0x2b, 0x4d) de 2 × 0x20, ...
        EXPECT_EQ(Cell(0x17, 0x4b), 8);
        EXPECT_EQ(Cell(0x2c, 0x4c), 8);
        EXPECT_EQ(Cell(0x2d, 0x4c), 0);
        EXPECT_EQ(Cell(0x2c, 0x6c), 8);
        EXPECT_EQ(Cell(0x2c, 0x6d), 0);
        // El tercero: (0x1b, 0x4f) de 0xe × 2.
        EXPECT_EQ(Cell(0x28, 0x50), 8);

        const EventPacketResult ended = Applied(Events::EventPlayState{.state = 7});
        EXPECT_EQ(Runtime().eventObject->field10, 0);
        EXPECT_EQ(ended.stoppedSounds, std::vector<std::int32_t>{0x99});
        EXPECT_EQ(ended.sounds, (std::vector<std::int32_t>{0x9e, 0x9f}));
    }
}

// Devil Square (0x40bdc0) no hace nada con el 9B; sin el objeto (Lorencia, Devias, Kalima, Crywolf), tampoco.
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

// ----- 9D y 9E.

// 0x4e4a86: el aviso 886 + n (de 0 a 4) o 899 (5), y los 0x40 bytes siempre.
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

// 0x4e4ad4: 0x5b, 0x5c y 0x5d con 0, 1 y 2.
TEST_F(EventPacketsTest, TheEventSoundPlaysItsSound)
{
    EXPECT_EQ(Applied(Events::EventSound{0}).sounds, std::vector<std::int32_t>{0x5b});
    EXPECT_EQ(Applied(Events::EventSound{2}).sounds, std::vector<std::int32_t>{0x5d});
    EXPECT_TRUE(Applied(Events::EventSound{3}).sounds.empty());
    EXPECT_EQ(Runtime().lastSoundId, 0x5d);
}

// ----- BC 00 y BC 01.

// El mensaje "%s%s %s" (mezcla o deshace, 1816 y 868 tal cual) o el aviso, y después la ventana de la mezcla
// (0x402e50, hito 6), que corta; el 1 abre los diálogos 0 y 0xa6.
TEST_F(EventPacketsTest, TheJewelResultsSayTheirMessageAndCutTheWindow)
{
    const std::string mixed = Cut(Events::JewelMixResult{.subcode = 0, .result = 0});
    EXPECT_NE(mixed.find("0x402e50"), std::string::npos) << mixed;
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "mezclapara permitida% d veces");
    Cut(Events::JewelMixResult{.subcode = 0, .result = 4});
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "faltan");
    Cut(Events::JewelMixResult{.subcode = 1, .result = 5});
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "deshacepara permitida% d veces");
    Cut(Events::JewelMixResult{.subcode = 1, .result = 7});
    EXPECT_EQ(Runtime().chat.log.entries.front().text, "sin lugar");

    Runtime().dialogs = {};
    Applied(Events::JewelMixResult{.subcode = 1, .result = 1});
    EXPECT_EQ(Runtime().dialogs.current, 0xa6);
    // 0x6236f0(0) pasa al que espera y el 0xa6 queda esperando detrás.
    Runtime().dialogs = {.current = 0, .pending = 0x55};
    Applied(Events::JewelMixResult{.subcode = 0, .result = 1});
    EXPECT_EQ(Runtime().dialogs.current, 0x55);
    EXPECT_EQ(Runtime().dialogs.pending, 0xa6);
    const std::size_t entries = Runtime().chat.log.entries.size();
    Applied(Events::JewelMixResult{.subcode = 0, .result = 9});
    EXPECT_EQ(Runtime().chat.log.entries.size(), entries);
}

// ----- BD 00 (0x42fe80), en Crywolf (34) y contra el ASM solo.

// La misma etapa y el mismo estado, nada. La 4 prende 0x723bec. La 5 pone 0x6c73e4..0x6c73f4 en −1 y, salvo con el
// estado 2, el aviso (con el 0, 0x6c73dc en 0xb). La 6 es 0x42fdc0. Sin cambio de estado no corta, y la etapa queda
// en el recorrido de la cámara (0x4033b0).
TEST_F(EventPacketsTest, TheCrywolfStageUpdatesTheInterface)
{
    Runtime().map = 34;
    Client::Scenes::CrywolfInterface& ui = Runtime().crywolf.ui;
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

// Con otro estado, 0x42ff81 y 0x42ffe8 vuelven a leer la luz y el .att de la carpeta del mapa + 1, después de lo de la
// etapa: en Crywolf, TerrainLight1/2 y EncTerrain351/352; con el 0 otra vez, los de siempre. Con el estado 2 la 5 no
// pone el aviso; con más de 2, el buffer de la luz sin escribir (Q-C1328).
TEST_F(EventPacketsTest, TheCrywolfStateReloadsTheLightAndTheAttributes)
{
    Runtime().map = 34;
    LoadTheTerrain(34);
    const auto plain = AttributesOf("world35/EncTerrain35.att");
    ASSERT_EQ(world.Map().terrain.attributes->cells, plain);
    Client::Scenes::CrywolfInterface& ui = Runtime().crywolf.ui;
    ui.message = -1;
    const auto first = Shaded("world35/TerrainLight1.OZJ");
    Applied(Events::CrywolfState{.state = 1, .stage = 1});
    EXPECT_EQ(Runtime().crywolf.state, 1);
    EXPECT_EQ(world.Map().terrain.lighting->cells, first);
    EXPECT_EQ(world.Map().terrain.attributes->cells, AttributesOf("world35/EncTerrain351.att"));
    EXPECT_EQ(Runtime().events.cameraTour.stage, 1);

    Applied(Events::CrywolfState{.state = 2, .stage = 5});
    EXPECT_EQ(Runtime().crywolf.state, 2);
    EXPECT_EQ(Runtime().crywolf.stage, 5);
    EXPECT_EQ(ui.message, -1);
    EXPECT_EQ(world.Map().terrain.lighting->cells, Shaded("world35/TerrainLight2.OZJ"));
    EXPECT_EQ(world.Map().terrain.attributes->cells, AttributesOf("world35/EncTerrain352.att"));

    Applied(Events::CrywolfState{.state = 0, .stage = 5});
    EXPECT_EQ(world.Map().terrain.lighting->cells, Shaded("world35/TerrainLight.OZJ"));
    EXPECT_EQ(world.Map().terrain.attributes->cells, plain);
    EXPECT_FALSE(Runtime().fatalDataMessage.has_value());

    // Q-C1328: con el estado ya escrito, se descartan la carga y 0x4033b0; la sesión sigue.
    const std::string broken = Discarded(Events::CrywolfState{.state = 3, .stage = 2});
    EXPECT_NE(broken.find("Q-C1328"), std::string::npos) << broken;
    EXPECT_EQ(Runtime().crywolf.state, 3);
    EXPECT_EQ(Runtime().crywolf.stage, 2);
    EXPECT_EQ(world.Map().terrain.lighting->cells, Shaded("world35/TerrainLight.OZJ"));
    EXPECT_EQ(world.Map().terrain.attributes->cells, plain);
    EXPECT_EQ(Runtime().events.cameraTour.stage, 5);
}

// En Lorencia y en Devias el BD 00 también recarga, de World1 y World3 (Q-C1340): TerrainLight1 no está y deja el aviso
// fatal; el .att 11 o 31 tampoco. Lo cargado queda como estaba. Con el 0 otra vez vuelve a leer los suyos.
TEST_F(EventPacketsTest, TheCrywolfStateReloadsTheCurrentMapToo)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().crywolf.state = 0;
        Runtime().fatalDataMessage.reset();
        LoadTheTerrain(static_cast<std::uint8_t>(map));
        const auto lighting = world.Map().terrain.lighting->cells;
        const auto attributes = world.Map().terrain.attributes->cells;
        Applied(Events::CrywolfState{.state = 1, .stage = 0});
        const std::string folder = map == 0 ? "World1" : "World3";
        ASSERT_TRUE(Runtime().fatalDataMessage.has_value());
        EXPECT_EQ(*Runtime().fatalDataMessage, "Data\\" + folder + "\\TerrainLight1.OZJ - File not exist.");
        EXPECT_EQ(world.Map().terrain.lighting->cells, lighting);
        EXPECT_EQ(world.Map().terrain.attributes->cells, attributes);

        world.Map().terrain.attributes->cells[0] = 0x55;
        Applied(Events::CrywolfState{.state = 0, .stage = 0});
        EXPECT_EQ(world.Map().terrain.attributes->cells, attributes);
    }
    // Sin la carpeta Data, corta.
    Runtime().crywolf.state = 0;
    dataRoot.clear();
    const std::string cut = Cut(Events::CrywolfState{.state = 1, .stage = 0});
    EXPECT_NE(cut.find("0x42ff81"), std::string::npos) << cut;
}

// 0x42fdc0 (también la vuelta a la selección): los globales de la interfaz a su valor inicial.
TEST_F(EventPacketsTest, TheCrywolfInterfaceResetMatchesTheData)
{
    Client::Scenes::CrywolfInterface& ui = Runtime().crywolf.ui;
    ui.dwords6c73e0 = {1, 2, 3, 4, 5, 6};
    ui.dword6c73d4 = 0;
    ui.light = 3;
    Runtime().crywolf.state = 2;
    Runtime().crywolf.stage = 3;
    Client::Scenes::ResetTheCrywolfInterface(Runtime());
    EXPECT_EQ(ui.dwords6c73e0, (std::array<std::int32_t, 6>{-1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(ui.dword6c73d4, 0x64);
    EXPECT_EQ(ui.light, 0);
    EXPECT_EQ(Runtime().crywolf.state, 0);
    EXPECT_EQ(Runtime().crywolf.stage, 0);
}

// ----- BD 02 a 0C: la interfaz de Crywolf (GS 0.97 no los manda; el oráculo es el ASM del cliente).

// BD 02, 07, 08 y 09 escriben en cualquier mapa y etapa: la luz y los altares (0x430000), el rango y la experiencia
// (0x437600), los resultados con su ventana (0x437620) y 0x7d2dc9d. Un lugar de 5 o más escribiría fuera de las tablas
// (Q-C1341): se descarta desde ese registro, y los de antes quedan.
TEST_F(EventPacketsTest, TheCrywolfInterfacePacketsWriteOnAnyMap)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        Runtime().map = map;
        Runtime().crywolf = {};
        Client::Scenes::CrywolfInterface& ui = Runtime().crywolf.ui;
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

// BD 04 solo con la etapa 4 en el 34 (0x4359f0 y 0x4303c0): pkt[4], los minutos, GetTickCount y 0x723bf8. BD 05 con
// la etapa 3 o 4 en el 34: 0x723bd8, la barra prendida con el dword en más de 0 y apagada con 0 o menos, y 0x6c73d4.
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
            Client::Scenes::CrywolfInterface& ui = Runtime().crywolf.ui;
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

// BD 03 (0x4e1010) con 0: el héroe se para y, con el registro propio abierto (0xf08 rand() al cerrarlo), menos de 350
// da 0x36 y 0x37 y 350 o más 0x3a, que solo se ven con la defensa en el 34. Con otro resultado que 0 y 1, nada.
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
            expected.Discard(Client::Render::Models::OwnRecordCloseRandoms);
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

// BD 03 con 1: 0x435b00(3) y (4), el altar de la clave − 0x13c (0x435ad0) solo con la defensa en el 34, y en cualquier
// mapa el héroe sentado (0x200000 en +0x7c y la acción 0xca) y el C1 05 18 con el rumbo de +0x24 y 0x82, con el XOR32.
// No saca rand(). Con la clave fuera de los cinco altares, en el 34 (Q-C1347) se descarta el resto después de los dos
// renglones: ni el altar ni el héroe sentado ni el 18; fuera del 34 sigue entero.
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

// BD 0C (0x430040, los efectos de la CL11) corta con su dirección.
TEST_F(EventPacketsTest, TheCrywolfEffectsAreCut)
{
    const std::string effects = Cut(Events::CrywolfUnported{.subcode = 0xc, .bytes = {}});
    EXPECT_NE(effects.find("0x430040"), std::string::npos) << effects;
}

// ----- F3 22, F3 23 y F3 40.

TEST_F(EventPacketsTest, TheEventTimeIsKept)
{
    Applied(Events::EventTime{.value4 = 2, .value6 = 0x0e10});
    EXPECT_EQ(Runtime().events.timeKind, 2);
    EXPECT_EQ(Runtime().events.timeValue, 0x0e10);
}

// 0x4d9f00: los dos nombres con su '\0', los puntos y el partido activo con pkt[0xc] distinto de 0xff; después, el
// efecto 0x60e (CL11) corta.
TEST_F(EventPacketsTest, TheSoccerScoreIsKeptBeforeTheEffectCut)
{
    const Events::SoccerScore score{.firstName = {'A', 'z', 'u', 'l', 'e', 's', 'x', 'y'},
                                    .firstScore = 2,
                                    .secondName = {'R', 'o', 'j', 'o', 0, 0, 0, 0},
                                    .secondScore = 1};
    const std::string cut = Cut(score);
    EXPECT_NE(cut.find("0x4d9ff2"), std::string::npos) << cut;
    const auto& soccer = Runtime().events.soccer;
    EXPECT_EQ(std::string(soccer.firstName.data()), "Azulesxy");
    EXPECT_EQ(std::string(soccer.secondName.data()), "Rojo");
    EXPECT_TRUE(soccer.active);
    EXPECT_EQ(soccer.firstScore, 2);
    EXPECT_EQ(soccer.secondScore, 1);
    Events::SoccerScore none = score;
    none.firstScore = 0xff;
    Cut(none);
    EXPECT_FALSE(soccer.active);
}

// 0x4da070: los avisos por comando y el sonido 0x5a; el efecto (CL11) corta.
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
    EXPECT_EQ(Applied(Events::ServerCommand{.command = 2}).sounds, std::vector<std::int32_t>{0x5a});
    EXPECT_NE(Cut(Events::ServerCommand{.command = 0}).find("0x4da125"), std::string::npos);
    ResetTheBox();
    Applied(Events::ServerCommand{.command = 4});
    Applied(Events::ServerCommand{.command = 0x32});
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x4da18f..0x4da195: el comando 5 es 0x632f70(pkt[5]), el diálogo de Dialog.bmd con sus respuestas en el 0x8d (o
// esperando, con otra caja abierta). Desde el 200 el original lee fuera de los 200 de 0x7688ac0 (Q-C1325): se
// descarta. Sin Dialog.bmd corta, como 0x632f70 desde las respuestas.
TEST_F(EventPacketsTest, TheServerCommandFiveOpensTheAnswersDialog)
{
    Client::Data::Text::DialogTable table(Client::Data::Text::DialogCount);
    std::ranges::copy(std::string_view("un dialogo inventado"), table[0xc7].text.begin());
    table[0xc7].answerCount = 1;
    std::ranges::copy(std::string_view("seguir"), table[0xc7].answers[0].begin());
    std::ranges::copy(std::string_view("sin respuestas"), table[3].text.begin());
    texts = Client::Tests::MakeTextTable({{Client::UI::NoAnswersText, "nada mas"}});
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
            Client::UI::DialogBoxState expected;
            Client::UI::DialogState expectedDialogs;
            ASSERT_TRUE(Client::UI::OpenTheAnswersDialog(expected, expectedDialogs, &table, &texts, index).HasValue());
            const EventPacketResult result = Applied(Events::ServerCommand{.command = 5, .value5 = index});
            EXPECT_FALSE(result.discarded.has_value());
            EXPECT_EQ(Runtime().dialogs.current, Client::UI::AnswersDialog);
            EXPECT_EQ(Runtime().dialogBox.dialogIndex, index);
            EXPECT_EQ(Runtime().dialogBox.lineCount, expected.lineCount);
            EXPECT_EQ(Runtime().dialogBox.answerCount, expected.answerCount);
            EXPECT_EQ(Runtime().dialogBox.rows, expected.rows);
            EXPECT_EQ(Runtime().dialogBox.answers, expected.answers);
        }
        ResetTheBox();
        Runtime().dialogs.current = Client::UI::MessageBoxDialog;
        Applied(Events::ServerCommand{.command = 5, .value5 = 3});
        EXPECT_EQ(Runtime().dialogs.current, Client::UI::MessageBoxDialog);
        EXPECT_EQ(Runtime().dialogs.pending, Client::UI::AnswersDialog);

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
