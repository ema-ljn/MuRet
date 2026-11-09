#include "Scenes/FullSetScan.h"

#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Client::Scenes::ScanFullSet: 0x505820, cotejado con el PE c32bbd5f (06, "El conjunto completo de
// 0x505820"). Los valores salen del ASM; ninguno de una corrida.
namespace
{
using Client::Data::Models::ItemModel;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::FullSetScan;
using Client::Scenes::OwnCharacterRecord;
using Client::Scenes::ScannedCharacter;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x505820;
constexpr std::uint8_t MagicGladiator = 3;
constexpr std::size_t Helm = 0;
constexpr std::size_t Armor = 1;
constexpr std::size_t Boots = 4;
// La armadura del registro propio: el lugar 3 (+0xc44).
constexpr std::size_t OwnArmorSlot = 3;

// Del casco a las botas, el índice `index` de los grupos 7 a 11, con los niveles `levels`.
ScannedCharacter Wearing(std::uint8_t characterClass, std::uint16_t index, const std::array<std::uint8_t, 5>& levels)
{
    ScannedCharacter character{.characterClass = characterClass};
    for (std::size_t part = 0; part < character.body.size(); ++part)
    {
        character.body[part] = {.model = ItemModel(static_cast<std::uint8_t>(7 + part), index), .level = levels[part]};
    }
    return character;
}

ScannedCharacter WearingPlus13(std::uint8_t characterClass, std::uint16_t index)
{
    return Wearing(characterClass, index, {13, 13, 13, 13, 13});
}

class FullSetScanTest : public ::testing::Test
{
protected:
    FullSetScan Scan(const ScannedCharacter& character)
    {
        return Client::Scenes::ScanFullSet(character, m_own, m_accesses, m_random);
    }

    OwnCharacterRecord m_own;
    std::uint8_t m_accesses = 0;
    CrtRandom m_random{Seed};
};

// 0x50582c..0x5058f4: el nivel más chico de las cinco, que van del 9 al 15 con el mismo índice que las
// botas; [0x743a17c] en 1 sin abrir el registro.
TEST_F(FullSetScanTest, AFullSetLeavesItsLowestLevel)
{
    const FullSetScan scan = Scan(Wearing(1, 5, {12, 9, 15, 10, 11}));
    EXPECT_TRUE(scan.complete);
    EXPECT_EQ(scan.globals.dword743a178, 9);
    EXPECT_EQ(scan.globals.byte743a17c, 1);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());

    // Con los cinco iguales, ese.
    EXPECT_EQ(Scan(Wearing(1, 5, {14, 14, 14, 14, 14})).globals.dword743a178, 14);
}

// 0x50582c..0x505846: todas las clases miran el casco salvo las de & 7 en 3, que empiezan por la armadura.
TEST_F(FullSetScanTest, OnlyClassThreeSkipsTheHelm)
{
    for (std::uint8_t characterClass = 0; characterClass < 16; ++characterClass)
    {
        SCOPED_TRACE(int{characterClass});
        const bool skipsTheHelm = (characterClass & 7) == MagicGladiator;
        // El casco del 9: con él, el más chico es 9; sin él, 13.
        const FullSetScan low = Scan(Wearing(characterClass, 5, {9, 13, 13, 13, 13}));
        EXPECT_TRUE(low.complete);
        EXPECT_EQ(low.globals.dword743a178, skipsTheHelm ? 13 : 9);

        // Sin casco, o con uno del 8 o de otro índice, solo la 3 tiene el conjunto.
        for (const std::uint16_t helm : {std::uint16_t{0xffff}, ItemModel(7, 6)})
        {
            ScannedCharacter character = WearingPlus13(characterClass, 5);
            character.body[Helm].model = helm;
            EXPECT_EQ(Scan(character).complete, skipsTheHelm);
        }
        ScannedCharacter lowHelm = WearingPlus13(characterClass, 5);
        lowHelm.body[Helm].level = 8;
        EXPECT_EQ(Scan(lowHelm).complete, skipsTheHelm);
    }
}

