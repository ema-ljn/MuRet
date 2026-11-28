#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Guild/GuildReceive.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/TextTable.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Outgoing.h"
#include "Models/Cloth.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Hud/GamePanels.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldEntry.h"
#include "FrontEnd/SelectionState.h"
#include "Camera/GameCamera.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "support/RecordingSink.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The incoming guild, union and war packets (CL9 milestone 8; 06 § El gremio): 0x4e2b10 with 50, 51, 52, 53, 54, 56,
// 5D, 60..64, 66, E1, E5, E6, E9 and EB, the two windows on closing (0x6522f0, 0x6570b0) and the panels' dialog
// answers (0x4d6b60, 0x653c00). The war gesture is milestone 1's emote (Actors::PlayTheEmote). The guild names, the
// character names and the texts are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Guild::GuildReceiveContext;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
namespace Guild = Client::Protocol::Guild;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr std::uint32_t Seed = 4242;
constexpr std::uint32_t Now = 0x00abcdef;
constexpr std::uint16_t PlayerKey = 0x0042;

std::map<std::size_t, std::string> GuildTexts()
{
    std::map<std::size_t, std::string> texts{
        {418, "%s quiere entrar"},
        {429, "Acepta?"},
        {516, "crear 0"},
        {517, "crear 2"},
        {515, "echado"},
        {568, "lista otra vez"},
        {526, "guerra con %s"},
        {533, "partido con %s"},
        {480, "empate"},
        {1280, "%s propone alianza"},
        {1281, "para unirse"},
        {1282, "para otra cosa"},
        {1283, "Acepta la alianza?"},
        {1284, "%s propone rivalidad"},
        {1285, "rival otra"},
        {1286, "rival unirse"},
        {1328, "sin alianza"},
        {1375, "asignado 2"},
        {1382, "alianza rota"},
        {1268, "resultado 20"},
    };
    for (std::size_t index = 503; index <= 512; ++index)
        texts.emplace(index, "texto " + std::to_string(index));
    for (std::size_t index = 519; index <= 525; ++index)
        texts.emplace(index, "guerra " + std::to_string(index));
    for (std::size_t index = 527; index <= 532; ++index)
        texts.emplace(index, "fin " + std::to_string(index));
    return texts;
}

void Name(ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

template <std::size_t Size> void Fill(std::array<char, Size>& target, std::string_view text)
{
    target.fill('\0');
    for (std::size_t i = 0; i < text.size() && i + 1 < Size; ++i)
        target[i] = text[i];
}

template <std::size_t Size> std::string TextOf(const std::array<char, Size>& text)
{
    return std::string(text.data());
}

Guild::GuildName GuildNameOf(std::string_view text)
{
    Guild::GuildName name{};
    for (std::size_t i = 0; i < text.size() && i < name.size(); ++i)
        name[i] = static_cast<std::byte>(text[i]);
    return name;
}

std::vector<std::uint8_t> Plain(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Xor32Decode(sent.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : plain.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

// Player.bmd with its 205 actions (the ones 0x4ea030 checks in the gesture); no other model.
class GestureModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == Client::Tables::PlayerSkeletonModel ? &player : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }
    Client::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
};

// 0x576a84: the standing actions (1..12 and 0x8a), the only ones with which 0x576a60 animates and sends.
constexpr std::uint8_t Standing = 1;
constexpr std::uint8_t Walking = 0x0d;
constexpr std::uint8_t ElfClass = 2;

class GuildReceiveTest : public ::testing::Test
{
protected:
    GuildReceiveTest() : m_texts(LoadTexts())
    {
        Reset(0);
    }

    static Client::Tables::TextTable LoadTexts()
    {
        auto table = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes(GuildTexts()));
        EXPECT_TRUE(table.HasValue());
        return std::move(table).Value();
    }

    // A new world in `map`, with the hero in slot 0.
    void Reset(std::int32_t map)
    {
        m_world = std::make_unique<WorldState>();
        m_random = CrtRandom(Seed);
        m_sent.clear();
        Runtime().map = map;
        Runtime().hero = 0;
        ActorState& hero = Actor(0, "Hornero", 0x0042);
        hero.word1f2 = 0xffff;
        hero.type = Client::Tables::PlayerSkeletonModel;
    }

    ActorState& Actor(std::size_t index, std::string_view name, std::uint16_t key)
    {
        ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = 1;
        actor.key = key;
        actor.word1f2 = 0xffff;
        Name(actor, name);
        return actor;
    }

    [[nodiscard]] GuildReceiveContext Context(bool withModels = true)
    {
        return GuildReceiveContext{.world = *m_world,
                                   .random = m_random,
                                   .measure = &m_letters,
                                   .screen = Client::Core::PixelSize{640, 480},
                                   .texts = &m_texts,
                                   .playerKey = PlayerKey,
                                   .now = Now,
                                   .send = [this](Client::Protocol::Outgoing packet)
                                   { m_sent.push_back(Plain(packet)); },
                                   .models = withModels ? &m_models : nullptr};
    }

    Client::Core::Expected<void, std::string> Apply(const Guild::GuildPacket& packet, bool withModels = true)
    {
        return Client::Guild::ApplyGuildPacket(packet, Context(withModels));
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    [[nodiscard]] Client::Guild::GuildState& GuildOf()
    {
        return Runtime().guild;
    }

    [[nodiscard]] Client::Ui::PanelDialogState& Dialog()
    {
        return Runtime().panels.delegated.panelDialog;
    }

    // The chat log, oldest to newest (0x45bb20 adds at the front).
    [[nodiscard]] std::vector<std::pair<std::int32_t, std::string>> Messages() const
    {
        std::vector<std::pair<std::int32_t, std::string>> messages;
        const auto& entries = m_world->Runtime().chat.log.entries;
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
            messages.emplace_back(entry->type, entry->text);
        return messages;
    }

    [[nodiscard]] std::string LastNotice() const
    {
        const auto& area = m_world->Runtime().chat.area;
        if (area.noticeCount == 0)
            return {};
        return area.notices[static_cast<std::size_t>(area.noticeCount - 1)].text;
    }

    [[nodiscard]] int DrawsSince() const
    {
        CrtRandom reference(Seed);
        for (int draws = 0; draws <= 1000; ++draws)
        {
            if (reference.State() == m_random.State())
                return draws;
            static_cast<void>(reference.Next());
        }
        return -1;
    }

    void NameTheGuild(std::size_t index, std::int32_t key, std::string_view guildName, std::string_view unionName = {})
    {
        Client::Guild::GuildTableEntry& entry = GuildOf().table[index];
        entry.key = key;
        Fill(entry.guildName, guildName);
        Fill(entry.unionName, unionName);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    LetterMeasure m_letters;
    Client::Tables::TextTable m_texts;
    std::vector<std::vector<std::uint8_t>> m_sent;
    GestureModels m_models;
};

TEST_F(GuildReceiveTest, TheMasterQuestionBorrowsBothCastleAndTrainerClosersFromTheWorld)
{
    for (const bool trainer : {false, true})
    {
        Reset(0);
        Client::FrontEnd::SelectionState selection(*m_world);
        Client::Models::ClothWorld cloth;
        Client::Camera::GameCamera camera;
        Client::Camera::GameCameraInput cameraInput;
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Client::Scenes::WorldEntryResources resources{.world = *m_world,
                                                      .selection = selection,
                                                      .dataRoot = {},
                                                      .cloth = cloth,
                                                      .camera = camera,
                                                      .cameraInput = cameraInput,
                                                      .models = m_models,
                                                      .log = log};
        resources.textMeasure = &m_letters;
        resources.texts = &m_texts;
        int npcPackets = 0;
        resources.sendNpcWindowPacket =
            [&](Client::Protocol::Outgoing packet) -> Client::Core::Expected<void, std::string>
        {
            ++npcPackets;
            m_sent.push_back(Plain(packet));
            return {};
        };
        if (trainer)
            Runtime().panels.delegated.trainer = 1;
        else
            Runtime().panels.delegated.secondCastleSiege = 1;
        auto context = Context();
        context.worldEntry = &resources;

        auto result = Client::Guild::ApplyGuildPacket(Guild::MasterQuestion{}, context);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(Runtime().panels.delegated.secondGuild, 1);
        EXPECT_EQ(Runtime().panels.delegated.secondCastleSiege, 0);
        EXPECT_EQ(Runtime().panels.delegated.trainer, 0);
        EXPECT_EQ(npcPackets, trainer ? 1 : 0);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 3, 0x31}}));
        EXPECT_EQ(DrawsSince(), 4);
    }
}

