#include "Quests/QuestWindow.h"
#include "Kernel/MouseState.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/QuestTable.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Hud/GameInventoryHud.h"
#include "Movement/GameMovementState.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Items/ItemNames.h"
#include "Monsters/NpcNames.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Quests/QuestState.h"
#include "Items/UiItemDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/ZenText.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The quest window (CL9 milestone 2; 06 § Las misiones, "La ventana"): the step 0x41b5a0 (0x41a9e0, 0x419bd0 and
// 0x41b170), the drawing 0x41ca40 (0x41ba20, 0x41b680, 0x41b810 and 0x41c600) and the 3D 0x41b9e0, in 640 × 480. The
// quests, dialogs, texts and names are made up. Each test runs in Lorencia and in Devias: nothing in the window looks
// at the map.
namespace
{
using Client::Crt::CrtRandom;
using Client::Ui::GameHudContext;
using Client::Ui::GameUiContext;
using Client::Kernel::WorldState;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Tests::RecordedText;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x5eed4242u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// An own record closed down to 0 spends 3848 rand() (0x40b4d0); the C3 draws the padding and the serial.
constexpr int OwnRecordClose = 0xf08;
constexpr int C3Randoms = 2;
// 0x41b5bb..0x41b64c.
constexpr std::int32_t Alone = 0x1c2;
constexpr std::int32_t Beside = 0x104;
// Quest 0's scroll, the first of the quest items (group 14, index 23).
constexpr std::int32_t ScrollType = 0x1c17;
constexpr std::int32_t NpcType = 0xeb;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// 0x419bf7..0x419c3a with one line and three answers: the text at 93 and the answers from 111 with state 1, or from
// 0xfa with another.
constexpr std::int32_t TextRowTop = 93;
constexpr std::int32_t AnswersTopInProgress = 111;
constexpr std::int32_t AnswersTopOtherwise = 0xfa;
constexpr std::int32_t AnswersCenterX = 0x236;

std::map<std::size_t, std::string> QuestTexts()
{
    return {{198, "Zen"},           {609, "Cerrar"},          {699, "Terminar"},     {906, "Pergamino de nivel"},
            {907, "Otro de nivel"}, {927, "Cerrar (%s)"},     {930, "Sin empezar"},  {931, "En curso"},
            {932, "Terminada"},     {1002, "Cerrar ventana"}, {1140, "Misiones"},    {1141, "Pestana dos"},
            {1142, "Pestana tres"}, {1145, "Titulo dos"},     {1146, "Titulo tres"}, {868, "Cuenta %d"},
            {869, "%d de %s y %s"}, {877, "Renglon uno"},     {878, "Renglon dos"}};
}

void PutText(std::span<char> to, std::string_view value)
{
    std::ranges::copy(value, to.begin());
}

// Item.bmd with the names of the four quest items, that of a sword (type 1) and nothing else.
Client::Tables::ItemTable QuestItems()
{
    using Client::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    const std::vector<std::pair<std::int32_t, std::string_view>> names{
        {0x1c17, "Pergamino"}, {0x1c18, "Hueso"}, {0x1c19, "Cuerno"}, {0x1c1a, "Pluma"}, {0x0001, "Espada"}};
    for (const auto& [type, name] : names)
    {
        const auto base = static_cast<std::size_t>(type) * ItemRecordSize;
        for (std::size_t i = 0; i < name.size(); ++i)
            plain[base + i] = static_cast<std::byte>(name[i]);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class RecordingRequests final : public Client::Ui::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::Player::OptionBody& /*body*/) override
    {
        requests.emplace_back("F3 30");
    }
    void RequestLogout(std::uint8_t /*kind*/) override
    {
        requests.emplace_back("F1 02");
    }
    void RequestPartyList() override
    {
        requests.emplace_back("42");
    }
    void SendGamePacket(Client::Protocol::Outgoing /*outgoing*/) override
    {
        requests.emplace_back("juego");
    }
    void SendWindowClose() override
    {
        requests.emplace_back("31");
    }
    void SendSetQuest(std::uint8_t quest, std::uint8_t state) override
    {
        requests.push_back("A2 " + std::to_string(quest) + " " + std::to_string(state));
    }
    void SendGuildPacket(Client::Protocol::Outgoing /*packet*/) override {}
    void SendFriendPacket(Client::Protocol::Outgoing /*packet*/) override
    {
        requests.emplace_back("amigos");
    }
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        requests.emplace_back("Macro.txt");
        return {};
    }
    void Log(std::string_view /*line*/) override
    {
        requests.emplace_back("log");
    }

    std::vector<std::string> requests;
};

// A batch quad: the slot (Marker for a text's mark), the color and corners 0 and 2 of the fan.
struct DrawnQuad
{
    int slot = NoTexture;
    Color color{};
    Client::Render::SpriteVertex first{};
    Client::Render::SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{
                .slot = slot, .color = vertices[first].color, .first = vertices[first], .third = vertices[first + 2]});
        }
    }
    return quads;
}

