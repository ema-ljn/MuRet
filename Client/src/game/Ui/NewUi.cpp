#include "Ui/NewUi.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Ui/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace Client::Ui
{
namespace
{
// 0x669df0 with 2.
constexpr int TitleHeight = 0x1a;
// 0x65f720: it sticks within 10 of an edge or a corner.
constexpr int StickDistance = 10;

// SetRect(x − 10, y − 10, x + 10, y + 10).
Rect AroundCorner(Point corner)
{
    return Rect{.x = corner.x - StickDistance,
                .y = corner.y - StickDistance,
                .width = 2 * StickDistance,
                .height = 2 * StickDistance};
}

// IntersectRect: the intersection is not empty.
bool Intersect(const Rect& a, const Rect& b)
{
    return std::max(a.x, b.x) < std::min(a.x + a.width, b.x + b.width) &&
           std::max(a.y, b.y) < std::min(a.y + a.height, b.y + b.height);
}
} // namespace

std::optional<Point> MouseOfTheTurn(NewUiTurn turn, const Input::InputState& input, Core::PixelSize screen)
{
    if (!turn.mouse)
    {
        return std::nullopt;
    }
    return MouseInPixels(input, screen);
}

bool NewUiWindow::Hit(WindowHit hit, Point mouse, Core::PixelSize screen) const
{
    return hit != WindowHit::Title && HitTheBounds(*this, hit, mouse, screen);
}

void NewUiWindow::PlaceAt(Point /*topLeft*/, Core::PixelSize /*screen*/) {}

bool HitTheBounds(const NewUiWindow& window, WindowHit hit, Point mouse, Core::PixelSize screen)
{
    if (!window.IsOpen())
    {
        return false;
    }
    Rect area = window.Bounds(screen);
    if (hit == WindowHit::Title)
    {
        area.height = TitleHeight;
    }
    return area.Contains(mouse);
}

void NewUi::Build(NewUiMode mode, std::span<NewUiWindow* const> windows)
{
    m_entries.clear();
    for (NewUiWindow* window : windows)
    {
        m_entries.insert(m_entries.begin(), Entry{.window = window});
    }
    m_mode = mode;
    m_hasTheMouse = false;
}

void NewUi::EnterTheGame()
{
    m_entries.clear();
    m_mode = NewUiMode::Game;
    m_hasTheMouse = false;
}

void NewUi::Clear()
{
    m_entries.clear();
    m_mode = NewUiMode::None;
    m_hasTheMouse = false;
}

NewUiMode NewUi::Mode() const
{
    return m_mode;
}

void NewUi::Raise(NewUiWindow& window)
{
    if (m_entries.empty())
    {
        return;
    }
    m_entries.front().focused = false;
    if (!window.IsOpen())
    {
        return;
    }
    const auto found = std::ranges::find(m_entries, &window, &Entry::window);
    if (found == m_entries.end())
    {
        return;
    }
    Entry entry = *found;
    m_entries.erase(found);
    entry.focused = true;
    m_entries.insert(m_entries.begin(), entry);
}

void NewUi::Close(NewUiWindow& window)
{
    if (Find(window) == nullptr)
    {
        return;
    }
    window.Close();
    MoveToTheTail(window);
}

void NewUi::MoveToTheTail(NewUiWindow& window)
{
    const auto found = std::ranges::find(m_entries, &window, &Entry::window);
    if (found == m_entries.end())
    {
        return;
    }
    Entry entry = *found;
    m_entries.erase(found);
    entry.focused = false;
    m_entries.push_back(entry);
    if (m_entries.front().window->IsOpen())
    {
        m_entries.front().focused = true;
    }
}

void NewUi::Step(const Input::InputState& input, Core::PixelSize screen, const StepWindow& step,
                 const std::function<void()>& buttonClicked)
{
    if (m_mode == NewUiMode::None || m_entries.empty())
    {
        return;
    }
    m_hasTheMouse = false;
    const Point mouse = MouseInPixels(input, screen);
    const auto underTheMouse = [&]() -> NewUiWindow*
    {
        for (const Entry& entry : m_entries)
        {
            if (entry.window->Hit(WindowHit::Area, mouse, screen))
            {
                return entry.window;
            }
        }
        return nullptr;
    };

    // 0x65fc61: with the left button pressed, the first from the head with the mouse inside moves to the head with the
    // focus; outside all of them, the head loses the focus. +0x8de9, which it zeroes on release, is read by nothing in
    // login or selection.
    if (input.WasPressed(Input::MouseButton::Left))
    {
        if (NewUiWindow* pressed = underTheMouse())
        {
            Raise(*pressed);
        }
        else
        {
            m_entries.front().focused = false;
        }
    }

    // 0x65fccb: this frame's list, with every window's buttons without the mouse (0x669ed0 with 0), and with it those
    // of the first that has it inside.
    std::vector<NewUiWindow*> snapshot;
    snapshot.reserve(m_entries.size());
    for (const Entry& entry : m_entries)
    {
        snapshot.push_back(entry.window);
    }
    const NewUiWindow* attended = underTheMouse();

    for (NewUiWindow* window : snapshot)
    {
        // 0x669f40.
        Entry* entry = Find(*window);
        if (entry == nullptr || !window->IsOpen())
        {
            continue;
        }
        if (input.WasReleased(Input::MouseButton::Left))
        {
            entry->dragging = false;
        }
        const bool buttons = !entry->dragging;
        const bool focused = entry->focused;
        if (focused)
        {
            Drag(*entry, input, mouse, screen);
        }
        step(*window,
             NewUiTurn{
                 .buttons = buttons, .mouse = window == attended, .logic = focused, .buttonClicked = buttonClicked});
        if (!window->IsOpen())
        {
            MoveToTheTail(*window);
        }
    }

    StickTheDragged(screen);

    // 0x65fd8e: any with +0x10 between 1 and 4 (dragging is 2) or, from the head, the first with the mouse inside.
    for (const Entry& entry : m_entries)
    {
        if (entry.dragging)
        {
            m_hasTheMouse = true;
        }
        if (m_hasTheMouse)
        {
            return;
        }
        if (entry.window->Hit(WindowHit::Area, mouse, screen))
        {
            m_hasTheMouse = true;
            return;
        }
    }
}

void NewUi::Drag(Entry& entry, const Input::InputState& input, Point mouse, Core::PixelSize screen)
{
    // 0x669fb8: pressed on the title, it starts from where it is.
    if (input.WasPressed(Input::MouseButton::Left) && entry.window->Hit(WindowHit::Title, mouse, screen))
    {
        const Rect bounds = entry.window->Bounds(screen);
        entry.dragFrom = mouse;
        entry.dragAt = Point{bounds.x, bounds.y};
        entry.dragging = true;
    }
    if (!entry.dragging)
    {
        return;
    }
    // 0x669ff0: adds the mouse's movement; stuck (+0xe), it does not move until 0x65f720 lets it go.
    entry.dragAt.x += mouse.x - entry.dragFrom.x;
    entry.dragAt.y += mouse.y - entry.dragFrom.y;
    if (!entry.stuck)
    {
        entry.window->PlaceAt(entry.dragAt, screen);
    }
    entry.dragFrom = mouse;
}

bool NewUi::StickAt(Entry& entry, Point at, Core::PixelSize screen)
{
    // 0x65fbb0: only if something stays inside the screen.
    const Rect bounds = entry.window->Bounds(screen);
    const Rect moved{.x = at.x, .y = at.y, .width = bounds.width, .height = bounds.height};
    if (!Intersect(Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height}, moved))
    {
        return false;
    }
    entry.window->PlaceAt(at, screen);
    entry.stuck = true;
    return true;
}

