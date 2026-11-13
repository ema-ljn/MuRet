#include "Scenes/SocialEvents.h"

#include "App/WindowState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Network/Packet.h"
#include "Protocol/PersonalShop.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/LoginModels.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialState.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "World/GameCamera.h"

#include "support/ItemFiles.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Las tiendas personales que llegan (CL9 hito 7, fase 3): los lectores del 3F y los carteles del mapa de 0x7245c8
// (0x4511f0, 0x451430, 0x451630, 0x451810, 0x451fe0, 0x4516d0 y 0x451720), contra el ASM de 0x4dae80, 0x4db700,
// 0x4dafe0 y los casos 3, 8 y 0x12 de 0x4e2b10. El GameServer 0.97 no tiene el 3F: los paquetes salen del cliente.
// Nombres, títulos y textos inventados; cada caso en Lorencia y en Devias.
namespace
{
using Client::Scenes::WorldState;
namespace Shop = Client::Protocol::PersonalShop;

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::size_t Hero = 0;
constexpr std::size_t Seller = 7;
constexpr std::size_t Buyer = 9;
constexpr std::size_t Monster = 12;
constexpr std::uint16_t HeroKey = 0x0101;
constexpr std::uint16_t SellerKey = 0x0234;
constexpr std::uint16_t BuyerKey = 0x0345;
constexpr std::uint16_t MonsterKey = 0x0456;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

void Append(std::vector<std::byte>& bytes, std::string_view text, std::size_t size)
{
    for (std::size_t at = 0; at < size; ++at)
        bytes.push_back(at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0});
}

Client::Network::Packet PacketOf(std::vector<std::byte> bytes)
{
    if (bytes[0] == std::byte{0xc1})
        bytes[1] = static_cast<std::byte>(bytes.size());
    else
        bytes[2] = static_cast<std::byte>(bytes.size());
    auto packet = Client::Network::Packet::FromBytes(std::move(bytes));
    EXPECT_TRUE(packet.HasValue());
    return std::move(packet).Value();
}

class PersonalShopsTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    PersonalShopsTest()
    {
        WorldState& world = *m_world;
        world.Runtime().map = GetParam();
        world.Runtime().hero = Hero;
        Place(Hero, HeroKey, "Tejon", 1);
        Place(Seller, SellerKey, "Brezal", 1);
        Place(Buyer, BuyerKey, "Cardo", 1);
        Place(Monster, MonsterKey, "Lobo", 2);
    }

    void Place(std::size_t index, std::uint16_t key, std::string_view name, std::uint8_t kind)
    {
        Client::Scenes::ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.word1f2 = 0xffff;
        actor.key = key;
        actor.byte88 = kind;
        for (std::size_t at = 0; at < name.size(); ++at)
            actor.name[at] = static_cast<std::byte>(name[at]);
    }

    Client::Scenes::PersonalShopState& Shops()
    {
        return m_world->Runtime().social.personalShop;
    }

    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *m_world,
                .selection = m_selection,
                .dataRoot = {},
                .cloth = m_cloth,
                .camera = m_camera,
                .cameraInput = m_input,
                .models = m_models,
                .items = &m_items,
                .log = m_log,
                .textMeasure = &m_letters,
                .texts = &m_texts};
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_input;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Scenes::SingleModelLibrary m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Data::Tables::ItemTable m_items = Client::Tests::ItemTableOf({});
    Client::Data::Text::TextTable m_texts =
        Client::Tests::MakeTextTable({{1122, "%s abrio su tienda"}, {1126, "la tienda se cerro"}});
    LetterMeasure m_letters;
};

