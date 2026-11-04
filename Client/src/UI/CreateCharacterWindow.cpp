#include "UI/CreateCharacterWindow.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/GameText.h"
#include "UI/TextField.h"
#include "UI/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
constexpr int BoxWidth = 454;
constexpr int BoxHeight = 406;
constexpr Render::TextureSlot NameBackSlot{0xc}; // Interface\cha_id.tga
constexpr Render::TextureSlot ClassSlot{0xd};    // Interface\cha_bt.tga, 4 cuadros
constexpr Render::TextureSlot OkSlot{0x724};     // message_ok_b_all.tga, el del aviso
constexpr Render::TextureSlot CancelSlot{0x725}; // loding_cancel_b_all.tga
constexpr int NameBackTop = 317;
constexpr int NameBackWidth = 346;
constexpr int NameBackHeight = 38;
constexpr int RightColumn = 346;
constexpr int AttributesTop = 50;
constexpr int AttributesWidth = 108;
constexpr int AttributesHeight = 80;
constexpr int DarkLordAttributesHeight = 96;
constexpr int StripTop = 355;
constexpr int StripHeight = 51;
constexpr int ClassWidth = 108;
constexpr int ClassHeight = 26;
constexpr int FirstClassesTop = 157;
constexpr int LastClassesTop = 246;
constexpr std::size_t FirstOfTheLastClasses = 3;
constexpr int ButtonWidth = 54;
constexpr int ButtonHeight = 30;
constexpr int ButtonsTop = 325;
constexpr int CancelLeft = 400;
constexpr int NameFieldLeft = 78;
constexpr int NameFieldTop = 338;
// La base de 0x669c70(W, H, −1, 0) y las dos cajas negras de alfa 0x8f.
constexpr float BaseAlpha = 128.0f / 255.0f;
constexpr float BoxAlpha = 143.0f / 255.0f;
// EM_LIMITTEXT(10) en 0x63d670.
constexpr std::size_t NameBytes = 10;
// 0x63c330(108, 26, 0xd, 4, 2, 1, 0, 3, 3, 3, 0): el 3 marcado (también encima y apretado) y el 0
// deshabilitado, marcado o no.
constexpr int CheckedFrame = 3;
constexpr int DisabledFrame = 0;
// Los rótulos de las clases, los textos 20 a 24, con la tabla de 0x63d360.
constexpr std::size_t FirstClassText = 20;
constexpr ButtonColors ClassColors{.normal = 0xffe2e2e2,
                                   .pressed = 0xffe2e2e2,
                                   .hover = 0xffffffff,
                                   .checkedNormal = 0xffe2e2e2,
                                   .checkedPressed = 0xffe2e2e2,
                                   .checkedHover = 0xffffffff,
                                   .disabled = 0xff777777,
                                   .checkedDisabled = 0xff777777};
// 0x63e0a0: con DAT_05763560, sin fondo; los valores en 0xff00b4ff y los rótulos (1701 + i) en
// blanco, en (ax + 76) y (ax + 22), de a 17 desde ay + 10; el comando del Dark Lord en ay + 78.
constexpr Render::Text::TextStyle ValueStyle{Render::Text::FontKind::OtherFace12,
                                             Render::Text::TextColors{0xff00b4ff, 0}};
constexpr Render::Text::TextStyle LabelStyle{Render::Text::FontKind::OtherFace12,
                                             Render::Text::TextColors{0xffffffff, 0}};
constexpr int ValueLeft = 76;
constexpr int LabelLeft = 22;
constexpr int FirstAttributeTop = 10;
constexpr int AttributeStep = 17;
constexpr int CommandTop = 78;
constexpr std::size_t FirstAttributeText = 1701;
constexpr std::size_t CommandText = 1738;
constexpr std::string_view CommandValue = "25";
constexpr std::uint8_t DarkLord = 4;
// Los strings de 0x6d657c..0x6d659c: fuerza, agilidad, vitalidad y energía, por clase.
constexpr std::array<std::array<std::string_view, 4>, CreatableClassCount> Attributes{{
    {"18", "18", "15", "30"},
    {"28", "20", "25", "10"},
    {"22", "25", "20", "15"},
    {"26", "26", "26", "26"},
    {"26", "20", "20", "15"},
}};
// La descripción: el texto 1705 + clase en dos renglones de menos de 75 bytes (0x632c90), en
// (x + 10, y + 355 + 12 + 19 j), en blanco.
constexpr std::size_t FirstDescriptionText = 1705;
constexpr std::size_t DescriptionLines = 2;
constexpr std::size_t DescriptionBytes = 75;
constexpr int DescriptionLeft = 10;
constexpr int DescriptionTop = StripTop + 12;
constexpr int DescriptionStep = 19;
// El EDIT del nombre: el texto en blanco (+0x9c) con DAT_05763554, y el cursor de los campos del
// login (0x468320 no se leyó entero).
constexpr Render::Text::TextStyle NameStyle{Render::Text::FontKind::Normal, Render::Text::TextColors{0xffffffff, 0}};
constexpr std::string_view Caret = "_";

