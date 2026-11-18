#include "UI/ChatLogWindow.h"

#include "UI/ChatLog.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::UI
{
namespace
{
// 0x459f00: el rectángulo común y el que cuelga de su borde de abajo (+0x78 de la lista).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t BottomLeftRect = 2;
// 0x479960: las flechas y la pista repiten desde 16 pasos con el botón apretado.
constexpr std::int32_t RepeatSteps = 0xf;
constexpr std::int32_t ArrowSize = 0xd;
// 0x45cbb0 y 0x45dec0: los renglones, de 13 desde 0x10 sobre el borde de abajo, el nombre en x + 10.
constexpr std::int32_t RowHeight = 0xd;
constexpr std::int32_t RowsBottom = 0x10;
constexpr std::int32_t NameX = 10;
// 0x479960: el alto con el borde agarrado, de 40 en 40 píxeles y de 3 en 3 renglones.
constexpr std::int32_t ResizeStep = 0x28;
constexpr std::int32_t ResizeSlack = 5;
constexpr std::int32_t MinRows = 3;
constexpr std::int32_t MaxRows = 0x1e;
// 0x45d760: el rectángulo donde aparecen los botones y la altura bajo la que se van.
constexpr std::int32_t ButtonsAreaX = 0xa0;
constexpr std::int32_t ButtonsAreaY = 0x1b4;
constexpr std::int32_t ButtonsAreaWidth = 0x140;
constexpr std::int32_t ButtonsAreaHeight = 0x32;
constexpr std::int32_t ButtonsHideY = 0x1a0;
// 0x6c7a90, 0x6c7a94 y 0x6c7a98: el primer botón y el paso entre ellos; 3,0 en 0x6b86b8.
constexpr float ButtonsX = 295.0f;
constexpr float ButtonsY = 417.0f;
constexpr float ButtonsStep = 18.0f;
constexpr float ButtonCount = 3.0f;
constexpr std::int32_t ButtonSize = 0x10;
// 0x45d990: la transparencia del fondo sube de a 0,2 (0x6b8854) y pasada 0,9 (0x6b8a1c) vuelve a 0,2.
constexpr float AlphaStep = 0.2f;
constexpr float AlphaTop = 0.9f;
// 0x47a230 y 0x479960: el cursor no baja de 2 (0x6b8818) y la cuenta del arrastre suma 0,5 (0x6b882c).
constexpr float MinThumb = 2.0f;
constexpr double DragHalf = 0.5;

// _ftol (0x69fd3c): trunca hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

bool InRect(const ChatLogStepView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
            std::int32_t mode)
{
    return MouseInRect(view.mouseX, view.mouseY, x, y, width, height, mode);
}

// 0x479960: una flecha o la pista con el botón apretado. En 0 corre y empieza la cuenta, hasta 15 la sube (la
// lista no tiene padre, +0x44 en 0) y desde 16 corre en cada paso.
void Repeat(ChatLogState& log, ChatModes modes, std::int32_t& counter, std::int32_t lines,
            std::optional<std::int32_t> sound, ChatLogStepResult& result)
{
    if (counter == 0)
    {
        ScrollTheLog(log, modes, lines);
        if (sound)
            result.sounds.push_back(*sound);
        counter = 1;
    }
    else if (counter <= RepeatSteps)
        ++counter;
    else
        ScrollTheLog(log, modes, lines);
}

// 0x45d760, la vtable + 0x78: el clic derecho sobre el elegido, la elección en 0, los botones a la vista y si el mouse
// está sobre la barra o sobre los botones.
bool OverTheLog(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                const ChatLogStepView& view, ChatLogStepResult& result)
{
    if (log.selection != 0 && globals.rightDown)
    {
        // 0x6c7b8c es 1 en este main.exe: el camino del 0 (el nombre en 0x7798cf8) no corre.
        const std::size_t at = SelectedInTheLog(log, globals.modes);
        if (at < log.entries.size())
            result.whisperTo = NameOf(log.entries[at]);
        globals.rightDown = false;
    }
    SelectInTheLog(log, globals.modes, 0);
    if (InRect(view, ButtonsAreaX, ButtonsAreaY, ButtonsAreaWidth, ButtonsAreaHeight, TopLeftRect))
        log.buttonsShown = 1;
    else if (view.mouseY < ButtonsHideY)
        log.buttonsShown = 0;
    if (InRect(view, frame.width - 0x1e + frame.x, frame.y - 6, 0x21, frame.height - 2, BottomLeftRect))
        return true;
    return log.buttonsShown == 1 && InRect(view, Truncate(ButtonsX), Truncate(ButtonsY),
                                           Truncate(ButtonsStep * ButtonCount), ButtonSize, TopLeftRect);
}

// 0x45d990, la vtable + 0x8c: el borde de arriba y los tres botones (F2, el tamaño y la transparencia).
void GrabOrPress(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                 const ChatLogStepView& view, ChatLogStepResult& result)
{
    if (InRect(view, frame.x, frame.y - frame.height, frame.width, 8, TopLeftRect) && globals.hovered == ChatLogId &&
        log.state == 0 && globals.capture == 0)
    {
        globals.capture = ChatLogId;
        log.state = 1;
        result.sounds.push_back(ChatButtonSound);
    }
    if (log.buttonsShown == 0 || view.chatOpen || globals.hovered != 0)
        return;
    const auto button = [&](float x)
    { return InRect(view, Truncate(x), Truncate(ButtonsY), ButtonSize, ButtonSize, TopLeftRect); };
    if (button(ButtonsX))
    {
        globals.modes.showChat = !globals.modes.showChat;
        result.sounds.push_back(ChatButtonSound);
        ScrollTheLog(log, globals.modes, 0);
        globals.leftClick = false;
    }
    if (globals.hovered != 0)
        return;
    // 0x45da90: 18 + 295 en el x87.
    if (button(ButtonsStep + ButtonsX))
    {
        CycleTheLogSize(log, globals.modes);
        result.sounds.push_back(ChatButtonSound);
        globals.leftClick = false;
    }
    if (globals.hovered != 0)
        return;
    // 0x45dae5: 18 + 18 + 295.
    if (button(ButtonsStep + ButtonsStep + ButtonsX))
    {
        // FST guarda la suma en float y FCOMP compara la que quedó en la pila, sin redondear.
        const double alpha = static_cast<double>(log.alpha) + static_cast<double>(AlphaStep);
        log.alpha = static_cast<float>(alpha);
        if (alpha > static_cast<double>(AlphaTop))
            log.alpha = AlphaStep;
        result.sounds.push_back(ChatButtonSound);
        globals.leftClick = false;
    }
}

// 0x45dec0, la vtable + 0x88: el mouse sobre el nombre de un renglón. Devuelve lo que corre el renglón del que llama:
// 0 lo repite (el renglón no se ve), negativo lo corre de más y positivo deja el paso normal.
std::int32_t HoverTheRow(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                         const ChatLogStepView& view, std::size_t at, std::int32_t row)
{
    ChatEntry& entry = log.entries[at];
    const ChatModes modes = globals.modes;
    // Con el registro en su ventana y en x = 0 (debajo de otra ventana), un renglón más arriba.
    if (modes.placement == 1 && frame.x == 0)
    {
        ++row;
        if (log.rows <= row)
            return 1;
    }
    if (!modes.showChat && entry.type == 3)
        return 0;
    if (modes.placement == 1)
    {
        if (entry.type == 1 || entry.type == 2)
            return 0;
    }
    else if (modes.placement == 0 && entry.age != 0 && log.scroll == 0)
    {
        const auto age = static_cast<std::int32_t>(entry.age);
        if (log.rows - age - row - 1 < 0)
            return row - log.rows;
        row += age;
    }
    // 0x6c7b8c en 1: sin la zona segura o sin el foco en el chat, nada.
    if (!view.heroSafeZone || !view.chatEditFocused || view.measure == nullptr)
        return 1;
    const std::string name = NameOf(entry);
    // GetTextExtentPointA en píxeles de la pantalla, sin pasar a 640 × 480.
    const std::int32_t nameWidth = view.measure->Measure(view.dcFont, name).width;
    if (InRect(view, frame.x + NameX, frame.y - row * RowHeight - RowsBottom, nameWidth, RowHeight, TopLeftRect) &&
        name != view.heroName)
    {
        SelectInTheLog(log, modes, log.scroll + 1 + row);
        // (+0 + 1) % 2 con el signo de C.
        entry.blink = static_cast<std::int32_t>(static_cast<std::uint32_t>(entry.blink) + 1u) % 2;
    }
    return 1;
}

// 0x479f84..0x47a03d: desde el primero que se ve, hasta `rows` renglones o el final. 0x5762ebc es siempre 0.
void HoverTheRows(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                  const ChatLogStepView& view)
{
    std::size_t at = FirstShownInTheLog(log, globals.modes);
    for (std::int32_t row = 0; row < log.rows; ++row)
    {
        if (at >= log.entries.size())
            return;
        const std::int32_t moved = HoverTheRow(log, frame, globals, view, at, row);
        if (moved < 0)
            row -= moved;
        else if (moved == 0)
            --row;
        ++at;
    }
}

// 0x479960, la vtable + 0x20.
bool StepTheLogMouse(ChatLogState& log, ChatLogFrame& frame, const ChatLogGlobals& globals, const ChatLogStepView& view,
                     ChatLogStepResult& result)
{
    bool handled = false;
    // 0x479974..0x479a33: las flechas del teclado solo con +0xbc en 1, que en la lista es 0.
    if (OverTheLog(log, frame, globals, view, result))
    {
        if (globals.wheel != 0)
        {
            ScrollTheLog(log, globals.modes, globals.wheel * -3);
            globals.wheel = 0;
        }
        if (!globals.leftClick || log.state != 0)
        {
            log.arrowRepeat = 0;
            log.trackRepeat = 0;
        }
        else
        {
            globals.focusedList = ChatLogId;
            GrabOrPress(log, frame, globals, view, result);
            const std::int32_t inset = log.barMode == 1 ? 8 : 0;
            const std::int32_t margin = log.barMode != 1 ? 8 : 0;
            const std::int32_t arrowX = frame.x + inset - 0x15 + frame.width;
            if (InRect(view, arrowX, frame.y - frame.height + margin, ArrowSize, ArrowSize, TopLeftRect))
                Repeat(log, globals.modes, log.arrowRepeat, -1, ChatButtonSound, result);
            if (InRect(view, arrowX, frame.y - (margin + ArrowSize), ArrowSize, ArrowSize, TopLeftRect))
                Repeat(log, globals.modes, log.arrowRepeat, 1, ChatButtonSound, result);
            if (log.rows <= VisibleInTheLog(log, globals.modes))
            {
                const std::int32_t barX = frame.x + inset - 0x13 + frame.width;
                const std::int32_t thumbWidth = Truncate(frame.thumbWidth);
                if (InRect(view, barX, Truncate(frame.thumbY), thumbWidth, Truncate(frame.thumbHeight), TopLeftRect))
                {
                    if (log.state == 0 && globals.capture == 0)
                    {
                        globals.capture = ChatLogId;
                        log.state = 2;
                        log.dragOffset =
                            static_cast<float>(static_cast<double>(view.mouseY) - static_cast<double>(frame.thumbY));
                    }
                }
                else if (InRect(view, barX, Truncate(frame.barTop), thumbWidth,
                                Truncate(static_cast<double>(frame.thumbY) - static_cast<double>(frame.barTop)),
                                TopLeftRect))
                    Repeat(log, globals.modes, log.trackRepeat, -log.rows, std::nullopt, result);
                else if (InRect(view, barX,
                                Truncate(static_cast<double>(frame.thumbHeight) + static_cast<double>(frame.thumbY)),
                                thumbWidth,
                                Truncate(static_cast<double>(frame.barBottom) - static_cast<double>(frame.thumbY) -
                                         static_cast<double>(frame.thumbHeight)),
                                TopLeftRect))
                    Repeat(log, globals.modes, log.trackRepeat, log.rows, std::nullopt, result);
            }
        }
        globals.mouseOnUi = 1;
        handled = true;
    }
    if (log.state == 1)
    {
        if (!globals.leftClick)
        {
            log.state = 0;
            if (globals.capture == ChatLogId)
                globals.capture = 0;
        }
        else
        {
            globals.mouseOnUi = 1;
            log.rows = ((frame.y - view.mouseY + ResizeSlack) / ResizeStep) * 3;
            if (log.rows < MinRows)
                log.rows = MinRows;
            else if (log.rows > MaxRows)
                log.rows = MaxRows;
            const std::int32_t visible = VisibleInTheLog(log, globals.modes);
            if (visible >= log.rows && visible - log.scroll < log.rows)
                log.scroll = visible - log.rows;
        }
    }
    else if (log.state == 2)
    {
        if (!globals.leftClick)
        {
            log.state = 0;
            if (globals.capture == ChatLogId)
                globals.capture = 0;
        }
        else
        {
            globals.mouseOnUi = 1;
            // FILD, FSUB del float y FST: la cuenta sigue con el valor sin redondear y el cursor queda en float.
            const double top = static_cast<double>(view.mouseY) - static_cast<double>(log.dragOffset);
            frame.thumbY = static_cast<float>(top);
            const std::int32_t visible = VisibleInTheLog(log, globals.modes);
            if (top < static_cast<double>(frame.barTop))
                log.scroll = visible - log.rows;
            else if (top <= static_cast<double>(frame.barBottom) - static_cast<double>(frame.thumbHeight))
            {
                const double small = frame.thumbHeight <= MinThumb ? 1.0 : 0.0;
                const double below = static_cast<double>(frame.barBottom) - static_cast<double>(frame.thumbY) -
                                     static_cast<double>(frame.thumbHeight) + DragHalf;
                const double track = static_cast<double>(frame.barBottom) - static_cast<double>(frame.barTop) - small;
                log.scroll = Truncate(static_cast<double>(visible) * (below / track));
            }
            else
                log.scroll = 0;
        }
    }
    HoverTheRows(log, frame, globals, view);
    return handled;
}
} // namespace

bool MouseInRect(std::int32_t mouseX, std::int32_t mouseY, std::int32_t x, std::int32_t y, std::int32_t width,
                 std::int32_t height, std::int32_t mode)
{
    if (x > mouseX || mouseX >= x + width)
        return false;
    if (mode == BottomLeftRect)
        return y - height <= mouseY && mouseY < y;
    return y <= mouseY && mouseY < y + height;
}

OldListBar PlaceTheListBar(std::int32_t y, std::int32_t height, std::int32_t barMode, std::int32_t rows,
                           std::int32_t count, std::int32_t scroll)
{
    OldListBar bar;
    if (barMode == 1)
    {
        bar.barTop = static_cast<float>(y - height + 0xc);
        bar.barBottom = static_cast<float>(y - 0xc);
    }
    else
    {
        bar.barTop = static_cast<float>(y - height + 0x15);
        bar.barBottom = static_cast<float>(y - 0x11);
    }
    double ratio = 1.0;
    if (count > 0)
    {
        // FILD de los renglones guardado en float y FDIVR con el FILD de los que se ven.
        const double shown = static_cast<double>(static_cast<float>(rows)) / static_cast<double>(count);
        if (shown < 1.0)
            ratio = shown;
    }
    const double thumb = (static_cast<double>(bar.barBottom) - static_cast<double>(bar.barTop)) * ratio;
    bar.thumbHeight = static_cast<float>(thumb);
    if (thumb < static_cast<double>(MinThumb))
        bar.thumbHeight = MinThumb;
    double scrolled = 0.0;
    if (count > 0)
        scrolled = static_cast<double>(static_cast<float>(scroll)) / static_cast<double>(count);
    const double small = bar.thumbHeight <= MinThumb ? static_cast<double>(bar.thumbHeight) : 0.0;
    const double track = static_cast<double>(bar.barBottom) - static_cast<double>(bar.barTop) - small;
    bar.thumbY = static_cast<float>(static_cast<double>(bar.barBottom) - track * scrolled -
                                    static_cast<double>(bar.thumbHeight));
    return bar;
}

void PlaceTheLogBar(const ChatLogState& log, ChatModes modes, ChatLogFrame& frame)
{
    const OldListBar bar =
        PlaceTheListBar(frame.y, frame.height, log.barMode, log.rows, VisibleInTheLog(log, modes), log.scroll);
    frame.barTop = bar.barTop;
    frame.barBottom = bar.barBottom;
    frame.thumbY = bar.thumbY;
    frame.thumbHeight = bar.thumbHeight;
}

ChatLogStepResult StepTheChatLog(ChatLogState& log, ChatLogFrame& frame, const ChatLogGlobals& globals,
                                 const ChatLogStepView& view)
{
    ChatLogStepResult result;
    // 0x45aa8c..0x45aab1: la cola de mensajes de la ventana (+0x30) está vacía: nadie le manda nada a la lista. La
    // vtable + 0x1c es 0x403160, un ret 4.
    if (InRect(view, frame.x, frame.y, frame.width, frame.height, BottomLeftRect))
    {
        if (globals.hovered == 0)
        {
            if (globals.capture != 0 && globals.capture != ChatLogId)
                return result;
            globals.hovered = ChatLogId;
        }
        if (log.state == 0 && globals.hovered != ChatLogId && globals.capture != ChatLogId)
            return result;
        // 0x45ab18..0x45abb3: con el izquierdo abajo, 0x490aa0 sobre el administrador de ventanas (0x5763514) busca
        // la lista para traerla al frente; WinMain no la registra ahí (0x4bb3e5), así que no hace nada (*inferido*).
    }
    result.handled = StepTheLogMouse(log, frame, globals, view, result);
    return result;
}
} // namespace Client::UI
