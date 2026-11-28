#pragma once

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Assets/SceneTextures.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "FrontEnd/ServerListModel.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The server list window (CL5.5; 06 § La lista de servidores): 0x6451d0 builds it, 0x645520 places it with 0x6456c0
// and 0x6457c0, 0x645860 sets its labels and description, 0x645e10 handles the clicks and 0x6463b0 draws it. 21 group
// buttons, in two columns of 10 and one centered below, and in the middle those of the chosen group: one per server,
// or a single one without a number if the group has one. The original has 16 server buttons; MuRet has 20, one per
// number (Q-C63). Until a group is chosen there are none (+0x3138 starts at -1). Around it, the server_deco_all frame
// and, with a group chosen, the bottom box with its description in two lines; and with group 20, three texts top
// left (Q-C83). *Static*.
//
// A click on a group does not ask for the list again: the original sends another F4 02 (Q-C82). And everything is
// handled in the step (Q-C81).
namespace Client::FrontEnd
{
// 0x6451d0: two columns of group buttons, one of server buttons and 56.
inline constexpr int ServerListWidth = 465;
// 0x66a1c0: server_ex01's, the top edge.
inline constexpr int DescriptionWidth = 512;

// The description of group button k is text 1717 + k (0x645860), in two lines of 83 bytes (0x632c90). The original's
// button k is slot 4 + k; in MuRet, which shows every slot (Q-C62), it is that of the group's slot: 1717 + slot − 4.
inline constexpr std::size_t FirstDescriptionText = 1717;
inline constexpr std::size_t DescriptionLines = 2;
inline constexpr std::size_t DescriptionLineBytes = 83;
// With group 20 chosen, texts 565, 566 and 567 (0x6463b0).
inline constexpr std::size_t FirstGroupTwentyText = 565;
inline constexpr std::size_t GroupTwentyTextCount = 3;

// 0x645e10
struct ServerClick
{
    enum class Kind : std::uint8_t
    {
        // Load under 100: the list closes and it is chosen (the F4 03).
        Chosen,
        // Load from 100 to 127: notice 0x63, and the list stays. With 128 or more nothing happens.
        Full,
    };

    Kind kind = Kind::Chosen;
    std::uint16_t code = 0;

    friend bool operator==(const ServerClick&, const ServerClick&) = default;
};

// In pixels, with y from the top.
struct ServerListLayout
{
    Ui::Rect window;
    std::array<Ui::Rect, GroupButtonCount> groupButtons{};
    // The group's 20 server slots, for `serverCount` servers.
    std::array<Ui::Rect, ServersPerGroup> serverButtons{};
    // Each one's load bar, at its full width (160).
    std::array<Ui::Rect, ServersPerGroup> loadBars{};
    // Centered below the window (0x645520).
    Ui::Rect description;
};

// The four pieces of server_deco_all around the group buttons (0x6451d0, 0x645520, 0x6457c0, 0x645c70 and
// 0x645ca0), in pixels and already shifted by their point: for each column, the top one and the chosen one's mark, in
// the order 0x6463b0 draws them.
struct ServerListFrame
{
    // The left column's, always, and the right one's, with more than 10 buttons.
    std::array<std::optional<Render::UiImage>, 2> corners{};
    // The chosen one's mark, in its column: 0 to 9 on the left, 10 to 19 on the right, and none with the 20.
    std::array<std::optional<Render::UiImage>, 2> markers{};
};

// The description box height (+0x3064, 0x6451d0): 10 lines of the buffer height of the slot 0x19 texture
// (server_ex02), plus 6 above and 6 below.
[[nodiscard]] int DescriptionHeight(Assets::TextureBufferSize lineTexture);

// 0x6451d0, 0x645520 and 0x6456c0 with the window where 0x65f300 puts it: ((W − 465) / 2, (H − height) / 2) with C
// division, and the height, 16 · 26 + 26 + the box + 10. The group rows start at y + height − 11 · 26 − the box − 10;
// the 20 goes in the middle, at y + height − the box − 26 − 5. The server ones go at x + 136 from the first row; with
// 10 or more, the last ends at row 10 and the earlier ones move up, even if they leave the window. The box, 512 wide,
// goes at (x − (512 − 465) / 2, y + height − the box).
[[nodiscard]] ServerListLayout LayoutServerList(Core::PixelSize screen, int descriptionHeight, std::size_t serverCount);

// The frame for `groupButtons` group buttons and the chosen one, from 0. Each piece puts its point on a corner of a
// button: (8, 19) top left of the 0, (60, 19) top right of the 10, (1, 2) top right of the chosen one on the left and
// (23, 2) top left of the one on the right.
[[nodiscard]] ServerListFrame LayoutServerListFrame(const ServerListLayout& layout, std::size_t groupButtons,
                                                    std::optional<std::size_t> selected, const Ui::TextureSizes& sizes);

// The five images of 0x66a1c0's box in `box`, in 0x66a960's order: the server_ex03 fill at (3, 3), repeated; the top
// and bottom edges from server_ex01; and the server_ex02 sides, the left one repeated and the right one stretched (06
// § La lista de servidores).
[[nodiscard]] std::array<Render::UiImage, 5> DescriptionBoxImages(Ui::Rect box, const Ui::TextureSizes& sizes);

class ServerListWindow final : public Ui::NewUiWindow
{
public:
    // `texts` can be nullptr (without Text.bmd the buttons have no label) and must live as long as the window.
    ServerListWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // With the list already grouped (GroupServers), with no group chosen.
    void Open(ServerGroups groups);
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // `window` from LayoutServerList.
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;
    // The chosen group button, from 0.
    [[nodiscard]] std::optional<std::size_t> Selected() const;

    // 0x645e10 in the step, with the focus: a click on a group chooses it; one on a server says what happened. Without
    // the focus, only the buttons.
    std::optional<ServerClick> Step(const Input::InputState& input, Core::PixelSize screen, Ui::NewUiTurn turn = {});
    // The chosen group's description lines, empty without one.
    [[nodiscard]] const std::vector<std::string>& Description() const;

    // 0x6463b0 with normal blending (the batch arrives with the arena's additive one, Q-C80): the box, the frame pieces
    // with the description lines, the buttons with their labels, the bars and, with group 20, its three texts. It lays
    // everything out for `screen` first, in case it changed.
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen);

private:
    void Select(std::size_t group);
    void Place(Core::PixelSize screen);
    [[nodiscard]] std::size_t ShownServers() const;
    [[nodiscard]] const ServerGroup* SelectedGroup() const;
    // Whether the chosen group's name is group 20's text (0x6463b0's strcmp).
    [[nodiscard]] bool ShowsGroupTwentyTexts() const;
    // 0x6463b0's steps: the box and, per line, the frame pieces and the description; the buttons and the bars, which
    // give the last label's color, or `textColor` without any; and group 20's texts in that color.
    void DrawBoxAndDescription(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text,
                               Core::PixelSize screen) const;
    [[nodiscard]] std::uint32_t DrawButtons(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text,
                                            Core::PixelSize screen, std::uint32_t textColor) const;
    void DrawGroupTwentyTexts(Render::Frame::ITextDrawer& text, std::uint32_t color) const;

    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts;
    bool m_open = false;
    ServerGroups m_groups;
    std::optional<std::size_t> m_selected;
    std::vector<Ui::Button> m_groupButtons;
    std::vector<Ui::Button> m_serverButtons;
    std::vector<std::string> m_description;
    ServerListLayout m_layout;
};
} // namespace Client::FrontEnd