// The first quad with that slot and that corner (in 640 × 480, the GL y is 480 − y).
const DrawnQuad* FindQuad(const std::vector<DrawnQuad>& quads, int slot, float x, float y)
{
    constexpr float Tolerance = 1e-3f;
    for (const DrawnQuad& quad : quads)
    {
        if (quad.slot == slot && std::abs(quad.first.x - x) < Tolerance &&
            std::abs(quad.first.y - (480.0f - y)) < Tolerance)
            return &quad;
    }
    return nullptr;
}

void ExpectSize(const DrawnQuad& quad, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_NEAR(quad.third.x - quad.first.x, width, Tolerance);
    EXPECT_NEAR(quad.first.y - quad.third.y, height, Tolerance);
}

const RecordedText* FindText(const std::vector<RecordedText>& texts, std::string_view text)
{
    const auto found = std::ranges::find_if(texts, [text](const RecordedText& drawn) { return drawn.text == text; });
    return found == texts.end() ? nullptr : &*found;
}

class QuestWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    QuestWindowTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Client::Kernel::WorldRuntime& runtime = Runtime();
        runtime.map = GetParam();
        runtime.hero = 3;
        Hero().alive = true;
        Hero().characterClass = 0;
        Client::Quests::QuestState& quest = Quest();
        quest.characterClass = 0;
        // Quest 0: the scroll for class 0, with level 10 and 1000 zen.
        Client::Tables::QuestRecord& first = quest.records[0];
        first.entryCount = 1;
        first.conditionCount = 1;
        first.npcType = NpcType;
        PutText(first.name, "La primera");
        auto& entry = first.entries[0];
        entry.byte0 = 1;
        entry.kind = 1;
        entry.itemGroup = 14;
        entry.itemIndex = 23;
        entry.itemCount = 1;
        entry.classes = {1, 0, 0, 0, 0};
        entry.dialogs = {10, 11, 12, 13};
        auto& condition = first.conditions[0];
        condition.byte0 = 1;
        condition.needQuest = -1;
        condition.minLevel = 10;
        condition.zen = 1000;
        condition.failDialog = 20;
        PutText(quest.records[1].name, "La segunda");
        quest.records[2].npcType = 0xe5;
        PutText(quest.records[2].name, "La tercera");
        // Dialog.bmd: dialog 10 with three answers (check and accept → 11, close, accept → 12).
        runtime.dialogTable.assign(Client::Tables::DialogCount, {});
        for (const std::size_t index : {10U, 11U, 12U, 20U})
            PutText(runtime.dialogTable[index].text, "Un texto inventado");
        auto& dialog = runtime.dialogTable[10];
        dialog.answerCount = 3;
        PutText(dialog.answers[0], "Acepto");
        PutText(dialog.answers[1], "Me voy");
        PutText(dialog.answers[2], "Dale");
        dialog.actions = {1, 2, 3};
        dialog.next = {11, 0, 12};
        m_world->OwnCharacter().level = 20;
        m_world->OwnCharacter().zen = 5000;
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }
    Client::Quests::QuestState& Quest()
    {
        return Runtime().quest;
    }
    Client::Kernel::ActorState& Hero()
    {
        return m_world->Hero();
    }
    Client::Kernel::MouseState& Mouse()
    {
        return Runtime().mouse;
    }

    Client::Movement::GameMovementState& Movement()
    {
        return Runtime().movement;
    }
    std::uint8_t& Window()
    {
        return Runtime().panels.delegated.quest;
    }

    // The NPC dialog window with quest 0 in `state` and dialog 10 (0x4198b0 without its 0x419600).
    void OpenTheDialog(std::uint8_t state)
    {
        Window() = Client::Quests::QuestDialogWindow;
        Quest().current = 0;
        Quest().currentState = state;
        ASSERT_TRUE(
            Client::Ui::ShowTheQuestDialog(Runtime().dialogBox, Runtime().dialogs, &Runtime().dialogTable, &m_texts, 10)
                .HasValue());
        Runtime().dialogs = {};
    }

    void GiveTheScroll()
    {
        Client::Kernel::ItemState& item = m_world->Inventory().cells[5];
        item.id = static_cast<std::uint16_t>(ScrollType);
        item.occupied = 1;
    }

    void Click()
    {
        Client::Kernel::LatchTheLeftButton(Mouse(), true, true);
    }

    GameUiContext UiContext(std::int32_t x, std::int32_t y, Client::Ui::IGameUiRequests* requests)
    {
        return GameUiContext{.world = *m_world,
                             .random = m_random,
                             .input = m_input,
                             .mouseX = x,
                             .mouseY = y,
                             .texts = &m_texts,
                             .requests = requests};
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, RecordingRequests* requests)
    {
        Client::Hud::GamePanels panels(*m_world, m_random, Client::Hud::GamePanelTables{.texts = &m_texts});
        panels.LendTheWindowClose(requests);
        return Client::Quests::StepTheQuestWindow(UiContext(x, y, requests), panels.Registry());
    }

    GameHudContext HudContext(std::int32_t x, std::int32_t y, CrtRandom& random)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        return GameHudContext{.world = *m_world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = Hero().cellXAccesses,
                              .cellYDepth = Hero().cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = m_sound,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = &m_texts,
                              .items = &m_items,
                              .mouse = {.x = x, .y = y}};
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x, std::int32_t y)
    {
        GameHudContext context = HudContext(x, y, m_random);
        Client::Hud::GamePanels panels(*m_world, m_random);
        return Client::Quests::DrawTheQuestWindow(context, panels.Registry());
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(QuestTexts());
    Client::Tables::ItemTable m_items = QuestItems();
};

