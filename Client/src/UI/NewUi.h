#pragma once

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "UI/Widget.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// El administrador de la UI nueva (CL8, hito 8; 06 § Las ventanas de la UI nueva): el objeto de
// 0x7d53728 (0x65ee60) con su lista doble de ventanas, la cabeza en +0x8ddc, la cola en +0x8de0 y
// la cuenta en +0x8de4. La cabeza va arriba: 0x65fe00 dibuja desde la cola hasta la cabeza, y
// 0x65fc30 le da el mouse a la primera desde la cabeza que lo tiene adentro. Abrir una (0x65f690 →
// 0x65f610) la pasa a la cabeza con el foco (+0xd), y cerrarla (0x65f6c0) la pasa a la cola y le
// da el foco a la nueva cabeza, si está abierta: solo la que tiene el foco corre su lógica (+0x30,
// los clics y las teclas), y como el foco lo da 0x65f610, que antes se lo saca a la cabeza, lo
// tiene una sola.
//
// Las ventanas son de la escena: el administrador guarda de cada una lo que en el original es de
// la base de 0x669c70 y mira 0x65fc30 (el foco, +0xd; el arrastre, +0x10, +4, +8, +0x28 y +0x2c;
// y si quedó pegada, +0xe), y le pide a la ventana si está abierta (+0xc), su rectángulo
// (+0x18..+0x24) y el resto de su vtable. El original lo corre una vez por frame, antes de los pasos
// de ese frame (0x63778c, con los milisegundos del frame, hasta 200); MuRet, como el resto de la UI
// (Q-C81), al empezar cada paso de la escena, con los 40 ms del paso (Q-C490).
namespace Client::UI
{
// Qué mira vtable + 0xc (0x669df0): el rectángulo entero (0) o el título, los 26 píxeles de arriba
// (2), que es lo que se agarra para arrastrar.
enum class WindowHit : std::uint8_t
{
    Area,
    Title,
};

// Lo que 0x669f40 hace con una ventana abierta en un frame. `buttons`: +0x10 en 0, sin arrastre,
// atiende sus botones (0x63c490). `mouse`: el +0xa9 de sus botones (0x669ed0), que es 1 solo en la
// primera desde la cabeza con el mouse adentro; con 0, ningún botón está debajo del mouse
// (0x63c470). `logic`: tiene el foco (+0xd) y corre su +0x30. Las del login y la selección no se
// arrastran, así que para ellas `buttons` vale siempre true. Una ventana que se atiende sola
// (sus pruebas, por ejemplo) lo hace con todo en true.
struct NewUiTurn
{
    bool buttons = true;
    bool mouse = true;
    bool logic = true;
};

// El mouse que ven los botones de una ventana en su turno: el de la UI nueva (MouseInPixels), o
// ninguno con `mouse` en false.
[[nodiscard]] std::optional<Point> MouseOfTheTurn(NewUiTurn turn, const Input::InputState& input,
                                                  App::PixelSize screen);

// Una ventana de la UI nueva, como la ve el administrador.
class NewUiWindow
{
public:
    NewUiWindow() = default;
    virtual ~NewUiWindow() = default;

