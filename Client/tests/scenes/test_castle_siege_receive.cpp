#include "Network/Xor32Packet.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/GamePanels.h"
#include "Scenes/CastleWindows.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleSiegeState.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"

#include "support/Bytes.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los paquetes del castillo fuera de la batalla sobre el mundo (CL9 hito 12; 04 § Castle Siege, las ventanas del
// castillo). Ningún caso mira el mapa: cada test corre en Lorencia (0), Devias (2) y el castillo (30). Los nombres y
// los textos son inventados; el estado del rand() se compara para ver que ninguno saca.
namespace
{
namespace Castle = Client::Protocol::CastleSiege;
using Client::Crt::CrtRandom;
using Client::Network::Xor32Decode;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::CastleReceiveContext;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::UI::CastleData;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr std::uint32_t Seed = 4242;

std::map<std::size_t, std::string> CastleTexts()
{
    std::map<std::size_t, std::string> texts;
    for (const std::size_t id :
         {std::size_t{860}, std::size_t{1386}, std::size_t{1609}, std::size_t{1616}, std::size_t{1641}})
        texts[id] = "aviso " + std::to_string(id);
    for (std::size_t id = 1500; id <= 1527; ++id)
        texts[id] = "aviso " + std::to_string(id);
    return texts;
}

std::string Text(std::size_t id)
{
    return "aviso " + std::to_string(id);
}

class CastleSiegeReceiveTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    CastleSiegeReceiveTest() : m_world(std::make_unique<WorldState>()), m_random(Seed)
    {
        m_world->Runtime().map = GetParam();
    }

    [[nodiscard]] CastleReceiveContext Context(bool withSender = true)
    {
        CastleReceiveContext context{.world = *m_world,
                                     .random = m_random,
                                     .log = m_log,
                                     .measure = &m_letters,
                                     .screen = Client::Core::PixelSize{640, 480},
                                     .texts = &m_texts};
        context.tick = 1234;
        if (withSender)
            context.send = [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); };
        return context;
    }

    // Aplica y espera que no corte.
    void Apply(const Castle::CastleSiegePacket& packet)
    {
        const auto applied = Client::Scenes::ApplyCastleSiegePacket(packet, Context());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
    }

    // El error con que corta.
    [[nodiscard]] std::string CutOf(const Castle::CastleSiegePacket& packet)
    {
        const auto applied = Client::Scenes::ApplyCastleSiegePacket(packet, Context());
        EXPECT_FALSE(applied.HasValue());
        return applied.HasValue() ? std::string{} : applied.Error();
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    [[nodiscard]] Client::UI::CastleSiegeState& Castle()
    {
        return Runtime().castleSiege;
    }

    // Los mensajes que llegaron al chat, en el orden en que llegaron (el registro guarda el último adelante).
    [[nodiscard]] std::vector<std::string> Messages(std::int32_t type = Client::UI::SystemMessage)
    {
        std::vector<std::string> texts;
        const auto& entries = Runtime().chat.log.entries;
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
        {
            EXPECT_EQ(entry->type, type);
            texts.push_back(entry->text);
        }
        return texts;
    }

    // El rand() no se movió.
    void ExpectNoRandom()
    {
        EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
    }

    // Que un warning del log diga `part`.
    [[nodiscard]] bool Warned(std::string_view part) const
    {
        for (const std::string& line : m_sink.lines)
        {
            if (line.find(part) != std::string::npos)
                return true;
        }
        return false;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    LetterMeasure m_letters;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(CastleTexts());
    std::vector<Client::Protocol::Outgoing> m_sent;
};

INSTANTIATE_TEST_SUITE_P(Maps, CastleSiegeReceiveTest, ::testing::Values(0, 2, 30));

template <std::size_t Size> std::array<std::byte, Size> Name(std::string_view text)
{
    std::array<std::byte, Size> name{};
    for (std::size_t i = 0; i < text.size(); ++i)
        name.at(i) = static_cast<std::byte>(text[i]);
    return name;
}

Castle::CastleState OwnedState(std::uint8_t result)
{
    Castle::CastleState state{.result = result, .state = -2};
    state.dates[0] = {.year = 2026, .rest = {11, 14, 20, 30}};
    state.dates[2] = {.year = 1, .rest = {0xff, 0x80, 0x7f, 0}};
    state.ownerGuild = Name<8>("Roca");
    state.ownerGuild[5] = std::byte{'x'}; // basura detrás del 0: strncpy la pone en 0
    state.ownerMaster = Name<10>("Lechuzaabc");
    state.remaining = 0x01020304;
    return state;
}

// 0x4e50f2: con el panel 0x80000 ya abierto, 0x6599a0 sale con 0 sin sacar rand() y 0x64b0f0 guarda todo.
TEST_P(CastleSiegeReceiveTest, StateWithTheRegistrationOpenStoresTheCastleState)
{
    Runtime().panels.delegated.castleSiege = 1;
    Apply(OwnedState(2));
    const Client::UI::SiegeRegistrationWindow& window = Castle().registration;
    EXPECT_EQ(window.state, -2);
    EXPECT_EQ(window.ownerGuild, (std::array<std::uint8_t, 9>{'R', 'o', 'c', 'a', 0, 0, 0, 0, 0}));
    EXPECT_EQ(window.ownerMaster, (std::array<std::uint8_t, 11>{'L', 'e', 'c', 'h', 'u', 'z', 'a', 'a', 'b', 'c', 0}));
    EXPECT_EQ(window.dates[0].year, 2026);
    EXPECT_EQ(window.dates[0].rest, (std::array<std::uint8_t, 4>{11, 14, 20, 30}));
    EXPECT_EQ(window.dates[1].year, 0);
    EXPECT_EQ(window.dates[2].rest, (std::array<std::uint8_t, 4>{0xff, 0x80, 0x7f, 0}));
    EXPECT_EQ(window.remaining, 0x01020304u);
    EXPECT_EQ(Runtime().panels.delegated.castleSiege, 1);
    EXPECT_TRUE(Messages().empty());
    ExpectNoRandom();
}

// 0x64b0f0 a secas: +4 en 1 aunque el panel estuviera cerrado.
TEST_P(CastleSiegeReceiveTest, StoreTheCastleStateMarksTheRegistrationOpen)
{
    Client::Scenes::StoreTheCastleState(OwnedState(1), Runtime());
    EXPECT_EQ(Runtime().panels.delegated.castleSiege, 1);
    EXPECT_EQ(Castle().registration.state, -2);
}

// 0x65bef0..0x65bf0d y 0x64ae10: abrir pide B2 03 antes de guardar el estado de B2 00.
TEST_P(CastleSiegeReceiveTest, StateWithTheRegistrationClosedOpensAndAsksForTheRegistration)
{
    const auto applied = Client::Scenes::ApplyCastleSiegePacket(OwnedState(1), Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Castle().registration.state, -2);
    EXPECT_EQ(Runtime().panels.delegated.castleSiege, 1);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Xor32Decode(std::move(m_sent[0].packet)).Bytes(), Bytes({0xc1, 4, 0xb2, 3})));

    // 0x6599be: ya abierta, la siguiente respuesta no vuelve a pedir la lista.
    Apply(OwnedState(2));
    EXPECT_EQ(m_sent.size(), 1u);
}

