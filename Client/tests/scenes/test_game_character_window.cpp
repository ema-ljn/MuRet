#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Character/LevelUpPoint.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include "support/ItemSetFiles.h"
#include "support/SameBytes.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// La ventana del personaje en el paso de la UI (CL8 hito 5; 06 § HUD y ventanas, "La ventana del personaje"): abrirla
// con la C (0x65c94b..0x65ca72, con 0x550d10), su paso (0x5e4510, los botones de puntos y el de cerrar) y el pedido
// del caballo oscuro (0x424130 → 0x424230). Las cuentas de rand() salen del ASM y del modelo de la CRT; ninguna de una
// corrida del original. Cada caso corre en Lorencia y en Devias.
namespace
{
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Protocol::Outgoing;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;
using Client::Tests::SameBytes;

// 0x5a05e5..0x5a10e2: el registro propio de la barra de skills; 0x5a343e..0x5a3587: las consultas del comercio y de
// la tienda de NPC de Q, W y E; 0x5a5511: IsOpen(0), que con el personaje (0x2000000) pasa por las dos antes.
constexpr int BaseStepRandoms = OwnRecordCloseRandoms + 2 + 2;
// Open(0x2000000): CanOpen consulta la tienda de NPC y el comercio, y la máscara de 0x657ab0 los dos de nuevo.
constexpr int OpenRandoms = 4;
// La cola de 0x550d10 (0x5512bf..0x5513b1): el rand() del IDIV y los dos % 100. Los 33 cierres de 0x40e1b0 quedan
// dentro del acceso de 0x65c9a4 y no sacan nada.
constexpr int TailRandoms = 3;
// Los dos mapas de los tests de mundo: Lorencia y Devias.
constexpr std::array<std::int32_t, 2> Maps{0, 2};
// Donde 0x5ee2e0 pone la ventana en cada frame (0x78aba14 y 0x78aba18).
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::int32_t WindowY = 0;
// Un tipo vacío del equipo: −1 en el WORD del tipo.
constexpr std::uint16_t EmptyType = 0xffff;
// La clase del Dark Lord (+0x1d4 & 7) y una que no lo es.
constexpr std::uint8_t DarkLordClass = 4;
constexpr std::uint8_t KnightClass = 1;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Item.bmd sin nada: 0x550d10 no suma nada de un lugar vacío.
Client::Data::Tables::ItemTable EmptyItems()
{
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(
        std::vector<std::byte>(Client::Data::Tables::ItemTableSize), Client::Data::Tables::ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// Los conjuntos sin ningún tipo ni opción.
Client::Data::Tables::ItemSetTables EmptySets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// El paso de la UI con lo que la ventana del personaje lee: las tablas de 0x550d10, lo que dejó el último 0x505820,
// el equipo vacío (la durabilidad en 0, que 0x550cf0 encuentra) y lo que sale por send().
class SteppingCharacter
{
public:
    explicit SteppingCharacter(std::int32_t map)
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.fullSetScan = Client::Scenes::Scan505820{};
        runtime.hud.characterX = WindowX;
        runtime.hud.characterY = WindowY;
        Client::World::ItemState empty;
        empty.id = EmptyType;
        world.OwnCharacter().equipment.fill(empty);
    }
    SteppingCharacter(const SteppingCharacter&) = delete;
    SteppingCharacter& operator=(const SteppingCharacter&) = delete;
    SteppingCharacter(SteppingCharacter&&) = delete;
    SteppingCharacter& operator=(SteppingCharacter&&) = delete;
    ~SteppingCharacter() = default;

    Client::Core::Expected<void, std::string> Step(std::int32_t x = 0, std::int32_t y = 0)
    {
        return Client::Scenes::StepTheGameUi(GameUiContext{.world = world,
                                                           .random = random,
                                                           .input = input,
                                                           .mouseX = x,
                                                           .mouseY = y,
                                                           .chat = &chat,
                                                           .inventory = &inventory,
                                                           .itemSetTables = sets});
    }
    void Press(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = key});
    }
    void Release(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = key});
    }
    void Click()
    {
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true, false);
    }
    // La ventana ya abierta, con el pedido del caballo hecho (0x723911 en 1).
    void OpenTheWindow()
    {
        world.Runtime().panels.character = 1;
        world.Runtime().panels.delegated.pet = 1;
    }

    WorldState world;
    CrtRandom random;
    InputState input;
    std::vector<Outgoing> sent;
    const Client::Data::Tables::ItemTable items = EmptyItems();
    const Client::Data::Tables::ItemSetTables setTables = EmptySets();
    const Client::Data::Tables::ItemSetTables* sets = &setTables;
    const Client::Scenes::InventoryServices inventory{.items = &items};
    const Client::Scenes::GameChatContext chat{
        .world = world, .random = random, .send = [this](Outgoing packet) { sent.push_back(std::move(packet)); }};
};