void NewUi::StickTheDragged(Core::PixelSize screen)
{
    // 0x65f720: only the head, and while dragged.
    if (m_entries.empty() || !m_entries.front().dragging)
    {
        return;
    }
    Entry& head = m_entries.front();
    head.stuck = false;
    const Rect bounds = head.window->Bounds(screen);
    const int w = bounds.width;
    const int h = bounds.height;
    const Point at = head.dragAt;
    const int screenW = screen.width;
    const int screenH = screen.height;
    const std::array<Rect, 4> corners{AroundCorner(at), AroundCorner(Point{at.x + w, at.y}),
                                      AroundCorner(Point{at.x, at.y + h}), AroundCorner(Point{at.x + w, at.y + h})};
    const auto place = [&](Point to)
    {
        head.window->PlaceAt(to, screen);
        head.stuck = true;
    };

    // The screen's corners and, if not, its edges (vtable + 8, without 0x65fbb0).
    if (corners[0].Contains(Point{0, 0}))
    {
        place(Point{0, 0});
    }
    else if (corners[1].Contains(Point{screenW, 0}))
    {
        place(Point{screenW - w, 0});
    }
    else if (corners[2].Contains(Point{0, screenH}))
    {
        place(Point{0, screenH - h});
    }
    else if (corners[3].Contains(Point{screenW, screenH}))
    {
        place(Point{screenW - w, screenH - h});
    }
    else if (at.y - StickDistance < 0 && at.y + StickDistance > 0)
    {
        place(Point{at.x, 0});
    }
    else if (at.y + h - StickDistance < screenH && at.y + h + StickDistance > screenH)
    {
        place(Point{at.x, screenH - h});
    }
    else if (at.x - StickDistance < 0 && at.x + StickDistance > 0)
    {
        place(Point{0, at.y});
    }
    else if (at.x + w - StickDistance < screenW && at.x + w + StickDistance > screenW)
    {
        place(Point{screenW - w, at.y});
    }

    for (std::size_t k = 1; k < m_entries.size(); ++k)
    {
        const NewUiWindow& other = *m_entries[k].window;
        if (!other.IsOpen())
        {
            continue;
        }
        const Rect v = other.Bounds(screen);
        const std::array<Point, 4> points{Point{v.x, v.y}, Point{v.x + v.width, v.y}, Point{v.x, v.y + v.height},
                                          Point{v.x + v.width, v.y + v.height}};
        // 0x65f94a: a corner of the other near a different corner of this one sticks the two corners, and it ends (the
        // table at 0x65fb94).
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            for (std::size_t j = 0; j < points.size(); ++j)
            {
                if (i == j || !corners[i].Contains(points[j]))
                {
                    continue;
                }
                const int left = j % 2 == 0 ? v.x : v.x + v.width;
                const int top = j < 2 ? v.y : v.y + v.height;
                const int x = i % 2 == 0 ? left : left - w;
                const int y = i < 2 ? top : top - h;
                static_cast<void>(StickAt(head, Point{x, y}, screen));
                return;
            }
        }
        // 0x65f99d: otherwise, an edge. With the two overlapping in width, above or below the other; otherwise, with
        // the two overlapping in height, to its right or left. Goes on with the rest.
        if (v.x < at.x + w - StickDistance && v.x + v.width > at.x + StickDistance)
        {
            const int bottom = v.y + v.height;
            if (bottom > at.y - StickDistance && bottom < at.y + StickDistance)
            {
                static_cast<void>(StickAt(head, Point{at.x, bottom}, screen));
            }
            else if (v.y > at.y + h - StickDistance && v.y < at.y + h + StickDistance)
            {
                static_cast<void>(StickAt(head, Point{at.x, v.y - h}, screen));
            }
        }
        else if (v.y < at.y + h - StickDistance && v.y + v.height > at.y + StickDistance)
        {
            const int right = v.x + v.width;
            if (right > at.x - StickDistance && right < at.x + StickDistance)
            {
                static_cast<void>(StickAt(head, Point{right, at.y}, screen));
            }
            else if (v.x > at.x + w - StickDistance && v.x < at.x + w + StickDistance)
            {
                static_cast<void>(StickAt(head, Point{v.x - w, at.y}, screen));
            }
        }
    }
}