// 0x657b76..0x657b99 llama 0x64b200: C1 03 31 antes de apagar el byte abierto.
TEST_P(CastleSiegeReceiveTest, ClosingTheRegistrationSendsTheWindowCloseOnlyOnce)
{
    Runtime().panels.delegated.castleSiege = 1;
    Castle().registration.tab = 2;
    Client::Scenes::GamePanels panels(*m_world, m_random);
    panels.LendTheCastleRequests(*m_world, Context().send);
    const auto closed = panels.Registry().CloseByMask(Client::UI::CastleSiegePanel);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_EQ(Runtime().panels.delegated.castleSiege, 0);
    EXPECT_EQ(Castle().registration.tab, 0);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_TRUE(SameBytes(m_sent[0].packet.Bytes(), Bytes({0xc1, 3, 0x31})));
    Castle().registration.tab = 3;
    const auto repeated = panels.Registry().CloseByMask(Client::UI::CastleSiegePanel);
    ASSERT_TRUE(repeated.HasValue()) << repeated.Error();
    EXPECT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(Castle().registration.tab, 3);
    ExpectNoRandom();
}

TEST_P(CastleSiegeReceiveTest, OpeningTheSeniorRequestsBothNpcListsAndTaxesInOrder)
{
    Client::Scenes::GamePanels panels(*m_world, m_random);
    panels.LendTheCastleRequests(*m_world, Context().send);
    const auto opened = panels.Registry().Open(Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(Runtime().panels.delegated.castleSiegeList, 1);
    EXPECT_EQ(Runtime().panels.delegated.castleSiegeListMode, 0);
    ASSERT_EQ(m_sent.size(), 3u);
    EXPECT_TRUE(SameBytes(Xor32Decode(m_sent[0].packet).Bytes(), Bytes({0xc1, 4, 0xb3, 1})));
    EXPECT_TRUE(SameBytes(Xor32Decode(m_sent[1].packet).Bytes(), Bytes({0xc1, 4, 0xb3, 2})));
    EXPECT_TRUE(SameBytes(Xor32Decode(m_sent[2].packet).Bytes(), Bytes({0xc1, 4, 0xb2, 8})));
    const auto repeated = panels.Registry().Open(Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(repeated.HasValue()) << repeated.Error();
    EXPECT_EQ(m_sent.size(), 3u);
}

TEST_P(CastleSiegeReceiveTest, TheHuntWindowOpensWithoutARequestAndClosesWith31)
{
    Client::Scenes::GamePanels panels(*m_world, m_random);
    panels.LendTheCastleRequests(*m_world, Context().send);
    const auto opened = panels.Registry().Open(Client::UI::SecondCastleSiegePanel);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(Runtime().panels.delegated.secondCastleSiege, 1);
    EXPECT_TRUE(m_sent.empty());
    const auto closed = panels.Registry().CloseByMask(Client::UI::SecondCastleSiegePanel);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_EQ(Runtime().panels.delegated.secondCastleSiege, 0);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_TRUE(SameBytes(m_sent[0].packet.Bytes(), Bytes({0xc1, 3, 0x31})));
}

TEST_P(CastleSiegeReceiveTest, StateWithZeroThreeOrFourShowsItsMessage)
{
    Apply(Castle::CastleState{.result = 0});
    Apply(Castle::CastleState{.result = 3});
    Apply(Castle::CastleState{.result = 4});
    Apply(Castle::CastleState{.result = 5});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1500), Text(1501), Text(1502)}));
    EXPECT_EQ(Castle().registration.state, -1);
}

