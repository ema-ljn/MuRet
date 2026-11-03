#include "UI/ServerListWindow.h"

#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "UI/GameText.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x6451d0: los botones de grupo, de 108 × 26 con cha_bt (slot 0xc) y cuatro cuadros, el 3
// marcado; los de servidor, de 193 × 26 con server_b2_all (0xd) y tres; la barra de la carga de
// 160 × 4 con server_b2_loding (0xe).
constexpr Render::TextureSlot GroupButtonSlot{0xc};
constexpr Render::TextureSlot ServerButtonSlot{0xd};
constexpr Render::TextureSlot LoadBarSlot{0xe};
constexpr Render::TextureSlot DescriptionLineSlot{0x19};
constexpr int GroupButtonWidth = 108;
constexpr int ButtonHeight = 26;
constexpr int CheckedFrame = 3;
constexpr int ServerButtonWidth = 193;
constexpr int LoadBarWidth = 160;
constexpr int LoadBarHeight = 4;

// 0x6451d0: el alto es el de los 16 botones de servidor del original más uno de grupo, la caja y
// 10; la caja, 10 líneas más 6 y 6.
constexpr int OriginalServerButtons = 16;
constexpr int BottomMargin = 10;
constexpr int DescriptionLines = 10;
constexpr int DescriptionPadding = 6 + 6;
// 0x645520: 10 filas de grupos, la fila del 20 a 5 de la caja.
constexpr int GroupRows = 10;
constexpr int LastGroupGap = 5;
// 0x6456c0: los de servidor, a 28 del borde de la columna de grupos; la barra en (+16, +19).
constexpr int ServerColumnGap = 28;
constexpr int LoadBarLeft = 16;
constexpr int LoadBarTop = 19;

// 0x645e10: con menos de 100 se elige; con menos de 128, el aviso.
constexpr std::uint8_t BusyLoad = 100;
constexpr std::uint8_t FullLoad = 128;
// 0x640580(carga, 100): la barra mide 160 · carga / 100, con la carga hasta 100.
constexpr unsigned MaxBarLoad = 100;

// Los colores de los rótulos (0x645860), 0xAABBGGRR: gris claro, blanco con el mouse encima; el
// servidor sin PvP, amarillo claro en todos.
constexpr std::uint32_t LabelGray = 0xffe2e2e2;
constexpr std::uint32_t LabelWhite = 0xffffffff;
constexpr std::uint32_t NonPvpYellow = 0xff79ffff;
constexpr ButtonColors GroupColors{.normal = LabelGray,
                                   .pressed = LabelGray,
                                   .hover = LabelWhite,
                                   .checkedNormal = LabelGray,
                                   .checkedPressed = LabelGray,
                                   .checkedHover = LabelWhite};
constexpr ButtonColors ServerColors{.normal = LabelGray, .pressed = LabelGray, .hover = LabelWhite};
constexpr ButtonColors NonPvpColors{.normal = NonPvpYellow, .pressed = NonPvpYellow, .hover = NonPvpYellow};

int BarWidth(std::uint8_t load)
{
    const unsigned clamped = std::min<unsigned>(load, MaxBarLoad);
    return static_cast<int>(static_cast<unsigned>(LoadBarWidth) * clamped / MaxBarLoad);
}
} // namespace

int DescriptionHeight(Scenes::TextureBufferSize lineTexture)
{
    return DescriptionLines * lineTexture.height + DescriptionPadding;
}

ServerListLayout LayoutServerList(App::PixelSize screen, int descriptionHeight, std::size_t serverCount)
{
    ServerListLayout layout;
    const int height = OriginalServerButtons * ButtonHeight + ButtonHeight + descriptionHeight + BottomMargin;
    const int x = (screen.width - ServerListWidth) / 2;
    const int y = (screen.height - height) / 2;
    layout.window = Rect{.x = x, .y = y, .width = ServerListWidth, .height = height};

    const int firstRow = y + height - (GroupRows + 1) * ButtonHeight - descriptionHeight - BottomMargin;
    const int rightColumn = x + ServerListWidth - GroupButtonWidth;
    constexpr auto Rows = static_cast<std::size_t>(GroupRows);
    for (std::size_t row = 0; row < Rows; ++row)
    {
        const int rowY = firstRow + static_cast<int>(row) * ButtonHeight;
        layout.groupButtons[row] = Rect{.x = x, .y = rowY, .width = GroupButtonWidth, .height = ButtonHeight};
        layout.groupButtons[row + Rows] =
            Rect{.x = rightColumn, .y = rowY, .width = GroupButtonWidth, .height = ButtonHeight};
    }
    layout.groupButtons[2 * Rows] = Rect{.x = x + (ServerListWidth - GroupButtonWidth) / 2,
                                         .y = y + height - descriptionHeight - ButtonHeight - LastGroupGap,
                                         .width = GroupButtonWidth,
                                         .height = ButtonHeight};

    // 0x6456c0: con 10 o más servidores (count · 26 >= 260), el primero sube.
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
        layout.serverButtons[i] = Rect{.x = serverX, .y = buttonY, .width = ServerButtonWidth, .height = ButtonHeight};
        layout.loadBars[i] =
            Rect{.x = serverX + LoadBarLeft, .y = buttonY + LoadBarTop, .width = LoadBarWidth, .height = LoadBarHeight};
    }
    return layout;
}