// 0x4e4279 opens window 2 (0x6599a0 with 0x40000: CanOpen and CloseByMask, four rand()); the 56 with 1 closes it
// (0x6570b0: guild 1999 goes back to −1, plays 0x1c and sends C1 04 54 00) and empties the message. In Lorencia,
// Devias and Chaos Castle (18), where the commands do not open.
TEST_F(GuildReceiveTest, CreatingAGuildOpensTheSecondWindowAndTheResultClosesIt)
{
    for (const std::int32_t map : {0, 2, 18})
    {
        SCOPED_TRACE(map);
        Reset(map);
        ASSERT_TRUE(Apply(Guild::MasterQuestion{}).HasValue());
        EXPECT_EQ(Runtime().panels.delegated.secondGuild, 1);
        EXPECT_EQ(DrawsSince(), 4);
        EXPECT_TRUE(m_sent.empty());

        // 0x4e34a0 (the Guild Master's 01, which stops): the hero without a guild is left with 1999.
        m_world->Actors()[0].word1f2 = 1999;
        Runtime().chat.input.messageBuffer[0] = 'x';
        Runtime().chat.input.messageLength = 1;
        GuildOf().createKind = 3;
        ASSERT_TRUE(Apply(Guild::CreateResult{1}).HasValue());
        EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
        EXPECT_EQ(m_world->Actors()[0].word1f2, 0xffff);
        EXPECT_EQ(GuildOf().createKind, 0);
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_EQ(Runtime().chat.input.messageBuffer[0], '\0');
        EXPECT_EQ(Runtime().chat.input.messageLength, 0);
        EXPECT_EQ(Runtime().mouse.updateTimeMax, 6);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x04, 0x54, 0x00}}));
        EXPECT_EQ(DrawsSince(), 4) << "cerrar 0x40000 no consulta el comercio ni la tienda";
    }
}

// 0x4d7ff0: the other results are an error message (1 has none; 7 or more, nothing).
TEST_F(GuildReceiveTest, TheOtherCreateResultsAreMessages)
{
    ASSERT_TRUE(Apply(Guild::CreateResult{0}).HasValue());
    ASSERT_TRUE(Apply(Guild::CreateResult{2}).HasValue());
    ASSERT_TRUE(Apply(Guild::CreateResult{7}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::Ui::ErrorMessage, "crear 0"},
                                                                     {Client::Ui::ErrorMessage, "crear 2"}};
    EXPECT_EQ(Messages(), expected);
}