// 0x41b5bb..0x41b64c: the corner, (0x104, 0) with the character open and (0x1c2, 0) without it, every step. With the
// window closed it does nothing more and draws no rand().
TEST_P(QuestWindowTest, TheCornerFollowsTheCharacterWindow)
{
    Quest().x = 7;
    Quest().y = 9;
    ASSERT_TRUE(Step(0, 0, nullptr).HasValue());
    EXPECT_EQ(Quest().x, Alone);
    EXPECT_EQ(Quest().y, 0);
    Runtime().panels.character = 1;
    ASSERT_TRUE(Step(0, 0, nullptr).HasValue());
    EXPECT_EQ(Quest().x, Beside);
    EXPECT_EQ(Quest().y, 0);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x41b5cf..0x41b63b: with the character open, the click in [0x1f9, 0x211) × [0x18b, 0x1a3) opens the list (Open(8),
// 0x419840 with the highest quest's dialog) and another closes it (Close(8), the C1 03 31); both sound. Without the
// character, the button is not there.
TEST_P(QuestWindowTest, TheCharacterButtonOpensAndClosesTheList)
{
    RecordingRequests requests;
    Quest().states[0] = Client::Quests::QuestAvailable;
    Click();
    ASSERT_TRUE(Step(0x1f9, 0x18b, &requests).HasValue());
    EXPECT_EQ(Window(), 0) << "sin el personaje";
    EXPECT_TRUE(Mouse().pendingClick);

    Runtime().panels.character = 1;
    for (const auto& [x, y] :
         {std::pair{0x1f8, 0x18b}, std::pair{0x211, 0x18b}, std::pair{0x1f9, 0x18a}, std::pair{0x1f9, 0x1a3}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        ASSERT_TRUE(Step(x, y, &requests).HasValue());
        EXPECT_EQ(Window(), 0);
        EXPECT_TRUE(Mouse().pendingClick);
    }
    Mouse().updateTime = 4;
    Mouse().updateTimeMax = 12;
    ASSERT_TRUE(Step(0x210, 0x1a2, &requests).HasValue());
    EXPECT_EQ(Window(), Client::Quests::QuestListWindow);
    EXPECT_FALSE(Mouse().pendingClick);
    EXPECT_EQ(Mouse().updateTime, 0);
    EXPECT_EQ(Mouse().updateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    // 0x419840: quest 0 available, with the conditions without the zen, and its dialog 10.
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 10);
    EXPECT_TRUE(requests.requests.empty());

    Runtime().lastSoundId = Client::Audio::SoundId{0};
    Click();
    ASSERT_TRUE(Step(0x1f9, 0x18b, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
}

// 0x419bd0, action 2: the click on the second answer (the answers at 0xfa with state 3) spends the click, releases the
// button, closes with 0x419900 (the C1 03 31, without the registry's sound) and sounds; dialog 0 is not shown.
TEST_P(QuestWindowTest, TheCloseAnswerClosesTheWindow)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Quests::QuestAvailable);
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 18, &requests).HasValue());
    EXPECT_EQ(Window(), Client::Quests::QuestDialogWindow) << "sin el clic";

    Click();
    Mouse().updateTime = 4;
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 18, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_FALSE(Mouse().pendingClick);
    EXPECT_FALSE(Mouse().leftHeld);
    EXPECT_EQ(Mouse().updateTime, 0);
    EXPECT_EQ(Mouse().updateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 10);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x419bd0, action 3: the A2 with the quest and 1 (the C3's two rand()) and dialog 12 that the answer leads to.
TEST_P(QuestWindowTest, TheAcceptAnswerSendsTheQuestAndShowsTheNextDialog)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Quests::QuestAvailable);
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 36, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"}));
    EXPECT_EQ(Window(), Client::Quests::QuestDialogWindow);
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 12);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{Seed}, C3Randoms).State());

    // Without the wiring, the A2 cuts before drawing the rand().
    OpenTheDialog(Client::Quests::QuestAvailable);
    Click();
    const std::uint32_t before = m_random.State();
    const auto cut = Step(AnswersCenterX, AnswersTopOtherwise + 36, nullptr);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x41a35b"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_random.State(), before);
}

