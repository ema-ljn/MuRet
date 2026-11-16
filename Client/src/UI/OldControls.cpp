#include "UI/OldControls.h"

#include "UI/ChatLogWindow.h"
#include "UI/OldWindowBase.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// 0x459f00: el rectángulo común y el que cuelga de su borde de abajo (+0x78 de la lista, 0x47bb80).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t BottomLeftRect = 2;
// 0x47b0f0: las flechas de la barra, de 13 × 13; repiten desde 16 pasos con el botón apretado.
constexpr std::int32_t ArrowSize = 0xd;
constexpr std::int32_t RepeatSteps = 0xf;
// 0x47b0f0: las flechas del teclado repiten desde 6 pasos.
constexpr std::int32_t KeyRepeatSteps = 5;
// 0x47b0f0: GetAsyncKeyState de las flechas de arriba y de abajo (las de los costados se leen y no se usan).
constexpr std::uint8_t UpKey = 0x26;
constexpr std::uint8_t DownKey = 0x28;
// 0x41ed10(0x19, 0, 0).
constexpr std::int32_t ButtonSound = 0x19;
// 0x47b0f0: el alto con el borde agarrado, de 40 en 40 píxeles y de 3 en 3 renglones.
constexpr std::int32_t ResizeStep = 0x28;
constexpr std::int32_t ResizeSlack = 5;
constexpr std::int32_t MinRows = 3;
constexpr std::int32_t MaxRows = 0x1e;
// 0x47a9d0: el 9 cuenta los renglones con 1/13 (0x6b9bf4, float) y 0,5 (0x6b882c).
constexpr float RowsPerPixel = 1.0f / 13.0f;
constexpr float Half = 0.5f;
// 0x47a9d0: el 0xe corre −1000 y elige el 1000.
constexpr std::int32_t FarAway = 1000;
// 0x461c70: los renglones de 13, desde 10 sobre el borde de arriba del último.
constexpr std::int32_t RowHeight = 0xd;
constexpr std::int32_t RowsTop = 10;
// 0x47b9c0 y 0x47b0f0: el cursor no baja de 2 (0x6b8818) y la cuenta del arrastre suma 0,5 (0x6b882c).
constexpr float MinThumb = 2.0f;
constexpr double DragHalf = 0.5;

// _ftol (0x69fd3c): trunca hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

bool InRect(const OldListView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
            std::int32_t mode)
{
    return MouseInRect(view.mouseX, view.mouseY, x, y, width, height, mode);
}

// 0x459f70 sobre la misma lista.
void PostToItself(OldList& list, std::int32_t type)
{
    list.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
}

// 0x47b11f..0x47b1c8: con la lista que toma las flechas, una flecha apretada se manda su mensaje: al apretarla, a los
// 6 pasos y desde ahí en cada paso. Soltar las dos vuelve la cuenta a 0.
void ReadTheArrows(OldList& list, const OldListGlobals& globals, const OldListView& view)
{
    if (list.keyboard != 1 || globals.focusedList != list.base.id || !view.keyDown)
        return;
    bool up = false;
    if (view.keyDown(UpKey))
    {
        up = true;
        if (list.keyRepeat == 0)
        {
            PostToItself(list, ArrowUpMessage);
            list.keyRepeat = 1;
        }
        else if (list.keyRepeat > KeyRepeatSteps)
            PostToItself(list, ArrowUpMessage);
        else
            ++list.keyRepeat;
    }
    if (view.keyDown(DownKey))
    {
        if (list.keyRepeat == 0)
        {
            PostToItself(list, ArrowDownMessage);
            list.keyRepeat = 1;
        }
        else if (list.keyRepeat > KeyRepeatSteps)
            PostToItself(list, ArrowDownMessage);
        else
            ++list.keyRepeat;
    }
    else if (!up)
        list.keyRepeat = 0;
}

// Las cuentas de una lista con padre solo suben con el administrador ya dibujado (0x47b2b0..0x47b2c2).
bool MayCount(const OldList& list, const OldListGlobals& globals)
{
    return list.base.parent == 0 || globals.drawn;
}

// 0x47b277..0x47b2c3: una flecha de la barra. En 0 corre con el sonido y empieza la cuenta, hasta 15 la sube y desde
// 16 corre en cada paso.
void RepeatTheArrow(OldList& list, std::size_t size, const OldListGlobals& globals, std::int32_t lines)
{
    if (list.arrowRepeat == 0)
    {
        ScrollTheList(list, size, lines);
        globals.sound = ButtonSound;
        list.arrowRepeat = 1;
    }
    else if (list.arrowRepeat > RepeatSteps)
        ScrollTheList(list, size, lines);
    else if (MayCount(list, globals))
        ++list.arrowRepeat;
}