// B2 01 (0x4df080): del 1503 al 1511, y con 1 el gremio queda registrado (0x64d290(1)).
TEST_P(CastleSiegeReceiveTest, RegisterShowsOneMessagePerResult)
{
    std::vector<std::string> expected;
    for (std::uint8_t result = 0; result <= 9; ++result)
    {
        Apply(Castle::RegisterResult{.result = result});
        if (result <= 8)
            expected.push_back(Text(1503 + std::size_t{result}));
        EXPECT_EQ(Castle().registration.registered, result >= 1 ? 1u : 0u) << int{result};
    }
    EXPECT_EQ(Messages(), expected);
    EXPECT_TRUE(m_sent.empty());
}

// B2 02 con 1 (0x4df1e7): el C1 04 B2 03 y el C1 03 B4, el registro en 0 y el 1513.
TEST_P(CastleSiegeReceiveTest, GiveUpWithOneAsksAgainAndClearsTheRegistration)
{
    Castle().registration.registered = 1;
    Apply(Castle::GiveUpResult{.result = 1});
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_TRUE(SameBytes(Xor32Decode(std::move(m_sent[0].packet)).Bytes(), Bytes({0xc1, 4, 0xb2, 3})));
    EXPECT_TRUE(SameBytes(m_sent[1].packet.Bytes(), Bytes({0xc1, 3, 0xb4})));
    EXPECT_EQ(Castle().registration.registered, 0u);
    Apply(Castle::GiveUpResult{.result = 0});
    Apply(Castle::GiveUpResult{.result = 2});
    Apply(Castle::GiveUpResult{.result = 3});
    Apply(Castle::GiveUpResult{.result = 4});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1513), Text(1512), Text(1514), Text(1515)}));
}

TEST_P(CastleSiegeReceiveTest, GiveUpWithOneAndNoGameServerCutsBeforeAnything)
{
    Castle().registration.registered = 1;
    const auto applied =
        Client::Scenes::ApplyCastleSiegePacket(Castle::GiveUpResult{.result = 1}, Context(/*withSender=*/false));
    EXPECT_FALSE(applied.HasValue());
    EXPECT_EQ(Castle().registration.registered, 1u);
    EXPECT_TRUE(Messages().empty());
}

// B2 03 (0x4e5186): con 0 o 2 el registro en 0; con 1, registrado si pkt[0x11] es 0, y las marcas.
TEST_P(CastleSiegeReceiveTest, RegistrationInfoSetsTheRegistrationAndTheMarks)
{
    Apply(Castle::RegistrationInfo{.result = 1, .marks = 300, .gaveUp = 0});
    EXPECT_EQ(Castle().registration.registered, 1u);
    EXPECT_EQ(Castle().registration.marks, 300u);
    Apply(Castle::RegistrationInfo{.result = 3, .marks = 5, .gaveUp = 0});
    EXPECT_EQ(Castle().registration.registered, 1u);
    EXPECT_EQ(Castle().registration.marks, 300u);
    Apply(Castle::RegistrationInfo{.result = 1, .marks = 12, .gaveUp = 1});
    EXPECT_EQ(Castle().registration.registered, 0u);
    EXPECT_EQ(Castle().registration.marks, 12u);
    Castle().registration.registered = 1;
    Apply(Castle::RegistrationInfo{.result = 2});
    EXPECT_EQ(Castle().registration.registered, 0u);
    Castle().registration.registered = 1;
    Apply(Castle::RegistrationInfo{.result = 0});
    EXPECT_EQ(Castle().registration.registered, 0u);
    EXPECT_TRUE(Messages().empty());
}

TEST_P(CastleSiegeReceiveTest, RegisterMarkWithOneKeepsTheMarksWithoutAMessage)
{
    Apply(Castle::RegisterMarkResult{.result = 1, .marks = 77});
    EXPECT_EQ(Castle().registration.marks, 77u);
    Apply(Castle::RegisterMarkResult{.result = 0});
    Apply(Castle::RegisterMarkResult{.result = 2});
    Apply(Castle::RegisterMarkResult{.result = 3});
    Apply(Castle::RegisterMarkResult{.result = 4});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1516), Text(1517), Text(1518)}));
}

