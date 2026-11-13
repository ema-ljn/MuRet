#pragma once

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Render::Text
{
class ITextMeasure;
}

// El diálogo de los paneles, [0x7d53704] (0x65feb0, 0x1064 bytes; CL9 hito 8, 06 § El diálogo de los paneles): una
// pregunta de hasta diez renglones con uno o dos botones, que abren los paquetes (el 50 y el E5) con 0x6600d0 y que
// responde a quien lo abrió con un motivo. Lo atienden el registro de los paneles (la tecla primero, 0x657880; el paso,
// 0x657930; cerrar todo, 0x657a90) y el dibujo de los paneles (0x6579e0 → 0x660830).
namespace Client::UI
{
// +0xa18: los botones y lo demás. 1, Aceptar (228); 2, Sí y No (228 y 229); 4, Confirmar y Cancelar (1037 y 1038); 8,
// el reloj; 0x10, el campo de texto.
inline constexpr std::uint32_t PanelDialogOk = 0x1;
inline constexpr std::uint32_t PanelDialogYesNo = 0x2;
inline constexpr std::uint32_t PanelDialogConfirm = 0x4;
inline constexpr std::uint32_t PanelDialogTimed = 0x8;
inline constexpr std::uint32_t PanelDialogTextField = 0x10;

// Los motivos con que responde (0x6603c0, 0x660410 y 0x6605b0).
inline constexpr std::uint32_t PanelAnswerEnter = 0x1;   // Enter sin botones
inline constexpr std::uint32_t PanelAnswerOk = 0x2;      // Aceptar o Sí, o Enter con ellos
inline constexpr std::uint32_t PanelAnswerNo = 0x4;      // No
inline constexpr std::uint32_t PanelAnswerConfirm = 0x8; // Confirmar, o Enter con él
inline constexpr std::uint32_t PanelAnswerCancel = 0x10; // Cancelar
inline constexpr std::uint32_t PanelAnswerTimeout = 0x20;
inline constexpr std::uint32_t PanelAnswerEscape = 0x41; // Esc sin botones
inline constexpr std::uint32_t PanelAnswerEscapeOk = 0x42;
inline constexpr std::uint32_t PanelAnswerEscapeYesNo = 0x44;
inline constexpr std::uint32_t PanelAnswerEscapeConfirm = 0x50;

// 0x6600d0: hasta diez renglones de 0x100 bytes.
inline constexpr std::size_t PanelDialogMaxLines = 10;
inline constexpr std::size_t PanelDialogLineBytes = 0x100;
// +0xe40, +0xec8, +0xf50 y +0xfd8: Aceptar o Sí (228), No (229), Confirmar (1037) y Cancelar (1038).
inline constexpr std::size_t PanelDialogButtonCount = 4;
inline constexpr std::size_t PanelOkButton = 0;
inline constexpr std::size_t PanelNoButton = 1;
inline constexpr std::size_t PanelConfirmButton = 2;
inline constexpr std::size_t PanelCancelButton = 3;
// 0x65feb0: el reloj de 5.000 ms (+0xe3c).
inline constexpr std::uint32_t PanelDialogTimerMs = 5000;

// +4: a quién responde. El original guarda la función; MuRet, cuál es, y la llama el que tiene el mundo y el cable
// (Scenes/GuildPanels).
enum class PanelDialogOwner : std::uint8_t
{
    None,
    GuildJoin,    // 0x4d6b60, el pedido de entrar al gremio (el 50)
    Relationship, // 0x653c00, el pedido de alianza o de rivalidad (el E5)
    GuildDismiss, // 0x64def0, quitarle el cargo a un miembro (la pestaña de los miembros)
    GuildKick,    // 0x64e550, echar a un miembro (la pestaña de los miembros)
};

// La respuesta: si la función devuelve distinto de 0 el diálogo se cierra (las de MuRet devuelven 1).
using PanelDialogAnswer =
    std::function<Core::Expected<bool, std::string>(PanelDialogOwner owner, std::uint32_t reason)>;

struct PanelDialogState
{
    PanelDialogOwner owner = PanelDialogOwner::None; // +4
    // +0x10: abierto, con el GetTickCount() de cuando se abrió (lo que devuelve 0x6600d0 y guardan las ventanas).
    std::uint32_t opened = 0;
    std::int32_t lineCount = 0; // +0x14
    std::array<std::string, PanelDialogMaxLines> lines{};
    std::uint32_t flags = 0;                    // +0xa18
    std::int32_t width = 0;                     // +0xa1c: 0xd5 al abrir
    std::int32_t height = 0;                    // +0xa20
    std::int32_t placed = 0;                    // +0xa24: distinto de 0, la caja va arriba (y = 10)
    std::uint32_t timerStart = 0;               // +0xe34
    std::uint32_t timerEnd = 0;                 // +0xe38
    std::uint32_t timerMs = PanelDialogTimerMs; // +0xe3c
    // 0x65feb0: los cuatro botones de 0x32 × 0x12 con su comando (0, 1, 2 y 3). Los ubica el dibujo (0x660830). Sus ids
    // son del contador 0x5762eb0 (P-C97): MuRet les da −1..−4, que no chocan con los de las ventanas viejas (P-C280).
    std::array<OldButton, PanelDialogButtonCount> buttons = DefaultButtons();

