#include "Scenes/JewelBundleList.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLogWindow.h"
#include "UI/GameUiState.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// La lista de 0x6e5660 (CL9 hito 6, fase 5): agregar (0x46e940), vaciar (0x482f80) y el elegido (0x483aa0); su paso
// (0x402af0 → 0x45aa80 → 0x483bc0) con las flechas del teclado y su cola, la rueda, las flechas y la pista de la barra,
// el clic y el derecho suelto sobre una fila; y su dibujo (0x4832e0 con 0x46ebe0 y 0x46f020). Ninguno saca rand().
// Textos inventados; el mundo, en Lorencia (0), Devias (2) y Noria (3).
namespace
{
using Client::Input::InputEvent;
using Client::Input::Key;
using Client::Render::Models::CrtRandom;
using Client::Scenes::JewelBundleList;
using Client::Scenes::WorldState;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
// 0x62f734: la esquina que pone el dibujo del 0xa4, (0xe4, 0x6d + 0xf0).
constexpr std::int32_t ListX = 0xe4;
constexpr std::int32_t ListY = 0x6d + 0xf0;
// 0x46f2c0 con 8 a la vista: la fila i en y = (8 − i) · 13 + 230, y el mouse la toma desde 3 más arriba, en x de 0xe4
// a 0xe4 + 168.
constexpr std::int32_t RowX = 300;
constexpr std::int32_t OutsideX = 100;
constexpr std::int32_t OutsideY = 100;

std::vector<std::pair<std::int32_t, std::uint8_t>> RowsOf(const JewelBundleList& list)
{
    std::vector<std::pair<std::int32_t, std::uint8_t>> rows;
    for (const JewelBundleList::Row& row : list.rows)
        rows.emplace_back(row.cell, row.level);
    return rows;
}

class SteppingTheList
{
public:
    explicit SteppingTheList(std::int32_t map)
    {
        world->Runtime().map = map;
        JewelBundleList& list = List();
        list.x = ListX;
        list.y = ListY;
    }

    // `count` filas desde la casilla 1, con el nivel de la casilla % 16, y la barra como la deja el dibujo.
    void Fill(std::int32_t count)
    {
        JewelBundleList& list = List();
        Client::Scenes::ClearTheBundles(list);
        for (std::int32_t cell = 1; cell <= count; ++cell)
            Client::Scenes::AddTheBundle(list, cell, static_cast<std::uint8_t>(cell % 16));
        PlaceTheBar();
    }

    void PlaceTheBar()
    {
        JewelBundleList& list = List();
        const Client::UI::OldListBar bar =
            Client::UI::PlaceTheListBar(list.y, list.height, list.barMode, list.shownRows,
                                        static_cast<std::int32_t>(list.rows.size()), list.scroll);
        list.barTop = bar.barTop;
        list.barBottom = bar.barBottom;
        list.thumbY = bar.thumbY;
        list.thumbHeight = bar.thumbHeight;
    }

    // Sin el clic.
    Client::Scenes::BundleListStep Step(std::int32_t x, std::int32_t y)
    {
        world->Runtime().movement.pendingClick = false;
        return Run(x, y);
    }

    Client::Scenes::BundleListStep Click(std::int32_t x, std::int32_t y)
    {
        world->Runtime().movement.pendingClick = true;
        return Run(x, y);
    }

    Client::Scenes::BundleListStep Run(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::StepTheBundleList(
            Client::Scenes::GameUiContext{.world = *world, .random = random, .input = input, .mouseX = x, .mouseY = y});
    }

    void Press(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = key});
    }

    void Release(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = key});
    }

    [[nodiscard]] JewelBundleList& List() const
    {
        return world->Runtime().npcWindows.jewels.bundles;
    }

    [[nodiscard]] Client::UI::GameUiState& Ui() const
    {
        return world->Runtime().ui;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x1357u};
    Client::Input::InputState input;
};

