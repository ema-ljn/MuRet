#include "Scenes/QuestWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/QuestTable.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/NpcNames.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/QuestState.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/ZenText.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// La ventana de las misiones (CL9 hito 2; 06 § Las misiones, "La ventana"): el paso 0x41b5a0 (0x41a9e0, 0x419bd0 y
// 0x41b170), el dibujo 0x41ca40 (0x41ba20, 0x41b680, 0x41b810 y 0x41c600) y el 3D 0x41b9e0, en 640 × 480. Las
// misiones, los diálogos, los textos y los nombres son inventados. Cada test corre en Lorencia y en Devias: nada de la
// ventana mira el mapa.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Scenes::GameHudContext;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x5eed4242u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// Un registro propio que se cierra hasta 0 gasta 3.848 rand() (0x40b4d0); el C3 saca el relleno y el serial.
constexpr int OwnRecordClose = 0xf08;
constexpr int C3Randoms = 2;
// 0x41b5bb..0x41b64c.
constexpr std::int32_t Alone = 0x1c2;
constexpr std::int32_t Beside = 0x104;
// El pergamino de la misión 0, el primero de los ítems de misión (grupo 14, índice 23).
constexpr std::int32_t ScrollType = 0x1c17;
constexpr std::int32_t NpcType = 0xeb;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// 0x419bf7..0x419c3a con un renglón y tres respuestas: el texto en 93 y las respuestas desde 111 con el estado 1, o
// desde 0xfa con otro.
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

// Item.bmd con los nombres de los cuatro ítems de misión y nada más.
Client::Data::Tables::ItemTable QuestItems()
{
    using Client::Data::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    const std::vector<std::pair<std::int32_t, std::string_view>> names{
        {0x1c17, "Pergamino"}, {0x1c18, "Hueso"}, {0x1c19, "Cuerno"}, {0x1c1a, "Pluma"}};
    for (const auto& [type, name] : names)
    {
        const auto base = static_cast<std::size_t>(type) * ItemRecordSize;
        for (std::size_t i = 0; i < name.size(); ++i)
            plain[base + i] = static_cast<std::byte>(name[i]);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Lo que pide la UI, en orden.
class RecordingRequests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::OptionData::OptionBody& /*body*/) override
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
    void SendWindowClose() override
    {
        requests.emplace_back("31");
    }
    void SendSetQuest(std::uint8_t quest, std::uint8_t state) override
    {
        requests.push_back("A2 " + std::to_string(quest) + " " + std::to_string(state));
    }
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

// Un quad del lote: el slot (Marker si es la marca de un texto), el color y las esquinas 0 y 2 del fan.
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

// El primer quad con ese slot y esa esquina (en 640 × 480, la y de GL es 480 − y).
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
        Client::Scenes::WorldRuntime& runtime = Runtime();
        runtime.map = GetParam();
        runtime.hero = 3;
        Hero().alive = true;
        Hero().characterClass = 0;
        Client::Scenes::QuestState& quest = Quest();
        quest.characterClass = 0;
        // La misión 0: el pergamino para la clase 0, con nivel 10 y 1.000 de zen.
        Client::Data::Tables::QuestRecord& first = quest.records[0];
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
        // Dialog.bmd: el 10 con tres respuestas (mirar y aceptar → 11, cerrar, aceptar → 12).
        runtime.dialogTable.assign(Client::Data::Text::DialogCount, {});
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

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }
    Client::Scenes::QuestState& Quest()
    {
        return Runtime().quest;
    }
    Client::Scenes::ActorState& Hero()
    {
        return m_world->Actors()[Runtime().hero];
    }
    Client::Scenes::GameMovementState& Movement()
    {
        return Runtime().movement;
    }
    std::uint8_t& Window()
    {
        return Runtime().panels.delegated.quest;
    }

    // La ventana del diálogo con el NPC con la misión 0 en `state` y el diálogo 10 (0x4198b0 sin su 0x419600).
    void OpenTheDialog(std::uint8_t state)
    {
        Window() = Client::Scenes::QuestDialogWindow;
        Quest().current = 0;
        Quest().currentState = state;
        ASSERT_TRUE(
            Client::UI::ShowTheQuestDialog(Runtime().dialogBox, Runtime().dialogs, &Runtime().dialogTable, &m_texts, 10)
                .HasValue());
        Runtime().dialogs = {};
    }

    void GiveTheScroll()
    {
        Client::World::ItemState& item = m_world->Inventory().cells[5];
        item.id = static_cast<std::uint16_t>(ScrollType);
        item.occupied = 1;
    }

    void Click()
    {
        Client::Scenes::LatchTheLeftButton(Movement(), true, true);
    }

    GameUiContext UiContext(std::int32_t x, std::int32_t y, Client::Scenes::IGameUiRequests* requests)
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
        Client::Scenes::GamePanels panels(*m_world, m_random, Client::Scenes::GamePanelTables{.texts = &m_texts});
        panels.LendTheWindowClose(requests);
        return Client::Scenes::StepTheQuestWindow(UiContext(x, y, requests), panels.Registry());
    }

    GameHudContext HudContext(std::int32_t x, std::int32_t y, CrtRandom& random)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
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
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::DrawTheQuestWindow(context, panels.Registry());
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(QuestTexts());
    Client::Data::Tables::ItemTable m_items = QuestItems();
};