// 0x4d74b0: the 50 stores the key and opens the dialog with the name; Confirm (0x4d6b60 with 8) sends C1 06 51 01 and
// the key; Esc (0x50) sends the 00. With the dialog busy, the 00 without asking.
TEST_F(GuildReceiveTest, AJoinRequestAsksAndTheAnswerCarriesTheKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Actor(5, "Benteveo", 0x0123);
        ASSERT_TRUE(Apply(Guild::JoinRequest{0x0123}).HasValue());
        EXPECT_EQ(GuildOf().requester, 0x0123);
        ASSERT_EQ(Dialog().opened, Now);
        EXPECT_EQ(Dialog().owner, Client::Ui::PanelDialogOwner::GuildJoin);
        EXPECT_EQ(Dialog().lines[0], "Benteveo quiere entrar");
        EXPECT_EQ(Dialog().lines[1], "Acepta?");
        EXPECT_EQ(Dialog().flags, Client::Ui::PanelDialogConfirm);
        EXPECT_TRUE(m_sent.empty());

        // Another request with the dialog open: no, and the new key stays.
        Actor(6, "Calandria", 0x0456);
        ASSERT_TRUE(Apply(Guild::JoinRequest{0x0456}).HasValue());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x00, 0x04, 0x56}}));
        EXPECT_EQ(GuildOf().requester, 0x0456);
        m_sent.clear();

        // Confirm answers with the stored key (0x78aba40), the last one's.
        const auto answered =
            Client::Guild::AnswerThePanelDialog(*m_world, Context().send, Client::Ui::PanelDialogOwner::GuildJoin, 8);
        ASSERT_TRUE(answered.HasValue()) << answered.Error();
        EXPECT_TRUE(answered.Value());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x01, 0x04, 0x56}}));
        m_sent.clear();

        // Esc through the registry (0x657880 → 0x660410): 0x50, the no, and the dialog closed.
        Client::Hud::GamePanels panels(*m_world, m_random);
        panels.LendTheGuild(*m_world, Context().send);
        const auto key = panels.Registry().PanelKey(0x1b);
        ASSERT_TRUE(key.HasValue()) << key.Error();
        EXPECT_TRUE(key.Value());
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x00, 0x04, 0x56}}));
    }
}

// 0x4d74f8: without the key's character, the original opens the dialog with the name it reads past the 400
// (Q-C1297); MuRet drops the 50 without stopping (the session goes on): neither the key, nor the dialog, nor the no.
// With the dialog busy, the no still goes out, since it looks for nobody. In Lorencia and Devias.
TEST_F(GuildReceiveTest, AJoinRequestFromSomeoneOutOfSightIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        GuildOf().requester = 0x0777;
        const auto applied = Apply(Guild::JoinRequest{0x0999});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(GuildOf().requester, 0x0777);
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_EQ(Dialog().owner, Client::Ui::PanelDialogOwner::None);
        EXPECT_TRUE(m_sent.empty());
        EXPECT_TRUE(Messages().empty());
        EXPECT_EQ(DrawsSince(), 0);

        Dialog().opened = Now;
        ASSERT_TRUE(Apply(Guild::JoinRequest{0x0999}).HasValue());
        EXPECT_EQ(GuildOf().requester, 0x0999);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x00, 0x09, 0x99}}));
    }
}

// 0x4d7a20: results 0 to 7 are an error message and always 0x6236f0(0).
TEST_F(GuildReceiveTest, TheJoinResultIsAMessage)
{
    Runtime().dialogs.current = 0x22;
    ASSERT_TRUE(Apply(Guild::JoinResult{1}).HasValue());
    ASSERT_TRUE(Apply(Guild::JoinResult{8}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::Ui::ErrorMessage, "texto 504"}};
    EXPECT_EQ(Messages(), expected);
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x4d7b60: the count, the score without negatives, the name and each member with its server (bit 7) or 0xff.
TEST_F(GuildReceiveTest, TheMemberListFillsTheSlotsAndTheWindow)
{
    GuildOf().window.unions.push_back({});
    Guild::MemberList list{.count = 2, .score = -7, .name = GuildNameOf("Horneros"), .members = {}};
    Guild::MemberEntry first;
    first.name = {std::byte{'H'}, std::byte{'o'}, std::byte{'r'}, std::byte{'n'}, std::byte{'e'},
                  std::byte{'r'}, std::byte{'o'}, std::byte{0},   std::byte{0},   std::byte{0}};
    first.number = 1;
    first.server = 0x83;
    first.status = 0x80;
    Guild::MemberEntry second = first;
    second.name[0] = std::byte{'T'};
    second.server = 0x03;
    second.status = 0;
    list.members = {first, second};
    ASSERT_TRUE(Apply(list).HasValue());
    EXPECT_EQ(GuildOf().memberCount, 2);
    EXPECT_EQ(GuildOf().score, 0);
    EXPECT_EQ(TextOf(GuildOf().window.name88), "Horneros");
    EXPECT_EQ(TextOf(GuildOf().members[0].name), "Hornero");
    EXPECT_EQ(GuildOf().members[0].server, 3);
    EXPECT_EQ(GuildOf().members[1].server, 0xff);
    EXPECT_EQ(GuildOf().members[1].status, 0);
    EXPECT_EQ(GuildOf().window.members.size(), 2u);
    EXPECT_TRUE(GuildOf().window.unions.empty());
}

// 0x4d7bc2..0x4d7c1f: member 80 onward falls on the guild table (Q-C1303); MuRet drops the whole 52 without stopping:
// the count, the score, the name, the members and the window's two lists stay as they were.
TEST_F(GuildReceiveTest, AMemberListOfMoreThanEightyIsDiscarded)
{
    GuildOf().memberCount = 3;
    GuildOf().score = 9;
    Fill(GuildOf().window.name88, "Talas");
    Fill(GuildOf().members[0].name, "Ombu");
    GuildOf().window.unions.push_back({});
    NameTheGuild(0, 70, "Horneros");
    Guild::MemberList list{.count = 81, .score = 5, .name = GuildNameOf("Ceibos"), .members = {}};
    Guild::MemberEntry entry;
    entry.name[0] = std::byte{'Z'};
    entry.server = 0x81;
    list.members.assign(81, entry);
    const auto applied = Apply(list);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(GuildOf().memberCount, 3);
    EXPECT_EQ(GuildOf().score, 9);
    EXPECT_EQ(TextOf(GuildOf().window.name88), "Talas");
    EXPECT_EQ(TextOf(GuildOf().members[0].name), "Ombu");
    EXPECT_EQ(TextOf(GuildOf().members[79].name), "");
    EXPECT_TRUE(GuildOf().window.members.empty());
    EXPECT_EQ(GuildOf().window.unions.size(), 1u);
    EXPECT_EQ(TextOf(GuildOf().table[0].guildName), "Horneros");
    EXPECT_TRUE(m_sent.empty());
}

// 0x4d7c30: 4 takes the hero out of the guild and erases the entry; 1 and 4 close the guild (0x6522f0); 5 asks for the
// list (C1 03 52).
TEST_F(GuildReceiveTest, LeavingTheGuildClosesTheWindow)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        NameTheGuild(3, 77, "Horneros", "Ceibos");
        ActorState& hero = m_world->Actors()[0];
        hero.word1f2 = 3;
        hero.guildStatus = 0x80;
        hero.byte370 = 4;
        GuildOf().warOn = 1;
        GuildOf().window.tab = 3;
        Runtime().panels.delegated.guild = 1;

        ASSERT_TRUE(Apply(Guild::LeaveResult{4}).HasValue());
        EXPECT_EQ(hero.word1f2, 0xffff);
        EXPECT_EQ(GuildOf().table[3].key, -1);
        EXPECT_EQ(TextOf(GuildOf().table[3].guildName), "");
        EXPECT_EQ(TextOf(GuildOf().table[3].unionName), "");
        EXPECT_EQ(hero.guildStatus, 0xff);
        EXPECT_EQ(hero.byte370, 0);
        EXPECT_EQ(GuildOf().warOn, 0);
        EXPECT_EQ(GuildOf().memberCount, -1);
        EXPECT_EQ(Runtime().panels.delegated.guild, 0);
        EXPECT_EQ(GuildOf().window.tab, 1);
        EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_EQ(DrawsSince(), 0);

        ASSERT_TRUE(Apply(Guild::LeaveResult{5}).HasValue());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x03, 0x52}}));
        const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::Ui::ErrorMessage, "echado"},
                                                                         {Client::Ui::ErrorMessage, "lista otra vez"}};
        EXPECT_EQ(Messages(), expected);
    }
}