// 0x419bd0, action 1: with the conditions (the level and the zen, each with its own record opening), the A2 and dialog
// 11; without the zen, the condition's dialog (20) and neither the A2 nor 11.
TEST_P(QuestWindowTest, TheCheckingAnswerLooksAtTheConditionsWithTheZen)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Quests::QuestAvailable);
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"}));
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 11);
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{Seed}, 2 * OwnRecordClose + C3Randoms).State());

    m_world->OwnCharacter().zen = 999;
    OpenTheDialog(Client::Quests::QuestAvailable);
    Runtime().lastSoundId = Client::Audio::SoundId{0};
    const std::uint32_t before = m_random.State();
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 17, &requests).HasValue());
    EXPECT_EQ(requests.requests.size(), 1U);
    EXPECT_EQ(Quest().currentState, Client::Quests::QuestConditionFailed);
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 20);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{before}, 2 * OwnRecordClose).State());
}

// Q-C870 and Q-C871: the row comes from (y − top) / 0x12 with C division, so 1 to 17 pixels above the first answer is
// row 0; from 18, row −1, which leaves after spending the click. The x is measured against 0x236, not the window: with
// the character open (the window at 0x104) the answers are not chosen where they are drawn.
TEST_P(QuestWindowTest, TheAnswerRowsAreMeasuredLikeTheOriginal)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Quests::QuestInProgress);
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopInProgress - 17, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"})) << "la respuesta 0, mirar y aceptar";

    OpenTheDialog(Client::Quests::QuestInProgress);
    requests.requests.clear();
    Runtime().lastSoundId = Client::Audio::SoundId{0};
    const std::uint32_t before = m_random.State();
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopInProgress - 18, &requests).HasValue());
    EXPECT_TRUE(requests.requests.empty());
    EXPECT_FALSE(Mouse().pendingClick) << "el clic se gasta igual";
    EXPECT_FALSE(Mouse().leftHeld);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0});
    EXPECT_EQ(m_random.State(), before);

    // 0x419c62..0x419c71: |0x236 − x| up to 0x6a.
    for (const auto& [x, inside] : {std::pair{AnswersCenterX - 0x6a, true}, std::pair{AnswersCenterX + 0x6a, true},
                                    std::pair{AnswersCenterX - 0x6b, false}, std::pair{AnswersCenterX + 0x6b, false},
                                    std::pair{Beside + 0x64, false}})
    {
        SCOPED_TRACE(testing::Message() << x);
        OpenTheDialog(Client::Quests::QuestInProgress);
        Click();
        ASSERT_TRUE(Step(x, AnswersTopInProgress + 18, &requests).HasValue());
        EXPECT_EQ(Window(), inside ? 0 : Client::Quests::QuestDialogWindow);
        EXPECT_EQ(Mouse().pendingClick, !inside);
    }
}

// 0x41aa4a..0x41b0cc: with the quest in progress and the scroll in the inventory (0x419b10), the click on the finish
// button, [x + 0x23, + 0x78) × [0x163, 0x17b), sends the A2, spends the click and releases the button, and sounds;
// 0x77f3b10 and 0x6c8dcc stay. Without the scroll, the button does nothing.
TEST_P(QuestWindowTest, TheFinishButtonNeedsTheItems)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Quests::QuestInProgress);
    Click();
    ASSERT_TRUE(Step(Alone + 0x23, 0x163, &requests).HasValue());
    EXPECT_TRUE(requests.requests.empty());
    EXPECT_TRUE(Mouse().pendingClick);

    GiveTheScroll();
    for (const auto& [x, y] : {std::pair{Alone + 0x22, 0x163}, std::pair{Alone + 0x9b, 0x163},
                               std::pair{Alone + 0x23, 0x162}, std::pair{Alone + 0x23, 0x17b}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        ASSERT_TRUE(Step(x, y, &requests).HasValue());
        EXPECT_TRUE(requests.requests.empty());
    }
    Mouse().updateTime = 4;
    Mouse().updateTimeMax = 12;
    ASSERT_TRUE(Step(Alone + 0x9a, 0x17a, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"}));
    EXPECT_FALSE(Mouse().pendingClick);
    EXPECT_FALSE(Mouse().leftHeld);
    EXPECT_EQ(Mouse().updateTime, 4);
    EXPECT_EQ(Mouse().updateTimeMax, 12);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{Seed}, C3Randoms).State());
}