// 0x47b427..0x47b48c: la pista. Con padre y sin dibujo, nada; si no, como las flechas sin sonido.
void RepeatTheTrack(OldList& list, std::size_t size, const OldListGlobals& globals, std::int32_t lines)
{
    if (!MayCount(list, globals))
        return;
    if (list.trackRepeat == 0)
    {
        ScrollTheList(list, size, lines);
        list.trackRepeat = 1;
    }
    else if (list.trackRepeat > RepeatSteps)
        ScrollTheList(list, size, lines);
    else
        ++list.trackRepeat;
}

// 0x47b201..0x47b545: el clic sobre la lista, con ella quieta: el foco de las flechas, las dos flechas y, con tantos
// como renglones o más, la barra y la pista.
void PressTheBar(OldList& list, std::size_t size, const OldListGlobals& globals, const OldListView& view)
{
    OldWindowBase& base = list.base;
    globals.focusedList = base.id;
    // La vtable + 0x8c de las listas de la CL9.4 es 0x472120, que da 1 sin hacer nada.
    const std::int32_t inset = list.barMode == 1 ? 8 : 0;
    const std::int32_t margin = list.barMode != 1 ? 8 : 0;
    const std::int32_t arrowX = base.x + inset - 0x15 + base.width;
    if (InRect(view, arrowX, base.y - base.height + margin, ArrowSize, ArrowSize, TopLeftRect))
        RepeatTheArrow(list, size, globals, -1);
    if (InRect(view, arrowX, base.y - (margin + ArrowSize), ArrowSize, ArrowSize, TopLeftRect))
        RepeatTheArrow(list, size, globals, 1);
    if (ListCount(list, size) < list.rows)
        return;
    const std::int32_t barX = base.x + inset - 0x13 + base.width;
    const std::int32_t thumbWidth = Truncate(list.thumbWidth);
    if (InRect(view, barX, Truncate(list.thumbY), thumbWidth, Truncate(list.thumbHeight), TopLeftRect))
    {
        if (base.state != ShownWindow || globals.capture != 0)
            return;
        globals.capture = base.id;
        base.state = 2;
        list.dragOffset = static_cast<float>(static_cast<double>(view.mouseY) - static_cast<double>(list.thumbY));
        return;
    }
    if (InRect(view, barX, Truncate(list.barTop), thumbWidth,
               Truncate(static_cast<double>(list.thumbY) - static_cast<double>(list.barTop)), TopLeftRect))
    {
        RepeatTheTrack(list, size, globals, -list.rows);
        return;
    }
    if (InRect(view, barX, Truncate(static_cast<double>(list.thumbHeight) + static_cast<double>(list.thumbY)),
               thumbWidth,
               Truncate(static_cast<double>(list.barBottom) - static_cast<double>(list.thumbY) -
                        static_cast<double>(list.thumbHeight)),
               TopLeftRect))
        RepeatTheTrack(list, size, globals, list.rows);
}

// La vtable + 4 (0x45a9b0) con 0 y soltar el mouse si lo tenía ella.
void LetGo(OldList& list, const OldListGlobals& globals)
{
    list.base.state = ShownWindow;
    if (globals.capture == list.base.id)
        globals.capture = 0;
}
} // namespace

std::int32_t ListCount(const OldList& list, std::size_t size)
{
    return list.fixedCount == 1 ? list.fixedCountValue : static_cast<std::int32_t>(size);
}

void ScrollTheList(OldList& list, std::size_t size, std::int32_t lines)
{
    const std::int32_t count = ListCount(list, size);
    // CMP sin signo (JC).
    if (static_cast<std::uint32_t>(count) < static_cast<std::uint32_t>(list.rows))
        return;
    list.scroll -= lines;
    if (list.scroll < 0)
    {
        list.scroll = 0;
        return;
    }
    if (list.scroll > count - list.rows)
        list.scroll = count - list.rows;
}

void SetTheListRows(OldList& list, std::size_t size, std::int32_t rows)
{
    const std::int32_t count = ListCount(list, size);
    if (rows < list.rows && rows < count)
    {
        list.rows = rows;
        ++list.scroll;
        return;
    }
    if (rows > count)
        list.scroll = 0;
    list.rows = rows;
}

void GrowTheListRows(OldList& list, std::int32_t steps)
{
    list.rows += steps * 3;
    if (list.rows < MinRows)
        list.rows = MinRows;
}