// 0x41b5bb..0x41b64c: la esquina, (0x104, 0) con el personaje abierto y (0x1c2, 0) sin él, en cada paso. Con la ventana
// cerrada no hace nada más ni saca rand().
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

// 0x41b5cf..0x41b63b: con el personaje abierto, el clic en [0x1f9, 0x211) × [0x18b, 0x1a3) abre la lista (Open(8),
// 0x419840 con el diálogo de la misión más alta) y otro la cierra (Close(8), el C1 03 31); los dos suenan. Sin el
// personaje, el botón no está.
TEST_P(QuestWindowTest, TheCharacterButtonOpensAndClosesTheList)
{
    RecordingRequests requests;
    Quest().states[0] = Client::Scenes::QuestAvailable;
    Click();
    ASSERT_TRUE(Step(0x1f9, 0x18b, &requests).HasValue());
    EXPECT_EQ(Window(), 0) << "sin el personaje";
    EXPECT_TRUE(Movement().pendingClick);

    Runtime().panels.character = 1;
    for (const auto& [x, y] :
         {std::pair{0x1f8, 0x18b}, std::pair{0x211, 0x18b}, std::pair{0x1f9, 0x18a}, std::pair{0x1f9, 0x1a3}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        ASSERT_TRUE(Step(x, y, &requests).HasValue());
        EXPECT_EQ(Window(), 0);
        EXPECT_TRUE(Movement().pendingClick);
    }
    Movement().mouseUpdateTime = 4;
    Movement().mouseUpdateTimeMax = 12;
    ASSERT_TRUE(Step(0x210, 0x1a2, &requests).HasValue());
    EXPECT_EQ(Window(), Client::Scenes::QuestListWindow);
    EXPECT_FALSE(Movement().pendingClick);
    EXPECT_EQ(Movement().mouseUpdateTime, 0);
    EXPECT_EQ(Movement().mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    // 0x419840: la misión 0 disponible, con las condiciones sin el zen, y su diálogo 10.
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 10);
    EXPECT_TRUE(requests.requests.empty());

    Runtime().lastSoundId = 0;
    Click();
    ASSERT_TRUE(Step(0x1f9, 0x18b, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
}

// 0x419bd0, la acción 2: el clic sobre la segunda respuesta (las respuestas en 0xfa con el estado 3) gasta el clic,
// suelta el botón, cierra con 0x419900 (el C1 03 31, sin el sonido del registro) y suena; el diálogo 0 no se muestra.
TEST_P(QuestWindowTest, TheCloseAnswerClosesTheWindow)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Scenes::QuestAvailable);
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 18, &requests).HasValue());
    EXPECT_EQ(Window(), Client::Scenes::QuestDialogWindow) << "sin el clic";

    Click();
    Movement().mouseUpdateTime = 4;
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 18, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_FALSE(Movement().pendingClick);
    EXPECT_FALSE(Movement().leftHeld);
    EXPECT_EQ(Movement().mouseUpdateTime, 0);
    EXPECT_EQ(Movement().mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 10);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x419bd0, la acción 3: el A2 con la misión y 1 (los dos rand() del C3) y el diálogo 12 al que lleva la respuesta.
TEST_P(QuestWindowTest, TheAcceptAnswerSendsTheQuestAndShowsTheNextDialog)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Scenes::QuestAvailable);
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 36, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"}));
    EXPECT_EQ(Window(), Client::Scenes::QuestDialogWindow);
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 12);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{Seed}, C3Randoms).State());

    // Sin el cable, el A2 corta antes de sacar los rand().
    OpenTheDialog(Client::Scenes::QuestAvailable);
    Click();
    const std::uint32_t before = m_random.State();
    const auto cut = Step(AnswersCenterX, AnswersTopOtherwise + 36, nullptr);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x41a35b"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_random.State(), before);
}