::testing::AssertionResult SamePacket(const Outgoing& actual, const Outgoing& expected)
{
    if (actual.delivery != expected.delivery)
        return ::testing::AssertionFailure() << "sale de otra forma";
    return SameBytes(actual.packet.Bytes(), expected.packet.Bytes());
}

Outgoing DarkHorseRequest()
{
    return Client::Protocol::Pet::WritePetInfoRequest(
        {Client::Protocol::Pet::DarkHorsePet, Client::Protocol::Pet::InventoryPlace, Client::Protocol::Pet::PetSlot});
}

// 0x65c94b..0x65ca72: la C abre el personaje, pone en 0 lo de los tooltips del set y corre 0x550d10 con el registro
// propio abierto alrededor; el A9 sale en el paso siguiente.
TEST(GameCharacterWindowTest, TheLetterCOpensTheCharacterAndRecalculatesTheOwnStats)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        Client::Scenes::ItemSetState& sets = tested.world.ItemSets();
        sets.byte9304 = 1;
        sets.byte9305 = 3;
        sets.byte9306 = 1;
        sets.names[1][0] = 'x';
        tested.world.OwnCharacter().wordef8 = 0x3456;
        tested.Press(Key::C);
        const CrtRandom expected =
            Advanced(tested.random, BaseStepRandoms + OpenRandoms + TailRandoms + OwnRecordCloseRandoms);
        const auto stepped = tested.Step(320, 240);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), expected.State()) << map;
        EXPECT_EQ(tested.world.Runtime().panels.character, 1);
        EXPECT_EQ(sets.byte9304, 0);
        EXPECT_EQ(sets.byte9305, 0);
        EXPECT_EQ(sets.byte9306, 0);
        EXPECT_EQ(sets.names[1][0], '\0');
        // La cola de 0x550d10 corrió: +0xef8 es una resta de WORD en 0.
        EXPECT_EQ(tested.world.OwnCharacter().wordef8, 0);
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x1c);
        EXPECT_TRUE(tested.sent.empty());
    }
}

// 0x550cf0: con la durabilidad de los doce lugares fuera de 0, 0x550d10 no corre; el acceso se abre y se cierra igual.
TEST(GameCharacterWindowTest, WithEveryPlaceWornTheOpeningSkipsTheRecalculation)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        for (Client::World::ItemState& item : tested.world.OwnCharacter().equipment)
            item.durability = 1;
        tested.world.OwnCharacter().wordef8 = 0x3456;
        tested.Press(Key::C);
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + OpenRandoms + OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.random.State(), expected.State()) << map;
        EXPECT_EQ(tested.world.OwnCharacter().wordef8, 0x3456);
    }
}

// Sin ItemSetType.bmd e ItemSetOption.bmd, 0x550d10 corta en 0x40e1b0, con el acceso cerrado.
TEST(GameCharacterWindowTest, WithoutTheSetTablesTheOpeningIsTheCutOf0x40e1b0)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.sets = nullptr;
        tested.Press(Key::C);
        const auto stepped = tested.Step(320, 240);
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("0x40e1b0"), std::string::npos) << stepped.Error();
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    }
}