// 0x50585f: una parte en 0xffff corta el primer lazo, antes de mirar los niveles: [0x743a178] en 0.
TEST_F(FullSetScanTest, AnEmptyPartIsNoSet)
{
    for (std::size_t part = Armor; part <= Boots; ++part)
    {
        SCOPED_TRACE(part);
        for (const std::uint8_t characterClass : {std::uint8_t{0}, MagicGladiator})
        {
            ScannedCharacter character = WearingPlus13(characterClass, 5);
            character.body[part].model = 0xffff;
            const FullSetScan scan = Scan(character);
            EXPECT_FALSE(scan.complete);
            EXPECT_EQ(scan.globals.dword743a178, 0);
            EXPECT_EQ(scan.globals.byte743a17c, 1);
        }
    }
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

// 0x5058ab..0x5058b4: el nivel es el byte & 0xf, y con signo menor que 9 corta. Lo que estaba de más
// arriba no cuenta: 0x19 es un 9 y 0xf8 un 8.
TEST_F(FullSetScanTest, ALevelBelowNineIsNoSetAndOnlyTheLowNibbleCounts)
{
    for (std::size_t part = 0; part <= Boots; ++part)
    {
        SCOPED_TRACE(part);
        ScannedCharacter character = WearingPlus13(0, 5);
        character.body[part].level = 8;
        const FullSetScan scan = Scan(character);
        EXPECT_FALSE(scan.complete);
        EXPECT_EQ(scan.globals.dword743a178, 0);
        EXPECT_EQ(scan.globals.byte743a17c, 1);
    }
    ScannedCharacter character = WearingPlus13(0, 5);
    character.body[Armor].level = 0x19;
    EXPECT_EQ(Scan(character).globals.dword743a178, 9);
    character.body[Armor].level = 0xf8;
    EXPECT_FALSE(Scan(character).complete);
}

// 0x505871..0x505893 y 0x5058b6..0x5058ce: el índice es (modelo con signo − 0x212) con el resto con signo de 512. El
// grupo no importa; uno de otro índice corta. Con modelos de menos de 0x212 el resto es negativo: 0x10 y 0x210 dan −2
// los dos, y 0x410 da 510, que no es −2 aunque esté 1.024 más arriba.
TEST_F(FullSetScanTest, TheIndexIsTheSignedRemainderOf512)
{
    for (std::size_t part = 0; part < Boots; ++part)
    {
        SCOPED_TRACE(part);
        ScannedCharacter character = WearingPlus13(0, 5);
        character.body[part].model = ItemModel(7, 6);
        EXPECT_FALSE(Scan(character).complete);
        // Cualquier grupo con el mismo índice.
        character.body[part].model = ItemModel(13, 5);
        EXPECT_TRUE(Scan(character).complete);
    }

    ScannedCharacter low = WearingPlus13(0, 0);
    for (std::size_t part = 0; part <= Boots; ++part)
    {
        low.body[part].model = part % 2 == 0 ? std::uint16_t{0x10} : std::uint16_t{0x210};
    }
    EXPECT_TRUE(Scan(low).complete);
    low.body[Armor].model = 0x410;
    EXPECT_FALSE(Scan(low).complete);
}

// 0x505902..0x505ba3: con la clase 3 y el conjunto abre el registro propio, mira su armadura (+0xc44) y lo
// cierra: [0x743a17c] queda en 1 solo con 0x100f, 0x1014, 0x1017 o 0x1020. El cierre que deja la cuenta
// en 0 saca 3.848 rand().
TEST_F(FullSetScanTest, TheMagicGladiatorKeepsTheBonusOnlyWithFourArmors)
{
    for (const std::uint16_t armor :
         {std::uint16_t{0x100f}, std::uint16_t{0x1014}, std::uint16_t{0x1017}, std::uint16_t{0x1020},
          std::uint16_t{0x1005}, std::uint16_t{0x0e0f}, std::uint16_t{0x200f}, std::uint16_t{0xffff}})
    {
        SCOPED_TRACE(armor);
        m_random = CrtRandom(Seed);
        m_own.equipment[OwnArmorSlot].id = armor;
        const bool keeps = armor == 0x100f || armor == 0x1014 || armor == 0x1017 || armor == 0x1020;
        // Lo que lleva puesto no importa: solo el registro.
        const FullSetScan scan = Scan(WearingPlus13(MagicGladiator, 9));
        EXPECT_TRUE(scan.complete);
        EXPECT_EQ(scan.globals.dword743a178, 13);
        EXPECT_EQ(scan.globals.byte743a17c, keeps ? 1 : 0);
        EXPECT_EQ(m_accesses, 0);
        EXPECT_EQ(m_random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());
    }
    // La clase 11 es una 3.
    m_own.equipment[OwnArmorSlot].id = 0x1005;
    EXPECT_EQ(Scan(WearingPlus13(MagicGladiator + 8, 9)).globals.byte743a17c, 0);
}

// Sin el conjunto, o con otra clase, no abre el registro: [0x743a17c] en 1 y ningún rand(). Con el registro
// ya abierto, el cierre no lo deja en 0 y tampoco saca.
TEST_F(FullSetScanTest, TheRecordOpensOnlyForAFullSetOfClassThree)
{
    m_own.equipment[OwnArmorSlot].id = 0x1005;
    ScannedCharacter broken = WearingPlus13(MagicGladiator, 9);
    broken.body[Boots].level = 7;
    EXPECT_EQ(Scan(broken).globals.byte743a17c, 1);
    EXPECT_EQ(Scan(WearingPlus13(2, 9)).globals.byte743a17c, 1);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());

    m_accesses = 1;
    EXPECT_EQ(Scan(WearingPlus13(MagicGladiator, 9)).globals.byte743a17c, 0);
    EXPECT_EQ(m_accesses, 1);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

// Lo que lee de un personaje: la clase y el modelo y el nivel de +0x218 a +0x298, de a 0x20.
TEST(FullSetScanCharacterTest, ItReadsTheClassAndTheFiveBodyParts)
{
    Client::Scenes::ActorState actor = Client::Tests::UnarmedPlayer(0x23);
    Client::GameLogic::CharacterAppearance appearance;
    appearance.characterClass = 0x23;
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        actor.body[part].model = static_cast<std::uint16_t>(0x1000 + part);
        actor.body[part].level = static_cast<std::uint8_t>(0x10 + part);
        appearance.bodyParts[part].model = static_cast<std::uint16_t>(0x2000 + part);
        appearance.bodyParts[part].level = static_cast<std::uint8_t>(0x20 + part);
    }

    const ScannedCharacter fromActor = Client::Scenes::ScannedCharacterOf(actor);
    const ScannedCharacter fromAppearance = Client::Scenes::ScannedCharacterOf(appearance);

    EXPECT_EQ(fromActor.characterClass, 0x23);
    EXPECT_EQ(fromAppearance.characterClass, 0x23);
    for (std::size_t part = 0; part < fromActor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(fromActor.body[part].model, actor.body[part].model);
        EXPECT_EQ(fromActor.body[part].level, actor.body[part].level);
        EXPECT_EQ(fromAppearance.body[part].model, appearance.bodyParts[part].model);
        EXPECT_EQ(fromAppearance.body[part].level, appearance.bodyParts[part].level);
    }
}