// 0x46e940: solo las casillas de 0 a 0x40 que no están; cada una deja el elegido en 0, la última a la vista y las
// filas por casilla. 0x482f80 las vacía con el elegido y el primero que se ve en 0; 0x483aa0 da la fila elegida, o
// ninguna sin elegido o sin filas (y entonces el elegido en 0).
TEST(JewelBundleListTest, AddingSortsByCellAndKeepsTheLastInView)
{
    JewelBundleList list;
    list.selection = 2;
    Client::Scenes::AddTheBundle(list, 5, 3);
    EXPECT_EQ(list.selection, 0);
    Client::Scenes::AddTheBundle(list, 2, 1);
    Client::Scenes::AddTheBundle(list, 5, 9);
    Client::Scenes::AddTheBundle(list, -1, 0);
    Client::Scenes::AddTheBundle(list, 0x41, 0);
    Client::Scenes::AddTheBundle(list, 0x40, 0xff);
    Client::Scenes::AddTheBundle(list, 0, 7);
    const std::vector<std::pair<std::int32_t, std::uint8_t>> expected{
        {0, std::uint8_t{7}}, {2, std::uint8_t{1}}, {5, std::uint8_t{3}}, {0x40, std::uint8_t{0xff}}};
    EXPECT_EQ(RowsOf(list), expected);
    EXPECT_EQ(list.scroll, 0);

    for (std::int32_t cell = 10; cell < 16; ++cell)
        Client::Scenes::AddTheBundle(list, cell, 0);
    EXPECT_EQ(list.rows.size(), 10u);
    EXPECT_EQ(list.scroll, 2);
    EXPECT_EQ(list.rows.back().flag, 0);

    EXPECT_FALSE(Client::Scenes::ChosenBundle(list).has_value());
    list.selection = 2;
    const auto chosen = Client::Scenes::ChosenBundle(list);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->cell, 2);
    EXPECT_EQ(chosen->level, 1);

    Client::Scenes::ClearTheBundles(list);
    EXPECT_TRUE(list.rows.empty());
    EXPECT_EQ(list.selection, 0);
    EXPECT_EQ(list.scroll, 0);
    list.selection = 3;
    EXPECT_FALSE(Client::Scenes::ChosenBundle(list).has_value());
    EXPECT_EQ(list.selection, 0);
}