// 0x419bd0, la acción 1: con las condiciones (el nivel y el zen, cada uno con su apertura del registro propio), el A2
// y el diálogo 11; sin el zen, el diálogo de la condición (20) y ni el A2 ni el 11.
TEST_P(QuestWindowTest, TheCheckingAnswerLooksAtTheConditionsWithTheZen)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Scenes::QuestAvailable);
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"}));
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 11);
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{Seed}, 2 * OwnRecordClose + C3Randoms).State());

    m_world->OwnCharacter().zen = 999;
    OpenTheDialog(Client::Scenes::QuestAvailable);
    Runtime().lastSoundId = 0;
    const std::uint32_t before = m_random.State();
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopOtherwise + 17, &requests).HasValue());
    EXPECT_EQ(requests.requests.size(), 1U);
    EXPECT_EQ(Quest().currentState, Client::Scenes::QuestConditionFailed);
    EXPECT_EQ(Runtime().dialogBox.dialogIndex, 20);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{before}, 2 * OwnRecordClose).State());
}

// Q-C870 y Q-C871: la fila sale de (y − arriba) / 0x12 con la división de C, así que de 1 a 17 píxeles por encima de
// la primera respuesta es la 0; desde 18, la −1, que sale después de gastar el clic. La x se mide contra 0x236, no
// contra la ventana: con el personaje abierto (la ventana en 0x104) las respuestas no se eligen donde se dibujan.
TEST_P(QuestWindowTest, TheAnswerRowsAreMeasuredLikeTheOriginal)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Scenes::QuestInProgress);
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopInProgress - 17, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"})) << "la respuesta 0, mirar y aceptar";

    OpenTheDialog(Client::Scenes::QuestInProgress);
    requests.requests.clear();
    Runtime().lastSoundId = 0;
    const std::uint32_t before = m_random.State();
    Click();
    ASSERT_TRUE(Step(AnswersCenterX, AnswersTopInProgress - 18, &requests).HasValue());
    EXPECT_TRUE(requests.requests.empty());
    EXPECT_FALSE(Movement().pendingClick) << "el clic se gasta igual";
    EXPECT_FALSE(Movement().leftHeld);
    EXPECT_EQ(Runtime().lastSoundId, 0);
    EXPECT_EQ(m_random.State(), before);

    // 0x419c62..0x419c71: |0x236 − x| hasta 0x6a.
    for (const auto& [x, inside] : {std::pair{AnswersCenterX - 0x6a, true}, std::pair{AnswersCenterX + 0x6a, true},
                                    std::pair{AnswersCenterX - 0x6b, false}, std::pair{AnswersCenterX + 0x6b, false},
                                    std::pair{Beside + 0x64, false}})
    {
        SCOPED_TRACE(testing::Message() << x);
        OpenTheDialog(Client::Scenes::QuestInProgress);
        Click();
        ASSERT_TRUE(Step(x, AnswersTopInProgress + 18, &requests).HasValue());
        EXPECT_EQ(Window(), inside ? 0 : Client::Scenes::QuestDialogWindow);
        EXPECT_EQ(Movement().pendingClick, !inside);
    }
}

// 0x41aa4a..0x41b0cc: con la misión en curso y el pergamino en el inventario (0x419b10), el clic en el botón de
// terminar, [x + 0x23, + 0x78) × [0x163, 0x17b), manda el A2, gasta el clic y suelta el botón, y suena; 0x77f3b10 y
// 0x6c8dcc quedan. Sin el pergamino, el botón no hace nada.
TEST_P(QuestWindowTest, TheFinishButtonNeedsTheItems)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Scenes::QuestInProgress);
    Click();
    ASSERT_TRUE(Step(Alone + 0x23, 0x163, &requests).HasValue());
    EXPECT_TRUE(requests.requests.empty());
    EXPECT_TRUE(Movement().pendingClick);

    GiveTheScroll();
    for (const auto& [x, y] : {std::pair{Alone + 0x22, 0x163}, std::pair{Alone + 0x9b, 0x163},
                               std::pair{Alone + 0x23, 0x162}, std::pair{Alone + 0x23, 0x17b}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        ASSERT_TRUE(Step(x, y, &requests).HasValue());
        EXPECT_TRUE(requests.requests.empty());
    }
    Movement().mouseUpdateTime = 4;
    Movement().mouseUpdateTimeMax = 12;
    ASSERT_TRUE(Step(Alone + 0x9a, 0x17a, &requests).HasValue());
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"A2 0 1"}));
    EXPECT_FALSE(Movement().pendingClick);
    EXPECT_FALSE(Movement().leftHeld);
    EXPECT_EQ(Movement().mouseUpdateTime, 4);
    EXPECT_EQ(Movement().mouseUpdateTimeMax, 12);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(m_random.State(), Advanced(CrtRandom{Seed}, C3Randoms).State());
}

