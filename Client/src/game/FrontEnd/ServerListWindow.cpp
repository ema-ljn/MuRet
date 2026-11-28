#include "FrontEnd/ServerListWindow.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Session/ServerList.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Assets/SceneTextures.h"
#include "Ui/Button.h"
#include "Ui/GameText.h"
#include "Ui/NewUi.h"
#include "FrontEnd/ServerListModel.h"
#include "Ui/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::FrontEnd
{
namespace
{
// 0x6451d0: the group buttons, 108 × 26 with cha_bt (slot 0xc) and four frames, the 3 checked; the server ones,
// 193 × 26 with server_b2_all (0xd) and three; the load bar, 160 × 4 with server_b2_loding (0xe).
constexpr Render::TextureSlot GroupButtonSlot{0xc};
constexpr Render::TextureSlot ServerButtonSlot{0xd};
constexpr Render::TextureSlot LoadBarSlot{0xe};
constexpr Render::TextureSlot DescriptionLineSlot{0x19};
// 0x6451d0: the frame, from server_deco_all; the box, from server_ex03, server_ex01 and server_ex02.
constexpr Render::TextureSlot FrameSlot{0xf};
constexpr Render::TextureSlot BoxFillSlot{0x17};
constexpr Render::TextureSlot BoxEdgeSlot{0x18};
constexpr int GroupButtonWidth = 108;
constexpr int ButtonHeight = 26;
constexpr int CheckedFrame = 3;
constexpr int ServerButtonWidth = 193;
constexpr int LoadBarWidth = 160;
constexpr int LoadBarHeight = 4;

// 0x6451d0: the height is that of the original's 16 server buttons plus one group button, the box and 10; the box,
// 10 lines plus 6 and 6.
constexpr int OriginalServerButtons = 16;
constexpr int BottomMargin = 10;
constexpr int BoxLines = 10;
constexpr int DescriptionPadding = 6 + 6;
// 0x645520: 10 rows of groups, the 20's row 5 from the box.
constexpr int GroupRows = 10;
constexpr int LastGroupGap = 5;
// 0x6456c0: the server ones, 28 from the edge of the group column; the bar at (+16, +19).
constexpr int ServerColumnGap = 28;
constexpr int LoadBarLeft = 16;
constexpr int LoadBarTop = 19;

// 0x645e10: under 100 it is chosen; under 128, the notice.
constexpr std::uint8_t BusyLoad = 100;
constexpr std::uint8_t FullLoad = 128;
// 0x640580(load, 100): the bar is 160 · load / 100, with the load up to 100.
constexpr unsigned MaxBarLoad = 100;

// The label colors (0x645860), 0xAABBGGRR: light gray, white with the mouse over it; the server without PvP, light
// yellow in all.
constexpr std::uint32_t LabelGray = 0xffe2e2e2;
constexpr std::uint32_t LabelWhite = 0xffffffff;
constexpr std::uint32_t NonPvpYellow = 0xff79ffff;
constexpr Ui::ButtonColors GroupColors{.normal = LabelGray,
                                       .pressed = LabelGray,
                                       .hover = LabelWhite,
                                       .checkedNormal = LabelGray,
                                       .checkedPressed = LabelGray,
                                       .checkedHover = LabelWhite};
constexpr Ui::ButtonColors ServerColors{.normal = LabelGray, .pressed = LabelGray, .hover = LabelWhite};
constexpr Ui::ButtonColors NonPvpColors{.normal = NonPvpYellow, .pressed = NonPvpYellow, .hover = NonPvpYellow};

// A piece of server_deco_all (0x647260): its place in the texture, its size and its point.
struct FramePiece
{
    int u = 0;
    int v = 0;
    int width = 0;
    int height = 0;
    Ui::Point point;
};
// 0x6451d0, de 0x6452a3 a 0x645378.
constexpr FramePiece LeftCorner{.u = 0, .v = 0, .width = 68, .height = 95, .point = {8, 19}};
constexpr FramePiece RightCorner{.u = 68, .v = 0, .width = 68, .height = 95, .point = {60, 19}};
constexpr FramePiece LeftMarker{.u = 136, .v = 0, .width = 23, .height = 29, .point = {1, 2}};
constexpr FramePiece RightMarker{.u = 136, .v = 30, .width = 23, .height = 29, .point = {23, 2}};
// 0x645c70: the right one, with more than 10 group buttons; 0x645ca0: the mark, on the chosen one's side, and none
// with the 20.
constexpr std::size_t GroupsPerColumn = 10;
constexpr std::size_t MarkedGroups = 2 * GroupsPerColumn;

// 0x66a1c0 and 0x66a3d0: the top and bottom edges are 6, the sides 3, and the fill starts at (3, 3), 6 narrower and
// shorter than the box.
constexpr int BoxEdgeHeight = 6;
constexpr int BoxSideWidth = 3;
constexpr int BoxFillInset = 3;
// 0x6451d0: the right side is the 3 × 4 frame that starts at column 3 of server_ex02.
constexpr int RightSideColumn = 3;
constexpr int RightSideRows = 4;

// 0x6463b0: the lines go at (+11, +12) from the box, each 20 lower, with the font DAT_05763560, in white and with no
// background; the group 20 texts, at (90, 104), (90, 119) and (90, 134) of 640 × 480.
constexpr int DescriptionLeft = 11;
constexpr int DescriptionTop = 12;
constexpr int DescriptionLineStep = 20;
constexpr Render::Text::FontKind ListFont = Render::Text::FontKind::OtherFace12;
constexpr std::uint32_t DescriptionColor = 0xffffffff;
constexpr int GroupTwentyLeft = 90;
constexpr int GroupTwentyTop = 104;
constexpr int GroupTwentyStep = 15;
// The group whose name, text 1829, 0x6463b0 compares with the chosen one's.
constexpr std::uint16_t GroupTwenty = 20;

// The piece with its point at `at`: 0x6472e0(x, y, 3) subtracts the point from +0x58 and +0x5c. Those that start at
// (0, 0) of the texture have no frames (0x647260 gives 0x647050 a null list).
Render::UiImage PlacePiece(const FramePiece& piece, Ui::Point at, const Ui::TextureSizes& sizes)
{
    Render::UiImage image = Ui::PixelImage(
        FrameSlot,
        Ui::Rect{.x = at.x - piece.point.x, .y = at.y - piece.point.y, .width = piece.width, .height = piece.height},
        sizes(FrameSlot));
    if (piece.u != 0 || piece.v != 0)
    {
        image.frameLeft = piece.u;
        image.frameTop = piece.v;
    }
    return image;
}

// 0x645860 splits text 1717 + chosen, and the original's button k is slot 4 + k. MuRet shows the groups of every
// slot (Q-C62), and button k can be another slot: the description is that of the group's slot.
std::size_t DescriptionText(int slot)
{
    return FirstDescriptionText + static_cast<std::size_t>(slot - FirstButtonSlot);
}

int BarWidth(std::uint8_t load)
{
    const unsigned clamped = std::min<unsigned>(load, MaxBarLoad);
    return static_cast<int>(static_cast<unsigned>(LoadBarWidth) * clamped / MaxBarLoad);
}
} // namespace

int DescriptionHeight(Assets::TextureBufferSize lineTexture)
{
    return BoxLines * lineTexture.height + DescriptionPadding;
}

ServerListLayout LayoutServerList(Core::PixelSize screen, int descriptionHeight, std::size_t serverCount)
{
    ServerListLayout layout;
    const int height = OriginalServerButtons * ButtonHeight + ButtonHeight + descriptionHeight + BottomMargin;
    const int x = (screen.width - ServerListWidth) / 2;
    const int y = (screen.height - height) / 2;
    layout.window = Ui::Rect{.x = x, .y = y, .width = ServerListWidth, .height = height};

    const int firstRow = y + height - (GroupRows + 1) * ButtonHeight - descriptionHeight - BottomMargin;
    const int rightColumn = x + ServerListWidth - GroupButtonWidth;
    constexpr auto Rows = static_cast<std::size_t>(GroupRows);
    for (std::size_t row = 0; row < Rows; ++row)
    {
        const int rowY = firstRow + static_cast<int>(row) * ButtonHeight;
        layout.groupButtons[row] = Ui::Rect{.x = x, .y = rowY, .width = GroupButtonWidth, .height = ButtonHeight};
        layout.groupButtons[row + Rows] =
            Ui::Rect{.x = rightColumn, .y = rowY, .width = GroupButtonWidth, .height = ButtonHeight};
    }
    layout.groupButtons[2 * Rows] = Ui::Rect{.x = x + (ServerListWidth - GroupButtonWidth) / 2,
                                             .y = y + height - descriptionHeight - ButtonHeight - LastGroupGap,
                                             .width = GroupButtonWidth,
                                             .height = ButtonHeight};

    // 0x6456c0: with 10 or more servers (count · 26 >= 260), the first moves up.
    const int count = static_cast<int>(std::min(serverCount, ServersPerGroup));
    int serverY = firstRow;
    if (count * ButtonHeight >= GroupRows * ButtonHeight)
    {
        serverY = firstRow - count * ButtonHeight + GroupRows * ButtonHeight;
    }
    const int serverX = x + GroupButtonWidth + ServerColumnGap;
    for (std::size_t i = 0; i < ServersPerGroup; ++i)
    {
        const int buttonY = serverY + static_cast<int>(i) * ButtonHeight;
        layout.serverButtons[i] =
            Ui::Rect{.x = serverX, .y = buttonY, .width = ServerButtonWidth, .height = ButtonHeight};
        layout.loadBars[i] = Ui::Rect{
            .x = serverX + LoadBarLeft, .y = buttonY + LoadBarTop, .width = LoadBarWidth, .height = LoadBarHeight};
    }

    // 0x645679..0x6456a7: (x − (512 − 465) / 2, y + height − the box), with C division.
    layout.description = Ui::Rect{.x = x - (DescriptionWidth - ServerListWidth) / 2,
                                  .y = y + height - descriptionHeight,
                                  .width = DescriptionWidth,
                                  .height = descriptionHeight};
    return layout;
}

ServerListFrame LayoutServerListFrame(const ServerListLayout& layout, std::size_t groupButtons,
                                      std::optional<std::size_t> selected, const Ui::TextureSizes& sizes)
{
    ServerListFrame frame;
    const Ui::Rect& first = layout.groupButtons[0];
    frame.corners[0] = PlacePiece(LeftCorner, Ui::Point{first.x, first.y}, sizes);
    if (groupButtons > GroupsPerColumn)
    {
        const Ui::Rect& top = layout.groupButtons[GroupsPerColumn];
        frame.corners[1] = PlacePiece(RightCorner, Ui::Point{top.x + top.width, top.y}, sizes);
    }
    if (selected && *selected < MarkedGroups)
    {
        const Ui::Rect& button = layout.groupButtons[*selected];
        if (*selected < GroupsPerColumn)
        {
            frame.markers[0] = PlacePiece(LeftMarker, Ui::Point{button.x + button.width, button.y}, sizes);
        }
        else
        {
            frame.markers[1] = PlacePiece(RightMarker, Ui::Point{button.x, button.y}, sizes);
        }
    }
    return frame;
}

std::array<Render::UiImage, 5> DescriptionBoxImages(Ui::Rect box, const Ui::TextureSizes& sizes)
{
    // 0x66a4e0: the sides span the 10 lines, the box height minus both edges.
    const int sideHeight = box.height - 2 * BoxEdgeHeight;

    // The fill, 506 × (height − 6), and the left side follow 0x647370 (+0x98 at 1) and repeat their texture.
    Render::UiImage fill = Ui::PixelImage(BoxFillSlot,
                                          Ui::Rect{.x = box.x + BoxFillInset,
                                                   .y = box.y + BoxFillInset,
                                                   .width = box.width - 2 * BoxFillInset,
                                                   .height = box.height - 2 * BoxFillInset},
                                          sizes(BoxFillSlot));
    fill.resizedWidth = true;
    fill.resizedHeight = true;

    const Render::UiImage top = Ui::PixelImage(
        BoxEdgeSlot, Ui::Rect{.x = box.x, .y = box.y, .width = box.width, .height = BoxEdgeHeight}, sizes(BoxEdgeSlot));
    Render::UiImage bottom = Ui::PixelImage(
        BoxEdgeSlot,
        Ui::Rect{.x = box.x, .y = box.y + box.height - BoxEdgeHeight, .width = box.width, .height = BoxEdgeHeight},
        sizes(BoxEdgeSlot));
    bottom.frameTop = BoxEdgeHeight;

    Render::UiImage left =
        Ui::PixelImage(DescriptionLineSlot,
                       Ui::Rect{.x = box.x, .y = box.y + BoxEdgeHeight, .width = BoxSideWidth, .height = sideHeight},
                       sizes(DescriptionLineSlot));
    left.resizedHeight = true;

    // The right one starts at (3, 0) of server_ex02: it has frames, and 0x647050 sets its +0x98 to 0. Its UVs stay on
    // the 4 rows 0x6451d0 builds it with, not those of the buffer, and it stretches.
    Render::UiImage right = Ui::PixelImage(DescriptionLineSlot,
                                           Ui::Rect{.x = box.x + box.width - BoxSideWidth,
                                                    .y = box.y + BoxEdgeHeight,
                                                    .width = BoxSideWidth,
                                                    .height = sideHeight},
                                           sizes(DescriptionLineSlot));
    right.frameTop = 0;
    right.frameLeft = RightSideColumn;
    right.uvHeight = RightSideRows;
    return {fill, top, bottom, left, right};
}

ServerListWindow::ServerListWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts)
{
}

