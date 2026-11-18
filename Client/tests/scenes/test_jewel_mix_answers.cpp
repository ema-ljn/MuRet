#include "Scenes/JewelMixAnswers.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Text/TextTable.h"
#include "Network/Delivery.h"
#include "Protocol/JewelMix.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/LoginModels.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "World/GameCamera.h"

#include "support/Bytes.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Las respuestas del BC de la mezcla de joyas (0x4e4547 con 0x4e60a8 y 0x4da540 con 0x4da620, CL9 hito 6, fase 4b):
// los mensajes de tipo 1, el diálogo 0xa6 y la vuelta de 0x402e50. El GameServer 0.97 no manda el BC; ninguna saca
// rand(). Textos inventados; el mundo, en Lorencia (0), Devias (2) y Noria (3), el mapa del NPC de las joyas.
namespace
{
using Client::Network::Delivery;
using Client::Render::Models::CrtRandom;
using Client::Scenes::WorldEntryResources;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace JewelMix = Client::Protocol::JewelMix;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr std::uint32_t Seed = 0x2468;
// 0x6284e5: el diálogo que espera el BC, y los de elegir (0xa2 juntar, 0xa4 deshacer) y del resultado (0xa6).
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

    [[nodiscard]] Client::Core::Expected<void, std::string> Answer(int subcode, int result)
    {
        WorldEntryResources resources{.world = world,
                                      .selection = selection,
                                      .dataRoot = std::filesystem::path{},
                                      .cloth = cloth,
                                      .camera = camera,
                                      .cameraInput = cameraInput,
                                      .models = models,
                                      .log = log};
        resources.textMeasure = &measure;
        resources.texts = &texts;
        resources.sendNpcWindowPacket = [this](Client::Protocol::Outgoing packet)
        {
            sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
        return Client::Scenes::ApplyJewelMixAnswer(
            JewelMix::JewelMixAnswer{.subcode = static_cast<std::uint8_t>(subcode),
                                     .result = static_cast<std::uint8_t>(result)},
            resources);
    }

    // Lo que avisó 0x555190 con el tipo 1, en orden.
    [[nodiscard]] std::vector<std::string> Messages() const
    {
        std::vector<std::string> messages;
        for (const Client::UI::MessageAreaRow& row : world.Runtime().chat.area.rows)
        {
            if (row.type == Client::UI::SystemMessage && !row.text.empty())
                messages.push_back(row.text);
        }
        return messages;
    }

    [[nodiscard]] Client::Scenes::NpcWindowState& Npc()
    {
        return world.Runtime().npcWindows;
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{868, "zen"},
                                                                        {1800, "no se deshizo"},
                                                                        {1801, "no se junto"},
                                                                        {1811, "once"},
                                                                        {1812, "doce"},
                                                                        {1815, "quince"},
                                                                        {1816, ", falta"},
                                                                        {1817, "diecisiete"}});
    LetterMeasure measure;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::vector<Client::Protocol::Outgoing> sent;
};

// En el heap: WorldState pesa unos 350 KB (la pila de MSVC es de 1 MB).
std::unique_ptr<Stage> StageIn(std::int32_t map, std::int32_t mode = 0)
{
    return std::make_unique<Stage>(map, mode);
}

// Lo que deja 0x402e50 con el modo 0 o 1: 0x6e5798 y 0x6e5799 en 0, la joya sin elegir y el diálogo de elegir.
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

// 0x4e4592: el BC 00 con 0, 2 o 3, "%s%s %s" con 1801, 1816 y 868, y 0x402e50 de vuelta al 0xa2.
TEST(JewelMixAnswersTest, TheMixFailuresShowTheSentenceAndReturnToTheChoice)
{
    for (const std::int32_t map : Maps)
    {
        for (const std::uint8_t result : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}})
        {
            auto stage = StageIn(map);
            const CrtRandom expected = stage->cloth.random;
            const auto answered = stage->Answer(JewelMix::MixSubcode, result);
            ASSERT_TRUE(answered.HasValue()) << answered.Error();
            EXPECT_EQ(stage->Messages(), (std::vector<std::string>{"no se junto, falta zen"})) << map;
            ExpectTheChoice(*stage, ChoiceDialog);
            EXPECT_EQ(stage->cloth.random.State(), expected.State());
        }
    }
}

