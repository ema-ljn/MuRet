#include "UI/ServerListWindow.h"

#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "UI/GameText.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
// 0x6451d0: el marco, de server_deco_all; la caja, de server_ex03, server_ex01 y server_ex02.
constexpr Render::TextureSlot FrameSlot{0xf};
constexpr Render::TextureSlot BoxFillSlot{0x17};
constexpr Render::TextureSlot BoxEdgeSlot{0x18};
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
constexpr int BoxLines = 10;
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

// Una pieza de server_deco_all (0x647260): su lugar en la textura, su tamaño y su punto.
struct FramePiece
{
    int u = 0;
    int v = 0;
    int width = 0;
    int height = 0;
    Point point;
};
// 0x6451d0, de 0x6452a3 a 0x645378.
constexpr FramePiece LeftCorner{.u = 0, .v = 0, .width = 68, .height = 95, .point = {8, 19}};
constexpr FramePiece RightCorner{.u = 68, .v = 0, .width = 68, .height = 95, .point = {60, 19}};
constexpr FramePiece LeftMarker{.u = 136, .v = 0, .width = 23, .height = 29, .point = {1, 2}};
constexpr FramePiece RightMarker{.u = 136, .v = 30, .width = 23, .height = 29, .point = {23, 2}};
// 0x645c70: la de la derecha, con más de 10 botones de grupo; 0x645ca0: la marca, del lado del
// elegido, y ninguna con el 20.
constexpr std::size_t GroupsPerColumn = 10;
constexpr std::size_t MarkedGroups = 2 * GroupsPerColumn;

// 0x66a1c0 y 0x66a3d0: los bordes de arriba y de abajo miden 6, los lados 3, y el fondo empieza
// en (3, 3) con 6 menos de ancho y de alto que la caja.
constexpr int BoxEdgeHeight = 6;
constexpr int BoxSideWidth = 3;
constexpr int BoxFillInset = 3;
// 0x6451d0: el lado de la derecha es el cuadro de 3 × 4 que empieza en la columna 3 de server_ex02.
constexpr int RightSideColumn = 3;
constexpr int RightSideRows = 4;

// 0x6463b0: los renglones van en (+11, +12) de la caja, cada uno 20 más abajo, con la fuente
// DAT_05763560, en blanco y sin fondo; los textos del grupo 20, en (90, 104), (90, 119) y (90, 134)
// de 640 × 480.
constexpr int DescriptionLeft = 11;
constexpr int DescriptionTop = 12;
constexpr int DescriptionLineStep = 20;
constexpr Render::Text::FontKind ListFont = Render::Text::FontKind::OtherFace12;
constexpr std::uint32_t DescriptionColor = 0xffffffff;
constexpr int GroupTwentyLeft = 90;
constexpr int GroupTwentyTop = 104;
constexpr int GroupTwentyStep = 15;
// El grupo cuyo nombre, el texto 1829, compara 0x6463b0 con el del elegido.
constexpr std::uint16_t GroupTwenty = 20;

// La pieza con su punto en `at`: 0x6472e0(x, y, 3) resta el punto de +0x58 y +0x5c. Las que
// empiezan en (0, 0) de la textura no tienen cuadros (0x647260 le da a 0x647050 una lista nula).
Render::UiImage PlacePiece(const FramePiece& piece, Point at, const TextureSizes& sizes)
{
    Render::UiImage image = PixelImage(
        FrameSlot,
        Rect{.x = at.x - piece.point.x, .y = at.y - piece.point.y, .width = piece.width, .height = piece.height},
        sizes(FrameSlot));
    if (piece.u != 0 || piece.v != 0)
    {
        image.frameLeft = piece.u;
        image.frameTop = piece.v;
    }
    return image;
}

// 0x645860 parte el texto 1717 + elegido, y el botón k del original es el lugar 4 + k. MuRet
// muestra los grupos de todos los lugares (Q-C62), y el botón k puede ser otro lugar: la
// descripción es la del lugar del grupo.
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