void ServerListWindow::Open(ServerGroups groups)
{
    m_groups = std::move(groups);
    m_selected.reset();
    m_open = true;
    m_groupButtons.clear();
    m_serverButtons.clear();
    m_description.clear();
    const std::size_t shown = std::min(m_groups.groups.size(), GroupButtonCount);
    for (std::size_t i = 0; i < shown; ++i)
    {
        Ui::Button button(GroupButtonSlot, GroupButtonWidth, ButtonHeight, CheckedFrame);
        if (const auto name = Ui::TextAt(m_texts, GroupNameText(m_groups.groups[i].group)))
        {
            button.SetLabel(std::string(*name), GroupColors);
        }
        m_groupButtons.push_back(std::move(button));
    }
}

void ServerListWindow::Close()
{
    m_open = false;
}

bool ServerListWindow::IsOpen() const
{
    return m_open;
}

std::optional<std::size_t> ServerListWindow::Selected() const
{
    return m_selected;
}

const std::vector<std::string>& ServerListWindow::Description() const
{
    return m_description;
}

Ui::Rect ServerListWindow::Bounds(Core::PixelSize screen) const
{
    return LayoutServerList(screen, DescriptionHeight(m_sizes(DescriptionLineSlot)), ShownServers()).window;
}

std::optional<ServerClick> ServerListWindow::Step(const Input::InputState& input, Core::PixelSize screen,
                                                  Ui::NewUiTurn turn)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const std::optional<Ui::Point> mouse = Ui::MouseOfTheTurn(turn, input, screen);
    if (!turn.logic)
    {
        // Without the focus, 0x669f40 handles the buttons and does not run 0x645e10.
        for (Ui::Button& button : m_groupButtons)
        {
            static_cast<void>(button.Step(input, mouse, turn.buttonClicked));
        }
        for (std::size_t i = 0; i < ShownServers(); ++i)
        {
            static_cast<void>(m_serverButtons[i].Step(input, mouse, turn.buttonClicked));
        }
        return std::nullopt;
    }

    // 0x645e10: the first group clicked, among the 21.
    std::optional<std::size_t> clickedGroup;
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        if (m_groupButtons[i].Step(input, mouse, turn.buttonClicked) && !clickedGroup)
        {
            clickedGroup = i;
        }
    }
    if (clickedGroup)
    {
        Select(*clickedGroup);
        Place(screen);
    }

    const ServerGroup* group = SelectedGroup();
    for (std::size_t i = 0; i < ShownServers(); ++i)
    {
        if (!m_serverButtons[i].Step(input, mouse, turn.buttonClicked) || group == nullptr)
        {
            continue;
        }
        const Protocol::Session::ServerEntry& server = group->servers[i];
        if (server.load < BusyLoad)
        {
            Close();
            return ServerClick{.kind = ServerClick::Kind::Chosen, .code = server.code};
        }
        if (server.load < FullLoad)
        {
            return ServerClick{.kind = ServerClick::Kind::Full, .code = server.code};
        }
    }
    return std::nullopt;
}