// 0x41aa0e..0x41aa3a and 0x41b0e7..0x41b14c: the mouse over the window belongs to the UI; the close button spends the
// click and closes with 0x419900 without sounding; the release (0x7d366ec) goes off every step.
TEST_P(QuestWindowTest, TheDialogCloseButtonAndTheMouse)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Quests::QuestAvailable);
    Mouse().releasedClick = true;
    ASSERT_TRUE(Step(Alone, 0, &requests).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_FALSE(Mouse().releasedClick);
    Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(Step(Alone - 1, 0, &requests).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 0);

    Click();
    ASSERT_TRUE(Step(Alone + 0x19 + 0x17, 0x18b + 0x17, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_FALSE(Mouse().pendingClick);
    EXPECT_TRUE(Mouse().leftHeld);
    EXPECT_EQ(Mouse().updateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, std::nullopt);
}

// 0x41b170: tab 0 is chosen and spends the click, without the C1 9F (Q-C845); tabs 1 and 2 spend nothing. The chosen
// one goes from 0xeb to 0xfb and the others from 0xed; the close button closes with Close(8), which sounds.
TEST_P(QuestWindowTest, TheListTabsAndItsCloseButton)
{
    RecordingRequests requests;
    Window() = Client::Quests::QuestListWindow;
    Quest().tab = 1;
    Click();
    ASSERT_TRUE(Step(Alone + 0x14 + 0x33, 0xeb, &requests).HasValue());
    EXPECT_TRUE(Mouse().pendingClick) << "la pestaña 1";
    EXPECT_EQ(Quest().tab, 1);
    ASSERT_TRUE(Step(Alone + 0x14, 0xec, &requests).HasValue());
    EXPECT_TRUE(Mouse().pendingClick) << "la 0 sin elegir arranca en 0xed";
    ASSERT_TRUE(Step(Alone + 0x14 + 0x30, 0xfa, &requests).HasValue());
    EXPECT_FALSE(Mouse().pendingClick);
    EXPECT_EQ(Quest().tab, 0);
    EXPECT_EQ(Mouse().updateTimeMax, 6);
    EXPECT_TRUE(requests.requests.empty());

    Click();
    ASSERT_TRUE(Step(Alone + 0x19, 0x18b, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5ee20d: the UI step reaches the window after the commands; with the character open, the button opens the list.
TEST_P(QuestWindowTest, TheGameUiStepsTheWindow)
{
    RecordingRequests requests;
    Runtime().panels.character = 1;
    // 0x424230: the dark horse request already went out.
    Runtime().panels.delegated.pet = 1;
    Quest().states[0] = Client::Quests::QuestAvailable;
    Click();
    const auto stepped = Client::Hud::StepTheGameUi(UiContext(0x1f9, 0x18b, &requests));
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Window(), Client::Quests::QuestListWindow);
    EXPECT_EQ(Quest().x, Beside);
}

// 0x41ba20 with the quest in progress: the background with the line, the line at y + 325, the scroll "Pergamino x 1"
// in bold at (0x1fe, 240), the finish button (white with the scroll, gray without it) with 699, the close one, the
// quest's name in its box of 150, the text and the answers from 111.
TEST_P(QuestWindowTest, TheDialogInProgressDrawsTheItemsAndTheFinishButton)
{
    OpenTheDialog(Client::Quests::QuestInProgress);
    GiveTheScroll();
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const auto quads = QuadsOf(m_sprites);
    ASSERT_NE(FindQuad(quads, 0x117, static_cast<float>(Alone), 225.0f), nullptr);
    const DrawnQuad* line = FindQuad(quads, 0x117, static_cast<float>(Alone), 325.0f);
    ASSERT_NE(line, nullptr);
    ExpectSize(*line, 190.0f, 10.0f);
    const DrawnQuad* finish = FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f);
    ASSERT_NE(finish, nullptr);
    ExpectSize(*finish, 120.0f, 24.0f);
    EXPECT_EQ(finish->color, White);
    ASSERT_NE(FindQuad(quads, 0x118, static_cast<float>(Alone + 25), 395.0f), nullptr);

    const auto& texts = m_text.texts;
    const RecordedText* item = FindText(texts, "Pergamino x 1");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->x, 0x1fe);
    EXPECT_EQ(item->y, 240);
    EXPECT_EQ(item->style.font, FontKind::Bold);
    EXPECT_EQ(item->style.colors, (TextColors{.text = 0xff67bfdfu, .background = 0}));
    const RecordedText* label = FindText(texts, "Terminar");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->x, Alone + 35 + 60 - 24);
    EXPECT_EQ(label->y, 0x168);
    EXPECT_EQ(label->style.font, FontKind::Normal);
    const RecordedText* name = FindText(texts, "La primera");
    ASSERT_NE(name, nullptr);
    EXPECT_TRUE(name->windowed);
    EXPECT_EQ(name->x, Alone + 0x16);
    EXPECT_EQ(name->y, 0x16);
    EXPECT_EQ(name->columns, 150);
    EXPECT_EQ(name->style.colors.text, 0xffffdcc8u);
    const RecordedText* row = FindText(texts, "Un texto inventado");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->y, TextRowTop);
    EXPECT_EQ(row->x, Alone + 0x64 - 18 * 6 / 2);
    for (const auto& [answer, y] : {std::pair{"1) Acepto", AnswersTopInProgress},
                                    std::pair{"2) Me voy", AnswersTopInProgress + 18}, std::pair{"3) Dale", 147}})
    {
        SCOPED_TRACE(answer);
        const RecordedText* drawn = FindText(texts, answer);
        ASSERT_NE(drawn, nullptr);
        EXPECT_EQ(drawn->y, y);
        EXPECT_EQ(drawn->style.font, FontKind::Bold);
        EXPECT_EQ(drawn->style.colors.text, 0xff67bfdfu);
    }
    // Without the table at 0x7681eb8 (P-C174), the NPC has no name: there is no title.
    EXPECT_EQ(std::ranges::count_if(texts, [](const RecordedText& drawn) { return drawn.y == 0xc; }), 0);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5b1c70 from 0x41b810 (Items::ItemNameOf): the 0x1c17 and the 0x1c18 with level 1 are named with texts 906 and 907;
// with another level other than 0 the buffer stays uninitialized and it cuts (Q-C874), as does another type with an
// unnamed level (the 0x1c09 with 2). An item that is not a quest item takes its Item.bmd name and, with a level,
// " +level"; with a group of 128 or more the type reads outside Item.bmd.
TEST_P(QuestWindowTest, TheAskedItemNamesFollowTheLevel)
{
    auto& entry = Quest().records[0].entries[0];
    entry.itemLevel = 1;
    OpenTheDialog(Client::Quests::QuestInProgress);
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Pergamino de nivel x 1"), nullptr);

    entry.itemIndex = 24;
    m_text.texts.clear();
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Otro de nivel x 1"), nullptr);

    for (const auto& [index, where] : {std::pair{23, "0x5b1cb1"}, std::pair{24, "0x5b1cf3"}, std::pair{9, "0x41b924"}})
    {
        SCOPED_TRACE(where);
        entry.itemIndex = static_cast<std::uint8_t>(index);
        entry.itemLevel = 2;
        const auto cut = Draw(0, 0);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_TRUE(cut.Error().starts_with(where)) << cut.Error();
        EXPECT_NE(cut.Error().find("Q-C874"), std::string::npos) << cut.Error();
    }

    entry.itemGroup = 0;
    entry.itemIndex = 1;
    for (const auto& [level, name] : {std::pair{0, "Espada x 1"}, std::pair{3, "Espada +3 x 1"}})
    {
        SCOPED_TRACE(level);
        entry.itemLevel = static_cast<std::uint8_t>(level);
        m_text.texts.clear();
        const auto drawn = Draw(0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_NE(FindText(m_text.texts, name), nullptr);
    }

    entry.itemGroup = 0x80;
    entry.itemLevel = 0;
    const auto cut = Draw(0, 0);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x5b1c76")) << cut.Error();
}

TEST_P(QuestWindowTest, WithoutTheItemsTheFinishButtonIsGrey)
{
    OpenTheDialog(Client::Quests::QuestInProgress);
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const DrawnQuad* finish = FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f);
    ASSERT_NE(finish, nullptr);
    EXPECT_EQ(finish->color, (Color{0.3f, 0.3f, 0.3f, 1.0f}));
    const RecordedText* item = FindText(m_text.texts, "Pergamino x 1");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->style.colors.text, 0xff1e1effu);
}

