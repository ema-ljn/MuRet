#pragma once

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// The new UI's manager (CL8, milestone 8; 06 § Las ventanas de la UI nueva): the object at 0x7d53728 (0x65ee60) with
// its doubly linked window list, the head at +0x8ddc, the tail at +0x8de0 and the count at +0x8de4. The head is on
// top: 0x65fe00 draws from the tail to the head, and 0x65fc30 gives the mouse to the first from the head that has it
// inside. Opening one (0x65f690 → 0x65f610) moves it to the head with the focus (+0xd), and closing it (0x65f6c0)
// moves it to the tail and gives the focus to the new head, if open: only the one with the focus runs its logic
// (+0x30, the clicks and the keys), and since 0x65f610 gives the focus after taking it from the head, only one has
// it.
//
// The windows belong to the scene: the manager keeps for each what in the original belongs to 0x669c70's base and
// 0x65fc30 reads (the focus, +0xd; the drag, +0x10, +4, +8, +0x28 and +0x2c; and whether it got stuck, +0xe), and
// asks the window whether it is open (+0xc), its rectangle (+0x18..+0x24) and the rest of its vtable. The original
// runs it once per frame, before that frame's steps (0x63778c, with the frame's milliseconds, up to 200); MuRet, like
// the rest of the UI (Q-C81), at the start of each scene step, with the step's 40 ms (Q-C490).
namespace Client::Ui
{
// What vtable + 0xc (0x669df0) checks: the whole rectangle (0) or the title, the top 26 pixels (2), which is what is
// grabbed to drag.
enum class WindowHit : std::uint8_t
{
    Area,
    Title,
};

// What 0x669f40 does with an open window in a frame. `buttons`: +0x10 at 0, not dragged, it handles its buttons
// (0x63c490). `mouse`: its buttons' +0xa9 (0x669ed0), 1 only in the first from the head with the mouse inside; with
// 0, no button is under the mouse (0x63c470). `logic`: it has the focus (+0xd) and runs its +0x30. The login and
// selection ones are not dragged, so for them `buttons` is always true. A window that handles itself (its tests, for
// example) does so with everything true.
struct NewUiTurn
{
    bool buttons = true;
    bool mouse = true;
    bool logic = true;
    std::function<void()> buttonClicked{};
};

// The mouse a window's buttons see in its turn: the new UI's (MouseInPixels), or none with `mouse` false.
[[nodiscard]] std::optional<Point> MouseOfTheTurn(NewUiTurn turn, const Input::InputState& input,
                                                  Core::PixelSize screen);

// As the manager sees it.
class NewUiWindow
{
public:
    NewUiWindow() = default;
    virtual ~NewUiWindow() = default;

    // +0xc.
    [[nodiscard]] virtual bool IsOpen() const = 0;
    // vtable + 0x10 with 0.
    virtual void Close() = 0;
    // +0x18..+0x24: the corner and the size, in pixels.
    [[nodiscard]] virtual Rect Bounds(Core::PixelSize screen) const = 0;
    // vtable + 0xc. All the login and selection ones (0x647d10 the Esc one, and the rest alike) answer no to the title
    // and call 0x669df0 with the rest: they are not dragged.
    [[nodiscard]] virtual bool Hit(WindowHit hit, Point mouse, Core::PixelSize screen) const;
    // vtable + 8: the drag moves it to `topLeft`. The base only moves the rectangle; the login and selection ones are
    // not dragged, and it does nothing.
    virtual void PlaceAt(Point topLeft, Core::PixelSize screen);

protected:
    NewUiWindow(const NewUiWindow&) = default;
    NewUiWindow(NewUiWindow&&) = default;
    NewUiWindow& operator=(const NewUiWindow&) = default;
    NewUiWindow& operator=(NewUiWindow&&) = default;
};

// 0x669df0: open and the mouse inside the rectangle (x, y, x + w, y + h) or the title (x, y, x + w, y + 26), with
// PtInRect.
[[nodiscard]] bool HitTheBounds(const NewUiWindow& window, WindowHit hit, Point mouse, Core::PixelSize screen);

// +0x8dec: what 0x65f300 (login), 0x65f4b0 (selection) or 0x65f5f0 (game) builds.
enum class NewUiMode : std::uint8_t
{
    None = 0,
    Login = 2,
    Selection = 4,
    Game = 5,
};

class NewUi
{
public:
    // What 0x669f40 does with each open window, in 0x65fc30's order: the scene handles the window with its turn and
    // whatever comes out of it.
    using StepWindow = std::function<void(NewUiWindow&, NewUiTurn)>;

    // 0x65f300 and 0x65f4b0: empties the list (0x65f2b0) and adds `windows` in that order, each at the head (0x644b60),
    // so the last one ends on top. None with the focus.
    void Build(NewUiMode mode, std::span<NewUiWindow* const> windows);
    // 0x65f5f0: empties the list (0x65f2b0) and sets mode 5.
    void EnterTheGame();
    // No windows and mode 0, like 0x65f2e0 (on leaving the scene).
    void Clear();
    [[nodiscard]] NewUiMode Mode() const;

    // 0x65f610: takes the focus from the head and, if `window` is open, moves it to the head with the focus. The scene
    // opens it before with its own: both together are 0x65f690.
    void Raise(NewUiWindow& window);
    // 0x65f6c0: if it is in the list, closes it, takes its focus, moves it to the tail and gives the focus to the new
    // head, if open.
    void Close(NewUiWindow& window);

    // 0x65fc30, with this step's mouse: the focus with the left button pressed, the buttons' mouse, each open window
    // with `step` (and 0x669f40's drag), the drag's sticking (0x65f720) and +0x8de8. A window that closed itself in its
    // turn goes through 0x65f6c0, like the original's, which call it themselves.
    void Step(const Input::InputState& input, Core::PixelSize screen, const StepWindow& step,
              const std::function<void()>& buttonClicked = {});

    // +0x8de8: some window has the mouse (or is being dragged) in this step. With it, the selection does not pick
    // characters (0x634680).
    [[nodiscard]] bool HasTheMouse() const;

    // From the head to the tail.
    [[nodiscard]] std::vector<NewUiWindow*> Order() const;
    // 0x65fe00: from the tail to the head, the open ones (vtable + 0x18 does not draw closed ones).
    [[nodiscard]] std::vector<NewUiWindow*> DrawOrder() const;
    [[nodiscard]] bool HasTheFocus(const NewUiWindow& window) const;
    [[nodiscard]] bool IsDragged(const NewUiWindow& window) const;
    [[nodiscard]] bool IsStuck(const NewUiWindow& window) const;

private:
    struct Entry
    {
        NewUiWindow* window = nullptr;
        bool focused = false;  // +0xd
        bool stuck = false;    // +0xe
        bool dragging = false; // +0x10 at 2
        Point dragFrom{};      // +4, +8: the mouse at the drag's last step
        Point dragAt{};        // +0x28, +0x2c: where the window goes
    };

    [[nodiscard]] Entry* Find(const NewUiWindow& window);
    [[nodiscard]] const Entry* Find(const NewUiWindow& window) const;
    void MoveToTheTail(NewUiWindow& window);
    void Drag(Entry& entry, const Input::InputState& input, Point mouse, Core::PixelSize screen);
    void StickTheDragged(Core::PixelSize screen);
    bool StickAt(Entry& entry, Point at, Core::PixelSize screen);

    // [0] is the head.
    std::vector<Entry> m_entries;
    NewUiMode m_mode = NewUiMode::None;
    bool m_hasTheMouse = false;
};
} // namespace Client::Ui