void ServerListWindow::Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen)
{
    if (!m_open)
    {
        return;
    }
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    DrawBoxAndDescription(sprites, text, screen);
    // The text color stays white, the description's, up to the first label (Q-C83).
    const std::uint32_t lastLabel = DrawButtons(sprites, text, screen, DescriptionColor);
    DrawGroupTwentyTexts(text, lastLabel);
}

void ServerListWindow::DrawBoxAndDescription(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text,
                                             Core::PixelSize screen) const
{
    // 1: the box, open with a group chosen (0x645ba0).
    if (m_selected)
    {
        for (const Render::UiImage& image : DescriptionBoxImages(m_layout.description, m_sizes))
        {
            sprites.Image(image);
        }
    }

    // 2: for each line, the top piece of the column with the same number, the mark and the line.
    const ServerListFrame frame = LayoutServerListFrame(m_layout, m_groupButtons.size(), m_selected, m_sizes);
    static_assert(DescriptionLines == std::tuple_size<decltype(ServerListFrame::corners)>::value,
                  "un renglón por columna");
    const Ui::Rect& box = m_layout.description;
    for (std::size_t line = 0; line < DescriptionLines; ++line)
    {
        if (frame.corners[line])
        {
            sprites.Image(*frame.corners[line]);
        }
        if (frame.markers[line])
        {
            sprites.Image(*frame.markers[line]);
        }
        // 0x5542f0 does not draw an empty text.
        if (line < m_description.size() && !m_description[line].empty())
        {
            const int y = box.y + DescriptionTop + static_cast<int>(line) * DescriptionLineStep;
            text.Draw(Render::Text::DesignX(box.x + DescriptionLeft, screen), Render::Text::DesignY(y, screen),
                      m_description[line],
                      Render::Text::TextStyle{ListFont, Render::Text::TextColors{DescriptionColor, 0}});
        }
    }
}