// 0x6599a0(0x10) through the registry with the guild hook: CanOpen and CloseByMask (four rand()), the C1 03 52 and the
// byte; in Chaos Castle it opens too.
TEST_F(GuildReceiveTest, OpeningTheGuildAsksForTheList)
{
    for (const std::int32_t map : {0, 2, 18})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Client::Hud::GamePanels panels(*m_world, m_random);
        panels.LendTheGuild(*m_world, Context().send);
        const auto opened = panels.Registry().Open(Client::Ui::GuildPanel);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        EXPECT_TRUE(opened.Value());
        EXPECT_EQ(Runtime().panels.delegated.guild, 1);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x03, 0x52}}));
        EXPECT_EQ(DrawsSince(), 4);

        // With window 2 open, CanOpen lets nothing open.
        const auto closed = panels.Registry().Close(Client::Ui::GuildPanel);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        Runtime().panels.delegated.secondGuild = 1;
        const auto refused = panels.Registry().Open(Client::Ui::GuildPanel);
        ASSERT_TRUE(refused.HasValue());
        EXPECT_FALSE(refused.Value());
    }
}

// 0x4e42f0: the 5D takes the key's character (without bit 7) out of the guild and closes the window.
TEST_F(GuildReceiveTest, TheViewportGuildDeleteClearsTheActor)
{
    ActorState& other = Actor(9, "Zorzal", 0x0077);
    other.word1f2 = 4;
    other.guildStatus = 0x20;
    other.byte369 = 1;
    other.dword36c = 5;
    other.byte370 = 2;
    ASSERT_TRUE(Apply(Guild::ViewportGuildDelete{0x0077}).HasValue());
    EXPECT_EQ(other.word1f2, 0xffff);
    EXPECT_EQ(other.guildStatus, 0xff);
    EXPECT_EQ(other.byte369, 0);
    EXPECT_EQ(other.dword36c, 0u);
    EXPECT_EQ(other.byte370, 0);
    EXPECT_EQ(GuildOf().memberCount, -1);
}

// 0x4e4306: without the character, the original writes past the 400 (Q-C1298); MuRet drops the whole 5D without
// stopping: neither the count, nor the character, nor the guild closed (nor its rand()). In Lorencia and Devias.
TEST_F(GuildReceiveTest, AViewportGuildDeleteOfSomeoneOutOfSightIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        ActorState& other = Actor(9, "Zorzal", 0x0077);
        other.word1f2 = 4;
        other.guildStatus = 0x20;
        GuildOf().memberCount = 3;
        Runtime().panels.delegated.guild = 1;
        const auto applied = Apply(Guild::ViewportGuildDelete{0x0078});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(GuildOf().memberCount, 3);
        EXPECT_EQ(other.word1f2, 4);
        EXPECT_EQ(other.guildStatus, 0x20);
        EXPECT_EQ(Runtime().panels.delegated.guild, 1);
        EXPECT_FALSE(Runtime().lastSoundId.has_value());
        EXPECT_EQ(DrawsSince(), 0);
        EXPECT_TRUE(m_sent.empty());
    }
}