std::array<std::uint8_t, 0x1c> RecordAt(const CastleData& data, std::uint32_t address)
{
    std::array<std::uint8_t, 0x1c> record{};
    for (std::size_t i = 0; i < record.size(); ++i)
        record[i] = data.bytes.at(address - Client::UI::CastleDataBase + i);
    return record;
}

// B2 05 con 1 (0x4df806 → 0x668fd0): el registro de la pila, 500000 de vida para una puerta y 400000 para una estatua.
TEST_P(CastleSiegeReceiveTest, NpcBuyWithOneStoresTheBoughtRecord)
{
    Apply(Castle::NpcBuyResult{.result = 1, .type = 0x115, .index = 2});
    EXPECT_EQ(RecordAt(Castle().data, 0x7d5c574 + 2 * 0x1c),
              (std::array<std::uint8_t, 0x1c>{0x15, 1, 0,    0,    2,    0, 0,    0,    0,    0, 0, 0, 0, 0,
                                              0,    0, 0x20, 0xa1, 0x07, 0, 0x20, 0xa1, 0x07, 0, 0, 0, 1, 0}));
    Apply(Castle::NpcBuyResult{.result = 1, .type = 0x11b, .index = 0});
    EXPECT_EQ(Castle().data.U32(0x7d5c504), 0x11bu);
    EXPECT_EQ(Castle().data.U32(0x7d5c504 + 0x10), 400000u);
    EXPECT_EQ(Castle().data.U32(0x7d5c504 + 0x14), 400000u);
    EXPECT_EQ(Castle().data.bytes.at(0x1a), 1);
    EXPECT_TRUE(Messages().empty());
}

// 0x668fd0 no copia un tipo que no es puerta ni estatua; la puerta 7 cae sobre las elegidas y los impuestos, dentro de
// la zona; la 8 saldría de la zona y se descarta (Q-C1360).
TEST_P(CastleSiegeReceiveTest, NpcBuyWritesWhereTheIndexSaysOrDiscardsOutsideTheZone)
{
    Apply(Castle::NpcBuyResult{.result = 1, .type = 0x120, .index = 0});
    EXPECT_EQ(Castle().data.bytes, CastleData{}.bytes);
    Apply(Castle::NpcBuyResult{.result = 1, .type = 0x115, .index = 7});
    EXPECT_EQ(Castle().data.U32(0x7d5c638), 0x115u);
    EXPECT_EQ(Castle().data.U32(0x7d5c648), 500000u);
    const auto before = Castle().data.bytes;
    Apply(Castle::NpcBuyResult{.result = 1, .type = 0x115, .index = 8});
    Apply(Castle::NpcBuyResult{.result = 1, .type = 0x11b, .index = 0xffffffff});
    EXPECT_EQ(Castle().data.bytes, before);
    EXPECT_TRUE(Warned("Q-C1360"));
}

TEST_P(CastleSiegeReceiveTest, NpcBuyMessages)
{
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3, 4, 5})
        Apply(Castle::NpcBuyResult{.result = result});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1519), Text(1386), Text(1520), Text(1616)}));
}

// B2 06 con 1 (0x4e52ec): 0x669030 elige las puertas solo con 0x115; pkt+0x10 va a +0x14 y pkt+0x14 a +0x10.
TEST_P(CastleSiegeReceiveTest, NpcRepairWritesBothLivesCrossed)
{
    Apply(Castle::NpcRepairResult{.result = 1, .type = 0x115, .index = 1, .life = 111, .maxLife = 222});
    EXPECT_EQ(Castle().data.U32(0x7d5c574 + 0x1c + 0x14), 111u);
    EXPECT_EQ(Castle().data.U32(0x7d5c574 + 0x1c + 0x10), 222u);
    Apply(Castle::NpcRepairResult{.result = 1, .type = 0x120, .index = 3, .life = 5, .maxLife = 6});
    EXPECT_EQ(Castle().data.U32(0x7d5c504 + 3 * 0x1c + 0x14), 5u);
    EXPECT_EQ(Castle().data.U32(0x7d5c504 + 3 * 0x1c + 0x10), 6u);
    const auto before = Castle().data.bytes;
    Apply(Castle::NpcRepairResult{.result = 1, .type = 0x115, .index = 8, .life = 1, .maxLife = 1});
    EXPECT_EQ(Castle().data.bytes, before);
    EXPECT_TRUE(Warned("el B2 06"));
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3, 4})
        Apply(Castle::NpcRepairResult{.result = result});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1519), Text(1386), Text(1520)}));
}