std::uint32_t ServerListWindow::DrawButtons(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text,
                                            Core::PixelSize screen, std::uint32_t textColor) const
{
    // 3: the buttons with their labels (0x66a090), first the group ones, then the bars.
    const auto drawButton = [&](const Ui::Button& button)
    {
        sprites.Image(button.Image(m_sizes));
        if (const std::optional<std::uint32_t> color = button.DrawLabel(text, screen))
        {
            textColor = *color;
        }
    };
    std::ranges::for_each(m_groupButtons, drawButton);
    const ServerGroup* group = SelectedGroup();
    if (group == nullptr)
    {
        return textColor;
    }
    for (std::size_t i = 0; i < ShownServers(); ++i)
    {
        drawButton(m_serverButtons[i]);
    }
    for (std::size_t i = 0; i < ShownServers(); ++i)
    {
        Render::UiImage bar = Ui::PixelImage(LoadBarSlot, m_layout.loadBars[i], m_sizes(LoadBarSlot));
        // 0x640580 changes its width with 0x647370(width, 0, 1): the right u, without the half texel.
        bar.width = BarWidth(group->servers[i].load);
        bar.resizedWidth = true;
        sprites.Image(bar);
    }
    return textColor;
}

void ServerListWindow::DrawGroupTwentyTexts(Render::Frame::ITextDrawer& text, std::uint32_t color) const
{
    // 4: with group 20, its three texts, in the last label's color (Q-C83). An empty one leaves nothing on screen, as
    // in the other windows.
    if (!ShowsGroupTwentyTexts())
    {
        return;
    }
    for (std::size_t k = 0; k < GroupTwentyTextCount; ++k)
    {
        if (const auto line = Ui::TextAt(m_texts, FirstGroupTwentyText + k); line && !line->empty())
        {
            text.Draw(GroupTwentyLeft, GroupTwentyTop + static_cast<int>(k) * GroupTwentyStep, *line,
                      Render::Text::TextStyle{ListFont, Render::Text::TextColors{color, 0}});
        }
    }
}