bool NewUi::HasTheMouse() const
{
    return m_hasTheMouse;
}

std::vector<NewUiWindow*> NewUi::Order() const
{
    std::vector<NewUiWindow*> order;
    order.reserve(m_entries.size());
    for (const Entry& entry : m_entries)
    {
        order.push_back(entry.window);
    }
    return order;
}

std::vector<NewUiWindow*> NewUi::DrawOrder() const
{
    std::vector<NewUiWindow*> order;
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
    {
        if (it->window->IsOpen())
        {
            order.push_back(it->window);
        }
    }
    return order;
}

bool NewUi::HasTheFocus(const NewUiWindow& window) const
{
    const Entry* entry = Find(window);
    return entry != nullptr && entry->focused;
}

bool NewUi::IsDragged(const NewUiWindow& window) const
{
    const Entry* entry = Find(window);
    return entry != nullptr && entry->dragging;
}

bool NewUi::IsStuck(const NewUiWindow& window) const
{
    const Entry* entry = Find(window);
    return entry != nullptr && entry->stuck;
}

NewUi::Entry* NewUi::Find(const NewUiWindow& window)
{
    const auto found = std::ranges::find(m_entries, &window, &Entry::window);
    return found == m_entries.end() ? nullptr : &*found;
}

const NewUi::Entry* NewUi::Find(const NewUiWindow& window) const
{
    const auto found = std::ranges::find(m_entries, &window, &Entry::window);
    return found == m_entries.end() ? nullptr : &*found;
}
} // namespace Client::Ui
