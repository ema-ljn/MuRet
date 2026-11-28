#include "Social/SocialEvents.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Network/Packet.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "FrontEnd/LoginModels.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "FrontEnd/SelectionState.h"
#include "Social/SocialState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Camera/GameCamera.h"

#include "support/ItemFiles.h"
#include "support/MinimumSize.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportHandlers.h"
#include "Chat/ChatRuntime.h"

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

// The incoming personal shops (CL9 milestone 7, phase 3): the 3F readers and the signs of 0x7245c8's map (0x4511f0,
// 0x451430, 0x451630, 0x451810, 0x451fe0, 0x4516d0 and 0x451720), against the ASM of 0x4dae80, 0x4db700, 0x4dafe0 and
// cases 3, 8 and 0x12 of 0x4e2b10. GameServer 0.97 has no 3F: the packets come from the client. Names, titles and
// texts are made up; each case in Lorencia and in Devias.
namespace
{
using Client::Kernel::WorldState;
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
        Client::Kernel::ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.word1f2 = 0xffff;
        actor.key = key;
        actor.byte88 = kind;
        for (std::size_t at = 0; at < name.size(); ++at)
            actor.name[at] = static_cast<std::byte>(name[at]);
    }

    Client::Social::PersonalShopState& Shops()
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
    Client::FrontEnd::SelectionState m_selection{*m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_input;
    Client::Models::ClothWorld m_cloth;
    Client::FrontEnd::SingleModelLibrary m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tables::ItemTable m_items = Client::Tests::ItemTableOf({});
    Client::Tables::TextTable m_texts =
        Client::Tests::MakeTextTable({{1122, "%s abrio su tienda"}, {1126, "la tienda se cerro"}});
    LetterMeasure m_letters;
};

// 0x4dae80: the 3F 00 arrives in C2 (the subcode in pkt[4]); the count in pkt[5] and the 0x26-byte entries, the key in
// big-endian and 36 bytes of title. Without the entries it states, TooShort.
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

// The lists (phase 4): 0x4db890 reads the result in pkt[5], the key in pkt[6..7] (BE), the title in pkt[0x12..0x35],
// the count in pkt[0x36] and the 0xc-byte entries from pkt[0x37] (the slot, the item and the signed LE price); the 0x13
// (0x4dbb40) the same without the key or the title. 0x4db120 reads pkt[4] and pkt[5]; 0x4dbcb0, pkt[4] and with 1 the
// item from pkt[7..0xd] and the slot from pkt[0xe]. Without result 1, the lists only read pkt[5].
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

