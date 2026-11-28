#include "Events/EventPackets.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/MessageArea.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Protocol/Opcodes.h"
#include "Chat/ChatRuntime.h"
#include "Ui/Dialogs.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The BC answers of the jewel mix (0x4e4547 with 0x4e60a8 and 0x4da540 with 0x4da620, CL9 milestones 6 and 11): the
// type 1 messages, dialog 0xa6 and the return of 0x402e50, through Events::ApplyEventPacket. GameServer 0.97 does not
// send BC; none draws rand(). Made-up texts; the world in Lorencia (0), Devias (2) and Noria (3), the map of the jewel
// NPC.
namespace
{
using Client::Crt::CrtRandom;
using Client::Network::Delivery;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Events = Client::Protocol::Events;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr std::uint32_t Seed = 0x2468;
// 0x6284e5: the dialog BC waits for, and those of the choice (0xa2 mix, 0xa4 unmix) and of the result (0xa6).
constexpr std::int32_t WaitingDialog = 0xa5;
constexpr std::int32_t ChoiceDialog = 0xa2;
constexpr std::int32_t UnmixDialog = 0xa4;
constexpr std::int32_t ResultDialog = 0xa6;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

struct Stage
{
    explicit Stage(std::int32_t map, std::int32_t mode)
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.dialogs.current = WaitingDialog;
        auto& npc = runtime.npcWindows;
        npc.field6e5798 = 1;
        npc.jewels.mode = mode;
        npc.jewels.refusal = 5;
        npc.jewels.kind = 1;
        npc.jewels.count = 20;
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Answer(Client::Network::Subcode subcode, int result)
    {
        const Client::Events::EventPacketContext context{.world = world,
                                                         .random = random,
                                                         .texts = &texts,
                                                         .measure = &measure,
                                                         .send = [this](Client::Protocol::Outgoing packet)
                                                         { sent.push_back(std::move(packet)); }};
        auto applied = Client::Events::ApplyEventPacket(
            Events::JewelMixResult{.subcode = subcode, .result = static_cast<std::uint8_t>(result)}, context);
        if (!applied.HasValue())
            return Client::Core::Unexpected(std::move(applied).Error());
        return {};
    }

    // What 0x555190 announced with type 1, in order.
    [[nodiscard]] std::vector<std::string> Messages() const
    {
        std::vector<std::string> messages;
        for (const Client::Ui::MessageAreaRow& row : world.Runtime().chat.area.rows)
        {
            if (row.type == Client::Ui::SystemMessage && !row.text.empty())
                messages.push_back(row.text);
        }
        return messages;
    }

    [[nodiscard]] Client::Npc::NpcWindowState& Npc()
    {
        return world.Runtime().npcWindows;
    }

    Client::Kernel::WorldState world;
    CrtRandom random{Seed};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{868, "zen"},
                                                                    {1800, "no se deshizo"},
                                                                    {1801, "no se junto"},
                                                                    {1811, "once"},
                                                                    {1812, "doce"},
                                                                    {1815, "quince"},
                                                                    {1816, ", falta"},
                                                                    {1817, "diecisiete"}});
    LetterMeasure measure;
    std::vector<Client::Protocol::Outgoing> sent;
};

// On the heap: WorldState weighs about 350 KB (the MSVC stack is 1 MB).
std::unique_ptr<Stage> StageIn(std::int32_t map, std::int32_t mode = 0)
{
    return std::make_unique<Stage>(map, mode);
}

// What 0x402e50 leaves with mode 0 or 1: 0x6e5798 and 0x6e5799 at 0, the jewel unchosen and the choice dialog.
void ExpectTheChoice(Stage& stage, std::int32_t dialog)
{
    auto& npc = stage.Npc();
    EXPECT_EQ(npc.field6e5798, 0);
    EXPECT_EQ(npc.jewels.refusal, 0);
    EXPECT_EQ(npc.jewels.kind, 0xff);
    EXPECT_EQ(npc.jewels.count, 20);
    EXPECT_EQ(stage.world.Runtime().dialogs.current, dialog);
    EXPECT_EQ(stage.world.Runtime().dialogs.pending, 0);
    EXPECT_TRUE(stage.sent.empty());
}

// 0x4e4592: BC 00 with 0, 2 or 3, "%s%s %s" with 1801, 1816 and 868, and 0x402e50 back to the 0xa2.
TEST(JewelMixAnswersTest, TheMixFailuresShowTheSentenceAndReturnToTheChoice)
{
    for (const std::int32_t map : Maps)
    {
        for (const std::uint8_t result : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}})
        {
            auto stage = StageIn(map);
            const CrtRandom expected = stage->random;
            const auto answered = stage->Answer(Client::Protocol::JewelMix::MixSubcode, result);
            ASSERT_TRUE(answered.HasValue()) << answered.Error();
            EXPECT_EQ(stage->Messages(), (std::vector<std::string>{"no se junto, falta zen"})) << map;
            ExpectTheChoice(*stage, ChoiceDialog);
            EXPECT_EQ(stage->random.State(), expected.State());
        }
    }
}

// 0x4e45f9 and 0x4e461e: 4 with 1817 and 5 with 1811, alone.
TEST(JewelMixAnswersTest, TheOtherMixAnswersShowTheirText)
{
    for (const std::int32_t map : Maps)
    {
        for (const auto& [result, text] :
             {std::pair<int, std::string_view>{4, "diecisiete"}, std::pair<int, std::string_view>{5, "once"}})
        {
            auto stage = StageIn(map);
            ASSERT_TRUE(stage->Answer(Client::Protocol::JewelMix::MixSubcode, result).HasValue());
            EXPECT_EQ(stage->Messages(), (std::vector<std::string>{std::string(text)})) << map;
            ExpectTheChoice(*stage, ChoiceDialog);
        }
    }
}