// 0x4dae80: el 3F 00 llega en C2 (el subcódigo en pkt[4]); la cuenta en pkt[5] y las entradas de 0x26 bytes, la clave
// en big-endian y 36 bytes de título. Sin las entradas que dice, TooShort.
TEST_P(PersonalShopsTest, TheTitlesListReadsItsEntries)
{
    std::vector<std::byte> bytes = Bytes({0xc2, 0x00, 0x00, 0x3f, 0x00, 0x02});
    Append(bytes, "\x02\x34", 2);
    Append(bytes, "Espadas baratas", Shop::TitleSize);
    Append(bytes, "\x03\x45", 2);
    Append(bytes, std::string(Shop::TitleSize, 'x'), Shop::TitleSize);
    const auto read = Shop::ReadShopTitles(PacketOf(bytes));
    ASSERT_TRUE(read.HasValue());
    ASSERT_EQ(read.Value().entries.size(), 2u);
    EXPECT_EQ(read.Value().entries[0].key, SellerKey);
    EXPECT_EQ(read.Value().entries[0].title, "Espadas baratas");
    EXPECT_EQ(read.Value().entries[1].key, BuyerKey);
    EXPECT_EQ(read.Value().entries[1].title, std::string(Shop::TitleSize, 'x'));

    bytes.resize(bytes.size() - 1);
    EXPECT_FALSE(Shop::ReadShopTitles(PacketOf(bytes)).HasValue());
}

// Las listas (fase 4): 0x4db890 lee el resultado en pkt[5], la clave en pkt[6..7] (BE), el título en pkt[0x12..0x35],
// la cuenta en pkt[0x36] y las entradas de 0xc bytes desde pkt[0x37] (el lugar, el ítem y el precio LE con signo); el
// 0x13 (0x4dbb40) lo mismo sin la clave ni el título. 0x4db120 lee pkt[4] y pkt[5]; 0x4dbcb0, pkt[4] y con 1 el ítem de
// pkt[7..0xd] y el lugar de pkt[0xe]. Sin el resultado 1, las listas solo leen pkt[5].
TEST_P(PersonalShopsTest, TheItemListsReadTheirEntries)
{
    std::vector<std::byte> bytes = Bytes({0xc2, 0x00, 0x00, 0x3f, 0x05, 0x01, 0x02, 0x34});
    Append(bytes, "", 0x12 - 8);
    Append(bytes, "Cosas baratas", Shop::TitleSize);
    bytes.push_back(std::byte{2});
    for (const auto& [slot, price] : {std::pair<int, std::uint32_t>{0x55, 1500}, {0x4f, 0xfffffffe}})
    {
        bytes.push_back(static_cast<std::byte>(slot));
        Append(bytes, "\x03\x08\x14", 7);
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((price >> shift) & 0xff));
    }
    const auto list = Shop::ReadShopItemList(PacketOf(bytes));
    ASSERT_TRUE(list.HasValue());
    EXPECT_EQ(list.Value().result, 1);
    EXPECT_EQ(list.Value().key, SellerKey);
    EXPECT_EQ(list.Value().title[0], std::byte{'C'});
    ASSERT_EQ(list.Value().entries.size(), 2u);
    EXPECT_EQ(list.Value().entries[0].slot, 0x55);
    EXPECT_EQ(list.Value().entries[0].item[1], std::byte{0x08});
    EXPECT_EQ(list.Value().entries[0].price, 1500);
    EXPECT_EQ(list.Value().entries[1].slot, 0x4f);
    EXPECT_EQ(list.Value().entries[1].price, -2);
    bytes[4] = std::byte{0x13};
    const auto refresh = Shop::ReadShopRefresh(PacketOf(bytes));
    ASSERT_TRUE(refresh.HasValue());
    EXPECT_EQ(refresh.Value().entries.size(), 2u);
    bytes.resize(bytes.size() - 1);
    EXPECT_FALSE(Shop::ReadShopItemList(PacketOf(bytes)).HasValue());
    const auto refused = Shop::ReadShopItemList(PacketOf(Bytes({0xc2, 0x00, 0x00, 0x3f, 0x05, 0x03})));
    ASSERT_TRUE(refused.HasValue());
    EXPECT_EQ(refused.Value().result, 3);

    const auto price = Shop::ReadPriceResult(PacketOf(Bytes({0xc1, 0x00, 0x3f, 0x01, 0x00, 0x4d})));
    ASSERT_TRUE(price.HasValue());
    EXPECT_EQ(price.Value().result, 0);
    EXPECT_EQ(price.Value().slot, 0x4d);
    const auto bought = Shop::ReadShopPurchase(
        PacketOf(Bytes({0xc1, 0x00, 0x3f, 0x06, 0x01, 0x02, 0x34, 0x03, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x11})));
    ASSERT_TRUE(bought.HasValue());
    EXPECT_EQ(bought.Value().result, 1);
    EXPECT_EQ(bought.Value().item[0], std::byte{0x03});
    EXPECT_EQ(bought.Value().item[2], std::byte{0x14});
    EXPECT_EQ(bought.Value().slot, 0x11);
    const auto failed = Shop::ReadShopPurchase(PacketOf(Bytes({0xc1, 0x00, 0x3f, 0x06, 0x07})));
    ASSERT_TRUE(failed.HasValue());
    EXPECT_EQ(failed.Value().result, 7);
}