// 0x41aa0e..0x41aa3a y 0x41b0e7..0x41b14c: el mouse sobre la ventana es de la UI; el botón de cerrar gasta el clic y
// cierra con 0x419900 sin sonar; el soltado (0x7d366ec) se apaga en cada paso.
TEST_P(QuestWindowTest, TheDialogCloseButtonAndTheMouse)
{
    RecordingRequests requests;
    OpenTheDialog(Client::Scenes::QuestAvailable);
    Movement().releasedClick = true;
    ASSERT_TRUE(Step(Alone, 0, &requests).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_FALSE(Movement().releasedClick);
    Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(Step(Alone - 1, 0, &requests).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 0);

    Click();
    ASSERT_TRUE(Step(Alone + 0x19 + 0x17, 0x18b + 0x17, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_FALSE(Movement().pendingClick);
    EXPECT_TRUE(Movement().leftHeld);
    EXPECT_EQ(Movement().mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, std::nullopt);
}

// 0x41b170: la pestaña 0 se elige y gasta el clic, sin el C1 9F (Q-C845); la 1 y la 2 no gastan nada. La elegida va de
// 0xeb a 0xfb y las otras de 0xed; el botón de cerrar cierra con Close(8), que suena.
TEST_P(QuestWindowTest, TheListTabsAndItsCloseButton)
{
    RecordingRequests requests;
    Window() = Client::Scenes::QuestListWindow;
    Quest().tab = 1;
    Click();
    ASSERT_TRUE(Step(Alone + 0x14 + 0x33, 0xeb, &requests).HasValue());
    EXPECT_TRUE(Movement().pendingClick) << "la pestaña 1";
    EXPECT_EQ(Quest().tab, 1);
    ASSERT_TRUE(Step(Alone + 0x14, 0xec, &requests).HasValue());
    EXPECT_TRUE(Movement().pendingClick) << "la 0 sin elegir arranca en 0xed";
    ASSERT_TRUE(Step(Alone + 0x14 + 0x30, 0xfa, &requests).HasValue());
    EXPECT_FALSE(Movement().pendingClick);
    EXPECT_EQ(Quest().tab, 0);
    EXPECT_EQ(Movement().mouseUpdateTimeMax, 6);
    EXPECT_TRUE(requests.requests.empty());

    Click();
    ASSERT_TRUE(Step(Alone + 0x19, 0x18b, &requests).HasValue());
    EXPECT_EQ(Window(), 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"31"}));
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5ee20d: el paso de la UI llega a la ventana después de los comandos; con el personaje abierto, el botón abre la
// lista.
TEST_P(QuestWindowTest, TheGameUiStepsTheWindow)
{
    RecordingRequests requests;
    Runtime().panels.character = 1;
    // 0x424230: el pedido del caballo oscuro ya salió.
    Runtime().panels.delegated.pet = 1;
    Quest().states[0] = Client::Scenes::QuestAvailable;
    Click();
    const auto stepped = Client::Scenes::StepTheGameUi(UiContext(0x1f9, 0x18b, &requests));
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Window(), Client::Scenes::QuestListWindow);
    EXPECT_EQ(Quest().x, Beside);
}

// 0x41ba20 con la misión en curso: el fondo con la línea, la línea de y + 325, el pergamino "Pergamino x 1" en negrita
// en (0x1fe, 240), el botón de terminar (blanco con el pergamino, gris sin él) con el 699, el de cerrar, el nombre de
// la misión en su caja de 150, el texto y las respuestas desde 111.
TEST_P(QuestWindowTest, TheDialogInProgressDrawsTheItemsAndTheFinishButton)
{
    OpenTheDialog(Client::Scenes::QuestInProgress);
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
    // Sin la tabla de 0x7681eb8 (P-C174), el NPC no tiene nombre: no hay título.
    EXPECT_EQ(std::ranges::count_if(texts, [](const RecordedText& drawn) { return drawn.y == 0xc; }), 0);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5b1c70 desde 0x41b810: el 0x1c17 y el 0x1c18 con nivel 1 se llaman con los textos 906 y 907; con otro nivel
// distinto de 0 el buffer queda sin iniciar y corta (Q-C874), y un ítem que no es de misión corta hacia el hito 11.
TEST_P(QuestWindowTest, TheAskedItemNamesFollowTheLevel)
{
    auto& entry = Quest().records[0].entries[0];
    entry.itemLevel = 1;
    OpenTheDialog(Client::Scenes::QuestInProgress);
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Pergamino de nivel x 1"), nullptr);

    entry.itemIndex = 24;
    m_text.texts.clear();
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Otro de nivel x 1"), nullptr);

    for (const auto& [index, where] : {std::pair{23, "0x5b1cb1"}, std::pair{24, "0x5b1cf3"}})
    {
        SCOPED_TRACE(where);
        entry.itemIndex = static_cast<std::uint8_t>(index);
        entry.itemLevel = 2;
        const auto cut = Draw(0, 0);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_TRUE(cut.Error().starts_with(where)) << cut.Error();
    }

    entry.itemGroup = 0;
    entry.itemIndex = 1;
    entry.itemLevel = 0;
    const auto cut = Draw(0, 0);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x5b1d39")) << cut.Error();
}

TEST_P(QuestWindowTest, WithoutTheItemsTheFinishButtonIsGrey)
{
    OpenTheDialog(Client::Scenes::QuestInProgress);
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const DrawnQuad* finish = FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f);
    ASSERT_NE(finish, nullptr);
    EXPECT_EQ(finish->color, (Color{0.3f, 0.3f, 0.3f, 1.0f}));
    const RecordedText* item = FindText(m_text.texts, "Pergamino x 1");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->style.colors.text, 0xff1e1effu);
}