// 0x45aa80 y 0x483bc0, en los tres mapas: con el mouse sobre la lista es la de abajo (sin otra), de la UI, y la rueda
// corre de a 3 hasta los bordes; 0x402af0 deja 0x5762eb4 en 0. Fuera de ella, nada de eso.
TEST(JewelBundleListTest, TheWheelScrollsOverTheList)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        tested.Fill(12);
        JewelBundleList& list = tested.List();
        auto& ui = tested.Ui();
        ASSERT_EQ(list.scroll, 4);
        const CrtRandom before = tested.random;

        ui.wheel = 1;
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(ui.wheel, 1);
        EXPECT_EQ(ui.hovered, 0);
        EXPECT_EQ(ui.mouseOnUi, 0);

        ui.wheel = -1;
        static_cast<void>(tested.Step(RowX, 300));
        EXPECT_EQ(list.scroll, 1);
        EXPECT_EQ(ui.wheel, 0);
        EXPECT_EQ(ui.hovered, list.id);
        EXPECT_EQ(ui.mouseOnUi, 1);
        EXPECT_EQ(ui.capture, 0);
        ui.wheel = -1;
        static_cast<void>(tested.Step(RowX, 300));
        EXPECT_EQ(list.scroll, 0);

        ui.hovered = 9;
        ui.wheel = 2;
        static_cast<void>(tested.Step(RowX, 300));
        EXPECT_EQ(list.scroll, 4);
        EXPECT_EQ(ui.hovered, 9);
        EXPECT_FALSE(tested.world->Runtime().lastSoundId.has_value());
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x483bc0, en los tres mapas: con el clic sobre la lista, el foco del teclado; las flechas de la barra (x en
// [395, 408), y en [240, 253) y [336, 349)) corren de a una con el sonido 0x19, cuentan hasta 15 con el clic y
// corren en cada paso desde 16; la pista arriba y abajo del cursor corre 8, sin sonido. El clic queda.
TEST(JewelBundleListTest, TheBarArrowsAndTheTrackScroll)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        tested.Fill(20);
        JewelBundleList& list = tested.List();
        auto& runtime = *tested.world;
        list.scroll = 5;
        tested.PlaceTheBar();

        static_cast<void>(tested.Click(400, 241));
        EXPECT_EQ(list.scroll, 6);
        EXPECT_EQ(list.arrowRepeat, 1);
        EXPECT_EQ(runtime.Runtime().lastSoundId, 0x19);
        EXPECT_EQ(runtime.Runtime().ui.focusedList, list.id);
        EXPECT_TRUE(runtime.Runtime().movement.pendingClick);
        runtime.Runtime().lastSoundId.reset();
        for (int step = 0; step < 14; ++step)
            static_cast<void>(tested.Click(400, 241));
        EXPECT_EQ(list.scroll, 6);
        EXPECT_EQ(list.arrowRepeat, 15);
        static_cast<void>(tested.Click(400, 241));
        static_cast<void>(tested.Click(400, 241));
        EXPECT_EQ(list.scroll, 7);
        EXPECT_FALSE(runtime.Runtime().lastSoundId.has_value());

        static_cast<void>(tested.Step(400, 241));
        EXPECT_EQ(list.arrowRepeat, 0);
        static_cast<void>(tested.Click(400, 340));
        EXPECT_EQ(list.scroll, 6);
        EXPECT_EQ(runtime.Runtime().lastSoundId, 0x19);
        runtime.Runtime().lastSoundId.reset();

        tested.PlaceTheBar();
        const std::int32_t aboveThumb = static_cast<std::int32_t>(list.thumbY) - 2;
        static_cast<void>(tested.Step(400, aboveThumb));
        static_cast<void>(tested.Click(400, aboveThumb));
        EXPECT_EQ(list.scroll, 12);
        EXPECT_EQ(list.trackRepeat, 1);
        tested.PlaceTheBar();
        static_cast<void>(tested.Step(400, 300));
        const std::int32_t belowThumb = static_cast<std::int32_t>(list.thumbY + list.thumbHeight) + 2;
        static_cast<void>(tested.Click(400, belowThumb));
        EXPECT_EQ(list.scroll, 4);
        EXPECT_FALSE(runtime.Runtime().lastSoundId.has_value());

        // El cursor no se agarra (07 Q-C1151): el clic sobre él no corre nada.
        tested.PlaceTheBar();
        static_cast<void>(tested.Step(400, 300));
        static_cast<void>(tested.Click(400, static_cast<std::int32_t>(list.thumbY) + 1));
        EXPECT_EQ(list.scroll, 4);
        EXPECT_EQ(runtime.Runtime().ui.capture, 0);
    }
}

// 0x483bc0 y 0x4834a0, en los tres mapas: con el foco, Arriba manda el 0xf (el siguiente) y Abajo el 0x10 (el
// anterior, hasta el 1), que atiende el paso siguiente; repiten desde el sexto paso. El elegido queda a la vista. Sin
// elegido no eligen, y con más filas que las que se ven el primero que se ve vuelve a 0 (07 Q-C1153). Sin el foco,
// nada.
TEST(JewelBundleListTest, TheArrowKeysMoveTheChoiceOnTheNextStep)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        tested.Fill(12);
        JewelBundleList& list = tested.List();
        auto& ui = tested.Ui();
        tested.Press(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_TRUE(list.messages.empty());

        ui.focusedList = list.id;
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.messages, std::vector<std::int32_t>{0xf});
        EXPECT_EQ(list.keyRepeat, 1);
        tested.Release(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_TRUE(list.messages.empty());
        EXPECT_EQ(list.keyRepeat, 0);
        EXPECT_EQ(list.selection, 0);
        EXPECT_EQ(list.scroll, 0);

        list.selection = 3;
        tested.Press(Key::Up);
        for (int step = 0; step < 7; ++step)
            static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.keyRepeat, 6);
        EXPECT_EQ(list.messages, std::vector<std::int32_t>{0xf});
        EXPECT_EQ(list.selection, 4);
        tested.Release(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.selection, 5);

        list.selection = 12;
        list.scroll = 0;
        tested.Press(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        tested.Release(Key::Up);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.selection, 12);
        EXPECT_EQ(list.scroll, 4);

        list.selection = 2;
        tested.Press(Key::Down);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        tested.Release(Key::Down);
        static_cast<void>(tested.Step(OutsideX, OutsideY));
        EXPECT_EQ(list.selection, 1);
        EXPECT_EQ(list.scroll, 0);
    }
}