int DescriptionHeight(Scenes::TextureBufferSize lineTexture)
{
    return BoxLines * lineTexture.height + DescriptionPadding;
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

    // 0x645679..0x6456a7: (x − (512 − 465) / 2, y + alto − la caja), con la división de C.
    layout.description = Rect{.x = x - (DescriptionWidth - ServerListWidth) / 2,
                              .y = y + height - descriptionHeight,
                              .width = DescriptionWidth,
                              .height = descriptionHeight};
    return layout;
}

ServerListFrame LayoutServerListFrame(const ServerListLayout& layout, std::size_t groupButtons,
                                      std::optional<std::size_t> selected, const TextureSizes& sizes)
{
    ServerListFrame frame;
    const Rect& first = layout.groupButtons[0];
    frame.corners[0] = PlacePiece(LeftCorner, Point{first.x, first.y}, sizes);
    if (groupButtons > GroupsPerColumn)
    {
        const Rect& top = layout.groupButtons[GroupsPerColumn];
        frame.corners[1] = PlacePiece(RightCorner, Point{top.x + top.width, top.y}, sizes);
    }
    if (selected && *selected < MarkedGroups)
    {
        const Rect& button = layout.groupButtons[*selected];
        if (*selected < GroupsPerColumn)
        {
            frame.markers[0] = PlacePiece(LeftMarker, Point{button.x + button.width, button.y}, sizes);
        }
        else
        {
            frame.markers[1] = PlacePiece(RightMarker, Point{button.x, button.y}, sizes);
        }
    }
    return frame;
}