void SelectInTheList(OldList& list, std::size_t size, std::int32_t selection)
{
    list.selection = selection;
    const std::int32_t count = ListCount(list, size);
    if (list.selection > count)
        list.selection = count;
}

void SelectTheNext(OldList& list, std::size_t size, std::int32_t steps)
{
    if (size == 0)
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection += steps;
    const std::int32_t count = ListCount(list, size);
    if (list.selection > count)
        list.selection = count;
}

void SelectThePrevious(OldList& list, std::size_t size, std::int32_t steps)
{
    if (size == 0)
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection -= steps;
    if (list.selection < 1)
        list.selection = 1;
}

std::optional<std::size_t> SelectedInTheList(OldList& list, std::size_t size)
{
    // 0x47afe3: sin elementos, la elección vuelve a 0.
    if (size == 0)
    {
        list.selection = 0;
        return std::nullopt;
    }
    if (list.selection <= 0)
        return std::nullopt;
    const auto at = static_cast<std::size_t>(list.selection) - 1;
    if (at >= size)
        return std::nullopt;
    return at;
}

std::size_t FirstShownInTheList(const OldList& list)
{
    return list.scroll < 0 ? 0 : static_cast<std::size_t>(list.scroll);
}

std::int32_t ListRowY(const OldList& list, std::size_t size, std::int32_t row)
{
    const std::int32_t count = ListCount(list, size);
    const std::int32_t shown = list.rows < count ? list.rows : count;
    return (shown - row) * RowHeight - list.base.height - RowsTop + list.base.y;
}

void HandleTheListMessage(OldList& list, std::size_t size)
{
    const std::int32_t type = list.base.current.type;
    switch (type)
    {
    case SizeMessage:
    {
        // La vtable + 0x90 de las listas de la CL9.4 es 0x472130, un ret.
        if (list.base.parent == 0)
            return;
        const std::int32_t before = list.rows;
        // FILD del alto, FMUL y FSUB de los dos float en la x87 y _ftol.
        const double rows =
            static_cast<double>(list.base.height) * static_cast<double>(RowsPerPixel) - static_cast<double>(Half);
        SetTheListRows(list, size, Truncate(rows));
        if (list.keepBottom == 1 &&
            static_cast<std::uint32_t>(list.fixedCountValue) >= static_cast<std::uint32_t>(list.rows))
            list.scroll += before - list.rows;
        ScrollTheList(list, size, 0);
        return;
    }
    case ToTheEndMessage:
        ScrollTheList(list, size, -FarAway);
        if (list.keyboard == 1 && size != 0)
            SelectInTheList(list, size, FarAway);
        return;
    case ArrowUpMessage:
    case ArrowDownMessage:
    {
        if (list.keyboard != 1 || size < 1)
            return;
        if (type == ArrowUpMessage)
            SelectTheNext(list, size, 1);
        else
            SelectThePrevious(list, size, 1);
        if (size <= static_cast<std::size_t>(list.rows < 0 ? 0 : list.rows))
            return;
        // La vtable + 0x68 (0x472100) da +0xc4.
        const std::int32_t selected = list.selection - 1;
        if (selected >= list.scroll + list.rows)
            ScrollTheList(list, size, list.scroll - (selected - list.rows + 1));
        else if (selected < list.scroll)
            ScrollTheList(list, size, list.scroll - selected);
        return;
    }
    default:
        return;
    }
}

void PlaceTheListBar(OldList& list, std::size_t size)
{
    const OldWindowBase& base = list.base;
    if (list.barMode == 1)
    {
        list.barTop = static_cast<float>(base.y - base.height + 0xc);
        list.barBottom = static_cast<float>(base.y - 0xc);
    }
    else
    {
        list.barTop = static_cast<float>(base.y - base.height + 0x15);
        list.barBottom = static_cast<float>(base.y - 0x11);
    }
    const std::int32_t count = ListCount(list, size);
    double ratio = 1.0;
    if (count > 0)
    {
        // FILD de los renglones guardado en float y FDIVR con el FILD de la cuenta.
        const double shown = static_cast<double>(static_cast<float>(list.rows)) / static_cast<double>(count);
        if (shown < 1.0)
            ratio = shown;
    }
    const double thumb = (static_cast<double>(list.barBottom) - static_cast<double>(list.barTop)) * ratio;
    list.thumbHeight = static_cast<float>(thumb);
    if (thumb < static_cast<double>(MinThumb))
        list.thumbHeight = MinThumb;
    double scrolled = 0.0;
    if (count > 0)
        scrolled = static_cast<double>(static_cast<float>(list.scroll)) / static_cast<double>(count);
    const double small = list.thumbHeight <= MinThumb ? static_cast<double>(list.thumbHeight) : 0.0;
    const double track = static_cast<double>(list.barBottom) - static_cast<double>(list.barTop) - small;
    list.thumbY = static_cast<float>(static_cast<double>(list.barBottom) - track * scrolled -
                                     static_cast<double>(list.thumbHeight));
}

