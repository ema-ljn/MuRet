#include "UI/ServerListWindow.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Protocol/Connection/ServerList.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/SceneTextures.h"
#include "UI/ServerListModel.h"
#include "UI/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Client::UI::ServerListWindow: la ventana de la lista (0x6451d0, 0x645520, 0x6456c0, 0x645860,
// 0x645e10, en el paso, y 0x6463b0; CL5.5). 21 botones de grupo en dos columnas de 10 y uno abajo
// al medio, y los de servidor del grupo elegido en el medio: 20, uno por número, donde el original
// tiene 16 (Q-C63). Alrededor, el marco de server_deco_all, y abajo la caja con la descripción del
// grupo elegido. Todo en píxeles, centrada como la ubica 0x65f300.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Protocol::Connection::ServerEntry;
using Client::Render::TextureSlot;
using Client::Render::UiImage;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::ClickAt;
using Client::UI::DescriptionBoxImages;
using Client::UI::DescriptionHeight;
using Client::UI::GroupServers;
using Client::UI::LayoutServerList;
using Client::UI::LayoutServerListFrame;
using Client::UI::Rect;
using Client::UI::ServerClick;
using Client::UI::ServerListFrame;
using Client::UI::ServerListLayout;
using Client::UI::ServerListWindow;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

// El alto de la caja de la descripción (+0x3064): 10 líneas del alto del buffer de server_ex02
// (slot 0x19, 6 × 4 en un buffer de 8 × 4) más 6 y 6: 52.
constexpr int Description = 52;

// Los buffers de las texturas del laboratorio, para las UV; el tamaño de cada botón es del
// binario, no de la textura. server_deco_all (0xf) mide 159 × 95, server_ex03 (0x17) 4 × 4 y
// server_ex01 (0x18) 512 × 12.
Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0xc, {128, 128}},
                                            {0xd, {256, 128}},
                                            {0xe, {256, 4}},
                                            {0xf, {256, 128}},
                                            {0x17, {4, 4}},
                                            {0x18, {512, 16}},
                                            {0x19, {8, 4}}});
}

// La caja de la descripción en 640 × 480: 512 de ancho, 23 a la izquierda de la ventana y al pie.
constexpr Rect Box640{.x = 64, .y = 440, .width = 512, .height = Description};

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

// Dónde quedó una imagen, como un rectángulo.
Rect Area(const UiImage& image)
{
    return Rect{.x = image.x, .y = image.y, .width = image.width, .height = image.height};
}

// La textura de cada quad del lote, en orden: un comando junta los quads seguidos de una misma.
std::vector<std::uint16_t> QuadSlots(const Client::Render::SpriteBatch& batch)
{
    std::vector<std::uint16_t> slots;
    for (const auto& command : batch.Commands())
    {
        const std::uint16_t slot = command.texture ? command.texture->value : std::uint16_t{0xffff};
        slots.insert(slots.end(), command.vertexCount / 6, slot);
    }
    return slots;
}

// Lo que dibuja la ventana en un lote de `screen`, con el ITextDrawer de los tests.
struct DrawnList
{
    Client::Render::SpriteBatch batch;
    std::vector<Client::Tests::RecordedText> texts;
};

DrawnList DrawList(ServerListWindow& window, PixelSize screen = Screen640)
{
    DrawnList drawn;
    drawn.batch.Begin(screen);
    Client::Tests::RecordingTextDrawer text(drawn.batch);
    window.Draw(drawn.batch, text, screen);
    drawn.texts = text.texts;
    return drawn;
}

// El 380 es el 1 del grupo 19, en el lugar 10: sin PvP, en amarillo claro. El 400 y el 401 son
// del grupo 20, en el lugar 4, donde todos tienen PvP.
constexpr std::uint32_t Gray = 0xffe2e2e2;
constexpr std::uint32_t White = 0xffffffff;
constexpr std::uint32_t Yellow = 0xff79ffff;

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
    // Las cinco de la caja, la pieza de la izquierda y la marca (6 vértices cada una); después
    // cada botón de grupo, su imagen y su rótulo, el de servidor con su rótulo, y la barra. El
    // segundo de grupo, marcado: su v de arriba es la del cuadro 3.
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 14u * 6u);
    EXPECT_FLOAT_EQ(vertices[7u * 6u].v, 0.5f / 128.0f);
    EXPECT_FLOAT_EQ(vertices[9u * 6u].v, (3.0f * 26.0f + 0.5f) / 128.0f);
}