// B2 07 con 1 (0x4df9a5): 1 la defensa (+8), 2 la recuperación (+0xc), 3 las dos vidas; otro, nada.
TEST_P(CastleSiegeReceiveTest, NpcUpgradeWritesWhatItSays)
{
    const std::uint32_t gate = 0x7d5c574 + 0x1c;
    Apply(Castle::NpcUpgradeResult{.result = 1, .type = 0x115, .index = 1, .kind = 1, .value = 3});
    Apply(Castle::NpcUpgradeResult{.result = 1, .type = 0x115, .index = 1, .kind = 2, .value = 4});
    Apply(Castle::NpcUpgradeResult{.result = 1, .type = 0x115, .index = 1, .kind = 3, .value = 800000});
    EXPECT_EQ(Castle().data.U32(gate + 8), 3u);
    EXPECT_EQ(Castle().data.U32(gate + 0xc), 4u);
    EXPECT_EQ(Castle().data.U32(gate + 0x10), 800000u);
    EXPECT_EQ(Castle().data.U32(gate + 0x14), 800000u);
    const auto before = Castle().data.bytes;
    Apply(Castle::NpcUpgradeResult{.result = 1, .type = 0x115, .index = 1, .kind = 4, .value = 9});
    Apply(Castle::NpcUpgradeResult{.result = 1, .type = 0x11b, .index = 12, .kind = 1, .value = 9});
    EXPECT_EQ(Castle().data.bytes, before);
    EXPECT_TRUE(Warned("el B2 07"));
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3, 4, 5, 6, 7, 8})
        Apply(Castle::NpcUpgradeResult{.result = result});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1519), Text(1386), Text(1520), Text(1521), Text(1522),
                                                    Text(1523), Text(1524)}));
}

// B2 08 con 1 (0x669070): los dos pares de impuestos y la plata.
TEST_P(CastleSiegeReceiveTest, TaxInfoWritesBothTaxPairsAndTheMoney)
{
    Apply(Castle::TaxInfo{.result = 1, .chaosTax = 3, .storeTax = 7, .money = 0x0102030405060708});
    EXPECT_EQ(Castle().data.U32(0x7d5c640), 3u);
    EXPECT_EQ(Castle().data.U32(0x7d5c648), 3u);
    EXPECT_EQ(Castle().data.U32(0x7d5c644), 7u);
    EXPECT_EQ(Castle().data.U32(0x7d5c64c), 7u);
    EXPECT_EQ(Castle().data.U64(0x7d5c650), 0x0102030405060708u);
    EXPECT_EQ(Castle().data.bytes.at(0x7d5c650 - 0x7d5c504), 0x08);
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3})
        Apply(Castle::TaxInfo{.result = result});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1525), Text(1386)}));
}

// B2 09 con 1 (0x4e541b, 0x6690f0): el 3 al precio de la zona de caza; el 1 y el 2 a su par de impuestos.
TEST_P(CastleSiegeReceiveTest, TaxRateWritesTheFeeOrOneTaxPair)
{
    Apply(Castle::TaxRateResult{.result = 1, .kind = 3, .value = 100000});
    EXPECT_EQ(Castle().huntZone.fee, 100000u);
    EXPECT_EQ(Castle().data.bytes, CastleData{}.bytes);
    Apply(Castle::TaxRateResult{.result = 1, .kind = 1, .value = 5});
    Apply(Castle::TaxRateResult{.result = 1, .kind = 2, .value = 9});
    Apply(Castle::TaxRateResult{.result = 1, .kind = 4, .value = 1});
    EXPECT_EQ(Castle().data.U32(0x7d5c640), 5u);
    EXPECT_EQ(Castle().data.U32(0x7d5c648), 5u);
    EXPECT_EQ(Castle().data.U32(0x7d5c644), 9u);
    EXPECT_EQ(Castle().data.U32(0x7d5c64c), 9u);
    EXPECT_EQ(Castle().huntZone.fee, 100000u);
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3})
        Apply(Castle::TaxRateResult{.result = result});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1526), Text(1386)}));
}

TEST_P(CastleSiegeReceiveTest, MoneyOutWritesTheMoney)
{
    Apply(Castle::MoneyOutResult{.result = 1, .money = 1000000000});
    EXPECT_EQ(Castle().data.U64(0x7d5c650), 1000000000u);
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3})
        Apply(Castle::MoneyOutResult{.result = result});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(1527), Text(1386)}));
}

// B2 1A (0x4e54f6): pkt[4] en 1 a 0x78aba34, en 2 a 0x78aba30.
TEST_P(CastleSiegeReceiveTest, SiegeFlagsUpdateTheCanonicalNpcTaxesWithoutChangingTheOtherRate)
{
    Runtime().npcWindows.taxRate = 0x1234;
    Runtime().npcWindows.chaosTaxRate = 0x5678;
    Apply(Castle::SiegeFlags{.which = 1, .value = 0x81});
    EXPECT_EQ(Runtime().npcWindows.taxRate, 0x1234);
    Apply(Castle::SiegeFlags{.which = 2, .value = 4});
    Apply(Castle::SiegeFlags{.which = 3, .value = 9});
    EXPECT_EQ(Runtime().npcWindows.chaosTaxRate, 0x81);
    EXPECT_EQ(Runtime().npcWindows.taxRate, 4);
    ExpectNoRandom();
}