// 0x41bb03..0x41bb84: con el clic sobre el botón de terminar, más oscuro; con el soltado, el clic y el botón abajo se
// gastan (no en un dibujo intermedio de MuRet).
TEST_P(QuestWindowTest, ThePressedFinishButtonSpendsTheReleasedClick)
{
    OpenTheDialog(Client::Scenes::QuestInProgress);
    GiveTheScroll();
    Click();
    ASSERT_TRUE(Draw(Alone + 35, 355).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const DrawnQuad* finish = FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f);
    ASSERT_NE(finish, nullptr);
    EXPECT_EQ(finish->color, (Color{0.4f, 0.4f, 0.4f, 1.0f}));
    EXPECT_TRUE(Movement().pendingClick) << "sin el soltado";

    Movement().releasedClick = true;
    {
        GameHudContext context = HudContext(Alone + 154, 378, m_random);
        context.projecting = true;
        Client::Scenes::GamePanels panels(*m_world, m_random);
        ASSERT_TRUE(Client::Scenes::DrawTheQuestWindow(context, panels.Registry()).HasValue());
        EXPECT_TRUE(Movement().pendingClick) << "proyectado";
    }
    ASSERT_TRUE(Draw(Alone + 154, 378).HasValue());
    EXPECT_FALSE(Movement().pendingClick);
    EXPECT_FALSE(Movement().leftHeld);
}

// 0x41bc0b..0x41bcca con la misión disponible: el fondo del zen en (x + 50, y + 367,7), el 198 en (x + 20, 370) y el
// zen que pide la condición con su color.
TEST_P(QuestWindowTest, TheAvailableQuestShowsItsZen)
{
    OpenTheDialog(Client::Scenes::QuestAvailable);
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
    EXPECT_EQ(amount->style.colors.text, Client::Scenes::ZenColor(1234567));
    EXPECT_EQ(FindQuad(quads, 0xf0, static_cast<float>(Alone + 35), 355.0f), nullptr);
    // Las respuestas, en 0xfa.
    const RecordedText* answer = FindText(m_text.texts, "1) Acepto");
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->y, AnswersTopOtherwise);
}