// 0x46f1a0, en los tres mapas: el clic sobre una fila la elige (el primero que se ve + 1 + la fila), da vuelta su +0
// y se apaga; el derecho suelto la elige, se apaga y devuelve la fila para 0x402a90, salvo la casilla 0 (07 Q-C1152).
TEST(JewelBundleListTest, TheRowsTakeTheClickAndTheRightRelease)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SteppingTheList tested(map);
        JewelBundleList& list = tested.List();
        for (const std::int32_t cell : {0, 7, 9})
            Client::Scenes::AddTheBundle(list, cell, static_cast<std::uint8_t>(cell));
        // Con 3 filas, la fila i en y = (3 − i) · 13 + 230.
        const std::int32_t secondRowY = 2 * 13 + 230 - 2;
        auto& runtime = tested.world->Runtime();
        auto stepped = tested.Click(RowX, secondRowY);
        EXPECT_FALSE(stepped.asked.has_value());
        EXPECT_EQ(list.selection, 2);
        EXPECT_EQ(list.rows[1].flag, 1);
        EXPECT_FALSE(runtime.movement.pendingClick);
        static_cast<void>(tested.Click(RowX, secondRowY));
        EXPECT_EQ(list.rows[1].flag, 0);
        static_cast<void>(tested.Click(OutsideX, secondRowY));
        EXPECT_TRUE(runtime.movement.pendingClick);

        runtime.ui.rightReleased = true;
        stepped = tested.Step(RowX, 3 * 13 + 230);
        EXPECT_FALSE(stepped.asked.has_value());
        EXPECT_EQ(list.selection, 1);
        EXPECT_FALSE(runtime.ui.rightReleased);

        runtime.ui.rightReleased = true;
        stepped = tested.Step(RowX, 13 + 230 + 9);
        ASSERT_TRUE(stepped.asked.has_value());
        EXPECT_EQ(stepped.asked->cell, 9);
        EXPECT_EQ(stepped.asked->level, 9);
        EXPECT_EQ(list.selection, 3);
        EXPECT_FALSE(runtime.ui.rightReleased);
    }
}

class DrawingTheList
{
public:
    explicit DrawingTheList(std::int32_t map)
    {
        world->Runtime().map = map;
        sprites.Begin(Client::App::PixelSize{640, 480});
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, bool withTexts = true)
    {
        Client::Scenes::WorldRuntime& runtime = world->Runtime();
        Client::Scenes::GameHudContext context{.world = *world,
                                               .random = random,
                                               .ownRecordDepth = runtime.ownRecordDepth,
                                               .cellXDepth = cellDepth,
                                               .cellYDepth = cellDepth,
                                               .hud = runtime.hud,
                                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                               .sound = sound,
                                               .projecting = projecting,
                                               .sprites = sprites,
                                               .text = text,
                                               .texts = withTexts ? &texts : nullptr,
                                               .mouse = mouse};
        return Client::Scenes::DrawTheBundleList(context, ListX, ListY);
    }