// 0x41bb03..0x41bb84: with the click over the finish button, darker; on release, the click and the button down are
// spent (not in an intermediate MuRet drawing).
TEST_P(QuestWindowTest, ThePressedFinishButtonSpendsTheReleasedClick)
{
    OpenTheDialog(Client::Quests::QuestInProgress);
    GiveTheScroll();
    Click();
    ASSERT_TRUE(Draw(Alone + 35, 355).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const DrawnQuad* finish = FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f);
    ASSERT_NE(finish, nullptr);
    EXPECT_EQ(finish->color, (Color{0.4f, 0.4f, 0.4f, 1.0f}));
    EXPECT_TRUE(Mouse().pendingClick) << "sin el soltado";

    Mouse().releasedClick = true;
    {
        GameHudContext context = HudContext(Alone + 154, 378, m_random);
        context.projecting = true;
        Client::Hud::GamePanels panels(*m_world, m_random);
        ASSERT_TRUE(Client::Quests::DrawTheQuestWindow(context, panels.Registry()).HasValue());
        EXPECT_TRUE(Mouse().pendingClick) << "proyectado";
    }
    ASSERT_TRUE(Draw(Alone + 154, 378).HasValue());
    EXPECT_FALSE(Mouse().pendingClick);
    EXPECT_FALSE(Mouse().leftHeld);
}

// 0x41bc0b..0x41bcca with the quest available: the zen background at (x + 50, y + 367.7), the 198 at (x + 20, 370)
// and the zen the condition requires with its color.
TEST_P(QuestWindowTest, TheAvailableQuestShowsItsZen)
{
    OpenTheDialog(Client::Quests::QuestAvailable);
    Quest().zen = 1234567;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const DrawnQuad* zen = FindQuad(quads, 0x10f, static_cast<float>(Alone + 50), 367.7f);
    ASSERT_NE(zen, nullptr);
    ExpectSize(*zen, 113.0f, 18.0f);
    const RecordedText* label = FindText(m_text.texts, "Zen");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->x, Alone + 50 - 30);
    EXPECT_EQ(label->y, 370);
    EXPECT_EQ(label->style.colors, (TextColors{.text = 0xff96dcffu, .background = 0xff282828u}));
    const RecordedText* amount = FindText(m_text.texts, "1,234,567");
    ASSERT_NE(amount, nullptr);
    EXPECT_EQ(amount->x, Alone + 60);
    EXPECT_EQ(amount->style.colors.text, Client::Ui::ZenColor(1234567));
    EXPECT_EQ(FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f), nullptr);
    // The answers, at 0xfa.
    const RecordedText* answer = FindText(m_text.texts, "1) Acepto");
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->y, AnswersTopOtherwise);
}