// 0x50c904..0x50c926 una vez por frame del original: deja lo del último en WorldRuntime::fullSetScan, aunque
// MuRet no supiera qué había, y la clase 3 con el conjunto abre y cierra el registro de WorldState en cada
// uno. Sin frames, nada.
TEST(FullSetScanDrawingTest, EachOriginalFrameScansTheDrawnCharacter)
{
    Client::Scenes::WorldState world;
    world.Runtime().fullSetScan = std::nullopt;
    world.OwnCharacter().equipment[OwnArmorSlot].id = 0x1005;
    CrtRandom random(Seed);

    Client::Scenes::ScanTheDrawnCharacter(world, WearingPlus13(MagicGladiator, 9), random, 0);
    EXPECT_FALSE(world.Runtime().fullSetScan.has_value());

    Client::Scenes::ScanTheDrawnCharacter(world, WearingPlus13(MagicGladiator, 9), random, 2);
    ASSERT_TRUE(world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(world.Runtime().fullSetScan->dword743a178, 13);
    EXPECT_EQ(world.Runtime().fullSetScan->byte743a17c, 0);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 2 * OwnRecordCloseRandoms).State());

    // El que viene después pisa los dos.
    Client::Scenes::ScanTheDrawnCharacter(world, Wearing(0, 9, {13, 13, 8, 13, 13}), random, 1);
    EXPECT_EQ(world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(world.Runtime().fullSetScan->byte743a17c, 1);
}
} // namespace