    // El quad `index` del lote, sin las marcas de los textos: su primer vértice (arriba a la izquierda) y el tercero.
    [[nodiscard]] std::vector<std::pair<Client::Render::SpriteVertex, Client::Render::SpriteVertex>> Quads() const
    {
        std::vector<std::pair<Client::Render::SpriteVertex, Client::Render::SpriteVertex>> quads;
        const auto vertices = sprites.Vertices();
        for (const Client::Render::DrawCommand& command : sprites.Commands())
        {
            if (command.texture == Client::Tests::TextMarkerSlot)
                continue;
            for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
                quads.emplace_back(vertices[command.firstVertex + quad * 6],
                                   vertices[command.firstVertex + quad * 6 + 2]);
        }
        return quads;
    }

    [[nodiscard]] std::vector<std::optional<std::uint16_t>> Slots() const
    {
        std::vector<std::optional<std::uint16_t>> slots;
        for (const Client::Render::DrawCommand& command : sprites.Commands())
        {
            if (command.texture == Client::Tests::TextMarkerSlot)
                continue;
            for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
                slots.push_back(command.texture ? std::optional<std::uint16_t>{command.texture->value} : std::nullopt);
        }
        return slots;
    }

    [[nodiscard]] JewelBundleList& List() const
    {
        return world->Runtime().npcWindows.jewels.bundles;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x1357u};
    std::uint8_t cellDepth = 0;
    std::optional<std::int32_t> sound;
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::UI::Point mouse{};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{1806, "bendicion"}, {1807, "alma"}});
};

