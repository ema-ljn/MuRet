#include "UI/ServerListWindow.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Connection/ServerList.h"
#include "Render/SpriteBatch.h"
#include "Scenes/SceneTextures.h"
#include "UI/ServerListModel.h"
#include "UI/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Client::UI::ServerListWindow: la ventana de la lista (0x6451d0, 0x645520, 0x6456c0, 0x645860 y
// 0x645e10, en el paso; CL5.5). 21 botones de grupo en dos columnas de 10 y uno abajo al medio, y
// los de servidor del grupo elegido en el medio: 20, uno por número, donde el original tiene 16
// (Q-C63). Todo en píxeles, centrada como la ubica 0x65f300.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Protocol::Connection::ServerEntry;
using Client::Tests::ClickAt;
using Client::UI::DescriptionHeight;
using Client::UI::GroupServers;
using Client::UI::LayoutServerList;
using Client::UI::Rect;
using Client::UI::ServerClick;
using Client::UI::ServerListLayout;
using Client::UI::ServerListWindow;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

// El alto de la caja de la descripción (+0x3064): 10 líneas del alto del buffer de server_ex02
// (slot 0x19, 6 × 4 en un buffer de 8 × 4) más 6 y 6: 52.
constexpr int Description = 52;

// Los buffers de las texturas del laboratorio, para las UV; el tamaño de cada botón es del
// binario, no de la textura.
Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0xc, {128, 128}}, {0xd, {256, 128}}, {0xe, {256, 4}}, {0x19, {8, 4}}});
}

// Textos sintéticos en los índices de la lista: los nombres de los grupos 20 y 19 y los de la
// carga.
Client::Data::Text::TextTable ListTexts()
{
    return Client::Tests::MakeTextTable(
        {{1829, "Twenty"}, {552, "Nineteen"}, {560, "Full"}, {561, "Busy"}, {562, "Free"}});
}

// Un paso con un clic en (x, y), con los flancos borrados después.
std::optional<ServerClick> ClickOn(ServerListWindow& window, int x, int y, PixelSize screen = Screen640)
{
    InputState input;
    ClickAt(input, x, y);
    return window.Step(input, screen);
}

// El medio de un rectángulo.
int MiddleX(const Rect& rect)
{
    return rect.x + rect.width / 2;
}

int MiddleY(const Rect& rect)
{
    return rect.y + rect.height / 2;
}

// 0x6451d0: el alto de la caja es 10 veces el alto del buffer del slot 0x19 más 12.
TEST(ServerListWindowTest, TheDescriptionIsTenLinesOfItsTexture)
{
    EXPECT_EQ(DescriptionHeight(Client::Scenes::TextureBufferSize{8, 4}), Description);
    EXPECT_EQ(DescriptionHeight(Client::Scenes::TextureBufferSize{8, 8}), 92);
}

// La ventana mide 465 × (16 · 26 + 26 + 52 + 10) = 465 × 504, y 0x65f300 la pone en ((W − 465) / 2,
// (H − 504) / 2) con la división de C: en 640 × 480, (87, -12). Los botones de grupo van en dos
// columnas desde la fila y + 504 − 11 · 26 − 52 − 10, el 20 al medio y abajo.
TEST(ServerListWindowTest, ItIsCenteredInPixels)
{
    const ServerListLayout small = LayoutServerList(Screen640, Description, 0);
    EXPECT_EQ(small.window, (Rect{.x = 87, .y = -12, .width = 465, .height = 504}));
    EXPECT_EQ(small.groupButtons[0], (Rect{.x = 87, .y = 144, .width = 108, .height = 26}));
    EXPECT_EQ(small.groupButtons[9], (Rect{.x = 87, .y = 378, .width = 108, .height = 26}));
    EXPECT_EQ(small.groupButtons[10], (Rect{.x = 444, .y = 144, .width = 108, .height = 26}));
    EXPECT_EQ(small.groupButtons[19], (Rect{.x = 444, .y = 378, .width = 108, .height = 26}));
    EXPECT_EQ(small.groupButtons[20], (Rect{.x = 265, .y = 409, .width = 108, .height = 26}));

    const ServerListLayout large = LayoutServerList(Screen1024, Description, 0);
    EXPECT_EQ(large.window, (Rect{.x = 279, .y = 132, .width = 465, .height = 504}));
    EXPECT_EQ(large.groupButtons[0], (Rect{.x = 279, .y = 288, .width = 108, .height = 26}));
    EXPECT_EQ(large.groupButtons[20], (Rect{.x = 457, .y = 553, .width = 108, .height = 26}));
}

