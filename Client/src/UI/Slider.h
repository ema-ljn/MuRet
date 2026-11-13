#pragma once

#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "UI/Button.h"
#include "UI/Widget.h"

#include <array>
#include <cstdint>

// Un deslizador de la UI nueva (vtable 0x6ba9f8; CL8, hito 8; 06 § Las opciones): 0x646640 lo arma
// con tres imágenes, la perilla (un botón de 0x63c330 de un cuadro, +0x28), la barra y su relleno
// (la barra de 0x640220, +0x108); 0x646a50 lo atiende y 0x646e90 lo dibuja. MuRet tiene solo el
// horizontal (+8 en 0), el de las opciones: el vertical no lo arma ninguna ventana del login ni de la
// selección.
namespace Client::UI
{
// Una imagen del deslizador: su slot y su tamaño, en píxeles.
struct SliderImage
{
    Render::TextureSlot slot;
    int width = 0;
    int height = 0;
};

// Lo que recibe 0x646640: la perilla, la barra, el relleno y el rectángulo del relleno adentro de
// la barra (+0xb0..+0xbc de 0x640220, que guarda izquierda, arriba, derecha y abajo).
struct SliderParts
{
    SliderImage thumb;
    SliderImage track;
    SliderImage fill;
    Rect fillArea;
};

class Slider
{
public:
    // 0x646640 con el horizontal: el máximo en 1 (+0x1c), el valor en 0 (+0x20), en (0, 0), y el
    // recorrido de la perilla (+0x24) es el ancho de la barra menos el de la perilla, o 0.
    explicit Slider(const SliderParts& parts);

    // 0x6468e0: el máximo, 1 o más.
    void SetMax(int max);
    // 0x646f80: el valor, entre 0 y el máximo.
    void SetValue(int value);
    [[nodiscard]] int Value() const;
    [[nodiscard]] int Max() const;
    // 0x646890: la esquina de arriba a la izquierda de la barra (+0xc, +0x10), en píxeles.
    void MoveTo(Point topLeft);
    [[nodiscard]] Point Position() const;
    // 0x646ef0: abre o cierra la perilla (0x63c450) y la barra (0x6407b0).
    void Open(bool open);

    // +9: apretado a la izquierda de la perilla (1), a la derecha (2) o arrastrándola (0x10). La
    // ventana mira si es distinto de 0.
    [[nodiscard]] bool Moving() const;
    [[nodiscard]] const Button& Thumb() const;

    // 0x646a50 con los `elapsedMs` del paso y el mouse de la UI (el de 0x640850, que no mira si la
    // ventana lo tiene): la ventana lo atiende en su lógica, con el foco.
    void Step(const Input::InputState& input, Point mouse, double elapsedMs);

    // 0x646e90: la barra y el relleno (0x6407e0) y la perilla (0x63c710), cerrado nada.
    [[nodiscard]] std::array<Render::UiImage, 3> Images(const TextureSizes& sizes) const;
    [[nodiscard]] bool IsOpen() const;
    // El ancho del relleno: 0x640580, (derecha − izquierda) · valor / máximo en enteros sin signo.
    [[nodiscard]] int FillWidth() const;

private:
    // 0x646830: la perilla en x + recorrido / máximo · valor (la x87 de 53 bits y _ftol).
    void PlaceTheThumb();
    // +0x1e en 0x646a50: la perilla en su lugar y nada se mueve.
    void Release();
    // El resto de 0x646a50: los dos lados de la perilla con `pressed` (+0x1c) y el arrastre.
    void StepThePress(Point mouse, double elapsedMs, bool pressed);
    // 0x646900 y 0x646990: un paso a la izquierda o a la derecha.
    void StepLeft();
    void StepRight();
    [[nodiscard]] int ThumbX() const;

    SliderParts m_parts;
    Button m_thumb;
    Point m_position;
    int m_max = 1;
    int m_value = 0;
    int m_travel = 0;
    std::uint8_t m_moving = 0;
    // +0x110 y +0x118: lo que lleva apretado y lo que pasó desde la última repetición.
    double m_held = 0.0;
    double m_repeat = 0.0;
    // +0x120 y +0x124: la x del mouse del último paso del arrastre y la de la perilla donde se agarró.
    int m_lastMouseX = 0;
    int m_grabX = 0;
    // Una soltada que llegó después del apretón en el mismo paso (Q-C498).
    bool m_releaseLater = false;
    bool m_open = false;
};
} // namespace Client::UI