// 0x6463b0: la caja; por cada renglón, la pieza de su columna, la marca y el renglón; los botones
// con sus rótulos, y las barras al final, después del último rótulo.
TEST(ServerListWindowTest, ItDrawsInTheOrderOfTheOriginal)
{
    auto texts =
        Client::Tests::MakeTextTable({{1829, "Twenty"}, {552, "Nineteen"}, {562, "Free"}, {1723, "The nineteen"}});
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {380, 0}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 1);
    ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1]));

    const DrawnList drawn = DrawList(window);
    constexpr std::uint16_t T = Client::Tests::TextMarkerSlot.value;
    EXPECT_EQ(QuadSlots(drawn.batch),
              (std::vector<std::uint16_t>{0x17, 0x18, 0x18, 0x19, 0x19, 0xf, 0xf, T, 0xc, T, 0xc, T, 0xd, T, 0xe}));
    ASSERT_EQ(drawn.texts.size(), 4u);
    EXPECT_EQ(drawn.texts[0].text, "The nineteen");
    EXPECT_EQ(drawn.texts[3].text, "Nineteen(Non-PVP) Free");
}

// 0x645679..0x6456a7: la caja va en (x − (512 − 465) / 2, y + 504 − 52), 23 a la izquierda de la
// ventana y al pie.
TEST(ServerListWindowTest, TheDescriptionBoxIsCenteredUnderTheWindow)
{
    EXPECT_EQ(LayoutServerList(Screen640, Description, 0).description, Box640);
    EXPECT_EQ(LayoutServerList(Screen1024, Description, 3).description,
              (Rect{.x = 256, .y = 584, .width = 512, .height = Description}));
}

// Cada pieza de server_deco_all pone su punto en una esquina de un botón de grupo. En 640 × 480:
// la de la izquierda, (8, 19) en (87, 144), arriba a la izquierda del 0; la de la derecha, (60, 19)
// en (552, 144), arriba a la derecha del 10; la marca del 3, (1, 2) en (195, 222), y la del 12,
// (23, 2) en (444, 196). Las que no empiezan en (0, 0) de la textura llevan su cuadro (0x647260).
TEST(ServerListWindowTest, TheFramePiecesPutTheirPointOnTheButtons)
{
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 0);
    const ServerListFrame left = LayoutServerListFrame(layout, 21, std::size_t{3}, LabSizes());
    ASSERT_TRUE(left.corners[0] && left.corners[1] && left.markers[0]);
    EXPECT_EQ(left.corners[0]->slot, TextureSlot{0xf});
    EXPECT_EQ(Area(*left.corners[0]), (Rect{.x = 79, .y = 125, .width = 68, .height = 95}));
    EXPECT_EQ(left.corners[0]->frameTop, std::nullopt);
    EXPECT_EQ(Area(*left.corners[1]), (Rect{.x = 492, .y = 125, .width = 68, .height = 95}));
    EXPECT_EQ(left.corners[1]->frameLeft, 68);
    EXPECT_EQ(left.corners[1]->frameTop, 0);
    EXPECT_EQ(Area(*left.markers[0]), (Rect{.x = 194, .y = 220, .width = 23, .height = 29}));
    EXPECT_EQ(left.markers[0]->frameLeft, 136);
    EXPECT_EQ(left.markers[0]->frameTop, 0);
    EXPECT_FALSE(left.markers[1]);

    const ServerListFrame right = LayoutServerListFrame(layout, 21, std::size_t{12}, LabSizes());
    ASSERT_TRUE(right.markers[1]);
    EXPECT_FALSE(right.markers[0]);
    EXPECT_EQ(Area(*right.markers[1]), (Rect{.x = 421, .y = 194, .width = 23, .height = 29}));
    EXPECT_EQ(right.markers[1]->frameLeft, 136);
    EXPECT_EQ(right.markers[1]->frameTop, 30);
}