// 0x41b72e..0x41b7ac: the answer under the mouse goes red if the mouse is no more than 0x6a from x + 0x6a; the row 1 to
// 17 pixels above is row 0 (Q-C870).
TEST_P(QuestWindowTest, TheAnswerUnderTheMouseIsRed)
{
    OpenTheDialog(Client::Quests::QuestInProgress);
    ASSERT_TRUE(Draw(Alone + 0xd4, AnswersTopInProgress - 17).HasValue());
    EXPECT_EQ(FindText(m_text.texts, "1) Acepto")->style.colors.text, 0xff0000ffu);
    EXPECT_EQ(FindText(m_text.texts, "2) Me voy")->style.colors.text, 0xff67bfdfu);

    m_text.texts.clear();
    ASSERT_TRUE(Draw(Alone + 0xd5, AnswersTopInProgress + 18).HasValue());
    EXPECT_EQ(FindText(m_text.texts, "2) Me voy")->style.colors.text, 0xff67bfdfu);
}

// 0x41bde0..0x41be9e: the title is the name of the quest's NPC in the table at 0x7681eb8; with a class 3 or 4 hero and
// 0x6eeda8, that of quest 2, and without the quest's name.
TEST_P(QuestWindowTest, TheTitleIsTheNameOfTheNpc)
{
    OpenTheDialog(Client::Quests::QuestAvailable);
    Runtime().npcNames[7].type = NpcType;
    PutText(Runtime().npcNames[7].name, "Guardia inventado");
    Runtime().npcNames[9].type = 0xe5;
    PutText(Runtime().npcNames[9].name, "Elfa inventada");
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const RecordedText* npc = FindText(m_text.texts, "Guardia inventado");
    ASSERT_NE(npc, nullptr);
    EXPECT_TRUE(npc->windowed);
    EXPECT_EQ(npc->x, Alone + 0x23);
    EXPECT_EQ(npc->y, 0xc);
    EXPECT_EQ(npc->columns, 120);
    EXPECT_EQ(npc->style.colors, (TextColors{.text = 0xfff0ff96u, .background = 0}));
    EXPECT_NE(FindText(m_text.texts, "La primera"), nullptr);

    m_text.texts.clear();
    Hero().characterClass = 4;
    Runtime().byte6eeda8 = 1;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Elfa inventada"), nullptr);
    EXPECT_EQ(FindText(m_text.texts, "La primera"), nullptr);
    EXPECT_EQ(FindText(m_text.texts, "La tercera"), nullptr);

    // The whole class of +0x1d4: a 4 with evolution is not class 4.
    m_text.texts.clear();
    Hero().characterClass = 4 + 8;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Guardia inventado"), nullptr);
}

// 0x41c600 and 0x41c2a0: the list with its three tabs (the chosen one taller and in another color; tabs 1 and 2 in
// red), the title "Quest", the highest's name, the text without answers and the state in the large bold.
TEST_P(QuestWindowTest, TheListDrawsItsTabsAndTheState)
{
    Window() = Client::Quests::QuestListWindow;
    Quest().reached = 1;
    Quest().currentState = Client::Quests::QuestInProgress;
    ASSERT_TRUE(
        Client::Ui::ShowTheQuestDialog(Runtime().dialogBox, Runtime().dialogs, &Runtime().dialogTable, &m_texts, 10)
            .HasValue());
    ASSERT_TRUE(Draw(Alone + 25, 395).HasValue());
    const auto quads = QuadsOf(m_sprites);
    for (const auto& [tab, top, height, color] :
         {std::tuple{0, 235.0f, 16.0f, White}, std::tuple{1, 237.0f, 14.0f, Color{1.0f, 0.3f, 0.2f, 1.0f}},
          std::tuple{2, 237.0f, 14.0f, Color{1.0f, 0.3f, 0.2f, 1.0f}}})
    {
        SCOPED_TRACE(tab);
        const DrawnQuad* drawn = FindQuad(quads, 0x646, static_cast<float>(Alone + 20 + 51 * tab), top);
        ASSERT_NE(drawn, nullptr);
        ExpectSize(*drawn, 49.0f, height);
        EXPECT_EQ(drawn->color, color);
    }
    const DrawnQuad* frame = FindQuad(quads, 0x117, static_cast<float>(Alone + 10), 260.0f);
    ASSERT_NE(frame, nullptr);
    ExpectSize(*frame, 170.0f, 5.0f);
    ASSERT_NE(FindQuad(quads, 0x104, static_cast<float>(Alone + 180), 260.0f), nullptr);

    const auto& texts = m_text.texts;
    const RecordedText* first = FindText(texts, "Misiones");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->x, Alone + 20 + 24 - 24);
    EXPECT_EQ(first->y, 240);
    EXPECT_EQ(first->style.colors.text, 0xff67bfdfu);
    EXPECT_EQ(FindText(texts, "Pestana dos")->style.colors.text, 0xffffffffu);
    const RecordedText* title = FindText(texts, "Quest");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->style.font, FontKind::Bold);
    EXPECT_EQ(title->style.colors, (TextColors{.text = 0xffe6e6e6u, .background = 0xff141414u}));
    EXPECT_NE(FindText(texts, "La segunda"), nullptr);
    EXPECT_EQ(FindText(texts, "Un texto inventado")->style.font, FontKind::Bold);
    EXPECT_EQ(FindText(texts, "1) Acepto"), nullptr);
    const RecordedText* state = FindText(texts, "En curso");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->style.font, FontKind::BigBold);
    EXPECT_EQ(state->y, 0x131);
    // The close button's help: text 927 with "T".
    EXPECT_NE(FindText(texts, "Cerrar (T)"), nullptr);
}