void ServerListWindow::Select(std::size_t index)
{
    m_selected = index;
    const ServerGroup& group = m_groups.groups[index];
    // 0x645860: the two lines, empty without the text.
    m_description.clear();
    if (const auto description = Ui::TextAt(m_texts, DescriptionText(group.slot)))
    {
        m_description = Ui::SplitIntoLines(*description, DescriptionLines, DescriptionLineBytes);
    }
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        m_groupButtons[i].SetChecked(i == index);
    }
    m_serverButtons.clear();
    const std::optional<std::string_view> name = Ui::TextAt(m_texts, GroupNameText(group.group));
    const bool single = group.servers.size() <= 1;
    for (const Protocol::Session::ServerEntry& server : group.servers)
    {
        Ui::Button button(ServerButtonSlot, ServerButtonWidth, ButtonHeight);
        const int number = ServerNumber(server.code);
        const bool nonPvp = IsNonPvp(group.slot, number);
        if (name)
        {
            const std::string_view load = Ui::TextAt(m_texts, LoadText(server.load)).value_or(std::string_view{});
            button.SetLabel(single ? SingleServerLabel(*name, nonPvp, load) : ServerLabel(*name, number, nonPvp, load),
                            nonPvp ? NonPvpColors : ServerColors);
        }
        m_serverButtons.push_back(std::move(button));
    }
}