// 0x447b90: the 66 writes the key's entry (0x447b00 looks up to the count inclusive) with the names and the mark in
// nibbles; without the key, nothing.
TEST_F(GuildReceiveTest, TheGuildInfoWritesTheEntryOfItsKey)
{
    GuildOf().tableCount = 1;
    NameTheGuild(1, 55, "viejo");
    Guild::GuildInfo info{
        .key = 55, .unionName = GuildNameOf("Ceibos"), .guildName = GuildNameOf("Horneros"), .mark = {}};
    info.mark[0] = 0x12;
    info.mark[31] = 0xab;
    ASSERT_TRUE(Apply(info).HasValue());
    const Client::Guild::GuildTableEntry& entry = GuildOf().table[1];
    EXPECT_EQ(TextOf(entry.guildName), "Horneros");
    EXPECT_EQ(TextOf(entry.unionName), "Ceibos");
    EXPECT_EQ(entry.mark[0], 1);
    EXPECT_EQ(entry.mark[1], 2);
    EXPECT_EQ(entry.mark[62], 0xa);
    EXPECT_EQ(entry.mark[63], 0xb);

    info.key = 56;
    ASSERT_TRUE(Apply(info).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[2].guildName), "");
}

// 0x4d9260: each entry of the 65 sets the rank, +0x369 and the relation; with the guild in the table, its index;
// without it, the C1 08 66 that asks for it and 0x447b50, which adds it at the end. +0x374: 1 with the hero's guild.
// With the hero's entry, 0x412120 copies its guild and rank. In Lorencia, Devias and Arena (6, the war's soccer one).
TEST_F(GuildReceiveTest, TheViewportGuildGivesEachActorItsIndexAndAsksForTheUnknownOnes)
{
    for (const std::int32_t map : {0, 2, 6})
    {
        SCOPED_TRACE(map);
        Reset(map);
        GuildOf().tableCount = 2;
        NameTheGuild(0, 70, "Horneros");
        NameTheGuild(1, 71, "Teros");
        ActorState& other = Actor(9, "Zorzal", 0x0077);
        Guild::ViewportGuild viewport;
        viewport.entries.push_back({.guildKey = 71, .status = 0x80, .byte369 = 1, .relation = 2, .key = 0x0042});
        viewport.entries.push_back({.guildKey = 71, .status = 0x20, .byte369 = 0, .relation = 0, .key = 0x0077});
        viewport.entries.push_back({.guildKey = 99, .status = 0, .byte369 = 0, .relation = 1, .key = 0x0077});
        ASSERT_TRUE(Apply(viewport).HasValue());
        const ActorState& hero = m_world->Actors()[0];
        EXPECT_EQ(hero.word1f2, 1);
        EXPECT_EQ(hero.guildStatus, 0x80);
        EXPECT_EQ(hero.byte369, 1);
        EXPECT_EQ(hero.byte370, 2);
        EXPECT_EQ(hero.byte374, 1);
        EXPECT_EQ(GuildOf().heroGuildIndex, 1);
        EXPECT_EQ(GuildOf().heroGuildStatus, 0x80);
        // The third entry overwrites the second: guild 99 was not there, it is asked for and stays in entry 2.
        EXPECT_EQ(other.word1f2, 2);
        EXPECT_EQ(other.byte370, 1);
        EXPECT_EQ(other.byte374, 0);
        EXPECT_EQ(GuildOf().tableCount, 3u);
        EXPECT_EQ(GuildOf().table[2].key, 99);
        ASSERT_EQ(m_sent.size(), 1u);
        EXPECT_EQ(m_sent[0], (std::vector<std::uint8_t>{0xc1, 0x08, 0x66, 0x00, 99, 0x00, 0x00, 0x00}));
        EXPECT_EQ(DrawsSince(), 0);
    }
}

// 0x4d96eb..0x4d976b: at war and without the rival's index, the 65 looks it up by name and leaves −1 if absent; with
// the rival, +0x374 at 2. The full table gives −1 without asking for anything but the 66. Without the character, it
// stops.
TEST_F(GuildReceiveTest, TheViewportGuildMarksTheRivalInAWar)
{
    GuildOf().tableCount = 2;
    NameTheGuild(0, 70, "Horneros");
    NameTheGuild(1, 71, "Teros");
    GuildOf().warOn = 1;
    Fill(GuildOf().rival, "Teros");
    ActorState& other = Actor(9, "Zorzal", 0x0077);
    Guild::ViewportGuild viewport;
    viewport.entries.push_back({.guildKey = 71, .status = 0, .byte369 = 0, .relation = 0, .key = 0x0077});
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, 1);
    EXPECT_EQ(other.byte374, 2);

    Fill(GuildOf().rival, "Benteveos");
    GuildOf().rivalIndex = -1;
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, -1);
    EXPECT_EQ(other.byte374, 0);

    // With the table full, 0x447b00 would read entry 2000 (Q-C1280): it stops before 0x447b50.
    GuildOf().tableCount = 2000;
    viewport.entries[0].guildKey = 5000;
    EXPECT_FALSE(Apply(viewport).HasValue());
    EXPECT_TRUE(m_sent.empty());
}

// 0x4d92b2: an entry without its character, which the original writes past the 400 (Q-C1299), is dropped whole
// (without the 66 that would ask for its guild or 0x447b50) and the others go on; the session goes on. In Lorencia and
// Devias.
TEST_F(GuildReceiveTest, AViewportGuildEntryOfSomeoneOutOfSightIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        GuildOf().tableCount = 1;
        NameTheGuild(0, 70, "Horneros");
        ActorState& other = Actor(9, "Zorzal", 0x0077);
        Guild::ViewportGuild viewport;
        viewport.entries.push_back({.guildKey = 99, .status = 0x20, .byte369 = 1, .relation = 2, .key = 0x0078});
        viewport.entries.push_back({.guildKey = 70, .status = 0x40, .byte369 = 0, .relation = 1, .key = 0x0077});
        const auto applied = Apply(viewport);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(other.word1f2, 0);
        EXPECT_EQ(other.guildStatus, 0x40);
        EXPECT_EQ(other.byte370, 1);
        EXPECT_EQ(GuildOf().tableCount, 1u);
        EXPECT_TRUE(m_sent.empty());
        EXPECT_EQ(DrawsSince(), 0);
    }
}