// 0x6456c0: los de servidor van en x + 136, desde la fila del primero de grupo; con 10 o más, el
// último termina donde la fila 10, y los de antes suben. La barra de la carga, en (+16, +19).
TEST(ServerListWindowTest, TheServerButtonsGoInTheMiddle)
{
    const ServerListLayout three = LayoutServerList(Screen640, Description, 3);
    EXPECT_EQ(three.serverButtons[0], (Rect{.x = 223, .y = 144, .width = 193, .height = 26}));
    EXPECT_EQ(three.serverButtons[2], (Rect{.x = 223, .y = 196, .width = 193, .height = 26}));
    EXPECT_EQ(three.loadBars[2], (Rect{.x = 239, .y = 215, .width = 160, .height = 4}));

    const ServerListLayout ten = LayoutServerList(Screen640, Description, 10);
    EXPECT_EQ(ten.serverButtons[0].y, 144);

    const ServerListLayout sixteen = LayoutServerList(Screen640, Description, 16);
    EXPECT_EQ(sixteen.serverButtons[0].y, -12);
    EXPECT_EQ(sixteen.serverButtons[15].y, 378);
}

// Q-C63: el original arma un botón por servidor, pero tiene 16; MuRet tiene 20 y los muestra a
// todos. Con 20, el primero sube a la fila de la 10 menos 10 filas: en 1024 × 768, 288 + 260 −
// 520 = 28.
TEST(ServerListWindowTest, ShowsTheTwentyServersOfAGroup)
{
    const ServerListLayout layout = LayoutServerList(Screen1024, Description, 20);
    EXPECT_EQ(layout.serverButtons[0].y, 28);
    EXPECT_EQ(layout.serverButtons[19].y, 522);

    std::vector<ServerEntry> list;
    for (std::uint16_t i = 0; i < 20; ++i)
    {
        list.push_back(ServerEntry{static_cast<std::uint16_t>(400 + i), 0});
    }
    const auto texts = ListTexts();
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(list));
    ASSERT_EQ(ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]), Screen1024),
              std::nullopt);
    const std::optional<ServerClick> last =
        ClickOn(window, MiddleX(layout.serverButtons[19]), MiddleY(layout.serverButtons[19]), Screen1024);
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->code, 419);

    window.Open(GroupServers(list));
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]), Screen1024);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen1024);
    Client::Tests::RecordingTextDrawer text(batch);
    window.Draw(batch, text, Screen1024);
    std::size_t serverLabels = 0;
    for (const auto& drawn : text.texts)
    {
        if (drawn.text.starts_with("Twenty-"))
        {
            ++serverLabels;
        }
    }
    EXPECT_EQ(serverLabels, 20u);
}

// 0x645e10: un clic en un grupo lo elige y muestra sus servidores; uno en un servidor con carga
// de menos de 100 cierra la lista y lo elige, con su código.
TEST(ServerListWindowTest, AClickOnAServerChoosesItsCode)
{
    const auto texts = ListTexts();
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {401, 30}, {380, 0}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 2);

    EXPECT_EQ(ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0])), std::nullopt);
    EXPECT_EQ(window.Selected(), std::optional<std::size_t>{std::size_t{0}});

    const std::optional<ServerClick> chosen =
        ClickOn(window, MiddleX(layout.serverButtons[1]), MiddleY(layout.serverButtons[1]));
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->kind, ServerClick::Kind::Chosen);
    EXPECT_EQ(chosen->code, 401);
    EXPECT_FALSE(window.IsOpen());
}