Rect Area(int x, int y, int width, int height)
{
    return Rect{.x = x, .y = y, .width = width, .height = height};
}

Render::UiImage BlackBox(const Rect& area, float alpha)
{
    return Render::UiImage{.slot = std::nullopt,
                           .x = area.x,
                           .y = area.y,
                           .width = area.width,
                           .height = area.height,
                           .color = Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = alpha}};
}
} // namespace

CreateWindowLayout LayoutCreateWindow(App::PixelSize screen, std::uint8_t characterClass)
{
    const int x = (screen.width - BoxWidth) / 2;
    const int y = (screen.height - BoxHeight) / 2;
    CreateWindowLayout layout{};
    layout.box = Area(x, y, BoxWidth, BoxHeight);
    layout.nameBack = Area(x, y + NameBackTop, NameBackWidth, NameBackHeight);
    layout.attributes = Area(x + RightColumn, y + AttributesTop, AttributesWidth,
                             characterClass == DarkLord ? DarkLordAttributesHeight : AttributesHeight);
    layout.strip = Area(x, y + StripTop, BoxWidth, StripHeight);
    for (std::size_t i = 0; i < CreatableClassCount; ++i)
    {
        const int top = i < FirstOfTheLastClasses
                            ? FirstClassesTop + ClassHeight * static_cast<int>(i)
                            : LastClassesTop + ClassHeight * static_cast<int>(i - FirstOfTheLastClasses);
        layout.classes[i] = Area(x + RightColumn, y + top, ClassWidth, ClassHeight);
    }
    layout.ok = Area(x + RightColumn, y + ButtonsTop, ButtonWidth, ButtonHeight);
    layout.cancel = Area(x + CancelLeft, y + ButtonsTop, ButtonWidth, ButtonHeight);
    layout.nameField =
        Point{Render::Text::DesignX(x + NameFieldLeft, screen), Render::Text::DesignY(y + NameFieldTop, screen)};
    return layout;
}

CreateCharacterWindow::CreateCharacterWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts),
      m_classes{Button(ClassSlot, ClassWidth, ClassHeight, CheckedFrame, DisabledFrame),
                Button(ClassSlot, ClassWidth, ClassHeight, CheckedFrame, DisabledFrame),
                Button(ClassSlot, ClassWidth, ClassHeight, CheckedFrame, DisabledFrame),
                Button(ClassSlot, ClassWidth, ClassHeight, CheckedFrame, DisabledFrame),
                Button(ClassSlot, ClassWidth, ClassHeight, CheckedFrame, DisabledFrame)},
      m_ok(OkSlot, ButtonWidth, ButtonHeight), m_cancel(CancelSlot, ButtonWidth, ButtonHeight), m_name(NameBytes, false)
{
    for (std::size_t i = 0; i < CreatableClassCount; ++i)
    {
        const std::optional<std::string_view> label = TextAt(m_texts, FirstClassText + i);
        m_classes[i].SetLabel(std::string(label.value_or(std::string_view{})), ClassColors);
        m_classes[i].SetChecked(i == m_class);
    }
}

void CreateCharacterWindow::EnableClasses(std::uint8_t creatable)
{
    const std::size_t last = std::min<std::size_t>(creatable, CreatableClassCount - 1);
    for (std::size_t i = 0; i < CreatableClassCount; ++i)
    {
        m_classes[i].SetEnabled(i <= last);
    }
}

void CreateCharacterWindow::Open()
{
    m_open = true;
    m_name.Clear();
}

void CreateCharacterWindow::Close()
{
    m_open = false;
    m_name.Clear();
}

bool CreateCharacterWindow::IsOpen() const
{
    return m_open;
}

std::uint8_t CreateCharacterWindow::ChosenClass() const
{
    return m_class;
}

std::string_view CreateCharacterWindow::Name() const
{
    return m_name.Value();
}

const Button& CreateCharacterWindow::ClassButton(std::size_t index) const
{
    return m_classes.at(index);
}

const Button& CreateCharacterWindow::OkButton() const
{
    return m_ok;
}

void CreateCharacterWindow::StepWithoutTheMouse(const Input::InputState& input)
{
    // Un punto que no está en ningún botón.
    constexpr Point Nowhere{-1, -1};
    for (Button& button : m_classes)
    {
        static_cast<void>(button.Step(input, Nowhere));
    }
    static_cast<void>(m_ok.Step(input, Nowhere));
    static_cast<void>(m_cancel.Step(input, Nowhere));
}

bool CreateCharacterWindow::WantsTextInput() const
{
    return m_open;
}

void CreateCharacterWindow::Choose(std::uint8_t characterClass)
{
    for (std::size_t i = 0; i < CreatableClassCount; ++i)
    {
        m_classes[i].SetChecked(i == characterClass);
    }
    m_class = characterClass;
}

