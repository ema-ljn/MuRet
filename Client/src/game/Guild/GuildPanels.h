#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Text
{
class ITextMeasure;
}

// Las dos ventanas del gremio y el diálogo de los paneles en el registro (CL9 hito 8; 06 § El gremio): lo que hacen al
// cerrarse (0x6522f0 y 0x6570b0) y las dos funciones que responden al diálogo (0x4d6b60 y 0x653c00). Lo que mandan va
// por `send` (el GameServer de la sesión, en claro).
namespace Client::Scenes
{
class WorldState;
struct WorldRuntime;

// El envío de los paquetes del gremio (la sesión, o lo que la UI le pide).
using GuildSender = std::function<void(Protocol::Outgoing)>;

// 0x6600d0 (CUIPopup::SetPopup, por sus OutputDebugStringA de 0x6d69c4 y 0x6d69f4) desde el juego: abre el diálogo de
// los paneles y devuelve lo que da, el GetTickCount() de la apertura (+0x10), o 0 sin abrirlo. Con más de diez
// renglones, 0. Con otro abierto (0x6600f7..0x66011d), el aviso de 0x6d69a8 al chat (0x555190("", aviso, 1)) y 0, sin
// tocar el abierto. Un renglón de 0x100 bytes o más corta (Q-C1305).
[[nodiscard]] Core::Expected<std::uint32_t, std::string> PopUpThePanelDialog(WorldRuntime& runtime,
                                                                             const UI::PanelDialogRequest& request,
                                                                             const Render::Text::ITextMeasure& measure,
                                                                             App::PixelSize screen, std::uint32_t now);

// 0x4d6b60 (el 50: con 8 acepta, C1 06 51 01; con cualquier otro motivo, 00) y 0x653c00 (el E5: con 8, C1 08 E6 con 1;
// con 0x10, con 0; con los demás, nada). Las dos devuelven 1: el diálogo se cierra. Sin `send`, corta.
[[nodiscard]] Core::Expected<bool, std::string> AnswerThePanelDialog(WorldState& world, const GuildSender& send,
                                                                     UI::PanelDialogOwner owner, std::uint32_t reason);

// 0x6522f0, la vtable + 0x2c de [0x7d536f0]: abierta, la cierra (+0x7c), vuelve a la pestaña 1 (+0x80 = 1, +0x84 = 0),
// suena el 0x1c y cancela el diálogo si es el suyo (0x64fbd0: el abierto es +0x428).
[[nodiscard]] Core::Expected<void, std::string> CloseTheGuildWindow(WorldState& world, const GuildSender& send);

// 0x64fbd0: si el diálogo de los paneles abierto (0x660340) es el de la ventana del gremio (+0x428; los dos en 0
// también), lo cancela (0x6603c0, con la respuesta de quien lo abrió).
[[nodiscard]] Core::Expected<void, std::string> CancelTheGuildWindowDialog(WorldState& world, const GuildSender& send);

// 0x6570b0, la vtable + 0x2c de [0x7d536f4]: abierta, la cierra; el héroe con el gremio 1999 vuelve a −1; suena el
// 0x1c; 0x7d536e4, 0x7d536ec y 0x77f3b68 en 0; cancela el diálogo si es suyo (0x654f50: +0x2a0 o +0x2a4) y manda C1 04
// 54 00.
[[nodiscard]] Core::Expected<void, std::string> CloseTheSecondGuildWindow(WorldState& world, const GuildSender& send);

// Los ganchos del gremio en el contexto del registro: abrir el gremio pide la lista, cerrar las dos ventanas y
// responder al diálogo. Sin `send`, no pone ninguno y lo que los usa corta.
void LendTheGuildTo(UI::PanelContext& context, WorldState& world, const GuildSender& send);

// 0x6605b0 en el paso de la UI: el diálogo con el mouse (640 × 480), los globales de la UI vieja y el reloj (el
// GetTickCount() del paso). Con los ganchos de la ventana de los cargos (+8), su paso (0x64e600, que manda por `send`).
void LendThePanelDialogStepTo(UI::PanelContext& context, WorldState& world, const GuildSender& send,
                              std::int32_t mouseX, std::int32_t mouseY, std::uint32_t now);
} // namespace Client::Scenes