bool StepTheListMouse(OldList& list, std::size_t size, const OldListGlobals& globals, const OldListView& view,
                      const OldListRowHover& hover)
{
    OldWindowBase& base = list.base;
    bool handled = false;
    ReadTheArrows(list, globals, view);
    if (InRect(view, base.x, base.y, base.width, base.height, BottomLeftRect))
    {
        if (globals.wheel != 0)
        {
            ScrollTheList(list, size, globals.wheel * -3);
            globals.wheel = 0;
        }
        if (!globals.leftClick || base.state != ShownWindow)
        {
            list.arrowRepeat = 0;
            list.trackRepeat = 0;
        }
        else
            PressTheBar(list, size, globals, view);
        globals.mouseOnUi = 1;
        handled = true;
    }
    if (base.state == 1)
    {
        if (!globals.leftClick)
            LetGo(list, globals);
        else
        {
            globals.mouseOnUi = 1;
            list.rows = ((base.y - view.mouseY + ResizeSlack) / ResizeStep) * 3;
            if (list.rows < MinRows)
                list.rows = MinRows;
            else if (list.rows > MaxRows)
                list.rows = MaxRows;
            const std::int32_t count = ListCount(list, size);
            if (count >= list.rows && count - list.scroll < list.rows)
                list.scroll = count - list.rows;
        }
    }
    else if (base.state == 2)
    {
        if (!globals.leftClick)
            LetGo(list, globals);
        else
        {
            globals.mouseOnUi = 1;
            // FILD, FSUB del float y FST: la cuenta sigue con el valor sin redondear y el cursor queda en float.
            const double top = static_cast<double>(view.mouseY) - static_cast<double>(list.dragOffset);
            list.thumbY = static_cast<float>(top);
            const std::int32_t count = ListCount(list, size);
            if (top < static_cast<double>(list.barTop))
                list.scroll = count - list.rows;
            else if (top <= static_cast<double>(list.barBottom) - static_cast<double>(list.thumbHeight))
            {
                const double small = list.thumbHeight <= MinThumb ? 1.0 : 0.0;
                const double below = static_cast<double>(list.barBottom) - static_cast<double>(list.thumbY) -
                                     static_cast<double>(list.thumbHeight) + DragHalf;
                const double track = static_cast<double>(list.barBottom) - static_cast<double>(list.barTop) - small;
                list.scroll = Truncate(static_cast<double>(count) * (below / track));
            }
            else
                list.scroll = 0;
        }
    }
    // 0x47b714..0x47b7c6: desde el primero que se ve (la vtable + 0x74), hasta los renglones o el final.
    if (globals.modal != 0 || !hover)
        return handled;
    std::size_t at = FirstShownInTheList(list);
    for (std::int32_t row = 0; row < list.rows; ++row)
    {
        if (at >= size)
            break;
        const std::int32_t moved = hover(row, at);
        if (moved < 0)
            row -= moved;
        else if (moved == 0)
            --row;
        ++at;
    }
    return handled;
}

std::optional<char> FilteredByTheEdit(std::int32_t filter, char typed)
{
    constexpr char Escape = 0x1b;
    constexpr char FirstIgnored = 0x19;
    constexpr char LastIgnored = 0x1c;
    if (typed == Escape || (typed >= FirstIgnored && typed <= LastIgnored))
        return std::nullopt;
    const bool digit = typed >= '0' && typed <= '9';
    if ((filter & DigitsOnlyEdit) != 0)
        return digit ? std::optional<char>(typed) : std::nullopt;
    if ((filter & UppercaseLettersAndDigitsEdit) == 0 || digit || (typed >= 'A' && typed <= 'Z'))
        return typed;
    if (typed < 'a' || typed > 'z')
        return std::nullopt;
    return static_cast<char>(typed - ('a' - 'A'));
}

void TypeInTheEdit(OldEdit& edit, std::string_view typed)
{
    if ((edit.filter & (DigitsOnlyEdit | UppercaseLettersAndDigitsEdit)) == 0)
    {
        edit.text.Type(typed);
        return;
    }
    std::string passed;
    for (const char letter : typed)
    {
        if (const std::optional<char> kept = FilteredByTheEdit(edit.filter, letter))
            passed.push_back(*kept);
    }
    edit.text.Type(passed);
}
} // namespace Client::UI