// Hasta que se elige un grupo no hay botones de servidor (+0x3138 empieza en -1), y un clic fuera
// de todo no elige nada.
TEST(ServerListWindowTest, AClickOutsideChoosesNothing)
{
    const auto texts = ListTexts();
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 1);

    EXPECT_EQ(ClickOn(window, MiddleX(layout.serverButtons[0]), MiddleY(layout.serverButtons[0])), std::nullopt);
    EXPECT_EQ(window.Selected(), std::nullopt);
    EXPECT_EQ(ClickOn(window, 5, 470), std::nullopt);
    EXPECT_EQ(window.Selected(), std::nullopt);
    // Un botón de grupo sin grupo (el 1, con un solo grupo en la lista) tampoco elige.
    EXPECT_EQ(ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1])), std::nullopt);
    EXPECT_EQ(window.Selected(), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// Con carga de 100 a 127, el clic abre el aviso 0x63 y la lista queda; con 128 o más no hace nada.
TEST(ServerListWindowTest, AFullServerIsNotChosen)
{
    const auto texts = ListTexts();
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 100}, {401, 128}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 2);
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]));

    const std::optional<ServerClick> busy =
        ClickOn(window, MiddleX(layout.serverButtons[0]), MiddleY(layout.serverButtons[0]));
    ASSERT_TRUE(busy.has_value());
    EXPECT_EQ(busy->kind, ServerClick::Kind::Full);
    EXPECT_EQ(busy->code, 400);
    EXPECT_TRUE(window.IsOpen());

    EXPECT_EQ(ClickOn(window, MiddleX(layout.serverButtons[1]), MiddleY(layout.serverButtons[1])), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// Un grupo de un servidor tiene un solo botón, sin número: "%s%s %s".
TEST(ServerListWindowTest, AGroupOfOneHasOneButton)
{
    const auto texts = ListTexts();
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{380, 120}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 1);
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]));

    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);
    window.Draw(batch, text, Screen640);

    std::vector<std::string> labels;
    for (const auto& drawn : text.texts)
    {
        labels.push_back(drawn.text);
    }
    // El 380 es el 1 del grupo 19, en el lugar 10: sin PvP.
    EXPECT_EQ(labels, (std::vector<std::string>{"Nineteen", "Nineteen(Non-PVP) Busy"}));
}

// El lote llega con la mezcla aditiva de la arena (Q-C80): la ventana pone la normal antes de
// dibujar, y el grupo elegido sale con el cuadro de marcado.
TEST(ServerListWindowTest, ItDrawsWithTheNormalBlend)
{
    const auto texts = ListTexts();
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {380, 0}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 1);
    ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1]));

    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(Client::Render::BlendMode::Additive);
    Client::Tests::RecordingTextDrawer text(batch);
    window.Draw(batch, text, Screen640);

    ASSERT_FALSE(batch.Commands().empty());
    for (const auto& command : batch.Commands())
    {
        EXPECT_EQ(command.blend, Client::Render::BlendMode::Alpha);
    }
    // Cada botón de grupo, su imagen y su rótulo (6 vértices cada uno); después el de servidor,
    // su barra y su rótulo. El segundo de grupo, marcado: su v de arriba es la del cuadro 3.
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 7u * 6u);
    EXPECT_FLOAT_EQ(vertices[0].v, 0.5f / 128.0f);
    EXPECT_FLOAT_EQ(vertices[12].v, (3.0f * 26.0f + 0.5f) / 128.0f);
}

// Sin abrir no hace nada: ni elige ni dibuja.
TEST(ServerListWindowTest, AClosedWindowDoesNothing)
{
    ServerListWindow window(LabSizes(), nullptr);
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 0);

    EXPECT_FALSE(window.IsOpen());
    EXPECT_EQ(ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0])), std::nullopt);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);
    window.Draw(batch, text, Screen640);
    EXPECT_TRUE(batch.Vertices().empty());
}
} // namespace