// 0x4e44aa: the 67 sets the union name in the guild's entry and the relation in the character; a guild the table does
// not have writes into member 74 (Q-C1284). The key does not lose bit 7, as in the 65: with it, nobody.
TEST_F(GuildReceiveTest, TheViewportUnionNamesTheAllianceAndTheRelation)
{
    GuildOf().tableCount = 1;
    NameTheGuild(1, 71, "Teros", "Viejos");
    ActorState& other = Actor(9, "Zorzal", 0x0077);
    Guild::ViewportUnion viewport;
    viewport.entries.push_back({.key = 0x0077, .guildKey = 71, .relation = 1, .unionName = GuildNameOf("Ceibos")});
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[1].unionName), "Ceibos");
    EXPECT_EQ(other.byte370, 1);

    viewport.entries[0].guildKey = 72;
    viewport.entries[0].unionName = GuildNameOf("Talas");
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(std::string(GuildOf().members[74].name.data()), "Talas");

    // 0x4e44f5: with key 0x8077 there is nobody and the original writes the relation past the 400 (Q-C1300): MuRet
    // drops the whole entry, also the union name, and the session goes on.
    viewport.entries[0].key = 0x8077;
    viewport.entries[0].guildKey = 71;
    viewport.entries[0].relation = 2;
    viewport.entries[0].unionName = GuildNameOf("Espinillo");
    const auto applied = Apply(viewport);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(TextOf(GuildOf().table[1].unionName), "Ceibos");
    EXPECT_EQ(other.byte370, 1);
}

// 0x6565e0: the union asks with the name of whoever proposes it; Confirm (0x653c00 with 8) sends C1 08 E6 with 1. With
// the dialog busy, the 0 without asking.
TEST_F(GuildReceiveTest, AnAllianceRequestAsksAndTheAnswerGoesBack)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Actor(5, "Benteveo", 0x0123);
        ASSERT_TRUE(
            Apply(Guild::RelationshipRequest{.relation = 1, .request = 1, .keyHigh = 0x01, .keyLow = 0x23}).HasValue());
        ASSERT_EQ(Dialog().opened, Now);
        EXPECT_EQ(Dialog().owner, Client::Ui::PanelDialogOwner::Relationship);
        EXPECT_EQ(Dialog().lines[0], "Benteveo propone alianza");
        EXPECT_EQ(Dialog().lines[1], "para unirse");
        EXPECT_EQ(Dialog().lines[2], "Acepta la alianza?");
        EXPECT_EQ(GuildOf().secondWindow.dialog2a4, Now);

        ASSERT_TRUE(
            Apply(Guild::RelationshipRequest{.relation = 2, .request = 2, .keyHigh = 0x01, .keyLow = 0x23}).HasValue());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x08, 0xe6, 0x02, 0x02, 0x00, 0x01, 0x23}}));
        m_sent.clear();

        const auto answered = Client::Guild::AnswerThePanelDialog(*m_world, Context().send,
                                                                  Client::Ui::PanelDialogOwner::Relationship, 8);
        ASSERT_TRUE(answered.HasValue()) << answered.Error();
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x08, 0xe6, 0x01, 0x01, 0x01, 0x01, 0x23}}));
        m_sent.clear();
        // 0x654289: anything other than Confirm or Cancel sends nothing.
        ASSERT_TRUE(Client::Guild::AnswerThePanelDialog(*m_world, Context().send,
                                                        Client::Ui::PanelDialogOwner::Relationship, 0x50)
                        .HasValue());
        EXPECT_TRUE(m_sent.empty());
    }
}

// 0x656d44..0x656db1: with the character and a relation that is neither 1 nor 2, the original opens the dialog with
// the uninitialized stack (Q-C1302); MuRet drops the whole E5 without stopping. Without the character (0x656ccb), it
// stores the four bytes and nothing else, like the original. In Lorencia and Devias.
TEST_F(GuildReceiveTest, ARelationshipRequestOfAnotherRelationIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Actor(5, "Benteveo", 0x0123);
        GuildOf().relation = 1;
        GuildOf().relationRequest = 1;
        const auto applied =
            Apply(Guild::RelationshipRequest{.relation = 3, .request = 2, .keyHigh = 0x01, .keyLow = 0x23});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(GuildOf().relation, 1);
        EXPECT_EQ(GuildOf().relationRequest, 1);
        EXPECT_EQ(GuildOf().relationKeyLow, 0);
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_EQ(GuildOf().secondWindow.dialog2a4, 0u);
        EXPECT_TRUE(m_sent.empty());

        ASSERT_TRUE(
            Apply(Guild::RelationshipRequest{.relation = 3, .request = 2, .keyHigh = 0x04, .keyLow = 0x56}).HasValue());
        EXPECT_EQ(GuildOf().relation, 3);
        EXPECT_EQ(GuildOf().relationRequest, 2);
        EXPECT_EQ(GuildOf().relationKeyHigh, 0x04);
        EXPECT_EQ(GuildOf().relationKeyLow, 0x56);
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_TRUE(m_sent.empty());
    }
}

// 0x6565e0 with the rivalry (2): its texts.
TEST_F(GuildReceiveTest, ARivalryRequestHasItsOwnLines)
{
    Actor(5, "Benteveo", 0x0123);
    ASSERT_TRUE(
        Apply(Guild::RelationshipRequest{.relation = 2, .request = 2, .keyHigh = 0x01, .keyLow = 0x23}).HasValue());
    EXPECT_EQ(Dialog().lines[0], "Benteveo propone rivalidad");
    EXPECT_EQ(Dialog().lines[1], "rival otra");
}