// 0x424230: el A9 01 00 08 sale una vez por apertura, en el paso que sigue al que la abre; cerrar el personaje pone
// 0x723911 en 0 en el paso siguiente, y la próxima apertura lo pide de nuevo.
TEST(GameCharacterWindowTest, TheDarkHorseIsAskedOncePerOpening)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        const Client::UI::DelegatedPanels& delegated = tested.world.Runtime().panels.delegated;
        tested.Press(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SamePacket(tested.sent[0], DarkHorseRequest()));
        EXPECT_EQ(delegated.pet, 1);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.sent.size(), 1u);

        tested.Release(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        tested.Press(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.world.Runtime().panels.character, 0);
        EXPECT_EQ(delegated.pet, 1);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(delegated.pet, 0);

        tested.Release(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        tested.Press(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.sent.size(), 1u);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        ASSERT_EQ(tested.sent.size(), 2u);
        EXPECT_TRUE(SamePacket(tested.sent[1], DarkHorseRequest()));
    }
}

// 0x5e4583..0x5e4a5a: con el personaje abierto, cada paso abre y cierra el registro propio (3.848 rand()) aunque no
// haya puntos; el mouse sobre la ventana es de la UI.
TEST(GameCharacterWindowTest, TheOpenWindowOpensTheOwnRecordInEachStep)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Step(WindowX + 0xbd, WindowY + 0x100).HasValue());
        EXPECT_EQ(tested.random.State(), expected.State()) << map;
        EXPECT_EQ(tested.world.Runtime().ui.mouseOnUi, 1);
        EXPECT_TRUE(tested.sent.empty());

        ASSERT_TRUE(tested.Step(WindowX - 1, WindowY + 0x100).HasValue());
        EXPECT_EQ(tested.world.Runtime().ui.mouseOnUi, 0);
    }
}

// 0x5e4638..0x5e4a54: con puntos (+0x68), el clic en el botón i manda F3 06 i y suena 0x19. Sin el Dark Lord son
// cuatro botones: el quinto no manda nada.
TEST(GameCharacterWindowTest, TheStatButtonsSendTheirPoint)
{
    for (const std::int32_t map : Maps)
    {
        for (std::int32_t button = 0; button < 5; ++button)
        {
            for (const std::uint8_t heroClass : {KnightClass, DarkLordClass})
            {
                SteppingCharacter tested(map);
                tested.OpenTheWindow();
                tested.world.Actors()[tested.world.Runtime().hero].characterClass = heroClass;
                tested.world.OwnCharacter().levelUpPoints = 2;
                tested.Click();
                const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + OwnRecordCloseRandoms);
                const auto stepped = tested.Step(WindowX + 0x7d, WindowY + 0x73 + 0x3c * button + 0x17);
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_EQ(tested.random.State(), expected.State());
                if (button == 4 && heroClass != DarkLordClass)
                {
                    EXPECT_TRUE(tested.sent.empty());
                    continue;
                }
                ASSERT_EQ(tested.sent.size(), 1u) << button;
                EXPECT_TRUE(SamePacket(
                    tested.sent[0], Client::Protocol::Character::WriteLevelUpPoint(static_cast<std::uint8_t>(button))));
                EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x19);
                EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
            }
        }
    }
}

// 0x5e461b: sin puntos, los botones no miran el clic.
TEST(GameCharacterWindowTest, WithoutPointsTheStatButtonsDoNothing)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        tested.Click();
        ASSERT_TRUE(tested.Step(WindowX + 0x80, WindowY + 0x76).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_FALSE(tested.world.Runtime().lastSoundId.has_value());
    }
}

// 0x5e4c3f..0x5e4ca9: el botón de cerrar toma el clic, pone el contador del mouse en 0 con su tope en 6 y cierra el
// personaje.
TEST(GameCharacterWindowTest, TheCloseButtonClosesTheCharacter)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        tested.world.Runtime().movement.mouseUpdateTime = 3;
        tested.Click();
        ASSERT_TRUE(tested.Step(WindowX + 0x19, WindowY + 0x18b).HasValue());
        EXPECT_EQ(tested.world.Runtime().panels.character, 0);
        EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
        EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTime, 0);
        EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTimeMax, 6);
    }
}
} // namespace