// 0x41ca66..0x41cb44: with the character open, the quest button at (505, 395), 0x102 with the window closed and 0x103
// open, and its help "Misiones(T)" with the mouse over it.
TEST_P(QuestWindowTest, TheCharacterWindowHasTheQuestButton)
{
    ASSERT_TRUE(Draw(0x1f9, 0x18b).HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty()) << "sin el personaje";

    Runtime().panels.character = 1;
    ASSERT_TRUE(Draw(0x1f8, 0x18b).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const DrawnQuad* closed = FindQuad(quads, 0x102, 505.0f, 395.0f);
    ASSERT_NE(closed, nullptr);
    ExpectSize(*closed, 24.0f, 24.0f);
    EXPECT_TRUE(m_text.texts.empty());

    Window() = Client::Quests::QuestListWindow;
    Quest().x = Beside;
    ASSERT_TRUE(Draw(0x210, 0x1a2).HasValue());
    ASSERT_NE(FindQuad(QuadsOf(m_sprites), 0x103, 505.0f, 395.0f), nullptr);
    const RecordedText* help = FindText(m_text.texts, "Misiones(T)");
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(Runtime().hud.textColors, (TextColors{.text = 0xffffffffu, .background = 0}));
}

// 0x41b9e0: with the dialog and the quest in progress, each requested item with 0x5e1480 at (480, 235), with its
// rand(); with the list (without +0x21380) or closed, nothing.
TEST_P(QuestWindowTest, TheUi3DPassDrawsTheAskedItems)
{
    Window() = Client::Quests::QuestListWindow;
    Quest().currentState = Client::Quests::QuestInProgress;
    {
        GameHudContext context = HudContext(0, 0, m_random);
        ASSERT_TRUE(Client::Quests::DrawTheQuestItems(context).HasValue());
    }
    EXPECT_EQ(m_random.State(), Seed);

    Window() = Client::Quests::QuestDialogWindow;
    {
        GameHudContext context = HudContext(0, 0, m_random);
        ASSERT_TRUE(Client::Quests::DrawTheQuestItems(context).HasValue());
    }
    CrtRandom expected{Seed};
    {
        GameHudContext context = HudContext(0, 0, expected);
        const Client::Items::UiItemBox box{.x = 480.0f, .y = 235.0f, .width = 20.0f, .height = 20.0f};
        ASSERT_TRUE(Client::Hud::DrawUiItem(context, box, ScrollType, 0, 0, 0, false).HasValue());
    }
    EXPECT_EQ(m_random.State(), expected.State());
}

INSTANTIATE_TEST_SUITE_P(Maps, QuestWindowTest, ::testing::Values(Lorencia, Devias));

// The cases of 0x5b1c70 that do not write the buffer, which 0x41b810 reads uninitialized (Q-C874): the unnamed levels
// of the types with one name per level. The 0x180b, boxes +1..+5 of the 0x1c0b, the level 2 0x1c0c and the other types
// write with any level.
TEST(QuestItemNamesTest, OnlyTheLevelsWithoutANameAreNotWritten)
{
    struct Case
    {
        std::uint16_t type;
        std::int32_t level;
        bool written;
    };
    for (const Case& probe :
         {Case{0x1c17, 0, true},  Case{0x1c17, 1, true},  Case{0x1c17, 2, false}, Case{0x1c17, -1, false},
          Case{0x1c18, 5, false}, Case{0x1c19, 9, true},  Case{0x1c09, 1, true},  Case{0x1c09, 2, false},
          Case{0x1c15, 3, true},  Case{0x1c15, 4, false}, Case{0x1a0f, 4, true},  Case{0x1a0f, 5, false},
          Case{0x1c0b, 3, true},  Case{0x1c0b, 4, false}, Case{0x1c0b, 9, true},  Case{0x1c0b, 16, false},
          Case{0x1c0c, 2, true},  Case{0x1c0c, 3, false}, Case{0x180b, 7, true},  Case{0x0001, 13, true}})
    {
        SCOPED_TRACE(std::format("{:#x} {}", probe.type, probe.level));
        EXPECT_EQ(Client::Items::ItemNameIsWritten(probe.type, probe.level, nullptr), probe.written);
    }
}
} // namespace