// 0x4dae80 and 0x4511f0: a sign per player of the key (the monster, +0x88 at 2, only goes to the log; a key with
// nobody, nothing), with the name from +0x1d9; the DC is left on the lines' Normal font (0x4531f7). 0x451810 sees it;
// so does 0x451770, on view.
TEST_P(PersonalShopsTest, TheTitlesGoToThePlayersOfTheirKeys)
{
    m_world->Runtime().hud.dcFont = Client::Render::Text::FontKind::BigBold;
    Client::Social::ApplyShopTitles(
        Shop::ShopTitles{
            {{SellerKey, "Espadas baratas"}, {MonsterKey, "Pieles"}, {0x0777, "Nadie"}, {BuyerKey, "Compro joyas"}}},
        *m_world);
    ASSERT_EQ(Shops().titles.size(), 2u);
    EXPECT_EQ(Shops().titles.at(Seller).key, SellerKey);
    EXPECT_EQ(Shops().titles.at(Seller).name, "Brezal");
    EXPECT_EQ(Shops().titles.at(Seller).title, "Espadas baratas");
    EXPECT_EQ(Shops().titles.at(Buyer).title, "Compro joyas");
    EXPECT_EQ(m_world->Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
    EXPECT_TRUE(Client::Social::HasShopTitle(Shops(), Seller));
    EXPECT_FALSE(Client::Social::HasShopTitle(Shops(), Monster));
    EXPECT_TRUE(Client::Social::ShopTitleShown(Shops(), Seller));

    // Another 3F 00 with the same key changes the title; 0x451430 removes it.
    Client::Social::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Escudos"}}}, *m_world);
    EXPECT_EQ(Shops().titles.at(Seller).title, "Escudos");
    Client::Social::RemoveShopTitle(Shops(), Seller);
    EXPECT_FALSE(Client::Social::HasShopTitle(Shops(), Seller));
}

// 0x4db700: with 1, 0x78aba50 at 1 and the hero's sign with the own title (0x78aba64); with another result, nothing.
// 0x4dafe0: another's sign only with its name (strncmp of 10). 0x4e4ea5: with 1, the hero's shop turns off and its
// sign goes.
TEST_P(PersonalShopsTest, TheOwnShopOpensAndAnotherShopShowsItsTitle)
{
    Shops().ownTitle = "Pociones";
    Client::Social::ApplyShopCreated(Shop::ShopCreated{0}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 0);
    EXPECT_TRUE(Shops().titles.empty());
    Client::Social::ApplyShopCreated(Shop::ShopCreated{1}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 1);
    ASSERT_TRUE(Client::Social::HasShopTitle(Shops(), Hero));
    EXPECT_EQ(Shops().titles.at(Hero).title, "Pociones");
    EXPECT_EQ(Shops().titles.at(Hero).key, HeroKey);

    Shop::ShopTitle other{.key = SellerKey, .title = "Arcos", .name = {}};
    const std::string_view wrong = "Brezil";
    for (std::size_t at = 0; at < wrong.size(); ++at)
        other.name[at] = static_cast<std::byte>(wrong[at]);
    Client::Social::ApplyOtherShopTitle(other, *m_world);
    EXPECT_FALSE(Client::Social::HasShopTitle(Shops(), Seller));
    other.name[4] = static_cast<std::byte>('a');
    other.name[5] = static_cast<std::byte>('l');
    Client::Social::ApplyOtherShopTitle(other, *m_world);
    ASSERT_TRUE(Client::Social::HasShopTitle(Shops(), Seller));
    EXPECT_EQ(Shops().titles.at(Seller).title, "Arcos");

    Client::Social::ApplyShopClosed(Shop::ShopClosed{.result = 0, .key = HeroKey}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 1);
    Client::Social::ApplyShopClosed(Shop::ShopClosed{.result = 1, .key = HeroKey}, *m_world);
    EXPECT_EQ(Shops().ownOpen, 0);
    EXPECT_FALSE(Client::Social::HasShopTitle(Shops(), Hero));
    EXPECT_TRUE(Client::Social::HasShopTitle(Shops(), Seller));
}

// 0x4511f0 with another key for the same character: only the log. 0x451fe0: with the key changed or not a player, the
// sign goes. 0x4ca7f0: the one leaving the viewport takes its own with it.
TEST_P(PersonalShopsTest, StaleTitlesLeave)
{
    Client::Social::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos"}, {BuyerKey, "Joyas"}}}, *m_world);
    Client::Social::AddShopTitle(*m_world, 0x0999, Seller, "Otra");
    EXPECT_EQ(Shops().titles.at(Seller).title, "Arcos");

    Client::Social::CheckTheShopKeys(*m_world);
    EXPECT_EQ(Shops().titles.size(), 2u);
    m_world->Actors()[Seller].key = 0x0998;
    m_world->Actors()[Buyer].byte88 = 2;
    Client::Social::CheckTheShopKeys(*m_world);
    EXPECT_TRUE(Shops().titles.empty());

    m_world->Actors()[Buyer].byte88 = 1;
    Client::Social::ApplyShopTitles(Shop::ShopTitles{{{BuyerKey, "Joyas"}}}, *m_world);
    ASSERT_TRUE(Client::Social::HasShopTitle(Shops(), Buyer));
    Client::Tests::ApplyViewportLeave(Client::Protocol::Actors::Leave{{BuyerKey}}, *m_world);
    EXPECT_FALSE(Client::Social::HasShopTitle(Shops(), Buyer));
}

// 0x4524e0: the creation stores the key with & 0x7fff and the place at (−1, −1); 0x4511f0 with the same key also puts
// the place back to (−1, −1) (0x4527d0). With a key from 0x8000 (MOVSX, 0x8001 is −32767), the stored one is 1: the
// update does not match it and only goes to the log, and 0x451fe0 removes the sign (07 Q-C969).
TEST_P(PersonalShopsTest, TheStoredKeyLosesItsHighBitAndAnUpdateUnplacesTheSign)
{
    Client::Social::AddShopTitle(*m_world, SellerKey, Seller, "Arcos");
    Client::Social::ShopTitle& sign = Shops().titles.at(Seller);
    EXPECT_EQ(sign.x, -1);
    EXPECT_EQ(sign.y, -1);
    sign.x = 120;
    sign.y = 80;
    Client::Social::AddShopTitle(*m_world, SellerKey, Seller, "Escudos");
    EXPECT_EQ(sign.title, "Escudos");
    EXPECT_EQ(sign.x, -1);
    EXPECT_EQ(sign.y, -1);

    m_world->Actors()[Buyer].key = 0x8001;
    const std::int32_t high = static_cast<std::int16_t>(std::uint16_t{0x8001});
    Client::Social::AddShopTitle(*m_world, high, Buyer, "Joyas");
    ASSERT_TRUE(Client::Social::HasShopTitle(Shops(), Buyer));
    EXPECT_EQ(Shops().titles.at(Buyer).key, 0x0001);
    Shops().titles.at(Buyer).x = 33;
    Client::Social::AddShopTitle(*m_world, high, Buyer, "Otra");
    EXPECT_EQ(Shops().titles.at(Buyer).title, "Joyas");
    EXPECT_EQ(Shops().titles.at(Buyer).x, 33);
    Client::Social::CheckTheShopKeys(*m_world);
    EXPECT_FALSE(Client::Social::HasShopTitle(Shops(), Buyer));
    EXPECT_TRUE(Client::Social::HasShopTitle(Shops(), Seller));
}

// 0x451590, after each 0x510f70 of the player's key (the map load, the F3 04 and the 1C): only the sign with the
// published hero's key stays.
TEST_P(PersonalShopsTest, ALoadKeepsOnlyTheHeroTitle)
{
    Shops().ownTitle = "Pociones";
    Client::Social::ApplyShopCreated(Shop::ShopCreated{1}, *m_world);
    Client::Social::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos"}, {BuyerKey, "Joyas"}}}, *m_world);
    ASSERT_EQ(Shops().titles.size(), 3u);
    Client::Social::KeepTheHeroShopTitle(*m_world);
    ASSERT_EQ(Shops().titles.size(), 1u);
    EXPECT_TRUE(Client::Social::HasShopTitle(Shops(), Hero));
}

// 0x5576a0..0x5576c9: with its owner's bubble on view, the sign hides (0x451720) and stops counting for the name under
// the mouse (0x451770); without the bubble, it comes back (0x4516d0).
TEST_P(PersonalShopsTest, ABubbleHidesTheTitle)
{
    Client::Social::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos"}}}, *m_world);
    Client::Chat::ChatBubble& bubble = m_world->Runtime().chat.bubbles[0];
    bubble.owner = Seller;
    bubble.time1 = 5;
    const Client::Chat::ChatBubbleStepInput input{.mouseX = 0, .mouseY = 0, .screen = {640, 480}};
    ASSERT_TRUE(Client::Chat::StepTheChatBubbles(*m_world, input).HasValue());
    EXPECT_FALSE(Client::Social::ShopTitleShown(Shops(), Seller));
    EXPECT_TRUE(Client::Social::HasShopTitle(Shops(), Seller));
    bubble.time1 = 1;
    ASSERT_TRUE(Client::Chat::StepTheChatBubbles(*m_world, input).HasValue());
    EXPECT_TRUE(Client::Social::ShopTitleShown(Shops(), Seller));
}

// 0x556dd0: the new bubble of an owner with a sign (0x451810) carries its short title (0x4518b0): with more than 14
// bytes, the first 12 and ".."; without a sign, the bubble's title stays empty.
TEST_P(PersonalShopsTest, TheBubbleOfAShopOwnerTakesTheShortTitle)
{
    Client::Social::ApplyShopTitles(Shop::ShopTitles{{{SellerKey, "Arcos y flechas finas"}}}, *m_world);
    const Client::Chat::ChatBubbleMeasure measure{.measure = m_letters, .screen = Client::Core::PixelSize{640, 480}};
    const auto added =
        Client::Chat::AddChatBubble(*m_world, measure, "Brezal", "hola", Seller, Client::Chat::ChatBubbleMode::Chat);
    ASSERT_TRUE(added.HasValue()) << added.Error();
    const auto bubbleOf = [&](std::size_t owner) -> const Client::Chat::ChatBubble*
    {
        for (const Client::Chat::ChatBubble& bubble : m_world->Runtime().chat.bubbles)
        {
            if (bubble.owner == owner)
                return &bubble;
        }
        return nullptr;
    };
    ASSERT_NE(bubbleOf(Seller), nullptr);
    EXPECT_EQ(bubbleOf(Seller)->shopTitle, "Arcos y flec..");

    const auto other =
        Client::Chat::AddChatBubble(*m_world, measure, "Cardo", "hola", Buyer, Client::Chat::ChatBubbleMode::Chat);
    ASSERT_TRUE(other.HasValue()) << other.Error();
    ASSERT_NE(bubbleOf(Buyer), nullptr);
    EXPECT_EQ(bubbleOf(Buyer)->shopTitle, "");
}

// 0x4e4f5d (3F 08): sprintf(Text[1122], name) with type 1. 0x4e4fcc (3F 12): with 0x6c931c at 2, the closes and text
// 1126 with type 2; otherwise, nothing.
TEST_P(PersonalShopsTest, TheNoticesGoToTheMessages)
{
    Shop::ShopNotice notice{};
    const std::string_view name = "Brezal";
    for (std::size_t at = 0; at < name.size(); ++at)
        notice.name[at] = static_cast<std::byte>(name[at]);
    ASSERT_TRUE(Client::Social::ApplyShopNotice(notice, Resources()).HasValue());
    const auto& entries = m_world->Runtime().chat.log.entries;
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries.front().text, "Brezal abrio su tienda");
    EXPECT_EQ(entries.front().type, Client::Ui::SystemMessage);

    const std::size_t before = entries.size();
    ASSERT_TRUE(Client::Social::ApplyShopOwnerLeft(Client::Protocol::PersonalShop::ShopOwnerLeft{}, Resources(), {})
                    .HasValue());
    EXPECT_EQ(entries.size(), before);
    Shops().mode = 2;
    ASSERT_TRUE(Client::Social::ApplyShopOwnerLeft(Client::Protocol::PersonalShop::ShopOwnerLeft{}, Resources(), {})
                    .HasValue());
    EXPECT_EQ(entries.front().text, "la tienda se cerro");
    EXPECT_EQ(entries.front().type, Client::Ui::ErrorMessage);
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, PersonalShopsTest, ::testing::Values(Lorencia, Devias));