// 0x4d9940: the E6 that breaks the hero's union (its key, 1, 2 and 1) erases the union name; the others are only a
// message (or "Invalid GuildRelationShipResult").
TEST_F(GuildReceiveTest, BreakingTheAllianceClearsItsName)
{
    NameTheGuild(3, 77, "Horneros", "Ceibos");
    m_world->Actors()[0].word1f2 = 3;
    ASSERT_TRUE(Apply(Guild::RelationshipResult{.relation = 1, .request = 2, .result = 1, .key = 0x0999}).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[3].unionName), "Ceibos");
    ASSERT_TRUE(
        Apply(Guild::RelationshipResult{.relation = 1, .request = 2, .result = 1, .key = PlayerKey}).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[3].unionName), "");
    ASSERT_TRUE(Apply(Guild::RelationshipResult{.relation = 1, .request = 1, .result = 0x20, .key = 0}).HasValue());
    ASSERT_TRUE(Apply(Guild::RelationshipResult{.relation = 1, .request = 1, .result = 0x12, .key = 0}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{
        {Client::Ui::SystemMessage, "alianza rota"},
        {Client::Ui::SystemMessage, "alianza rota"},
        {Client::Ui::SystemMessage, "resultado 20"},
        // 0x45bb20 cuts from 30 bytes into two rows.
        {Client::Ui::SystemMessage, "Invalid GuildRelationShipResul"},
        {Client::Ui::SystemMessage, "t"}};
    EXPECT_EQ(Messages(), expected);
}

// 0x4d9940 without a guild (−1): writes the first byte of member 74 (Q-C1281).
TEST_F(GuildReceiveTest, BreakingWithoutAGuildWritesTheMemberBeforeTheTable)
{
    Fill(GuildOf().members[74].name, "Chingolo");
    ASSERT_TRUE(
        Apply(Guild::RelationshipResult{.relation = 1, .request = 2, .result = 1, .key = PlayerKey}).HasValue());
    EXPECT_EQ(GuildOf().members[74].name[0], '\0');
    EXPECT_EQ(GuildOf().members[74].name[1], 'h');
}

// 0x4d9e50 adds the rows with the mark in nibbles, each at the front (0x6517e0 → 0x46d900); 0x4d9b80 with 1 asks for
// the list again (with more than 2) and empties it (0x651830); with 0, the message.
TEST_F(GuildReceiveTest, TheUnionListAndItsRefresh)
{
    Guild::UnionEntry entry{.byte0 = 3, .mark = {}, .name = GuildNameOf("Ceibos")};
    entry.mark[0] = 0x4f;
    const Guild::UnionEntry last{.byte0 = 7, .mark = {}, .name = GuildNameOf("Talas")};
    ASSERT_TRUE(Apply(Guild::UnionList{.count = 3, .result = 1, .entries = {entry, entry, last}}).HasValue());
    ASSERT_EQ(GuildOf().window.unions.size(), 3u);
    EXPECT_EQ(TextOf(GuildOf().window.unions[0].name), "Talas");
    EXPECT_EQ(GuildOf().window.unions[0].byte0, 7);
    EXPECT_EQ(GuildOf().window.unions[1].mark[0], 4);
    EXPECT_EQ(GuildOf().window.unions[1].mark[1], 0xf);
    EXPECT_EQ(TextOf(GuildOf().window.unions[1].name), "Ceibos");
    EXPECT_EQ(GuildOf().window.unions[1].byte0, 3);
    EXPECT_EQ(GuildOf().window.unionList.selection, 3);

    ASSERT_TRUE(Apply(Guild::UnionListRefresh{.kind = 1, .result = 1}).HasValue());
    EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x03, 0xe9}}));
    EXPECT_TRUE(GuildOf().window.unions.empty());

    ASSERT_TRUE(Apply(Guild::UnionListRefresh{.kind = 1, .result = 0}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::Ui::SystemMessage, "sin alianza"}};
    EXPECT_EQ(Messages(), expected);
}