    // +0xc.
    [[nodiscard]] virtual bool IsOpen() const = 0;
    // vtable + 0x10 con 0.
    virtual void Close() = 0;
    // +0x18..+0x24: la esquina y el tamaño, en píxeles.
    [[nodiscard]] virtual Rect Bounds(App::PixelSize screen) const = 0;
    // vtable + 0xc. Todas las del login y la selección (0x647d10 la de Esc, y las demás igual)
    // contestan que no al título y llaman a 0x669df0 con el resto: no se arrastran.
    [[nodiscard]] virtual bool Hit(WindowHit hit, Point mouse, App::PixelSize screen) const;
    // vtable + 8: el arrastre la mueve a `topLeft`. La base solo mueve el rectángulo; las del login
    // y la selección no se arrastran, y no hace nada.
    virtual void PlaceAt(Point topLeft, App::PixelSize screen);

protected:
    NewUiWindow(const NewUiWindow&) = default;
    NewUiWindow(NewUiWindow&&) = default;
    NewUiWindow& operator=(const NewUiWindow&) = default;
    NewUiWindow& operator=(NewUiWindow&&) = default;
};

// 0x669df0: abierta y el mouse adentro del rectángulo (x, y, x + w, y + h) o del título
// (x, y, x + w, y + 26), con PtInRect.
[[nodiscard]] bool HitTheBounds(const NewUiWindow& window, WindowHit hit, Point mouse, App::PixelSize screen);

// +0x8dec: lo que arma 0x65f300 (el login), 0x65f4b0 (la selección) o 0x65f5f0 (el juego).
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
    // Lo que 0x669f40 hace con cada ventana abierta, en el orden de 0x65fc30: la escena atiende la
    // ventana con su turno y lo que salga de ella.
    using StepWindow = std::function<void(NewUiWindow&, NewUiTurn)>;

    // 0x65f300 y 0x65f4b0: vacía la lista (0x65f2b0) y suma `windows` en ese orden, cada una en la
    // cabeza (0x644b60), así que la última queda arriba. Ninguna con el foco.
    void Build(NewUiMode mode, std::span<NewUiWindow* const> windows);
    // 0x65f5f0: vacía la lista (0x65f2b0) y pone el modo 5.
    void EnterTheGame();
    // Sin ventanas y en el modo 0, como 0x65f2e0 (al salir de la escena).
    void Clear();
    [[nodiscard]] NewUiMode Mode() const;

    // 0x65f610: le saca el foco a la cabeza y, si `window` está abierta, la pasa a la cabeza con el
    // foco. La escena la abre antes con lo suyo: las dos cosas son 0x65f690.
    void Raise(NewUiWindow& window);
    // 0x65f6c0: si está en la lista, la cierra, le saca el foco, la pasa a la cola y le da el foco a
    // la nueva cabeza, si está abierta.
    void Close(NewUiWindow& window);

    // 0x65fc30, con el mouse de este paso: el foco con el botón izquierdo apretado, el mouse de los
    // botones, cada ventana abierta con `step` (y el arrastre de 0x669f40), el pegado del arrastre
    // (0x65f720) y +0x8de8. Una ventana que se cerró sola en su turno pasa por 0x65f6c0, como las del
    // original, que lo llaman ellas.
    void Step(const Input::InputState& input, App::PixelSize screen, const StepWindow& step);

    // +0x8de8: alguna ventana tiene el mouse (o se está arrastrando) en este paso. Con él, la
    // selección no elige personajes (0x634680).
    [[nodiscard]] bool HasTheMouse() const;

    // Desde la cabeza hasta la cola.
    [[nodiscard]] std::vector<NewUiWindow*> Order() const;
    // 0x65fe00: desde la cola hasta la cabeza, las abiertas (vtable + 0x18 no dibuja las cerradas).
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
        bool dragging = false; // +0x10 en 2
        Point dragFrom{};      // +4, +8: el mouse del último paso del arrastre
        Point dragAt{};        // +0x28, +0x2c: adónde va la ventana
    };

    [[nodiscard]] Entry* Find(const NewUiWindow& window);
    [[nodiscard]] const Entry* Find(const NewUiWindow& window) const;
    void MoveToTheTail(NewUiWindow& window);
    void Drag(Entry& entry, const Input::InputState& input, Point mouse, App::PixelSize screen);
    void StickTheDragged(App::PixelSize screen);
    bool StickAt(Entry& entry, Point at, App::PixelSize screen);

    // [0] es la cabeza.
    std::vector<Entry> m_entries;
    NewUiMode m_mode = NewUiMode::None;
    bool m_hasTheMouse = false;
};
} // namespace Client::UI