std::optional<CreateWindowEvent> CreateCharacterWindow::Step(const Input::InputState& input, App::PixelSize screen)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    // El subclass del EDIT (0x467b10) descarta Esc y le pasa Enter a la ventana; Tab no es un byte
    // que el campo tome.
    for (const char byte : input.Text())
    {
        if (byte == Input::BackspaceChar)
        {
            m_name.Backspace();
        }
        else if (byte != Input::TabChar)
        {
            m_name.Type(std::string_view(&byte, 1));
        }
    }
    const Point mouse = MouseInPixels(input, screen);
    std::optional<std::uint8_t> clicked;
    for (std::size_t i = 0; i < CreatableClassCount; ++i)
    {
        if (m_classes[i].Step(input, mouse) && !clicked)
        {
            clicked = static_cast<std::uint8_t>(i);
        }
    }
    if (clicked)
    {
        Choose(*clicked);
    }
    const bool ok = m_ok.Step(input, mouse);
    const bool cancel = m_cancel.Step(input, mouse);
    if (ok)
    {
        return CreateWindowEvent::Submitted;
    }
    if (cancel)
    {
        Close();
        return CreateWindowEvent::Cancelled;
    }
    if (input.WasPressed(Input::Key::Enter))
    {
        return CreateWindowEvent::Submitted;
    }
    if (input.WasPressed(Input::Key::Escape))
    {
        Close();
        return CreateWindowEvent::Cancelled;
    }
    return std::nullopt;
}

void CreateCharacterWindow::DrawBase(Render::SpriteBatch& sprites, App::PixelSize screen) const
{
    if (!m_open)
    {
        return;
    }
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.Image(BlackBox(Area(0, 0, screen.width, screen.height), BaseAlpha));
}

void CreateCharacterWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
{
    if (!m_open)
    {
        return;
    }
    Place(screen);
    // 0x620a00(1): la mezcla normal con la textura (la base la apagó).
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    sprites.Image(PixelImage(NameBackSlot, m_layout.nameBack, m_sizes(NameBackSlot)));
    sprites.Image(BlackBox(m_layout.attributes, BoxAlpha));
    sprites.Image(BlackBox(m_layout.strip, BoxAlpha));
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    for (const Button& button : m_classes)
    {
        sprites.Image(button.Image(m_sizes));
        static_cast<void>(button.DrawLabel(text, screen));
    }
    sprites.Image(m_ok.Image(m_sizes));
    sprites.Image(m_cancel.Image(m_sizes));

    const Rect& box = m_layout.attributes;
    const auto drawAttribute = [&](std::string_view value, std::size_t label, int top)
    {
        const int y = Render::Text::DesignY(box.y + top, screen);
        text.Draw(Render::Text::DesignX(box.x + ValueLeft, screen), y, value, ValueStyle);
        if (const std::optional<std::string_view> name = TextAt(m_texts, label); name && !name->empty())
        {
            text.Draw(Render::Text::DesignX(box.x + LabelLeft, screen), y, *name, LabelStyle);
        }
    };
    for (std::size_t i = 0; i < Attributes[m_class].size(); ++i)
    {
        drawAttribute(Attributes[m_class][i], FirstAttributeText + i,
                      FirstAttributeTop + AttributeStep * static_cast<int>(i));
    }
    if (m_class == DarkLord)
    {
        drawAttribute(CommandValue, CommandText, CommandTop);
    }
    if (const std::optional<std::string_view> description = TextAt(m_texts, FirstDescriptionText + m_class);
        description && !description->empty())
    {
        const std::vector<std::string> lines = SplitIntoLines(*description, DescriptionLines, DescriptionBytes);
        for (std::size_t j = 0; j < lines.size(); ++j)
        {
            text.Draw(
                Render::Text::DesignX(m_layout.box.x + DescriptionLeft, screen),
                Render::Text::DesignY(m_layout.box.y + DescriptionTop + DescriptionStep * static_cast<int>(j), screen),
                lines[j], LabelStyle);
        }
    }
    const std::string_view name = m_name.Value();
    if (!name.empty())
    {
        text.Draw(m_layout.nameField.x, m_layout.nameField.y, name, NameStyle);
    }
    const int width = name.empty() ? 0 : text.Measure(NameStyle.font, name).width;
    text.Draw(m_layout.nameField.x + Render::Text::DesignX(width, screen), m_layout.nameField.y, Caret, NameStyle);
}

void CreateCharacterWindow::Place(App::PixelSize screen)
{
    m_layout = LayoutCreateWindow(screen, m_class);
    for (std::size_t i = 0; i < CreatableClassCount; ++i)
    {
        m_classes[i].MoveTo(Point{m_layout.classes[i].x, m_layout.classes[i].y});
    }
    m_ok.MoveTo(Point{m_layout.ok.x, m_layout.ok.y});
    m_cancel.MoveTo(Point{m_layout.cancel.x, m_layout.cancel.y});
}
} // namespace Client::UI