// Each case reads its fields at fixed places, up to the last one it reads.
TEST(PersonalShopsProtocolTest, ReadsEachMinimumSizeAndNotOneByteLess)
{
    std::vector<std::byte> titles = Bytes({0xc2, 0x00, 0x2c, 0x3f, 0x00, 0x01});
    titles.resize(0x2c);
    std::vector<std::byte> notice = Bytes({0xc1, 0x0f, 0x3f, 0x08});
    notice.resize(0x0f);
    std::vector<std::byte> title = Bytes({0xc1, 0x34, 0x3f, 0x10});
    title.resize(0x34);
    std::vector<std::byte> list = Bytes({0xc2, 0x00, 0x37, 0x3f, 0x05, 0x01});
    list.resize(0x37);
    std::vector<std::byte> refresh = Bytes({0xc2, 0x00, 0x37, 0x3f, 0x13, 0x01});
    refresh.resize(0x37);
    std::vector<std::byte> purchase = Bytes({0xc1, 0x0f, 0x3f, 0x06, 0x01});
    purchase.resize(0x0f);
    Client::Tests::ExpectMinimumSize(Shop::ReadShopTitles, Bytes({0xc2, 0x00, 0x06, 0x3f, 0x00, 0x00}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopTitles, titles);
    Client::Tests::ExpectMinimumSize(Shop::ReadShopCreated, Bytes({0xc1, 0x05, 0x3f, 0x02, 0x01}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopClosed, Bytes({0xc1, 0x05, 0x3f, 0x03, 0x00}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopClosed, Bytes({0xc1, 0x07, 0x3f, 0x03, 0x01, 0x00, 0x05}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopNotice, notice);
    Client::Tests::ExpectMinimumSize(Shop::ReadShopTitle, title);
    Client::Tests::ExpectMinimumSize(Shop::ReadPriceResult, Bytes({0xc1, 0x06, 0x3f, 0x01, 0x00, 0x05}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopItemList, Bytes({0xc2, 0x00, 0x06, 0x3f, 0x05, 0x00}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopItemList, list);
    Client::Tests::ExpectMinimumSize(Shop::ReadShopRefresh, Bytes({0xc2, 0x00, 0x06, 0x3f, 0x13, 0x00}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopRefresh, refresh);
    Client::Tests::ExpectMinimumSize(Shop::ReadShopPurchase, Bytes({0xc1, 0x05, 0x3f, 0x06, 0x00}));
    Client::Tests::ExpectMinimumSize(Shop::ReadShopPurchase, purchase);
}
} // namespace