// 0x645c70: la pieza de la derecha, con más de 10 botones de grupo; 0x645ca0: la marca, con el
// elegido de 0 a 19, y ninguna con el 20 o sin elegido.
TEST(ServerListWindowTest, SomePiecesDependOnTheGroups)
{
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 0);
    const ServerListFrame ten = LayoutServerListFrame(layout, 10, std::size_t{9}, LabSizes());
    EXPECT_TRUE(ten.corners[0]);
    EXPECT_FALSE(ten.corners[1]);
    EXPECT_TRUE(ten.markers[0]);

    const ServerListFrame eleven = LayoutServerListFrame(layout, 11, std::nullopt, LabSizes());
    EXPECT_TRUE(eleven.corners[1]);
    EXPECT_FALSE(eleven.markers[0] || eleven.markers[1]);

    EXPECT_TRUE(LayoutServerListFrame(layout, 21, std::size_t{19}, LabSizes()).markers[1]);
    const ServerListFrame twenty = LayoutServerListFrame(layout, 21, std::size_t{20}, LabSizes());
    EXPECT_FALSE(twenty.markers[0] || twenty.markers[1]);
}

// 0x66a1c0, 0x66a3d0 y 0x66a4e0, en 640 × 480: el fondo de server_ex03 en (67, 443), de 506 × 46
// y repetido; los bordes de server_ex01, el de abajo con el cuadro (0, 6); y los lados de
// server_ex02, de 3 × 40, el de la izquierda repetido y el de la derecha desde (3, 0), con las UV
// del alto con que lo arma 0x6451d0 (4), estirado.
TEST(ServerListWindowTest, TheDescriptionBoxHasFiveImages)
{
    const std::array<UiImage, 5> box = DescriptionBoxImages(Box640, LabSizes());
    const auto& [fill, top, bottom, left, right] = box;
    EXPECT_EQ(fill.slot, TextureSlot{0x17});
    EXPECT_EQ(Area(fill), (Rect{.x = 67, .y = 443, .width = 506, .height = 46}));
    EXPECT_TRUE(fill.resizedWidth);
    EXPECT_TRUE(fill.resizedHeight);

    EXPECT_EQ(top.slot, TextureSlot{0x18});
    EXPECT_EQ(Area(top), (Rect{.x = 64, .y = 440, .width = 512, .height = 6}));
    EXPECT_EQ(top.frameTop, std::nullopt);
    EXPECT_EQ(bottom.slot, TextureSlot{0x18});
    EXPECT_EQ(Area(bottom), (Rect{.x = 64, .y = 486, .width = 512, .height = 6}));
    EXPECT_EQ(bottom.frameTop, 6);
    EXPECT_EQ(bottom.frameLeft, 0);

    EXPECT_EQ(left.slot, TextureSlot{0x19});
    EXPECT_EQ(Area(left), (Rect{.x = 64, .y = 446, .width = 3, .height = 40}));
    EXPECT_FALSE(left.resizedWidth);
    EXPECT_TRUE(left.resizedHeight);
    EXPECT_EQ(right.slot, TextureSlot{0x19});
    EXPECT_EQ(Area(right), (Rect{.x = 573, .y = 446, .width = 3, .height = 40}));
    EXPECT_EQ(right.frameLeft, 3);
    EXPECT_EQ(right.frameTop, 0);
    EXPECT_EQ(right.uvHeight, 4);
    EXPECT_FALSE(right.resizedHeight);
}