    [[nodiscard]] static std::array<OldButton, PanelDialogButtonCount> DefaultButtons();
};

// 0x660090: todo en 0 menos el tamaño, el reloj y los botones.
void ResetThePanelDialog(PanelDialogState& dialog);

enum class PanelDialogOpening : std::uint8_t
{
    Opened,
    TooManyLines, // 0x6600d6: más de diez renglones, OutputDebugStringA y 0
    Busy,         // 0x6600f9: ya hay uno abierto: OutputDebugStringA, el aviso (0x6d69a8) y 0
};

struct PanelDialogRequest
{
    std::span<const std::string> lines;
    std::uint32_t flags = 0;
    PanelDialogOwner owner = PanelDialogOwner::None;
    std::int32_t placed = 0;
};

// 0x6600d0(renglones, cuenta, paso, banderas, función, arriba): copia los renglones (hasta su '\0', recortados a 0xff),
// suma el alto de cada uno que no esté vacío (GetTextExtentPoint32A con la fuente del DC, cy + 2) y lo demás, y lo abre
// con el reloj de `now` (GetTickCount()).
[[nodiscard]] PanelDialogOpening OpenThePanelDialog(PanelDialogState& dialog, const PanelDialogRequest& request,
                                                    const Render::Text::ITextMeasure& measure,
                                                    Render::Text::FontKind dcFont, std::uint32_t now);

// 0x660340: el GetTickCount() con que se abrió, 0 cerrado.
[[nodiscard]] inline std::uint32_t PanelDialogHandle(const PanelDialogState& dialog)
{
    return dialog.opened;
}

// 0x660350: lo cierra. Con el campo de texto (0x10) vacía el EDIT o el buffer del chat, que ningún diálogo de MuRet
// abre: corta.
[[nodiscard]] Core::Expected<void, std::string> CloseThePanelDialog(PanelDialogState& dialog);

// 0x6603c0: abierto, responde con el motivo de cancelar (1 → 2, 2 → 4, 4 → 0x10, sin botones 1) sin mirar lo que
// devuelve, y lo cierra.
[[nodiscard]] Core::Expected<void, std::string> CancelThePanelDialog(PanelDialogState& dialog,
                                                                     const PanelDialogAnswer& answer);

// 0x660410(vk): abierto, Esc responde según los botones (0x42, 0x44, 0x50 o 0x41) y Enter, con el campo de texto, como
// el primer botón (2, 2, 8 o 1); si la función devuelve 0 queda abierto, si no se cierra. true si se quedó con la
// tecla.
[[nodiscard]] Core::Expected<bool, std::string> ThePanelDialogKey(PanelDialogState& dialog, std::int32_t virtualKey,
                                                                  const PanelDialogAnswer& answer);

// 0x660580: con el reloj (8), si ya pasó el final.
[[nodiscard]] bool ThePanelDialogTimedOut(const PanelDialogState& dialog, std::uint32_t now);

// 0x45b200, la vtable + 0x20 del botón (0x6b8d9c): el clic que empieza sobre él lo aprieta (0x5762ec4) y el soltado
// sobre él da 1 con el sonido 0x19. Antes manda su comando al padre con 0x490b40(+0x44, 10, …), que busca la ventana
// del id en el administrador: los botones del diálogo no tienen padre y los de la ventana 2 del gremio cuelgan de una
// que no está en él (*inferido*): no llega a nadie. Apagado (6), nada. Con el mouse afuera y el botón apretado, suelta
// todo. Es la copia del hito 8, que devuelve el soltado (MuRet no tiene StepTheButtonMouse con lo que devuelve).
[[nodiscard]] bool ClickTheButton(OldButton& button, const OldWindowGlobals& globals, std::int32_t mouseX,
                                  std::int32_t mouseY);

// 0x6605b0: abierto, el mouse sobre la caja (0x140 − ancho / 2, 0x82 − alto / 2) es de la UI; después, los botones
// (0x45b200) o el reloj, y la respuesta.
[[nodiscard]] Core::Expected<void, std::string> StepThePanelDialog(PanelDialogState& dialog,
                                                                   const OldWindowGlobals& globals, std::int32_t mouseX,
                                                                   std::int32_t mouseY, std::uint32_t now,
                                                                   const PanelDialogAnswer& answer);

// 0x660830, la parte que el paso lee: la caja (0x6576a0: x 0xd6 e y 0x82 − alto / 2, nunca menos de 40, o 10 con
// +0xa24) y la y de los botones (y + 0x14, 15 por renglón, 17 con el campo, 17 con el reloj y 9), con sus x. Devuelve
// la esquina de la caja.
UI::Point PlaceThePanelDialog(PanelDialogState& dialog);
} // namespace Client::UI
