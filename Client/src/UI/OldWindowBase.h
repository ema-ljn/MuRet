#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

// La ventana base de las ventanas viejas (CL8 hito 7; 06 § HUD y ventanas, "Las ventanas viejas"), 0x45a210 (vtable
// 0x6b8d6c), y lo que comparten todas: los estados, los mensajes y el botón (0x45ad80). Están acá y no en
// UI/OldWindows.h para que los controles de UI/OldList.h (las listas y el EDIT) los usen sin el administrador.
namespace Client::UI
{
// +0x48 (0x45a9b0 y 0x45a9c0): a la vista, cambiando de tamaño, oculta, arrastrada, minimizada (nadie la pone en la
// CL8) y apagado (el botón, 0x45b200; nadie lo pone en la CL8).
inline constexpr std::int32_t ShownWindow = 0;
inline constexpr std::int32_t ResizedWindow = 1;
inline constexpr std::int32_t HiddenWindow = 3;
inline constexpr std::int32_t DraggedWindow = 4;
inline constexpr std::int32_t MinimizedWindow = 5;
inline constexpr std::int32_t DisabledWindow = 6;

// Los mensajes del administrador (0x490b70) y de las ventanas (0x45abc0, 0x4a3b80): subir y dar el foco, minimizar,
// estirar (la vtable + 0x38, 0x4930f0), cerrar y mandar al fondo; ubicar y medir contra el padre; el comando de un
// botón, y el aviso al dueño de una caja de mensaje. El 6 se lo manda el administrador a la que sube: lo atienden la
// ventana de amigos (0x4a2be0) y la de agregar un amigo (0x4a3850).
inline constexpr std::int32_t RaiseMessage = 1;
inline constexpr std::int32_t MinimizeMessage = 2;
inline constexpr std::int32_t StretchMessage = 3;
inline constexpr std::int32_t CloseMessage = 4;
inline constexpr std::int32_t SendBackMessage = 5;
inline constexpr std::int32_t RaisedMessage = 6;
// 0x467b10: Enter en el EDIT de una ventana vieja se lo manda a su padre.
inline constexpr std::int32_t EnterMessage = 7;
inline constexpr std::int32_t PlaceMessage = 8;
inline constexpr std::int32_t SizeMessage = 9;
inline constexpr std::int32_t CommandMessage = 10;
// 0x4a37e0: el nombre que se escribió en la caja de agregar un amigo, a la pestaña que la abrió (OldMessage::text).
inline constexpr std::int32_t NameMessage = 11;
inline constexpr std::int32_t AnswerMessage = 12;
// Los de las listas (0x47a9d0): el renglón con doble clic (al padre, 0x461f00) o con clic (0x462bd0), ir al final y
// las flechas de arriba y de abajo del teclado (0x47b0f0).
inline constexpr std::int32_t ChosenRowMessage = 13;
inline constexpr std::int32_t ToTheEndMessage = 14;
inline constexpr std::int32_t ArrowUpMessage = 15;
inline constexpr std::int32_t ArrowDownMessage = 16;

// 0x459f70: un mensaje de 12 bytes (el tipo y dos argumentos) al final de la cola (+0x08..+0x30, un std::deque);
// 0x45a140 saca el primero a +0x34..+0x3c.
struct OldMessage
{
    std::int32_t type = 0;
    std::int32_t a = 0;
    std::int32_t b = 0;
    // El 11 lleva en b un puntero a un buffer de 0x100 bytes (malloc en 0x4a37e0, free en 0x49d6d6): MuRet lleva el
    // texto.
    std::string text;

    friend bool operator==(const OldMessage&, const OldMessage&) = default;
};

// La ventana base, 0x45a210: lo que comparten todas.
struct OldWindowBase
{
    // +0x40: del contador 0x5762eb0 (0x459ef0), que suma antes de dar.
    std::int32_t id = 0;
    // +0x44: la ventana de la que cuelga (0, ninguna).
    std::int32_t parent = 0;
    // +0x48.
    std::int32_t state = ShownWindow;
    // +0x4c.
    std::int32_t flags = 0;
    // +0x50..+0x5c, en 640 × 480. 0x45a210 la deja en (0, 0) de 100 × 100.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 100;
    std::int32_t height = 100;
    // +0x60, +0x68 y +0x6c (0x45aa10): cómo la ubica el mensaje 8 contra el área de adentro del padre (0, desde
    // arriba a la izquierda; 1, 2 y 3, desde la derecha, desde abajo o desde los dos).
    std::int32_t placeMode = 0;
    std::int32_t placeX = 0;
    std::int32_t placeY = 0;
    // +0x64, +0x70 y +0x74 (0x45aa30): lo mismo para el tamaño con el mensaje 9.
    std::int32_t sizeMode = 0;
    std::int32_t sizeX = 0;
    std::int32_t sizeY = 0;
    // +0x78: el modo de 0x459f00 para el mouse (1, desde arriba a la izquierda).
    std::int32_t rectMode = 1;
    std::deque<OldMessage> queue;
    OldMessage current;
};

// El botón, 0x45ad80 (vtable 0x6b8d9c): 0x46 × 0x14 con win_button.tga.
struct OldButton
{
    OldWindowBase base;
    // +0x7c: el comando que le manda al padre (0x45b150).
    std::int32_t command = 0;
    // +0x80: el texto (strdup de 0x45b170), o ninguno.
    std::optional<std::string> label;
    // +0x84: apretado (el clic empezó sobre él).
    std::int32_t pressed = 0;
};

} // namespace Client::UI