ServerListWindow::ServerListWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
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
    const std::size_t shown = std::min(m_groups.groups.size(), GroupButtonCount);
    for (std::size_t i = 0; i < shown; ++i)
    {
        Button button(GroupButtonSlot, GroupButtonWidth, ButtonHeight, CheckedFrame);
        if (const auto name = TextAt(m_texts, GroupNameText(m_groups.groups[i].group)))
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

std::optional<ServerClick> ServerListWindow::Step(const Input::InputState& input, App::PixelSize screen)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const Point mouse = MouseInPixels(input, screen);

    // 0x645e10: el primer grupo con clic, entre los 21.
    std::optional<std::size_t> clickedGroup;
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        if (m_groupButtons[i].Step(input, mouse) && !clickedGroup)
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
        if (!m_serverButtons[i].Step(input, mouse) || group == nullptr)
        {
            continue;
        }
        const Protocol::Connection::ServerEntry& server = group->servers[i];
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

void ServerListWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
{
    if (!m_open)
    {
        return;
    }
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    for (const Button& button : m_groupButtons)
    {
        sprites.Image(button.Image(m_sizes));
        button.DrawLabel(text, screen);
    }
    const ServerGroup* group = SelectedGroup();
    for (std::size_t i = 0; i < ShownServers() && group != nullptr; ++i)
    {
        sprites.Image(m_serverButtons[i].Image(m_sizes));
        Render::UiImage bar = PixelImage(LoadBarSlot, m_layout.loadBars[i], m_sizes(LoadBarSlot));
        // 0x640580 le cambia el ancho con 0x647370(ancho, 0, 1): la u de la derecha, sin medio
        // téxel.
        bar.width = BarWidth(group->servers[i].load);
        bar.resizedWidth = true;
        sprites.Image(bar);
        m_serverButtons[i].DrawLabel(text, screen);
    }
}

void ServerListWindow::Select(std::size_t index)
{
    m_selected = index;
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        m_groupButtons[i].SetChecked(i == index);
    }
    m_serverButtons.clear();
    const ServerGroup& group = m_groups.groups[index];
    const std::optional<std::string_view> name = TextAt(m_texts, GroupNameText(group.group));
    const bool single = group.servers.size() <= 1;
    for (const Protocol::Connection::ServerEntry& server : group.servers)
    {
        Button button(ServerButtonSlot, ServerButtonWidth, ButtonHeight);
        const int number = ServerNumber(server.code);
        const bool nonPvp = IsNonPvp(group.slot, number);
        if (name)
        {
            const std::string_view load = TextAt(m_texts, LoadText(server.load)).value_or(std::string_view{});
            button.SetLabel(single ? SingleServerLabel(*name, nonPvp, load) : ServerLabel(*name, number, nonPvp, load),
                            nonPvp ? NonPvpColors : ServerColors);
        }
        m_serverButtons.push_back(std::move(button));
    }
}

void ServerListWindow::Place(App::PixelSize screen)
{
    const int description = DescriptionHeight(m_sizes(DescriptionLineSlot));
    m_layout = LayoutServerList(screen, description, ShownServers());
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        m_groupButtons[i].MoveTo(Point{m_layout.groupButtons[i].x, m_layout.groupButtons[i].y});
    }
    for (std::size_t i = 0; i < m_serverButtons.size(); ++i)
    {
        m_serverButtons[i].MoveTo(Point{m_layout.serverButtons[i].x, m_layout.serverButtons[i].y});
    }
}

std::size_t ServerListWindow::ShownServers() const
{
    return std::min(m_serverButtons.size(), ServersPerGroup);
}

const ServerGroup* ServerListWindow::SelectedGroup() const
{
    if (!m_selected || *m_selected >= m_groups.groups.size())
    {
        return nullptr;
    }
    return &m_groups.groups[*m_selected];
}
} // namespace Client::UI