// El 4 de la derecha es el de la tabla de 0x6451d0 (3 × 4 desde (3, 0)), no el del buffer: con
// uno de 8 × 8 la caja mide 92 y los lados 80, y las UV de la derecha siguen en 4 filas.
TEST(ServerListWindowTest, TheRightSideKeepsTheHeightOfItsTable)
{
    const auto sizes = Client::Tests::FakeTextureSizes({{0x17, {4, 4}}, {0x18, {512, 16}}, {0x19, {8, 8}}});
    const std::array<UiImage, 5> box = DescriptionBoxImages(Rect{.x = 64, .y = 420, .width = 512, .height = 92}, sizes);
    const UiImage& left = box[3];
    const UiImage& right = box[4];
    EXPECT_EQ(Area(left), (Rect{.x = 64, .y = 426, .width = 3, .height = 80}));
    EXPECT_EQ(Area(right), (Rect{.x = 573, .y = 426, .width = 3, .height = 80}));
    EXPECT_EQ(right.textureHeight, 8.0f);
    EXPECT_EQ(right.uvHeight, 4);
}

// 0x645860: al elegir el grupo k, el texto 1717 + k en dos renglones de 83 bytes; 0x6463b0 los
// dibuja en (+11, +12) y (+11, +32) de la caja, pasados a 640 × 480, en blanco y sin fondo. Al
// abrir otra vez, sin elegido, no hay descripción.
TEST(ServerListWindowTest, ItDrawsTheDescriptionOfTheChosenGroup)
{
    const std::string first = std::string(76, 'a') + " ";
    const std::string second(20, 'b');
    auto texts = Client::Tests::MakeTextTable({{1829, "Twenty"}, {562, "Free"}, {1717, first + second}});
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}}));
    EXPECT_TRUE(window.Description().empty());
    const ServerListLayout small = LayoutServerList(Screen640, Description, 1);
    ClickOn(window, MiddleX(small.groupButtons[0]), MiddleY(small.groupButtons[0]));
    EXPECT_EQ(window.Description(), (std::vector<std::string>{first, second}));

    const TextStyle style{FontKind::OtherFace12, TextColors{White, 0}};
    const DrawnList at640 = DrawList(window);
    ASSERT_EQ(at640.texts.size(), 4u);
    EXPECT_EQ(at640.texts[0].text, first);
    EXPECT_EQ(at640.texts[0].x, 75);
    EXPECT_EQ(at640.texts[0].y, 452);
    EXPECT_EQ(at640.texts[0].style, style);
    EXPECT_EQ(at640.texts[1].text, second);
    EXPECT_EQ(at640.texts[1].x, 75);
    EXPECT_EQ(at640.texts[1].y, 472);
    EXPECT_EQ(at640.texts[1].style, style);

    // En 1024 × 768 la caja va en (256, 584), y los renglones, desde (267, 596) pasados a
    // 640 × 480 con las proporciones en float (1,6 y 1,6000001): 166, 372 y, el de 616, 384.
    const DrawnList at1024 = DrawList(window, Screen1024);
    ASSERT_EQ(at1024.texts.size(), 4u);
    EXPECT_EQ(at1024.texts[0].x, 166);
    EXPECT_EQ(at1024.texts[0].y, 372);
    EXPECT_EQ(at1024.texts[1].y, 384);

    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}}));
    EXPECT_TRUE(window.Description().empty());
    EXPECT_EQ(DrawList(window).texts.size(), 1u);
}

// En el original el botón k es el lugar 4 + k, y 1717 + k es la descripción de ese lugar. MuRet
// muestra los grupos de todos los lugares (Q-C62): la descripción es la del lugar del grupo, 1717
// + lugar − 4, y no la del botón. El 380 es del grupo 19, en el lugar 10: la 1723, solo en el
// botón 0 o en el 1 después del grupo 20.
TEST(ServerListWindowTest, TheDescriptionIsTheOneOfTheSlot)
{
    auto texts = Client::Tests::MakeTextTable({{1829, "Twenty"},
                                               {552, "Nineteen"},
                                               {562, "Free"},
                                               {1717, "Slot four"},
                                               {1718, "Slot five"},
                                               {1723, "Slot ten"}});
    ServerListWindow window(LabSizes(), &texts);
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 1);

    window.Open(GroupServers(std::vector<ServerEntry>{{380, 0}}));
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]));
    EXPECT_EQ(window.Description(), (std::vector<std::string>{"Slot ten"}));

    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {380, 0}}));
    ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1]));
    EXPECT_EQ(window.Description(), (std::vector<std::string>{"Slot ten"}));
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]));
    EXPECT_EQ(window.Description(), (std::vector<std::string>{"Slot four"}));
}