// 0x4e45f9 y 0x4e461e: el 4 con el 1817 y el 5 con el 1811, solos.
TEST(JewelMixAnswersTest, TheOtherMixAnswersShowTheirText)
{
    for (const std::int32_t map : Maps)
    {
        for (const auto& [result, text] :
             {std::pair<int, std::string_view>{4, "diecisiete"}, std::pair<int, std::string_view>{5, "once"}})
        {
            auto stage = StageIn(map);
            ASSERT_TRUE(stage->Answer(JewelMix::MixSubcode, result).HasValue());
            EXPECT_EQ(stage->Messages(), (std::vector<std::string>{std::string(text)})) << map;
            ExpectTheChoice(*stage, ChoiceDialog);
        }
    }
}

// 0x4e45d9 y 0x4da5af: el 1 cierra el 0xa5 y abre el 0xa6, sin mensaje ni 0x402e50.
TEST(JewelMixAnswersTest, TheSuccessOpensTheResultDialog)
{
    for (const std::int32_t map : Maps)
    {
        for (const std::uint8_t subcode : {JewelMix::MixSubcode, JewelMix::UnmixSubcode})
        {
            auto stage = StageIn(map, subcode);
            ASSERT_TRUE(stage->Answer(subcode, 1).HasValue());
            EXPECT_EQ(stage->world.Runtime().dialogs.current, ResultDialog);
            EXPECT_TRUE(stage->Messages().empty());
            EXPECT_EQ(stage->Npc().field6e5798, 1);
            EXPECT_EQ(stage->Npc().jewels.refusal, 5);
            EXPECT_EQ(stage->Npc().jewels.kind, 1);
        }
    }
}

// 0x4da540: el BC 01 con 0 o 5 arma la frase con 1800; con 2, 3, 4 o 6 el 1812, con 7 el 1815 y con 8 el 1811. Todos
// vuelven al 0xa4.
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
            const auto answered = stage->Answer(JewelMix::UnmixSubcode, result);
            ASSERT_TRUE(answered.HasValue()) << answered.Error();
            EXPECT_EQ(stage->Messages(), (std::vector<std::string>{std::string(text)})) << result;
            ExpectTheChoice(*stage, UnmixDialog);
        }
    }
}

// Fuera de las tablas (más de 5 con el 0, más de 8 con el 1) o con otro subcódigo, nada.
TEST(JewelMixAnswersTest, OutOfTheTablesNothingHappens)
{
    for (const auto& [subcode, result] : {std::pair<int, int>{0, 6}, {0, 0xff}, {1, 9}, {2, 0}})
    {
        auto stage = StageIn(0);
        ASSERT_TRUE(stage->Answer(subcode, result).HasValue());
        EXPECT_TRUE(stage->Messages().empty());
        EXPECT_EQ(stage->world.Runtime().dialogs.current, WaitingDialog);
        EXPECT_EQ(stage->Npc().field6e5798, 1);
        EXPECT_TRUE(stage->sent.empty());
    }
}

// 0x402e50 con un modo que no es 0 ni 1 cierra todo (0x402eb0): 0x402e10 y el C1 03 31 en claro.
TEST(JewelMixAnswersTest, WithAnotherModeTheReturnClosesTheMix)
{
    auto stage = StageIn(2, 7);
    ASSERT_TRUE(stage->Answer(JewelMix::MixSubcode, 5).HasValue());
    EXPECT_EQ(stage->Messages(), (std::vector<std::string>{"once"}));
    ASSERT_EQ(stage->sent.size(), 1u);
    EXPECT_EQ(stage->sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    EXPECT_EQ(stage->Npc().jewels.mode, 0);
    EXPECT_EQ(stage->Npc().jewels.count, 0xff);
    EXPECT_EQ(stage->Npc().jewels.price, -1);
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0);
}

// El sprintf de 0x4e45ad escribe en 0x100 bytes: una frase de 0x100 o más (con su 0) corta.
TEST(JewelMixAnswersTest, ASentenceThatDoesNotFitTheBufferCuts)
{
    auto stage = StageIn(0);
    stage->texts = Client::Tests::MakeTextTable(
        {{868, std::string(0x80, 'z')}, {1801, std::string(0x7f, 'j')}, {1816, ""}, {1817, "diecisiete"}});
    const auto overflow = stage->Answer(JewelMix::MixSubcode, 0);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x4e4570(0)"), std::string::npos) << overflow.Error();
    EXPECT_TRUE(stage->Messages().empty());

    stage->texts =
        Client::Tests::MakeTextTable({{868, std::string(0x7f, 'z')}, {1801, std::string(0x7f, 'j')}, {1816, ""}});
    // 0xff letras y el 0 entran justo; el área lo parte en renglones.
    ASSERT_TRUE(stage->Answer(JewelMix::MixSubcode, 0).HasValue());
    ASSERT_FALSE(stage->Messages().empty());
}
} // namespace