std::array<Render::UiImage, 5> DescriptionBoxImages(Rect box, const TextureSizes& sizes)
{
    // 0x66a4e0: los lados miden las 10 líneas, el alto de la caja menos los dos bordes.
    const int sideHeight = box.height - 2 * BoxEdgeHeight;

    // El fondo, de 506 × (alto − 6), y el lado de la izquierda siguen a 0x647370 (+0x98 en 1) y
    // repiten su textura.
    Render::UiImage fill = PixelImage(BoxFillSlot,
                                      Rect{.x = box.x + BoxFillInset,
                                           .y = box.y + BoxFillInset,
                                           .width = box.width - 2 * BoxFillInset,
                                           .height = box.height - 2 * BoxFillInset},
                                      sizes(BoxFillSlot));
    fill.resizedWidth = true;
    fill.resizedHeight = true;

    const Render::UiImage top = PixelImage(
        BoxEdgeSlot, Rect{.x = box.x, .y = box.y, .width = box.width, .height = BoxEdgeHeight}, sizes(BoxEdgeSlot));
    Render::UiImage bottom = PixelImage(
        BoxEdgeSlot,
        Rect{.x = box.x, .y = box.y + box.height - BoxEdgeHeight, .width = box.width, .height = BoxEdgeHeight},
        sizes(BoxEdgeSlot));
    bottom.frameTop = BoxEdgeHeight;

    Render::UiImage left = PixelImage(
        DescriptionLineSlot, Rect{.x = box.x, .y = box.y + BoxEdgeHeight, .width = BoxSideWidth, .height = sideHeight},
        sizes(DescriptionLineSlot));
    left.resizedHeight = true;

    // El de la derecha empieza en (3, 0) de server_ex02: tiene cuadros, y 0x647050 le pone +0x98
    // en 0. Sus UV quedan en las 4 filas con que lo arma 0x6451d0, no en las del buffer, y se
    // estira.
    Render::UiImage right = PixelImage(DescriptionLineSlot,
                                       Rect{.x = box.x + box.width - BoxSideWidth,
                                            .y = box.y + BoxEdgeHeight,
                                            .width = BoxSideWidth,
                                            .height = sideHeight},
                                       sizes(DescriptionLineSlot));
    right.frameTop = 0;
    right.frameLeft = RightSideColumn;
    right.uvHeight = RightSideRows;
    return {fill, top, bottom, left, right};
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
    m_description.clear();
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

const std::vector<std::string>& ServerListWindow::Description() const
{
    return m_description;
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
    // 1: la caja, abierta con un grupo elegido (0x645ba0).
    if (m_selected)
    {
        for (const Render::UiImage& image : DescriptionBoxImages(m_layout.description, m_sizes))
        {
            sprites.Image(image);
        }
    }

    // 2: por cada renglón, la pieza de arriba de su columna, la marca y el renglón. El color del
    // texto queda en blanco hasta el primer rótulo (Q-C83).
    const ServerListFrame frame = LayoutServerListFrame(m_layout, m_groupButtons.size(), m_selected, m_sizes);
    std::uint32_t textColor = DescriptionColor;
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
        // 0x5542f0 no dibuja un texto vacío.
        if (line < m_description.size() && !m_description[line].empty())
        {
            const Rect& box = m_layout.description;
            const int y = box.y + DescriptionTop + static_cast<int>(line) * DescriptionLineStep;
            text.Draw(Render::Text::DesignX(box.x + DescriptionLeft, screen), Render::Text::DesignY(y, screen),
                      m_description[line],
                      Render::Text::TextStyle{ListFont, Render::Text::TextColors{DescriptionColor, 0}});
        }
    }

    // 3: los botones con sus rótulos (0x66a090), primero los de grupo, y después las barras.
    const auto drawButton = [&](const Button& button)
    {
        sprites.Image(button.Image(m_sizes));
        if (const std::optional<std::uint32_t> color = button.DrawLabel(text, screen))
        {
            textColor = *color;
        }
    };
    std::ranges::for_each(m_groupButtons, drawButton);
    const ServerGroup* group = SelectedGroup();
    if (group != nullptr)
    {
        for (std::size_t i = 0; i < ShownServers(); ++i)
        {
            drawButton(m_serverButtons[i]);
        }
        for (std::size_t i = 0; i < ShownServers(); ++i)
        {
            Render::UiImage bar = PixelImage(LoadBarSlot, m_layout.loadBars[i], m_sizes(LoadBarSlot));
            // 0x640580 le cambia el ancho con 0x647370(ancho, 0, 1): la u de la derecha, sin medio
            // téxel.
            bar.width = BarWidth(group->servers[i].load);
            bar.resizedWidth = true;
            sprites.Image(bar);
        }
    }

    // 4: con el grupo 20, sus tres textos, con el color del último rótulo (Q-C83). Uno vacío no
    // deja nada en la pantalla, como en las otras ventanas.
    if (ShowsGroupTwentyTexts())
    {
        for (std::size_t k = 0; k < GroupTwentyTextCount; ++k)
        {
            if (const auto line = TextAt(m_texts, FirstGroupTwentyText + k); line && !line->empty())
            {
                text.Draw(GroupTwentyLeft, GroupTwentyTop + static_cast<int>(k) * GroupTwentyStep, *line,
                          Render::Text::TextStyle{ListFont, Render::Text::TextColors{textColor, 0}});
            }
        }
    }
}

void ServerListWindow::Select(std::size_t index)
{
    m_selected = index;
    const ServerGroup& group = m_groups.groups[index];
    // 0x645860: los dos renglones, vacíos sin el texto.
    m_description.clear();
    if (const auto description = TextAt(m_texts, DescriptionText(group.slot)))
    {
        m_description = SplitIntoLines(*description, DescriptionLines, DescriptionLineBytes);
    }
    for (std::size_t i = 0; i < m_groupButtons.size(); ++i)
    {
        m_groupButtons[i].SetChecked(i == index);
    }
    m_serverButtons.clear();
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

bool ServerListWindow::ShowsGroupTwentyTexts() const
{
    // Sin elegido, el original compara el nombre del lugar 3, que no tiene: nunca da.
    const ServerGroup* group = SelectedGroup();
    if (group == nullptr)
    {
        return false;
    }
    const std::optional<std::string_view> name = TextAt(m_texts, GroupNameText(group->group));
    const std::optional<std::string_view> twenty = TextAt(m_texts, GroupNameText(GroupTwenty));
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
} // namespace Client::UI