// Q-C83: con el grupo 20 elegido (su nombre es el texto 1829), los textos 565 a 567 van en (90,
// 104), (90, 119) y (90, 134) de 640 × 480 con el color del último rótulo: gris, el de un servidor
// sin el mouse encima, o blanco, con el mouse encima. Con otro grupo no van.
TEST(ServerListWindowTest, TheGroupTwentyTextsTakeTheLastLabelColor)
{
    auto texts = Client::Tests::MakeTextTable(
        {{1829, "Twenty"}, {552, "Nineteen"}, {562, "Free"}, {565, "One"}, {566, "Two"}, {567, "Three"}});
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {401, 20}, {380, 0}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 2);
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]));

    // El rótulo del grupo 20 sale en blanco, con el mouse encima, y el del 401, el último, en gris.
    const DrawnList gray = DrawList(window);
    ASSERT_EQ(gray.texts.size(), 7u);
    EXPECT_EQ(gray.texts[0].style.colors.text, White);
    EXPECT_EQ(gray.texts[3].text, "Twenty-2 Free");
    EXPECT_EQ(gray.texts[3].style.colors.text, Gray);
    const std::array<std::string, 3> lines{"One", "Two", "Three"};
    for (std::size_t k = 0; k < lines.size(); ++k)
    {
        const Client::Tests::RecordedText& drawn = gray.texts[4 + k];
        EXPECT_EQ(drawn.text, lines[k]);
        EXPECT_EQ(drawn.x, 90);
        EXPECT_EQ(drawn.y, 104 + 15 * static_cast<int>(k));
        EXPECT_EQ(drawn.style, (TextStyle{FontKind::OtherFace12, TextColors{Gray, 0}}));
    }

    // Con el mouse sobre el último, su rótulo y los tres textos van en blanco.
    InputState hover;
    Client::Tests::MoveMouse(hover, MiddleX(layout.serverButtons[1]), MiddleY(layout.serverButtons[1]));
    window.Step(hover, Screen640);
    const DrawnList white = DrawList(window);
    ASSERT_EQ(white.texts.size(), 7u);
    EXPECT_EQ(white.texts[3].style.colors.text, White);
    EXPECT_EQ(white.texts[6].style.colors.text, White);

    // El grupo 19 no los tiene.
    ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1]));
    const DrawnList nineteen = DrawList(window);
    for (const auto& drawn : nineteen.texts)
    {
        EXPECT_NE(drawn.text, "One");
    }
    EXPECT_EQ(nineteen.texts.size(), 3u);
}

// El strcmp de 0x6463b0 compara textos: un grupo que se llama como el 20 (el 19, con el texto 552
// igual al 1829) también tiene los tres textos, en el botón 1. Su servidor, el 380, es sin PvP: el
// último rótulo es amarillo claro, y los tres textos también (Q-C83).
TEST(ServerListWindowTest, AGroupNamedLikeTheTwentyShowsTheTextsWithItsColor)
{
    auto texts = Client::Tests::MakeTextTable(
        {{1829, "Twenty"}, {552, "Twenty"}, {562, "Free"}, {565, "One"}, {566, "Two"}, {567, "Three"}});
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {380, 0}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 1);
    ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1]));

    const DrawnList drawn = DrawList(window);
    ASSERT_EQ(drawn.texts.size(), 6u);
    EXPECT_EQ(drawn.texts[2].text, "Twenty(Non-PVP) Free");
    EXPECT_EQ(drawn.texts[2].style.colors.text, Yellow);
    EXPECT_EQ(drawn.texts[3].text, "One");
    for (std::size_t k = 3; k < drawn.texts.size(); ++k)
    {
        EXPECT_EQ(drawn.texts[k].style.colors.text, Yellow);
    }
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
