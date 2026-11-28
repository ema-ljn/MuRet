#include "FrontEnd/ServerListWindow.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Session/ServerList.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Assets/SceneTextures.h"
#include "FrontEnd/ServerListModel.h"
#include "Ui/Widget.h"
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

// Client::FrontEnd::ServerListWindow: the list window (0x6451d0, 0x645520, 0x6456c0, 0x645860, 0x645e10, in the
// step, and 0x6463b0; CL5.5). 21 group buttons in two columns of 10 and one centered below, and the chosen group's
// server buttons in the middle: 20, one per number, where the original has 16 (Q-C63). Around, the server_deco_all
// frame, and below the box with the chosen group's description. All in pixels, centered as 0x65f300 places it.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::DescriptionBoxImages;
using Client::FrontEnd::DescriptionHeight;
using Client::FrontEnd::GroupServers;
using Client::FrontEnd::LayoutServerList;
using Client::FrontEnd::LayoutServerListFrame;
using Client::FrontEnd::ServerClick;
using Client::FrontEnd::ServerListFrame;
using Client::FrontEnd::ServerListLayout;
using Client::FrontEnd::ServerListWindow;
using Client::Input::InputState;
using Client::Protocol::Session::ServerEntry;
using Client::Render::TextureSlot;
using Client::Render::UiImage;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::ClickAt;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

// The description box height (+0x3064): 10 lines of the buffer height of server_ex02 (slot 0x19, 6 × 4 in an 8 × 4
// buffer) plus 6 and 6: 52.
constexpr int Description = 52;

// The buffers of the lab's textures, for the UVs; each button's size is the binary's, not the texture's.
// server_deco_all (0xf) is 159 × 95, server_ex03 (0x17) 4 × 4 and server_ex01 (0x18) 512 × 12.
Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0xc, {128, 128}},
                                            {0xd, {256, 128}},
                                            {0xe, {256, 4}},
                                            {0xf, {256, 128}},
                                            {0x17, {4, 4}},
                                            {0x18, {512, 16}},
                                            {0x19, {8, 4}}});
}

// The description box at 640 × 480: 512 wide, 23 to the left of the window and at the bottom.
constexpr Rect Box640{.x = 64, .y = 440, .width = 512, .height = Description};

// Synthetic texts at the list's indices: the names of groups 20 and 19 and the load ones.
Client::Tables::TextTable ListTexts()
{
    return Client::Tests::MakeTextTable(
        {{1829, "Twenty"}, {552, "Nineteen"}, {560, "Full"}, {561, "Busy"}, {562, "Free"}});
}

// A step with a click at (x, y), with the edges cleared afterwards.
std::optional<ServerClick> ClickOn(ServerListWindow& window, int x, int y, PixelSize screen = Screen640)
{
    InputState input;
    ClickAt(input, x, y);
    return window.Step(input, screen);
}

int MiddleX(const Rect& rect)
{
    return rect.x + rect.width / 2;
}

int MiddleY(const Rect& rect)
{
    return rect.y + rect.height / 2;
}

Rect Area(const UiImage& image)
{
    return Rect{.x = image.x, .y = image.y, .width = image.width, .height = image.height};
}

// A command groups the consecutive quads of the same texture.
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

// What the window draws in a batch of `screen`, with the tests' ITextDrawer.
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

// 380 is number 1 of group 19, at slot 10: non-PvP, in light yellow. 400 and 401 are in group 20, at slot 4, where
// all have PvP.
constexpr std::uint32_t Gray = 0xffe2e2e2;
constexpr std::uint32_t White = 0xffffffff;
constexpr std::uint32_t Yellow = 0xff79ffff;

// 0x6451d0: the box height is 10 times the buffer height of slot 0x19 plus 12.
TEST(ServerListWindowTest, TheDescriptionIsTenLinesOfItsTexture)
{
    EXPECT_EQ(DescriptionHeight(Client::Assets::TextureBufferSize{8, 4}), Description);
    EXPECT_EQ(DescriptionHeight(Client::Assets::TextureBufferSize{8, 8}), 92);
}

// The window is 465 × (16 · 26 + 26 + 52 + 10) = 465 × 504, and 0x65f300 puts it at ((W − 465) / 2, (H − 504) / 2)
// with C division: at 640 × 480, (87, -12). The group buttons go in two columns from row
// y + 504 − 11 · 26 − 52 − 10, the 20 in the middle and below.
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

// 0x6456c0: the server ones go at x + 136, from the row of the first group one; with 10 or more, the last ends where
// row 10 does, and the earlier ones move up. The load bar, at (+16, +19).
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

// Q-C63: the original builds one button per server, but has 16; MuRet has 20 and shows them all. With 20, the first
// moves up to row 10's minus 10 rows: at 1024 × 768, 288 + 260 − 520 = 28.
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

// 0x645e10: a click on a group chooses it and shows its servers; one on a server with a load under 100 closes the
// list and chooses it, with its code.
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

// Until a group is chosen there are no server buttons (+0x3138 starts at -1), and a click outside everything chooses
// nothing.
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
    // A group button without a group (1, with a single group in the list) does not choose either.
    EXPECT_EQ(ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1])), std::nullopt);
    EXPECT_EQ(window.Selected(), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// With a load of 100 to 127, the click opens notice 0x63 and the list stays; with 128 or more it does nothing.
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

// A group of one server has a single button, without a number: "%s%s %s".
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
    // 380 is number 1 of group 19, at slot 10: non-PvP.
    EXPECT_EQ(labels, (std::vector<std::string>{"Nineteen", "Nineteen(Non-PVP) Busy"}));
}