// 0x4832e0, en los tres mapas: el fondo negro de 0,4 un punto más grande, las dos flechas de win_scrollbar (0x649),
// las dos líneas de la pista con el 2 de la paleta y el cursor en tres quads; con 3 filas (menos que 8) el cursor
// ocupa la pista. Las filas de abajo hacia arriba, "%s,  %d" con el nombre de la joya y (nivel + 1) · 10 en x + 6,
// el elegido con su velo de 0,5 y el texto en negro, los demás en 0xffc8dce6, en la fuente normal. Deja la barra
// para el paso, y la mezcla apagada.
TEST(JewelBundleListTest, TheListDrawsItsFrameBarAndRows)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        DrawingTheList tested(map);
        JewelBundleList& list = tested.List();
        tested.world->Runtime().npcWindows.jewels.kind = 1;
        for (const std::int32_t cell : {4, 10, 20})
            Client::Scenes::AddTheBundle(list, cell, static_cast<std::uint8_t>(cell == 20 ? 15 : cell / 5));
        list.selection = 2;
        tested.world->Runtime().hud.dcFont = Client::Render::Text::FontKind::Bold;
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Draw().HasValue());
        EXPECT_EQ(list.x, ListX);
        EXPECT_EQ(list.y, ListY);
        EXPECT_FLOAT_EQ(list.barTop, 252.0f);
        EXPECT_FLOAT_EQ(list.barBottom, 337.0f);
        EXPECT_FLOAT_EQ(list.thumbHeight, 85.0f);

        constexpr std::uint16_t Bar = 0x649;
        const std::vector<std::optional<std::uint16_t>> slots{std::nullopt, Bar, Bar, std::nullopt, std::nullopt,
                                                              Bar,          Bar, Bar, std::nullopt};
        EXPECT_EQ(tested.Slots(), slots);
        const auto quads = tested.Quads();
        ASSERT_EQ(quads.size(), 9u);
        EXPECT_FLOAT_EQ(quads[0].first.x, 227.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[0].first.y, 239.0f);
        EXPECT_NEAR(quads[0].first.color.alpha, 102.0f / 255.0f, 1e-6);
        EXPECT_FLOAT_EQ(quads[0].first.color.red, 0.0f);
        EXPECT_FLOAT_EQ(quads[1].first.x, 396.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[1].first.y, 239.0f);
        EXPECT_FLOAT_EQ(quads[1].first.v, 0.09375f);
        EXPECT_FLOAT_EQ(480.0f - quads[2].first.y, 337.0f);
        EXPECT_FLOAT_EQ(quads[2].first.v, 0.5f);
        EXPECT_FLOAT_EQ(quads[3].first.x, 396.0f);
        EXPECT_FLOAT_EQ(quads[4].first.x, 408.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[4].first.y, 252.0f);
        EXPECT_FLOAT_EQ(quads[5].first.x, 397.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[5].first.y, 252.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[7].first.y, 336.0f);
        EXPECT_FLOAT_EQ(quads[7].first.v, 0.0625f);
        // El velo de la fila 1 (el elegido 2), en (0xe4, 256 − 3).
        EXPECT_FLOAT_EQ(quads[8].first.x, 228.0f);
        EXPECT_FLOAT_EQ(480.0f - quads[8].first.y, 253.0f);
        EXPECT_FLOAT_EQ(quads[8].first.color.alpha, 0.5f);
        EXPECT_FLOAT_EQ(quads[8].second.x, 396.0f);

        const auto& drawn = tested.text.texts;
        ASSERT_EQ(drawn.size(), 3u);
        EXPECT_EQ(drawn[0].text, "alma,  10");
        EXPECT_EQ(drawn[0].x, 0xe4 + 6);
        EXPECT_EQ(drawn[0].y, 269);
        EXPECT_EQ(drawn[0].style.colors.text, 0xffc8dce6u);
        EXPECT_EQ(drawn[0].style.colors.background, 0u);
        EXPECT_EQ(drawn[0].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(drawn[1].text, "alma,  30");
        EXPECT_EQ(drawn[1].y, 256);
        EXPECT_EQ(drawn[1].style.colors.text, 0xff000000u);
        EXPECT_EQ(drawn[2].text, "alma,  160");
        EXPECT_EQ(drawn[2].y, 243);
        EXPECT_EQ(tested.sprites.Blend(), Client::Render::BlendMode::Opaque);
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x46ebe0 y 0x4832e0, en los tres mapas: con el clic sobre una flecha, dada vuelta (las UV de abajo hacia arriba);
// con más filas que las que se ven, el cursor es el de la barra y la bendición dice 1806. Un dibujo intermedio no
// escribe la esquina ni la barra. Sin Text.bmd, corta en el texto de la joya.
TEST(JewelBundleListTest, ThePressedArrowFlipsAndAnIntermediateDrawWritesNothing)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        DrawingTheList tested(map);
        JewelBundleList& list = tested.List();
        tested.world->Runtime().npcWindows.jewels.kind = 0;
        for (std::int32_t cell = 0; cell < 12; ++cell)
            Client::Scenes::AddTheBundle(list, cell, 0);
        tested.world->Runtime().movement.pendingClick = true;
        tested.mouse = {397, 345};
        ASSERT_TRUE(tested.Draw(true).HasValue());
        EXPECT_EQ(list.x, 0);
        EXPECT_FLOAT_EQ(list.barTop, 0.0f);
        const auto quads = tested.Quads();
        ASSERT_EQ(quads.size(), 8u);
        EXPECT_FLOAT_EQ(quads[1].first.u, 0.0f);
        EXPECT_FLOAT_EQ(quads[2].first.u, 0.8125f);
        EXPECT_FLOAT_EQ(quads[2].first.v, 0.5f);
        EXPECT_FLOAT_EQ(quads[2].second.u, 0.0f);
        EXPECT_FLOAT_EQ(quads[2].second.v, 0.09375f);
        // El cursor de 8/12 de la pista, arriba con el primero que se ve en 4 (el último).
        EXPECT_NEAR(480.0f - quads[5].first.y, 252.0f, 1e-3);
        ASSERT_EQ(tested.text.texts.size(), 8u);
        EXPECT_EQ(tested.text.texts[0].text, "bendicion,  10");

        const auto cut = tested.Draw(false, false);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("1806"), std::string::npos) << cut.Error();
        EXPECT_EQ(list.x, ListX);
    }
}
} // namespace