void ServerListWindow::Place(Core::PixelSize screen)
{
    const int description = DescriptionHeight(m_sizes(DescriptionLineSlot));
    m_layout = LayoutServerList(screen, description, ShownServers());
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        m_groupButtons[i].MoveTo(Ui::Point{m_layout.groupButtons[i].x, m_layout.groupButtons[i].y});
    }
    for (std::size_t i = 0; i < m_serverButtons.size(); ++i)
    {
        m_serverButtons[i].MoveTo(Ui::Point{m_layout.serverButtons[i].x, m_layout.serverButtons[i].y});
    }
}

std::size_t ServerListWindow::ShownServers() const
{
    return std::min(m_serverButtons.size(), ServersPerGroup);
}

bool ServerListWindow::ShowsGroupTwentyTexts() const
{
    // With nothing chosen, the original compares the name of slot 3, which it does not have: it never matches.
    const ServerGroup* group = SelectedGroup();
    if (group == nullptr)
    {
        return false;
    }
    const std::optional<std::string_view> name = Ui::TextAt(m_texts, GroupNameText(group->group));
    const std::optional<std::string_view> twenty = Ui::TextAt(m_texts, GroupNameText(GroupTwenty));
    return name && twenty && *name == *twenty;
}

const ServerGroup* ServerListWindow::SelectedGroup() const
{
    if (!m_selected || *m_selected >= m_groups.groups.size())
    {
        return nullptr;
    }
    return &m_groups.groups[*m_selected];
}
} // namespace Client::FrontEnd
