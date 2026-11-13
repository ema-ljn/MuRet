#include "UI/NewUi.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "UI/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Client::UI
{
namespace
{
// 0x669df0 con 2: el título mide 26 de alto.
constexpr int TitleHeight = 0x1a;
// 0x65f720: lo que se pega, a menos de 10 de un borde o de una esquina.
constexpr int StickDistance = 10;

// SetRect(x − 10, y − 10, x + 10, y + 10): el rectángulo de 20 × 20 alrededor de una esquina.
Rect AroundCorner(Point corner)
{
    return Rect{.x = corner.x - StickDistance,
                .y = corner.y - StickDistance,
                .width = 2 * StickDistance,
                .height = 2 * StickDistance};
}

// IntersectRect: la intersección no está vacía.
bool Intersect(const Rect& a, const Rect& b)
{
    return std::max(a.x, b.x) < std::min(a.x + a.width, b.x + b.width) &&
           std::max(a.y, b.y) < std::min(a.y + a.height, b.y + b.height);
}
} // namespace

std::optional<Point> MouseOfTheTurn(NewUiTurn turn, const Input::InputState& input, App::PixelSize screen)
{
    if (!turn.mouse)
    {
        return std::nullopt;
    }
    return MouseInPixels(input, screen);
}

bool NewUiWindow::Hit(WindowHit hit, Point mouse, App::PixelSize screen) const
{
    return hit != WindowHit::Title && HitTheBounds(*this, hit, mouse, screen);
}

void NewUiWindow::PlaceAt(Point /*topLeft*/, App::PixelSize /*screen*/) {}

bool HitTheBounds(const NewUiWindow& window, WindowHit hit, Point mouse, App::PixelSize screen)
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

void NewUi::Step(const Input::InputState& input, App::PixelSize screen, const StepWindow& step)
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

    // 0x65fc61: con el izquierdo apretado, la primera desde la cabeza con el mouse adentro pasa a la
    // cabeza con el foco; afuera de todas, la cabeza pierde el foco. +0x8de9, que pone en 0 al
    // soltarlo, no lo lee nadie del login ni de la selección.
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

    // 0x65fccb: la lista de este frame, con los botones de todas sin el mouse (0x669ed0 con 0), y
    // con él los de la primera que lo tiene adentro.
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
        step(*window, NewUiTurn{.buttons = buttons, .mouse = window == attended, .logic = focused});
        if (!window->IsOpen())
        {
            MoveToTheTail(*window);
        }
    }

    StickTheDragged(screen);

    // 0x65fd8e: alguna con +0x10 entre 1 y 4 (el arrastre es el 2) o, desde la cabeza, la primera
    // con el mouse adentro.
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

void NewUi::Drag(Entry& entry, const Input::InputState& input, Point mouse, App::PixelSize screen)
{
    // 0x669fb8: apretado en el título, empieza desde donde está.
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
    // 0x669ff0: suma lo que se movió el mouse; pegada (+0xe), no se mueve hasta que 0x65f720 la
    // suelte.
    entry.dragAt.x += mouse.x - entry.dragFrom.x;
    entry.dragAt.y += mouse.y - entry.dragFrom.y;
    if (!entry.stuck)
    {
        entry.window->PlaceAt(entry.dragAt, screen);
    }
    entry.dragFrom = mouse;
}

bool NewUi::StickAt(Entry& entry, Point at, App::PixelSize screen)
{
    // 0x65fbb0: solo si queda algo adentro de la pantalla.
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

void NewUi::StickTheDragged(App::PixelSize screen)
{
    // 0x65f720: solo la cabeza, y arrastrándose.
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
    // Las cuatro esquinas de donde va: arriba a la izquierda, arriba a la derecha, abajo a la
    // izquierda y abajo a la derecha.
    const std::array<Rect, 4> corners{AroundCorner(at), AroundCorner(Point{at.x + w, at.y}),
                                      AroundCorner(Point{at.x, at.y + h}), AroundCorner(Point{at.x + w, at.y + h})};
    const auto place = [&](Point to)
    {
        head.window->PlaceAt(to, screen);
        head.stuck = true;
    };

    // Las esquinas de la pantalla y, si no, sus bordes (vtable + 8, sin 0x65fbb0).
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

    // Las otras abiertas, desde la segunda.
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
        // 0x65f94a: una esquina de la otra cerca de una esquina distinta de esta pega las dos
        // esquinas, y termina (la tabla de 0x65fb94).
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
        // 0x65f99d: si no, un borde. Con las dos superpuestas a lo ancho, arriba o abajo de la
        // otra; si no, con las dos superpuestas a lo alto, a su derecha o a su izquierda. Sigue
        // con las demás.
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
} // namespace Client::UI