// 0x4e45d9 and 0x4da5af: 1 closes the 0xa5 and opens the 0xa6, without a message or 0x402e50.
TEST(JewelMixAnswersTest, TheSuccessOpensTheResultDialog)
{
    for (const std::int32_t map : Maps)
    {
        for (const Client::Network::Subcode subcode :
             {Client::Protocol::JewelMix::MixSubcode, Client::Protocol::JewelMix::UnmixSubcode})
        {
            auto stage = StageIn(map, Client::Network::ByteOf(subcode));
            ASSERT_TRUE(stage->Answer(subcode, 1).HasValue());
            EXPECT_EQ(stage->world.Runtime().dialogs.current, ResultDialog);
            EXPECT_TRUE(stage->Messages().empty());
            EXPECT_EQ(stage->Npc().field6e5798, 1);
            EXPECT_EQ(stage->Npc().jewels.refusal, 5);
            EXPECT_EQ(stage->Npc().jewels.kind, 1);
        }
    }
}

// 0x4da540: BC 01 with 0 or 5 builds the sentence with 1800; with 2, 3, 4 or 6, 1812, with 7, 1815 and with 8, 1811.
// All go back to the 0xa4.
TEST(JewelMixAnswersTest, TheUnmixAnswersFollowTheirTable)
{
    const std::array<std::pair<int, std::string_view>, 8> answers{{{0, "no se deshizo, falta zen"},
                                                                   {2, "doce"},
                                                                   {3, "doce"},
                                                                   {4, "doce"},
                                                                   {5, "no se deshizo, falta zen"},
                                                                   {6, "doce"},
                                                                   {7, "quince"},
                                                                   {8, "once"}}};
    for (const std::int32_t map : Maps)
    {
        for (const auto& [result, text] : answers)
        {
            auto stage = StageIn(map, 1);
            const auto answered = stage->Answer(Client::Protocol::JewelMix::UnmixSubcode, result);
            ASSERT_TRUE(answered.HasValue()) << answered.Error();
            EXPECT_EQ(stage->Messages(), (std::vector<std::string>{std::string(text)})) << result;
            ExpectTheChoice(*stage, UnmixDialog);
        }
    }
}

// Outside the tables (above 5 with 0, above 8 with 1), nothing. Another subcode does not arrive: the session registers
// BC only with 0 and 1.
TEST(JewelMixAnswersTest, OutOfTheTablesNothingHappens)
{
    for (const auto& [subcode, result] :
         {std::pair<Client::Network::Subcode, int>{Client::Protocol::JewelMix::MixSubcode, 6},
          {Client::Protocol::JewelMix::MixSubcode, 0xff},
          {Client::Protocol::JewelMix::UnmixSubcode, 9}})
    {
        auto stage = StageIn(0);
        ASSERT_TRUE(stage->Answer(subcode, result).HasValue());
        EXPECT_TRUE(stage->Messages().empty());
        EXPECT_EQ(stage->world.Runtime().dialogs.current, WaitingDialog);
        EXPECT_EQ(stage->Npc().field6e5798, 1);
        EXPECT_TRUE(stage->sent.empty());
    }
}

// 0x402e50 with a mode that is neither 0 nor 1 closes everything (0x402eb0): 0x402e10 and C1 03 31 in the clear.
TEST(JewelMixAnswersTest, WithAnotherModeTheReturnClosesTheMix)
{
    auto stage = StageIn(2, 7);
    ASSERT_TRUE(stage->Answer(Client::Protocol::JewelMix::MixSubcode, 5).HasValue());
    EXPECT_EQ(stage->Messages(), (std::vector<std::string>{"once"}));
    ASSERT_EQ(stage->sent.size(), 1u);
    EXPECT_EQ(stage->sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    EXPECT_EQ(stage->Npc().jewels.mode, 0);
    EXPECT_EQ(stage->Npc().jewels.count, 0xff);
    EXPECT_EQ(stage->Npc().jewels.price, -1);
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0);
}

// The sprintf of 0x4e45ad writes into 0x100 bytes: a sentence of 0x100 or more (with its 0) stops.
TEST(JewelMixAnswersTest, ASentenceThatDoesNotFitTheBufferCuts)
{
    auto stage = StageIn(0);
    stage->texts = Client::Tests::MakeTextTable(
        {{868, std::string(0x80, 'z')}, {1801, std::string(0x7f, 'j')}, {1816, ""}, {1817, "diecisiete"}});
    const auto overflow = stage->Answer(Client::Protocol::JewelMix::MixSubcode, 0);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x4e45ad"), std::string::npos) << overflow.Error();
    EXPECT_TRUE(stage->Messages().empty());

    stage->texts =
        Client::Tests::MakeTextTable({{868, std::string(0x7f, 'z')}, {1801, std::string(0x7f, 'j')}, {1816, ""}});
    // 0xff letters and the 0 fit exactly; the area splits it into lines.
    ASSERT_TRUE(stage->Answer(Client::Protocol::JewelMix::MixSubcode, 0).HasValue());
    ASSERT_FALSE(stage->Messages().empty());
}
} // namespace