// B2 1F (0x4e5532): con 1, SETNZ de pkt[5] en +0x15; con 0 y 2, los diálogos del hito 8.
TEST_P(CastleSiegeReceiveTest, HuntZoneGateKeepsTheEntryAndReplacesTheFailureDialogs)
{
    Apply(Castle::HuntZoneGateResult{.result = 1, .allowed = 0x40});
    EXPECT_EQ(Castle().huntZone.allowed, 1);
    Apply(Castle::HuntZoneGateResult{.result = 1, .allowed = 0});
    EXPECT_EQ(Castle().huntZone.allowed, 0);
    Apply(Castle::HuntZoneGateResult{.result = 3});
    Apply(Castle::HuntZoneGateResult{.result = 0});
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.lines[0], Text(860));
    Apply(Castle::HuntZoneGateResult{.result = 2});
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.lines[0], Text(1386));
}

Castle::NpcRecord RecordOf(std::uint32_t type, std::uint32_t index, std::uint8_t fill)
{
    Castle::NpcRecord record{};
    for (std::size_t i = 0; i < record.size(); ++i)
        record[i] = static_cast<std::byte>(fill + i);
    for (std::size_t i = 0; i < 4; ++i)
    {
        record[i] = static_cast<std::byte>(type >> (8 * i));
        record[4 + i] = static_cast<std::byte>(index >> (8 * i));
    }
    return record;
}

// B3 con 1 (0x4dfc60 → 0x668fd0 por registro): cada uno entero donde dice; uno fuera de la zona descarta el paquete.
TEST_P(CastleSiegeReceiveTest, NpcListStoresEveryRecordOrNone)
{
    Apply(Castle::NpcList{.result = 1,
                          .records = {RecordOf(0x115, 0, 0x10), RecordOf(0x11b, 3, 0x40), RecordOf(0x99, 0, 0x70)}});
    const auto gate = RecordAt(Castle().data, 0x7d5c574);
    EXPECT_EQ(gate[0], 0x15);
    EXPECT_EQ(gate[8], 0x18);
    EXPECT_EQ(gate[0x1b], 0x2b);
    const auto statue = RecordAt(Castle().data, 0x7d5c504 + 3 * 0x1c);
    EXPECT_EQ(statue[0], 0x1b);
    EXPECT_EQ(statue[4], 3);
    EXPECT_EQ(statue[0x1b], 0x5b);
    const auto before = Castle().data.bytes;
    Apply(Castle::NpcList{.result = 1, .records = {RecordOf(0x115, 1, 0), RecordOf(0x11b, 0x10, 0)}});
    EXPECT_EQ(Castle().data.bytes, before);
    EXPECT_TRUE(Warned("el B3"));
    for (const std::uint8_t result : std::initializer_list<std::uint8_t>{0, 2, 3})
        Apply(Castle::NpcList{.result = result, .records = {}});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(860), Text(1386)}));
}

// B4 y B5 con 1: las listas de [0x7d536f8] son de las ventanas (P-C300).
TEST_P(CastleSiegeReceiveTest, GuildListsShowTheirMessagesAndAcceptEmptyLists)
{
    Apply(Castle::SiegeGuildList{.result = 0, .entries = {}});
    Apply(Castle::SiegeGuildList{.result = 2, .entries = {}});
    Apply(Castle::SiegeGuildList{.result = 1, .entries = {}});
    Apply(Castle::RegisteredGuildList{.result = 0, .entries = {}});
    Apply(Castle::RegisteredGuildList{.result = 2, .entries = {}});
    Apply(Castle::RegisteredGuildList{.result = 3, .entries = {}});
    Apply(Castle::RegisteredGuildList{.result = 4, .entries = {}});
    Apply(Castle::RegisteredGuildList{.result = 1, .entries = {}});
    EXPECT_EQ(Messages(), (std::vector<std::string>{Text(860), Text(860), Text(1609), Text(1609)}));
}

