#include "Render/Models/Cloth.h"
#include "UI/Dialogs.h"
#include "UI/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <ios>
#include <string>
#include <vector>

// Client::UI: el registro de paneles (CL8 hito 1; 06 § HUD y ventanas, "Los paneles"), contra el ASM de 0x659290,
// 0x659860, 0x6599a0, 0x65cae0, 0x657ab0, 0x657880 y 0x5bb8d0. Lo que se mira es cuántos rand() saca cada camino:
// solo las consultas del comercio (0x20) y de la tienda de NPC (0x10000) sacan uno.
namespace
{
using Client::Render::Models::CrtRandom;
using namespace Client::UI;

constexpr std::uint32_t Seed = 1;

// Cuántos rand() sacó random desde Seed (hasta 20.000).
int DrawsSince(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 20'000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

struct Tested
{
    CrtRandom random{Seed};
    DialogState dialogs{};
    PanelState state{};
    std::vector<std::int32_t> sounds{};
    PanelContext context{
        .random = random, .dialogs = dialogs, .map = 0, .sound = [this](std::int32_t id) { sounds.push_back(id); }};
    PanelRegistry panels{state, context};
};

TEST(PanelRegistryTest, OnlyTheTradeAndTheNpcShopQueriesDrawOneRandomEach)
{
    for (PanelId id = 1; id <= 0x40000000u; id <<= 1)
    {
        Tested tested;
        EXPECT_FALSE(tested.panels.IsOpen(id)) << std::hex << id;
        const int expected = id == TradePanel || id == NpcShopPanel ? 1 : 0;
        EXPECT_EQ(DrawsSince(tested.random), expected) << std::hex << id;
    }
}

TEST(PanelRegistryTest, AProtectedQueryInsideAnOpenAccessDoesNotDraw)
{
    Tested tested;
    tested.state.tradeDepth = 1;
    tested.state.trade = 1;
    EXPECT_TRUE(tested.panels.IsOpen(TradePanel));
    EXPECT_EQ(DrawsSince(tested.random), 0);
    EXPECT_EQ(tested.state.tradeDepth, 1);
}

TEST(PanelRegistryTest, EachIdReadsItsOwnState)
{
    Tested tested;
    PanelState& state = tested.state;
    state.overlayMode = 3;
    EXPECT_FALSE(tested.panels.IsOpen(MovePanel));
    state.overlayMode = MoveOverlayMode;
    EXPECT_TRUE(tested.panels.IsOpen(MovePanel));
    state.personalShop = 1;
    EXPECT_TRUE(tested.panels.IsOpen(PersonalShopPanel));
    EXPECT_TRUE(tested.panels.IsOpen(OtherShopPanel));
    state.eventNpc = 1;
    EXPECT_TRUE(tested.panels.IsOpen(EventNpcPanel));
    EXPECT_FALSE(tested.panels.IsOpen(SecondEventNpcPanel));
    state.eventNpcKind = 1;
    EXPECT_FALSE(tested.panels.IsOpen(EventNpcPanel));
    EXPECT_TRUE(tested.panels.IsOpen(SecondEventNpcPanel));
    state.delegated.siegeWeapon = 2;
    EXPECT_FALSE(tested.panels.IsOpen(SiegeWeaponPanel));
    EXPECT_TRUE(tested.panels.IsOpen(SecondSiegeWeaponPanel));
    state.delegated.trainer = -1;
    EXPECT_FALSE(tested.panels.IsOpen(TrainerPanel));
    state.delegated.quest = 2;
    EXPECT_TRUE(tested.panels.IsOpen(QuestPanel));
    EXPECT_FALSE(tested.panels.IsOpen(ClosedPanel));
    // 0x420cc0 compara 0x723911 con 2.
    state.delegated.pet = 1;
    EXPECT_FALSE(tested.panels.IsOpen(PetPanel));
    state.delegated.pet = 2;
    EXPECT_TRUE(tested.panels.IsOpen(PetPanel));
}

TEST(PanelRegistryTest, AnyPanelStopsAtTheFirstOpenOne)
{
    Tested closed;
    EXPECT_FALSE(closed.panels.IsOpen(AnyPanel));
    EXPECT_EQ(DrawsSince(closed.random), 2);

    // La party (4) está antes que el comercio (0x20): no llega a las consultas que sacan.
    Tested party;
    party.state.party = 1;
    EXPECT_TRUE(party.panels.IsOpen(AnyPanel));
    EXPECT_EQ(DrawsSince(party.random), 0);

    // El inventario está después de los dos.
    Tested inventory;
    inventory.state.inventory = 1;
    EXPECT_TRUE(inventory.panels.IsOpen(AnyPanel));
    EXPECT_EQ(DrawsSince(inventory.random), 2);
}

TEST(PanelRegistryTest, OpeningTheInventoryDrawsFourAndSoundsTwice)
{
    Tested tested;
    const auto opened = tested.panels.Open(InventoryPanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
    EXPECT_EQ(tested.state.inventory, 1);
    // CanOpen: 0x10000 y 0x20; la máscara de 0x657ab0: 0x20 y 0x10000.
    EXPECT_EQ(DrawsSince(tested.random), 4);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
}

TEST(PanelRegistryTest, ClosingTheInventoryDrawsTwoAndClearsItsGlobals)
{
    Tested tested;
    tested.state.inventory = 1;
    tested.state.field78abb04 = 5;
    tested.state.field78abb09 = 1;
    tested.state.repairMode = 2;
    const auto closed = tested.panels.Close(InventoryPanel);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_TRUE(closed.Value());
    EXPECT_EQ(tested.state.inventory, 0);
    EXPECT_EQ(tested.state.field78abb04, 0);
    EXPECT_EQ(tested.state.field78abb09, 0);
    EXPECT_EQ(tested.state.repairMode, 0);
    // 0x11d9e0 consulta el comercio y la tienda de NPC.
    EXPECT_EQ(DrawsSince(tested.random), 2);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
}

TEST(PanelRegistryTest, AnItemOnTheCursorKeepsTheInventoryOpen)
{
    Tested tested;
    tested.state.inventory = 1;
    tested.context.cursorItemCount = 1;
    const auto closed = tested.panels.Close(InventoryPanel);
    ASSERT_TRUE(closed.HasValue());
    EXPECT_TRUE(closed.Value());
    EXPECT_EQ(tested.state.inventory, 1);
    EXPECT_EQ(DrawsSince(tested.random), 0);
    EXPECT_EQ(tested.sounds.size(), 2u);
}

TEST(PanelRegistryTest, ClosingAClosedPanelDoesNothing)
{
    Tested tested;
    const auto closed = tested.panels.Close(InventoryPanel);
    ASSERT_TRUE(closed.HasValue());
    EXPECT_FALSE(closed.Value());
    EXPECT_TRUE(tested.sounds.empty());
}

TEST(PanelRegistryTest, OpeningAnOpenPanelDoesNotAsk)
{
    Tested tested;
    tested.state.inventory = 1;
    const auto opened = tested.panels.Open(InventoryPanel);
    ASSERT_TRUE(opened.HasValue());
    EXPECT_FALSE(opened.Value());
    EXPECT_EQ(DrawsSince(tested.random), 0);
    EXPECT_TRUE(tested.sounds.empty());
}

TEST(PanelRegistryTest, CanOpenNeedsNoDialogOrThePanelFriendlyOne)
{
    Tested tested;
    tested.dialogs.current = EscapeMenuDialog;
    EXPECT_FALSE(tested.panels.CanOpen(InventoryPanel));
    tested.dialogs.current = PanelFriendlyDialog;
    EXPECT_TRUE(tested.panels.CanOpen(InventoryPanel));
    tested.dialogs.current = 0;
    tested.state.field78abb00 = 3;
    EXPECT_FALSE(tested.panels.CanOpen(InventoryPanel));
    tested.state.field78abb00 = 0;
    tested.state.delegated.quest = 1;
    EXPECT_FALSE(tested.panels.CanOpen(InventoryPanel));
    tested.state.delegated.quest = 2;
    EXPECT_TRUE(tested.panels.CanOpen(InventoryPanel));
}

TEST(PanelRegistryTest, CanOpenStopsAtTheFirstBlockingPanel)
{
    // La tienda de NPC es la primera consulta: no llega a la del comercio.
    Tested tested;
    tested.state.npcShop = 1;
    EXPECT_FALSE(tested.panels.CanOpen(InventoryPanel));
    EXPECT_EQ(DrawsSince(tested.random), 1);
}

TEST(PanelRegistryTest, CanOpenKeepsItsPerIdRules)
{
    Tested tested;
    EXPECT_FALSE(tested.panels.CanOpen(PetPanel));
    tested.state.character = 1;
    EXPECT_TRUE(tested.panels.CanOpen(PetPanel));
    EXPECT_FALSE(tested.panels.CanOpen(PersonalShopPanel));
    tested.state.inventory = 1;
    EXPECT_TRUE(tested.panels.CanOpen(PersonalShopPanel));
    for (const std::int32_t map : {18, 23})
    {
        tested.context.map = map;
        EXPECT_FALSE(tested.panels.CanOpen(FriendsPanel)) << map;
        EXPECT_FALSE(tested.panels.CanOpen(CommandsPanel)) << map;
        EXPECT_TRUE(tested.panels.CanOpen(PartyPanel)) << map;
    }
    tested.context.map = 17;
    EXPECT_TRUE(tested.panels.CanOpen(FriendsPanel));
    tested.context.map = 24;
    EXPECT_TRUE(tested.panels.CanOpen(CommandsPanel));
}

TEST(PanelRegistryTest, TheCastleSiegeListBlocksWithABusyCursor)
{
    Tested tested;
    tested.state.delegated.castleSiegeList = 1;
    EXPECT_TRUE(tested.panels.CanOpen(InventoryPanel));
    tested.state.delegated.chaosBoxHasItem = true;
    EXPECT_FALSE(tested.panels.CanOpen(InventoryPanel));
}

TEST(PanelRegistryTest, OpenTakesThePendingDialogUnlessItIsThePanelFriendlyOne)
{
    Tested tested;
    tested.dialogs.pending = 0x77;
    ASSERT_TRUE(tested.panels.Open(MovePanel).HasValue());
    EXPECT_EQ(tested.dialogs.current, 0x77);
    EXPECT_EQ(tested.dialogs.pending, 0);
    EXPECT_EQ(tested.state.overlayMode, MoveOverlayMode);

    Tested friendly;
    friendly.dialogs.current = PanelFriendlyDialog;
    friendly.dialogs.pending = 0x77;
    ASSERT_TRUE(friendly.panels.Open(MovePanel).HasValue());
    EXPECT_EQ(friendly.dialogs.current, PanelFriendlyDialog);
    EXPECT_EQ(friendly.dialogs.pending, 0x77);
}

TEST(PanelRegistryTest, TheMoveWindowOpensWithoutClosingTheOthers)
{
    Tested tested;
    tested.state.inventory = 1;
    const auto opened = tested.panels.Open(MovePanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
    EXPECT_EQ(tested.state.inventory, 1);
    // Solo CanOpen.
    EXPECT_EQ(DrawsSince(tested.random), 2);
}

TEST(PanelRegistryTest, ALogoutOrderKeepsThePanelsShut)
{
    Tested tested;
    tested.context.logoutRequested = true;
    const auto opened = tested.panels.Open(InventoryPanel);
    ASSERT_TRUE(opened.HasValue());
    EXPECT_FALSE(opened.Value());
    EXPECT_EQ(tested.state.inventory, 0);
    EXPECT_TRUE(tested.sounds.empty());
}

TEST(PanelRegistryTest, TheInventoryKeepsTheCharacterOpen)
{
    Tested tested;
    tested.state.character = 1;
    tested.state.overlayMode = MoveOverlayMode;
    const auto opened = tested.panels.Open(InventoryPanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(tested.state.inventory, 1);
    EXPECT_EQ(tested.state.character, 1);
    EXPECT_EQ(tested.state.overlayMode, 0);
}

TEST(PanelRegistryTest, TheDarkLordsPetRequestIsACut)
{
    Tested tested;
    tested.context.heroClass = 4;
    const auto opened = tested.panels.Open(InventoryPanel);
    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find("0x65c039"), std::string::npos);
}

TEST(PanelRegistryTest, PanelsOfOtherWavesCutWhenTheyOpen)
{
    for (const PanelId id : {FriendsPanel, QuestPanel, GuildPanel, CharacterPanel})
    {
        Tested tested;
        const auto opened = tested.panels.Open(id);
        ASSERT_FALSE(opened.HasValue()) << std::hex << id;
        EXPECT_NE(opened.Error().find("0x6599a0"), std::string::npos) << opened.Error();
    }
}

// 0x659a82..0x659c44: la party cierra lo demás menos los amigos, pone la cuenta en 0 y pide la lista (el gancho) antes
// de prenderse, y suena. Los únicos rand() son los de las consultas del comercio y de la tienda, en CanOpen y en lo
// que cierra.
TEST(PanelRegistryTest, OpeningThePartyAsksForTheListBeforeItsByte)
{
    Tested tested;
    tested.state.overlayMode = MoveOverlayMode;
    std::vector<std::uint8_t> partyWhenAsked;
    tested.context.partyOpened = [&tested, &partyWhenAsked]() { partyWhenAsked.push_back(tested.state.party); };
    const auto opened = tested.panels.Open(PartyPanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
    EXPECT_EQ(partyWhenAsked, (std::vector<std::uint8_t>{0}));
    EXPECT_EQ(tested.state.party, 1);
    EXPECT_EQ(tested.state.overlayMode, 0) << "la ventana /move se cierra";
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
    EXPECT_TRUE(tested.panels.IsOpen(PartyPanel));
    EXPECT_EQ(tested.panels.WorldWidth(), OnePanelWorldWidth);

    // Abierta, Open no hace nada: ni pide la lista ni suena.
    tested.sounds.clear();
    const auto again = tested.panels.Open(PartyPanel);
    ASSERT_TRUE(again.HasValue());
    EXPECT_FALSE(again.Value());
    EXPECT_EQ(partyWhenAsked.size(), 1u);
    EXPECT_TRUE(tested.sounds.empty());

    // Close(4) va por CloseByMask y suena.
    const auto closed = tested.panels.Close(PartyPanel);
    ASSERT_TRUE(closed.HasValue());
    EXPECT_TRUE(closed.Value());
    EXPECT_EQ(tested.state.party, 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
    EXPECT_EQ(DrawsSince(tested.random), 4) << "las consultas del comercio y de la tienda de CanOpen y de CloseByMask";
}

// Sin el cable (el gancho), abrir la party corta donde el original arma el C1 03 42, sin prenderla.
TEST(PanelRegistryTest, OpeningThePartyWithoutTheListRequestCuts)
{
    Tested tested;
    const auto opened = tested.panels.Open(PartyPanel);
    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find("0x659aa0"), std::string::npos) << opened.Error();
    EXPECT_EQ(tested.state.party, 0);
    EXPECT_TRUE(tested.sounds.empty());
}

// 0x65a91c..0x65a934: los comandos prenden +4 de [0x6e592c]; en Chaos Castle CanOpen no los deja.
TEST(PanelRegistryTest, OpeningTheCommandsSetsTheirByte)
{
    Tested tested;
    tested.state.party = 1;
    const auto opened = tested.panels.Open(CommandsPanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
    EXPECT_EQ(tested.state.delegated.commands, 1);
    EXPECT_EQ(tested.state.party, 0) << "cierra lo demás";
    EXPECT_TRUE(tested.panels.IsOpen(CommandsPanel));
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));

    Tested chaosCastle;
    chaosCastle.context.map = 20;
    const auto refused = chaosCastle.panels.Open(CommandsPanel);
    ASSERT_TRUE(refused.HasValue());
    EXPECT_FALSE(refused.Value());
    EXPECT_EQ(chaosCastle.state.delegated.commands, 0);
}

// 0x65897a..0x658999: cerrar los comandos (por Close, Esc o CloseAll) deja el objeto como lo deja la entrada al juego.
TEST(PanelRegistryTest, ClosingTheCommandsResetsTheirObject)
{
    const auto arrange = [](Tested& tested)
    {
        DelegatedPanels& delegated = tested.state.delegated;
        delegated.commands = 1;
        delegated.commandsWindow.target = 7;
        delegated.commandsWindow.chosen = 4;
        delegated.commandsWindow.states[4] = 0;
        delegated.commandsWindow.states[11] = 0xfe;
    };
    const auto expectReset = [](const Tested& tested)
    {
        const DelegatedPanels& delegated = tested.state.delegated;
        EXPECT_EQ(delegated.commands, 0);
        EXPECT_EQ(delegated.commandsWindow.target, -1);
        EXPECT_EQ(delegated.commandsWindow.chosen, -1);
        for (const std::uint8_t state : delegated.commandsWindow.states)
            EXPECT_EQ(state, CommandReady);
    };
    Tested closed;
    arrange(closed);
    ASSERT_TRUE(closed.panels.Close(CommandsPanel).HasValue());
    expectReset(closed);
    EXPECT_EQ(closed.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));

    Tested escaped;
    arrange(escaped);
    const auto key = escaped.panels.PanelKey(0x1b);
    ASSERT_TRUE(key.HasValue());
    EXPECT_TRUE(key.Value());
    expectReset(escaped);

    Tested all;
    arrange(all);
    ASSERT_TRUE(all.panels.CloseAll().HasValue());
    expectReset(all);
}

TEST(PanelRegistryTest, CloseByMaskQueriesOnlyWhatIsInTheMask)
{
    Tested all;
    const auto closedAll = all.panels.CloseByMask(AllPanels);
    ASSERT_TRUE(closedAll.HasValue());
    EXPECT_TRUE(closedAll.Value());
    EXPECT_EQ(DrawsSince(all.random), 2);

    Tested trade;
    ASSERT_TRUE(trade.panels.CloseByMask(TradePanel).HasValue());
    EXPECT_EQ(DrawsSince(trade.random), 1);

    Tested none;
    ASSERT_TRUE(none.panels.CloseByMask(~(TradePanel | NpcShopPanel)).HasValue());
    EXPECT_EQ(DrawsSince(none.random), 0);
}

TEST(PanelRegistryTest, CloseByMaskClosesTheOwnPanels)
{
    Tested tested;
    tested.state.character = 1;
    tested.state.party = 1;
    tested.state.overlayMode = MoveOverlayMode;
    tested.state.inventory = 1;
    tested.state.field78abb00 = 2;
    const auto closed = tested.panels.CloseByMask(AllPanels);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_EQ(tested.state.character, 0);
    EXPECT_EQ(tested.state.party, 0);
    EXPECT_EQ(tested.state.overlayMode, 0);
    EXPECT_EQ(tested.state.inventory, 0);
    EXPECT_EQ(tested.state.field78abb00, 0);
    EXPECT_TRUE(tested.sounds.empty());
}

TEST(PanelRegistryTest, CloseByMaskFollowsTheFixedOrder)
{
    // El personaje es el primero: con el comercio abierto (y sin dueño), corta en el comercio después de cerrarlo.
    Tested tested;
    tested.state.character = 1;
    tested.state.trade = 1;
    tested.state.inventory = 1;
    const auto closed = tested.panels.CloseByMask(AllPanels);
    ASSERT_FALSE(closed.HasValue());
    EXPECT_NE(closed.Error().find("0x657ab0(0x20)"), std::string::npos) << closed.Error();
    EXPECT_EQ(tested.state.character, 0);
    EXPECT_EQ(tested.state.inventory, 1);
}

TEST(PanelRegistryTest, EscapeWithNothingOpenDrawsTwoAndClosesNothing)
{
    Tested tested;
    const auto key = tested.panels.PanelKey(0x1b);
    ASSERT_TRUE(key.HasValue());
    EXPECT_FALSE(key.Value());
    EXPECT_EQ(DrawsSince(tested.random), 2);
}

TEST(PanelRegistryTest, EscapeClosesTheLowestOpenBit)
{
    Tested tested;
    tested.state.overlayMode = MoveOverlayMode;
    tested.state.inventory = 1;
    const auto key = tested.panels.PanelKey(0x1b);
    ASSERT_TRUE(key.HasValue()) << key.Error();
    EXPECT_TRUE(key.Value());
    EXPECT_EQ(tested.state.overlayMode, 0);
    EXPECT_EQ(tested.state.inventory, 1);
    // Se queda en el 2: no llega al comercio.
    EXPECT_EQ(DrawsSince(tested.random), 0);
    EXPECT_EQ(tested.sounds.size(), 2u);
}

TEST(PanelRegistryTest, EscapeWithOnlyTheInventoryDrawsFour)
{
    Tested tested;
    tested.state.inventory = 1;
    const auto key = tested.panels.PanelKey(0x1b);
    ASSERT_TRUE(key.HasValue()) << key.Error();
    EXPECT_TRUE(key.Value());
    EXPECT_EQ(tested.state.inventory, 0);
    EXPECT_EQ(DrawsSince(tested.random), 4);
}

TEST(PanelRegistryTest, OtherKeysGoOnlyToThePanelDialog)
{
    Tested tested;
    tested.state.inventory = 1;
    const auto key = tested.panels.PanelKey(0x0d);
    ASSERT_TRUE(key.HasValue());
    EXPECT_FALSE(key.Value());
    EXPECT_EQ(tested.state.inventory, 1);

    tested.state.delegated.panelDialog = 1;
    EXPECT_FALSE(tested.panels.PanelKey(0x1b).HasValue());
}

TEST(PanelRegistryTest, CloseAllWithNothingOpenDrawsTwo)
{
    Tested tested;
    ASSERT_TRUE(tested.panels.CloseAll().HasValue());
    EXPECT_EQ(DrawsSince(tested.random), 2);
}

TEST(PanelRegistryTest, TheObjectPanelsStepDoesNotDraw)
{
    Tested tested;
    ASSERT_TRUE(tested.panels.StepTheObjectPanels().HasValue());
    EXPECT_EQ(DrawsSince(tested.random), 0);
    tested.state.delegated.guild = 1;
    EXPECT_FALSE(tested.panels.StepTheObjectPanels().HasValue());
}

TEST(PanelRegistryTest, TheWorldIsFullWidthWithNothingOpenAndDrawsNothing)
{
    Tested tested;
    EXPECT_EQ(tested.panels.WorldWidth(), FullWorldWidth);
    EXPECT_EQ(DrawsSince(tested.random), 0);
}

TEST(PanelRegistryTest, TheInventoryTakesOnePanelAndDrawsTwo)
{
    Tested tested;
    tested.state.inventory = 1;
    EXPECT_EQ(tested.panels.WorldWidth(), OnePanelWorldWidth);
    EXPECT_EQ(DrawsSince(tested.random), 2);
}

TEST(PanelRegistryTest, TheInventoryWithTheCharacterTakesTwoPanels)
{
    Tested tested;
    tested.state.inventory = 1;
    tested.state.character = 1;
    EXPECT_EQ(tested.panels.WorldWidth(), TwoPanelWorldWidth);
    EXPECT_EQ(DrawsSince(tested.random), 0);
}

TEST(PanelRegistryTest, ThePairsThatTakeTwoPanels)
{
    Tested quest;
    quest.state.character = 1;
    quest.state.delegated.quest = 2;
    EXPECT_EQ(quest.panels.WorldWidth(), TwoPanelWorldWidth);

    Tested trainer;
    trainer.state.inventory = 1;
    trainer.state.delegated.trainer = 1;
    EXPECT_EQ(trainer.panels.WorldWidth(), TwoPanelWorldWidth);

    Tested list;
    list.state.inventory = 1;
    list.state.delegated.castleSiegeList = 1;
    list.state.delegated.castleSiegeListMode = 3;
    EXPECT_EQ(list.panels.WorldWidth(), OnePanelWorldWidth);
    list.context.heroSiegeMark = true;
    EXPECT_EQ(list.panels.WorldWidth(), TwoPanelWorldWidth);
}

TEST(PanelRegistryTest, OnePanelOnTheRight)
{
    for (const PanelId id : {CharacterPanel, PartyPanel, MovePanel})
    {
        Tested tested;
        tested.state.character = id == CharacterPanel ? 1 : 0;
        tested.state.party = id == PartyPanel ? 1 : 0;
        tested.state.overlayMode = id == MovePanel ? MoveOverlayMode : 0;
        const std::int32_t expected = id == MovePanel ? FullWorldWidth : OnePanelWorldWidth;
        EXPECT_EQ(tested.panels.WorldWidth(), expected) << std::hex << id;
    }
    Tested chips;
    chips.state.field78abb00 = 1;
    EXPECT_EQ(chips.panels.WorldWidth(), OnePanelWorldWidth);
}

TEST(PanelRegistryTest, TheSiegeWeaponAndTheGateTakeOnePanel)
{
    Tested weapon;
    weapon.state.inventory = 1;
    weapon.state.character = 1;
    weapon.state.delegated.siegeWeapon = 1;
    EXPECT_EQ(weapon.panels.WorldWidth(), OnePanelWorldWidth);

    Tested gate;
    gate.state.delegated.siegeGate = 1;
    EXPECT_EQ(gate.panels.WorldWidth(), OnePanelWorldWidth);
}
} // namespace