// 0x4d9810: the E1 with 1 gives the type's text; with another, the result's; otherwise, "Invalid GuildAssign".
TEST_F(GuildReceiveTest, TheAssignResultIsASystemMessage)
{
    ASSERT_TRUE(Apply(Guild::AssignResult{.type = 2, .result = 1}).HasValue());
    ASSERT_TRUE(Apply(Guild::AssignResult{.type = 9, .result = 1}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{
        {Client::Ui::SystemMessage, "asignado 2"}, {Client::Ui::SystemMessage, "Invalid GuildAssign"}};
    EXPECT_EQ(Messages(), expected);
}

// The war: the 61 stores the rival and opens dialog 0x7f; the 62 starts it (the notice, +0x374 of each character, the
// 0x576a60 gesture with 0xc0 and 0x94 and its C1 05 18); the 64 brings the scores; the 63 ends it with the winning
// gesture (0xa7, 0x89) and its C1 18. With the hero standing, each gesture animates and sends two equal C1 18
// (0x576a60's and the own one). In Lorencia, Devias and Arena (6).
TEST_F(GuildReceiveTest, AWarStartsScoresAndEndsWithTwoActions)
{
    for (const std::int32_t map : {0, 2, 6})
    {
        SCOPED_TRACE(map);
        Reset(map);
        NameTheGuild(1, 11, "Horneros");
        NameTheGuild(2, 22, "Teros");
        ActorState& hero = m_world->Actors()[0];
        hero.word1f2 = 1;
        hero.angles.z = 90.0f;
        hero.animation.action = Standing;
        Actor(5, "Calandria", 0x0105).word1f2 = 1;
        Actor(6, "Carancho", 0x0106).word1f2 = 2;
        Actor(7, "Chingolo", 0x0107);

        ASSERT_TRUE(Apply(Guild::WarRequest{.name = GuildNameOf("Teros"), .type = 1}).HasValue());
        EXPECT_EQ(TextOf(GuildOf().rival), "Teros");
        EXPECT_EQ(GuildOf().warType, 1);
        EXPECT_EQ(Runtime().dialogs.current, 0x7f);
        GuildOf().warType = 0;

        ASSERT_TRUE(Apply(Guild::WarStart{.name = GuildNameOf("Teros"), .type = 0, .team = 1}).HasValue());
        EXPECT_EQ(GuildOf().warOn, 1);
        EXPECT_EQ(GuildOf().warType, 0);
        EXPECT_EQ(GuildOf().warTeam, 1);
        EXPECT_EQ(GuildOf().rivalIndex, 2);
        EXPECT_EQ(LastNotice(), "guerra con Teros");
        EXPECT_EQ(hero.byte374, 1);
        EXPECT_EQ(m_world->Actors()[5].byte374, 1);
        EXPECT_EQ(m_world->Actors()[6].byte374, 2);
        EXPECT_EQ(m_world->Actors()[7].byte374, 0);
        // (90 + 22.5) / 45 + 1 = 3.5: sector 3.
        EXPECT_EQ(hero.animation.action, 0xc0);
        const std::vector<std::uint8_t> started{0xc1, 0x05, 0x18, 0x03, 0x94};
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{started, started}));

        ASSERT_TRUE(Apply(Guild::WarScore{.ours = 3, .theirs = 1}).HasValue());
        EXPECT_EQ(GuildOf().ourScore, 3);
        EXPECT_EQ(GuildOf().theirScore, 1);

        // The gesture's animation ended: the hero is standing again. 0x4d8958 sets the WORD 0x5767816 to 0, the F3 22's
        // time that the duel's AB also clears (0x4e4d60).
        hero.animation.action = Standing;
        Runtime().events.timeValue = 0x1234;
        ASSERT_TRUE(Apply(Guild::WarEnd{0}).HasValue());
        EXPECT_EQ(Runtime().events.timeValue, 0);
        EXPECT_EQ(LastNotice(), "fin 527");
        EXPECT_EQ(GuildOf().warOn, 0);
        EXPECT_EQ(GuildOf().rivalIndex, -1);
        EXPECT_EQ(TextOf(GuildOf().rival), "");
        EXPECT_EQ(m_world->Actors()[6].byte374, 0);
        EXPECT_EQ(m_world->Actors()[5].byte374, 1);
        EXPECT_EQ(hero.animation.action, 0xa7);
        const std::vector<std::uint8_t> won{0xc1, 0x05, 0x18, 0x03, 0x89};
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{started, started, won, won}));
        EXPECT_EQ(DrawsSince(), 0);
    }
}

// 0x4d8870: losing (1, 2 and 4) is the other gesture (0xad, 0x8d; the elf animates the next one, 0x576a9d); 6 has
// none; above 6 shows the uninitialized stack (Q-C1301): MuRet drops the 63 without stopping and the war goes on.
// Walking, 0x576a60 neither animates nor sends: only the own C1 18 is left.
TEST_F(GuildReceiveTest, TheWarEndGestureDependsOnTheResult)
{
    ActorState& hero = m_world->Actors()[0];
    hero.characterClass = ElfClass;
    hero.animation.action = Standing;
    // (0 + 22.5) / 45 + 1 = 1.5: sector 1.
    const std::vector<std::uint8_t> lost{0xc1, 0x05, 0x18, 0x01, 0x8d};
    ASSERT_TRUE(Apply(Guild::WarEnd{2}).HasValue());
    EXPECT_EQ(hero.animation.action, 0xae);
    EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{lost, lost}));
    ASSERT_TRUE(Apply(Guild::WarEnd{6}).HasValue());
    EXPECT_EQ(LastNotice(), "empate");
    EXPECT_EQ(m_sent.size(), 2u);

    hero.animation.action = Walking;
    ASSERT_TRUE(Apply(Guild::WarEnd{4}).HasValue());
    EXPECT_EQ(hero.animation.action, Walking);
    EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{lost, lost, lost}));

    GuildOf().warOn = 1;
    GuildOf().rivalIndex = 2;
    Fill(GuildOf().rival, "Teros");
    Runtime().events.timeValue = 0x1234;
    const std::string notice = LastNotice();
    const auto applied = Apply(Guild::WarEnd{7});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(GuildOf().warOn, 1);
    EXPECT_EQ(GuildOf().rivalIndex, 2);
    EXPECT_EQ(TextOf(GuildOf().rival), "Teros");
    EXPECT_EQ(Runtime().events.timeValue, 0x1234);
    EXPECT_EQ(LastNotice(), notice);
    EXPECT_EQ(m_sent.size(), 3u);
}

// Without 0x4ea030's models, the 62 stops after marking the characters and before the gesture and the C1 18s.
TEST_F(GuildReceiveTest, WithoutTheModelsTheWarStartIsCut)
{
    NameTheGuild(2, 22, "Teros");
    const auto applied = Apply(Guild::WarStart{.name = GuildNameOf("Teros"), .type = 1, .team = 0}, false);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x576a60"), std::string::npos);
    EXPECT_EQ(GuildOf().warOn, 1);
    EXPECT_EQ(GuildOf().warType, 1);
    EXPECT_EQ(LastNotice(), "partido con Teros");
    EXPECT_TRUE(m_sent.empty());
}

// 0x4d8150: with a result other than 1 and without a war, it clears the rival and unmarks.
TEST_F(GuildReceiveTest, ARefusedWarRequestForgetsTheRival)
{
    Fill(GuildOf().rival, "Teros");
    GuildOf().rivalIndex = 2;
    m_world->Actors()[0].byte374 = 2;
    ASSERT_TRUE(Apply(Guild::WarRequestResult{1}).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, 2);
    ASSERT_TRUE(Apply(Guild::WarRequestResult{3}).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, -1);
    EXPECT_EQ(TextOf(GuildOf().rival), "");
    EXPECT_EQ(m_world->Actors()[0].byte374, 0);
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::Ui::ErrorMessage, "guerra 520"},
                                                                     {Client::Ui::ErrorMessage, "guerra 522"}};
    EXPECT_EQ(Messages(), expected);
}
} // namespace