TEST_P(CastleSiegeReceiveTest, SuccessfulGuildListsClearTheEntriesAndSelectionButPreservePageSize)
{
    auto& registration = Castle().registration;
    registration.siegeGuilds.push_back({});
    registration.siegeGuildList.rows = 9;
    registration.siegeGuildList.selection = 1;
    registration.siegeGuildList.scroll = 4;
    auto applied =
        Client::Scenes::ApplyCastleSiegePacket(Castle::SiegeGuildList{.result = 1, .entries = {}}, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_TRUE(registration.siegeGuilds.empty());
    EXPECT_EQ(registration.siegeGuildList.selection, 0);
    EXPECT_EQ(registration.siegeGuildList.scroll, 0);
    EXPECT_EQ(registration.siegeGuildList.rows, 9);
    registration.registeredGuilds.push_back({});
    registration.registeredGuildList.selection = 1;
    applied =
        Client::Scenes::ApplyCastleSiegePacket(Castle::RegisteredGuildList{.result = 1, .entries = {}}, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_TRUE(registration.registeredGuilds.empty());
    EXPECT_EQ(registration.registeredGuildList.selection, 0);
    ExpectNoRandom();
}

// B6 (0x4e0ed0): sólo clase2; vacía el vector y añade los puntos con índice0, también con cuenta0.
TEST_P(CastleSiegeReceiveTest, MinimapPointsOnlyCountWithTheKindTwoPanel)
{
    const Castle::MinimapPoints points{.points = {{.x = 1, .y = 2}}};
    Apply(points);
    Runtime().siegePanelKind = std::uint8_t{0};
    Apply(points);
    Runtime().siegePanelKind = std::uint8_t{1};
    Apply(points);
    Runtime().siegePanelKind = std::uint8_t{2};
    Runtime().siegePanel.members.push_back({7, 8, 9});
    Apply(points);
    ASSERT_EQ(Runtime().siegePanel.members.size(), 1u);
    EXPECT_EQ(Runtime().siegePanel.members[0], (std::array<std::uint8_t, 3>{0, 1, 2}));
    EXPECT_EQ(Runtime().siegePanel.membersHidden, 0);
    Apply(Castle::MinimapPoints{.points = {}});
    EXPECT_TRUE(Runtime().siegePanel.members.empty());
    EXPECT_EQ(Runtime().siegePanel.membersHidden, 1);
    ExpectNoRandom();
}

// B8 01 (0x4e0700) sobre el héroe: otro valor a +0x372; 0 devuelve la habilidad guardada (+0x373) a +0x425.
TEST_P(CastleSiegeReceiveTest, KillCountWritesTheHero)
{
    Runtime().hero = 17;
    Client::Scenes::ActorState& hero = m_world->Actors()[17];
    hero.skillIndex = 2;
    hero.byte373 = 0xff;
    Apply(Castle::KillCount{.value = 5});
    EXPECT_EQ(hero.byte372, 5);
    EXPECT_EQ(hero.skillIndex, 2);
    Apply(Castle::KillCount{.value = 0});
    EXPECT_EQ(hero.byte372, 0);
    EXPECT_EQ(hero.byte373, 0xff);
    EXPECT_EQ(hero.skillIndex, 2);
    hero.byte373 = 9;
    hero.byte372 = 3;
    Apply(Castle::KillCount{.value = 0});
    EXPECT_EQ(hero.skillIndex, 9);
    EXPECT_EQ(hero.byte373, 0xff);
    EXPECT_EQ(hero.byte372, 0);
    EXPECT_EQ(m_world->Actors()[0].byte372, 0);
}

TEST_P(CastleSiegeReceiveTest, AMessageWithoutAMeasureCuts)
{
    CastleReceiveContext context = Context();
    context.measure = nullptr;
    EXPECT_FALSE(Client::Scenes::ApplyCastleSiegePacket(Castle::RegisterResult{.result = 0}, context).HasValue());
    ExpectNoRandom();
}
} // namespace

TEST_P(CastleSiegeReceiveTest, SiegeMarksAreSortedAsSignedDwordsAfterTheOldestEntryWasDiscarded)
{
    Castle::SiegeGuildList packet{.result = 1, .entries = {}};
    for (std::uint32_t i = 0; i < 151; ++i)
        packet.entries.push_back({.name = Name<8>("g"), .marks = i == 0 ? 0x80000000u : i});
    Apply(packet);
    const auto& window = Castle().registration;
    ASSERT_EQ(window.siegeGuilds.size(), 150u);
    EXPECT_EQ(window.siegeGuilds.front().marks, 1u);
    EXPECT_EQ(window.siegeGuilds.back().marks, 150u);
    EXPECT_EQ(window.siegeGuildList.selection, 150);
    EXPECT_EQ(window.siegeGuildList.scroll, 132);
    Apply(Castle::SiegeGuildList{.result = 1,
                                 .entries = {{.name = Name<8>("positive"), .marks = 1},
                                             {.name = Name<8>("negative"), .marks = 0xffffffffu},
                                             {.name = Name<8>("minimum"), .marks = 0x80000000u}}});
    EXPECT_EQ(window.siegeGuilds[0].marks, 0x80000000u);
    EXPECT_EQ(window.siegeGuilds[1].marks, 0xffffffffu);
    EXPECT_EQ(window.siegeGuilds[2].marks, 1u);
    ExpectNoRandom();
}

TEST_P(CastleSiegeReceiveTest, EqualMarksKeepTheVc6SixteenAndSeventeenEntryPartitionBoundary)
{
    for (const std::uint8_t count : {std::uint8_t{16}, std::uint8_t{17}})
    {
        Castle::SiegeGuildList packet{.result = 1, .entries = {}};
        for (std::uint8_t i = 0; i < count; ++i)
            packet.entries.push_back({.name = Name<8>("g"), .marks = 7, .first = i});
        Apply(packet);
        const auto& rows = Castle().registration.siegeGuilds;
        ASSERT_EQ(rows.size(), count);
        for (std::size_t i = 0; i < rows.size(); ++i)
            EXPECT_EQ(rows[i].first, count == 16 ? static_cast<std::size_t>(count) - 1u - i : i);
    }
    ExpectNoRandom();
}

TEST_P(CastleSiegeReceiveTest, RegisteredGuildsUsePushFrontWithoutSortingAndIgnoreAnEmptyName)
{
    Apply(Castle::RegisteredGuildList{.result = 1,
                                      .entries = {{.first = 1, .name = Name<8>("first")},
                                                  {.first = 9, .name = {}},
                                                  {.first = 2, .name = Name<8>("second")}}});
    const auto& window = Castle().registration;
    ASSERT_EQ(window.registeredGuilds.size(), 2u);
    EXPECT_EQ(window.registeredGuilds[0].first, 2);
    EXPECT_EQ(window.registeredGuilds[1].first, 1);
    EXPECT_EQ(window.registeredGuildList.selection, 2);
    EXPECT_EQ(window.registeredGuildList.scroll, 0);
    ExpectNoRandom();
}

TEST_P(CastleSiegeReceiveTest, HuntInformationNormalizesAccessAndCopiesTheSignedFeeFieldsBeforeOpening)
{
    auto applied = Client::Scenes::ApplyHuntInformation(3, 0x80, 0xffffffffu, 0x80000000u, 7, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    auto& hunt = Castle().huntZone;
    EXPECT_EQ(hunt.mode, 3);
    EXPECT_EQ(hunt.allowed, 1);
    EXPECT_EQ(hunt.fee, 0xffffffffu);
    EXPECT_EQ(hunt.editedFee, -1);
    EXPECT_EQ(hunt.max, (-2147483647 - 1));
    EXPECT_EQ(hunt.step, 7);
    EXPECT_EQ(Runtime().panels.delegated.secondCastleSiege, 1);
    EXPECT_TRUE(m_sent.empty());
    // 659860 y657ab0 consultan comercio y tienda NPC: dos cierres protegidos en cada uno.
    CrtRandom expected(Seed);
    expected.Discard(4);
    EXPECT_EQ(m_random.State(), expected.State());
    applied = Client::Scenes::ApplyHuntInformation(0, 0, 0, 0, 0, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(hunt.mode, 3);
    EXPECT_EQ(hunt.fee, 0xffffffffu);
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.lines[0], Text(860));
    EXPECT_EQ(m_random.State(), expected.State());
}

TEST_P(CastleSiegeReceiveTest, BusySeniorCloseReturnsTrueButKeepsTheWindowAndUsesTheSharedBusyWarning)
{
    for (auto& item : Runtime().npcWindows.chaosBox)
        item.id = 0xffff;
    Runtime().npcWindows.chaosBox[31].id = 0;
    Runtime().panels.delegated.castleSiegeList = 1;
    Runtime().panels.delegated.castleSiegeListMode = 3;
    Client::Scenes::GamePanels panels(*m_world, m_random);
    Client::Scenes::LendTheCastlePanelServices(panels, Context());
    auto closed = panels.Registry().Close(Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_TRUE(closed.Value());
    EXPECT_EQ(Runtime().panels.delegated.castleSiegeList, 1);
    EXPECT_EQ(Runtime().panels.delegated.castleSiegeListMode, 3);
    EXPECT_EQ(Messages(Client::UI::ErrorMessage), (std::vector<std::string>{Text(1641)}));
    EXPECT_TRUE(m_sent.empty());
    closed = panels.Registry().CloseByMask(Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_TRUE(closed.Value());
    EXPECT_EQ(Runtime().panels.delegated.castleSiegeList, 1);
    ExpectNoRandom();
}

TEST_P(CastleSiegeReceiveTest, SeniorCloseChooses87Or31AndPreservesTheTabAndAnUnrelatedDialog)
{
    for (auto& item : Runtime().npcWindows.chaosBox)
        item.id = 0xffff;
    for (const bool close87 : {false, true})
    {
        Runtime().panels.delegated.castleSiegeList = 1;
        Runtime().panels.delegated.castleSiegeListMode = 2;
        Castle().senior.closeWith87 = close87;
        auto& dialog = Runtime().panels.delegated.panelDialog;
        dialog.opened = 18;
        Castle().senior.dialogId = 17;
        Client::Scenes::GamePanels panels(*m_world, m_random);
        Client::Scenes::LendTheCastlePanelServices(panels, Context());
        const auto closed = panels.Registry().CloseByMask(Client::UI::CastleSiegeListPanel);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        EXPECT_EQ(Runtime().panels.delegated.castleSiegeList, 0);
        EXPECT_EQ(Runtime().panels.delegated.castleSiegeListMode, 2);
        EXPECT_TRUE(SameBytes(m_sent.back().packet.Bytes(), Bytes({0xc1, 3, close87 ? 0x87 : 0x31})));
        EXPECT_EQ(dialog.opened, 18u);
    }
    ExpectNoRandom();
}