// 0x4dae80 y 0x4511f0: un cartel por jugador de la clave (el monstruo, +0x88 en 2, solo va al log; una clave sin nadie,
// nada), con el nombre de +0x1d9; el DC queda en la normal de las líneas (0x4531f7). 0x451810 lo ve; 0x451770 también,
// a la vista.
TEST_P(PersonalShopsTest, TheTitlesGoToThePlayersOfTheirKeys)
{
    m_world->Runtime().hud.dcFont = Client::Render::Text::FontKind::BigBold;
    Client::Scenes::ApplyShopTitles(
        Shop::ShopTitles{
            {{SellerKey, "Espadas baratas"}, {MonsterKey, "Pieles"}, {0x0777, "Nadie"}, {BuyerKey, "Compro joyas"}}},
        *m_world);
    ASSERT_EQ(Shops().titles.size(), 2u);
    EXPECT_EQ(Shops().titles.at(Seller).key, SellerKey);
    EXPECT_EQ(Shops().titles.at(Seller).name, "Brezal");
    EXPECT_EQ(Shops().titles.at(Seller).title, "Espadas baratas");
    EXPECT_EQ(Shops().titles.at(Buyer).title, "Compro joyas");
    EXPECT_EQ(m_world->Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
    EXPECT_TRUE(Client::Scenes::HasShopTitle(Shops(), Seller));
    EXPECT_FALSE(Client::Scenes::HasShopTitle(Shops(), Monster));
    EXPECT_TRUE(Client::Scenes::ShopTitleShown(Shops(), Seller));

    // Otro 3F 00 con la misma clave cambia el título; 0x451430 lo saca.
    Client::Scenes::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Escudos"}}}, *m_world);
    EXPECT_EQ(Shops().titles.at(Seller).title, "Escudos");
    Client::Scenes::RemoveShopTitle(Shops(), Seller);
    EXPECT_FALSE(Client::Scenes::HasShopTitle(Shops(), Seller));
}

// Abrir la propia y ver la de otro. 0x4db700: con 1, 0x78aba50 en 1 y el cartel del héroe con el título propio
// (0x78aba64); con otro resultado, nada. 0x4dafe0: el cartel de otro solo con su nombre (strncmp de 10). 0x4e4ea5:
// con 1, la tienda del héroe se apaga y su cartel sale.
TEST_P(PersonalShopsTest, TheOwnShopOpensAndAnotherShopShowsItsTitle)
{
    Shops().ownTitle = "Pociones";
    Client::Scenes::ApplyShopCreated(Shop::ShopCreated{0}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 0);
    EXPECT_TRUE(Shops().titles.empty());
    Client::Scenes::ApplyShopCreated(Shop::ShopCreated{1}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 1);
    ASSERT_TRUE(Client::Scenes::HasShopTitle(Shops(), Hero));
    EXPECT_EQ(Shops().titles.at(Hero).title, "Pociones");
    EXPECT_EQ(Shops().titles.at(Hero).key, HeroKey);

    Shop::ShopTitle other{.key = SellerKey, .title = "Arcos", .name = {}};
    const std::string_view wrong = "Brezil";
    for (std::size_t at = 0; at < wrong.size(); ++at)
        other.name[at] = static_cast<std::byte>(wrong[at]);
    Client::Scenes::ApplyOtherShopTitle(other, *m_world);
    EXPECT_FALSE(Client::Scenes::HasShopTitle(Shops(), Seller));
    other.name[4] = static_cast<std::byte>('a');
    other.name[5] = static_cast<std::byte>('l');
    Client::Scenes::ApplyOtherShopTitle(other, *m_world);
    ASSERT_TRUE(Client::Scenes::HasShopTitle(Shops(), Seller));
    EXPECT_EQ(Shops().titles.at(Seller).title, "Arcos");

    Client::Scenes::ApplyShopClosed(Shop::ShopClosed{.result = 0, .key = HeroKey}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 1);
    Client::Scenes::ApplyShopClosed(Shop::ShopClosed{.result = 1, .key = HeroKey}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 0);
    EXPECT_FALSE(Client::Scenes::HasShopTitle(Shops(), Hero));
    EXPECT_TRUE(Client::Scenes::HasShopTitle(Shops(), Seller));
}

// 0x4511f0 con otra clave para el mismo personaje: solo el log. 0x451fe0: con la clave cambiada o sin ser jugador,
// el cartel sale. 0x4ca7f0: el que sale del viewport se lleva el suyo.
TEST_P(PersonalShopsTest, StaleTitlesLeave)
{
    Client::Scenes::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos"}, {BuyerKey, "Joyas"}}}, *m_world);
    Client::Scenes::AddShopTitle(*m_world, 0x0999, Seller, "Otra");
    EXPECT_EQ(Shops().titles.at(Seller).title, "Arcos");

    Client::Scenes::CheckTheShopKeys(*m_world);
    EXPECT_EQ(Shops().titles.size(), 2u);
    m_world->Actors()[Seller].key = 0x0998;
    m_world->Actors()[Buyer].byte88 = 2;
    Client::Scenes::CheckTheShopKeys(*m_world);
    EXPECT_TRUE(Shops().titles.empty());

    m_world->Actors()[Buyer].byte88 = 1;
    Client::Scenes::ApplyShopTitles(Shop::ShopTitles{{{BuyerKey, "Joyas"}}}, *m_world);
    ASSERT_TRUE(Client::Scenes::HasShopTitle(Shops(), Buyer));
    Client::Scenes::ApplyViewportLeave(Client::Protocol::Viewport::Leave{{BuyerKey}}, *m_world);
    EXPECT_FALSE(Client::Scenes::HasShopTitle(Shops(), Buyer));
}

// 0x451590, detrás de cada 0x510f70 de la clave del jugador (la carga del mapa, el F3 04 y el 1C): solo queda el cartel
// con la clave del héroe publicado.
TEST_P(PersonalShopsTest, ALoadKeepsOnlyTheHeroTitle)
{
    Shops().ownTitle = "Pociones";
    Client::Scenes::ApplyShopCreated(Shop::ShopCreated{1}, *m_world);
    Client::Scenes::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos"}, {BuyerKey, "Joyas"}}}, *m_world);
    ASSERT_EQ(Shops().titles.size(), 3u);
    Client::Scenes::KeepTheHeroShopTitle(*m_world);
    ASSERT_EQ(Shops().titles.size(), 1u);
    EXPECT_TRUE(Client::Scenes::HasShopTitle(Shops(), Hero));
}

// 0x5576a0..0x5576c9: con el globo de su dueño a la vista, el cartel se esconde (0x451720) y deja de contar para el
// nombre bajo el mouse (0x451770); sin el globo, vuelve (0x4516d0).
TEST_P(PersonalShopsTest, ABubbleHidesTheTitle)
{
    Client::Scenes::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos"}}}, *m_world);
    Client::Scenes::ChatBubble& bubble = m_world->Runtime().chat.bubbles[0];
    bubble.owner = Seller;
    bubble.time1 = 5;
    const Client::Scenes::ChatBubbleStepInput input{.mouseX = 0, .mouseY = 0, .screen = {640, 480}};
    ASSERT_TRUE(Client::Scenes::StepTheChatBubbles(*m_world, input).HasValue());
    EXPECT_FALSE(Client::Scenes::ShopTitleShown(Shops(), Seller));
    EXPECT_TRUE(Client::Scenes::HasShopTitle(Shops(), Seller));
    bubble.time1 = 1;
    ASSERT_TRUE(Client::Scenes::StepTheChatBubbles(*m_world, input).HasValue());
    EXPECT_TRUE(Client::Scenes::ShopTitleShown(Shops(), Seller));
}

// 0x556dd0: el globo nuevo de un dueño con cartel (0x451810) lleva su título corto (0x4518b0): con más de 14 bytes,
// los primeros 12 y ".."; sin cartel, el título del globo queda vacío.
TEST_P(PersonalShopsTest, TheBubbleOfAShopOwnerTakesTheShortTitle)
{
    Client::Scenes::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos y flechas finas"}}}, *m_world);
    const Client::Scenes::ChatBubbleMeasure measure{.measure = m_letters, .screen = Client::App::PixelSize{640, 480}};
    const auto added = Client::Scenes::AddChatBubble(*m_world, measure, "Brezal", "hola", Seller,
                                                     Client::Scenes::ChatBubbleMode::Chat);
    ASSERT_TRUE(added.HasValue()) << added.Error();
    const auto bubbleOf = [&](std::size_t owner) -> const Client::Scenes::ChatBubble*
    {
        for (const Client::Scenes::ChatBubble& bubble : m_world->Runtime().chat.bubbles)
        {
            if (bubble.owner == owner)
                return &bubble;
        }
        return nullptr;
    };
    ASSERT_NE(bubbleOf(Seller), nullptr);
    EXPECT_EQ(bubbleOf(Seller)->shopTitle, "Arcos y flec..");

    const auto other =
        Client::Scenes::AddChatBubble(*m_world, measure, "Cardo", "hola", Buyer, Client::Scenes::ChatBubbleMode::Chat);
    ASSERT_TRUE(other.HasValue()) << other.Error();
    ASSERT_NE(bubbleOf(Buyer), nullptr);
    EXPECT_EQ(bubbleOf(Buyer)->shopTitle, "");
}

// 0x4e4f5d (3F 08): sprintf(Text[1122], nombre) con el tipo 1. 0x4e4fcc (3F 12): con 0x6c931c en 2, los cierres y el
// texto 1126 con el tipo 2; si no, nada.
TEST_P(PersonalShopsTest, TheNoticesGoToTheMessages)
{
    Shop::ShopNotice notice{};
    const std::string_view name = "Brezal";
    for (std::size_t at = 0; at < name.size(); ++at)
        notice.name[at] = static_cast<std::byte>(name[at]);
    ASSERT_TRUE(Client::Scenes::ApplyShopNotice(notice, Resources()).HasValue());
    const auto& entries = m_world->Runtime().chat.log.entries;
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries.front().text, "Brezal abrio su tienda");
    EXPECT_EQ(entries.front().type, Client::UI::SystemMessage);

    const std::size_t before = entries.size();
    ASSERT_TRUE(Client::Scenes::ApplyShopOwnerLeft(Resources(), {}).HasValue());
    EXPECT_EQ(entries.size(), before);
    Shops().mode = 2;
    ASSERT_TRUE(Client::Scenes::ApplyShopOwnerLeft(Resources(), {}).HasValue());
    EXPECT_EQ(entries.front().text, "la tienda se cerro");
    EXPECT_EQ(entries.front().type, Client::UI::ErrorMessage);
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, PersonalShopsTest, ::testing::Values(Lorencia, Devias));
} // namespace