// The batch arrives with the arena's additive blending (Q-C80): the window sets the normal one before drawing, and
// the chosen group comes out with the checked frame.
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
    // The box's five, the left piece and the mark (6 vertices each); then each group button, its image and its label,
    // the server one with its label, and the bar. The second group one, checked: its top v is that of frame 3.
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 14u * 6u);
    EXPECT_FLOAT_EQ(vertices[7u * 6u].v, 0.5f / 128.0f);
    EXPECT_FLOAT_EQ(vertices[9u * 6u].v, (3.0f * 26.0f + 0.5f) / 128.0f);
}

// 0x6463b0: the box; per line, its column's piece, the mark and the line; the buttons with their labels, and the bars
// at the end, after the last label.
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

// 0x645679..0x6456a7: the box goes at (x − (512 − 465) / 2, y + 504 − 52), 23 to the left of the window and at the
// bottom.
TEST(ServerListWindowTest, TheDescriptionBoxIsCenteredUnderTheWindow)
{
    EXPECT_EQ(LayoutServerList(Screen640, Description, 0).description, Box640);
    EXPECT_EQ(LayoutServerList(Screen1024, Description, 3).description,
              (Rect{.x = 256, .y = 584, .width = 512, .height = Description}));
}

// Each server_deco_all piece puts its point on a corner of a group button. At 640 × 480: the left one, (8, 19) at
// (87, 144), top left of 0; the right one, (60, 19) at (552, 144), top right of 10; the mark of 3, (1, 2) at
// (195, 222), and that of 12, (23, 2) at (444, 196). Those that do not start at (0, 0) of the texture carry their
// frame (0x647260).
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

// 0x645c70: the right piece, with more than 10 group buttons; 0x645ca0: the mark, with the chosen one from 0 to 19,
// and none with the 20 or with nothing chosen.
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

// 0x66a1c0, 0x66a3d0 and 0x66a4e0, at 640 × 480: the server_ex03 fill at (67, 443), 506 × 46 and repeated; the
// server_ex01 edges, the bottom one with frame (0, 6); and the server_ex02 sides, 3 × 40, the left one repeated and
// the right one from (3, 0), with the UVs of the height 0x6451d0 builds it with (4), stretched.
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

// The right side's 4 is that of 0x6451d0's table (3 × 4 from (3, 0)), not the buffer's: with an 8 × 8 one the box is
// 92 and the sides 80, and the right UVs stay on 4 rows.
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

// 0x645860: on choosing group k, text 1717 + k in two lines of 83 bytes; 0x6463b0 draws them at (+11, +12) and
// (+11, +32) of the box, converted to 640 × 480, in white and with no background. On opening again, with nothing
// chosen, there is no description.
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

    // At 1024 × 768 the box goes at (256, 584), and the lines, from (267, 596) converted to 640 × 480 with the ratios
    // in float (1.6 and 1.6000001): 166, 372 and, the one at 616, 384.
    const DrawnList at1024 = DrawList(window, Screen1024);
    ASSERT_EQ(at1024.texts.size(), 4u);
    EXPECT_EQ(at1024.texts[0].x, 166);
    EXPECT_EQ(at1024.texts[0].y, 372);
    EXPECT_EQ(at1024.texts[1].y, 384);

    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}}));
    EXPECT_TRUE(window.Description().empty());
    EXPECT_EQ(DrawList(window).texts.size(), 1u);
}

// In the original button k is slot 4 + k, and 1717 + k is that slot's description. MuRet shows the groups of every
// slot (Q-C62): the description is that of the group's slot, 1717 + slot − 4, and not the button's. 380 is in group
// 19, at slot 10: 1723, only on button 0 or on 1 after group 20.
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

// Q-C83: with group 20 chosen (its name is text 1829), texts 565 to 567 go at (90, 104), (90, 119) and (90, 134) of
// 640 × 480 in the last label's color: gray, a server's without the mouse over it, or white, with the mouse over it.
// With another group they do not.
TEST(ServerListWindowTest, TheGroupTwentyTextsTakeTheLastLabelColor)
{
    auto texts = Client::Tests::MakeTextTable(
        {{1829, "Twenty"}, {552, "Nineteen"}, {562, "Free"}, {565, "One"}, {566, "Two"}, {567, "Three"}});
    ServerListWindow window(LabSizes(), &texts);
    window.Open(GroupServers(std::vector<ServerEntry>{{400, 10}, {401, 20}, {380, 0}}));
    const ServerListLayout layout = LayoutServerList(Screen640, Description, 2);
    ClickOn(window, MiddleX(layout.groupButtons[0]), MiddleY(layout.groupButtons[0]));

    // Group 20's label comes out white, with the mouse over it, and 401's, the last, gray.
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

    // With the mouse over the last one, its label and the three texts go white.
    InputState hover;
    Client::Tests::MoveMouse(hover, MiddleX(layout.serverButtons[1]), MiddleY(layout.serverButtons[1]));
    window.Step(hover, Screen640);
    const DrawnList white = DrawList(window);
    ASSERT_EQ(white.texts.size(), 7u);
    EXPECT_EQ(white.texts[3].style.colors.text, White);
    EXPECT_EQ(white.texts[6].style.colors.text, White);

    // Group 19 does not have them.
    ClickOn(window, MiddleX(layout.groupButtons[1]), MiddleY(layout.groupButtons[1]));
    const DrawnList nineteen = DrawList(window);
    for (const auto& drawn : nineteen.texts)
    {
        EXPECT_NE(drawn.text, "One");
    }
    EXPECT_EQ(nineteen.texts.size(), 3u);
}

// 0x6463b0's strcmp compares texts: a group named like 20 (19, with text 552 equal to 1829) also has the three
// texts, on button 1. Its server, 380, is non-PvP: the last label is light yellow, and so are the three texts
// (Q-C83).
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