// 0x41b72e..0x41b7ac: la respuesta bajo el mouse va en rojo si el mouse está a no más de 0x6a de x + 0x6a; la fila de
// 1 a 17 píxeles por encima es la 0 (Q-C870).
TEST_P(QuestWindowTest, TheAnswerUnderTheMouseIsRed)
{
    OpenTheDialog(Client::Scenes::QuestInProgress);
    ASSERT_TRUE(Draw(Alone + 0xd4, AnswersTopInProgress - 17).HasValue());
    EXPECT_EQ(FindText(m_text.texts, "1) Acepto")->style.colors.text, 0xff0000ffu);
    EXPECT_EQ(FindText(m_text.texts, "2) Me voy")->style.colors.text, 0xff67bfdfu);

    m_text.texts.clear();
    ASSERT_TRUE(Draw(Alone + 0xd5, AnswersTopInProgress + 18).HasValue());
    EXPECT_EQ(FindText(m_text.texts, "2) Me voy")->style.colors.text, 0xff67bfdfu);
}

// 0x41bde0..0x41be9e: el título es el nombre del NPC de la misión en la tabla de 0x7681eb8; con el héroe de clase 3 o 4
// y 0x6eeda8, el de la misión 2, y sin el nombre de la misión.
TEST_P(QuestWindowTest, TheTitleIsTheNameOfTheNpc)
{
    OpenTheDialog(Client::Scenes::QuestAvailable);
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

    // La clase entera de +0x1d4: un 4 con evolución no es la 4.
    m_text.texts.clear();
    Hero().characterClass = 4 + 8;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_NE(FindText(m_text.texts, "Guardia inventado"), nullptr);
}

// 0x41c600 y 0x41c2a0: la lista con sus tres pestañas (la elegida más alta y en otro color; la 1 y la 2 en rojo), el
// título "Quest", el nombre de la más alta, el texto sin respuestas y el estado en la negrita grande.
TEST_P(QuestWindowTest, TheListDrawsItsTabsAndTheState)
{
    Window() = Client::Scenes::QuestListWindow;
    Quest().reached = 1;
    Quest().currentState = Client::Scenes::QuestInProgress;
    ASSERT_TRUE(
        Client::UI::ShowTheQuestDialog(Runtime().dialogBox, Runtime().dialogs, &Runtime().dialogTable, &m_texts, 10)
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
    // La ayuda del botón de cerrar: el 927 con "T".
    EXPECT_NE(FindText(texts, "Cerrar (T)"), nullptr);
}

// 0x41ca66..0x41cb44: con el personaje abierto, el botón de las misiones en (505, 395), 0x102 con la ventana cerrada y
// 0x103 abierta, y su ayuda "Misiones(T)" con el mouse encima.
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

    Window() = Client::Scenes::QuestListWindow;
    Quest().x = Beside;
    ASSERT_TRUE(Draw(0x210, 0x1a2).HasValue());
    ASSERT_NE(FindQuad(QuadsOf(m_sprites), 0x103, 505.0f, 395.0f), nullptr);
    const RecordedText* help = FindText(m_text.texts, "Misiones(T)");
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(Runtime().hud.textColors, (TextColors{.text = 0xffffffffu, .background = 0}));
}

// 0x41b9e0: con el diálogo y la misión en curso, cada ítem pedido con 0x5e1480 en (480, 235), con sus rand(); con la
// lista (sin +0x21380) o cerrada, nada.
TEST_P(QuestWindowTest, TheUi3DPassDrawsTheAskedItems)
{
    Window() = Client::Scenes::QuestListWindow;
    Quest().currentState = Client::Scenes::QuestInProgress;
    {
        GameHudContext context = HudContext(0, 0, m_random);
        ASSERT_TRUE(Client::Scenes::DrawTheQuestItems(context).HasValue());
    }
    EXPECT_EQ(m_random.State(), Seed);

    Window() = Client::Scenes::QuestDialogWindow;
    {
        GameHudContext context = HudContext(0, 0, m_random);
        ASSERT_TRUE(Client::Scenes::DrawTheQuestItems(context).HasValue());
    }
    CrtRandom expected{Seed};
    {
        GameHudContext context = HudContext(0, 0, expected);
        const Client::Scenes::UiItemBox box{.x = 480.0f, .y = 235.0f, .width = 20.0f, .height = 20.0f};
        ASSERT_TRUE(Client::Scenes::DrawUiItem(context, box, ScrollType, 0, 0, 0, false).HasValue());
    }
    EXPECT_EQ(m_random.State(), expected.State());
}

INSTANTIATE_TEST_SUITE_P(Maps, QuestWindowTest, ::testing::Values(Lorencia, Devias));
} // namespace
