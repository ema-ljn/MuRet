#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Input/TextInputEvent.h"
#include "Protocol/Outgoing.h"
#include "UI/ChatInput.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform

// La entrada del chat en el juego (CL8 hito 3; 06 § El chat): lo que el WndProc (0x4af4e0) hace con los WM_CHAR que
// le llegan o que le pasa la subclase de los EDIT (0x467b10: Tab y Enter), lo que los EDIT hacen con los demás, y las
// macros de 0x59ff10 (Alt y un número). Lo que se manda (el C1 00, el C1 02 y los C1 18 del emote, CL9 hito 1) va por
// `send`; lo que se avisa, por 0x555190 (AddGameMessage). Lo que el jugador escribe y a quién le susurra no va al log.
// *Estático*.
namespace Client::Scenes
{
class WorldState;

// Quien manda los paquetes del chat (App::ServerSession::SendChat). Vacío, no se manda nada.
using ChatSender = std::function<void(Protocol::Outgoing)>;

struct GameChatContext
{
    WorldState& world;
    // El rand() de la tela: el registro propio de 0x554a40 lo gasta al cerrarse.
    Crt::CrtRandom& random;
    // Text.bmd: los comandos, los avisos y el "..." del filtro. Sin él, el primero que se lee es el error.
    const Tables::TextTable* texts = nullptr;
    // La cola de los nombres de 0x56c440 (UI::ChatCommandContext); sin ellas, no hay nombres.
    const Tables::ItemTable* items = nullptr;
    const Tables::ItemSetOptionTable* setOptions = nullptr;
    // Las palabras de Filter.bmd (0x622b40).
    std::span<const std::string> filter{};
    // Las de FilterName.bmd, que con Filter.bmd mira 0x622c70 en el nombre del gremio nuevo (0x654f80).
    std::span<const std::string> filterName{};
    // GetTextExtentPointA sobre el DC de los textos, que 0x555190 usa al cortar. Sin él, un aviso es el error.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    ChatSender send{};
    // El C1 03 31 que /personalshop manda al cerrar las misiones o un NPC de evento (0x419900, 0x657a90).
    std::function<void()> sendWindowClose{};
    Platform::IClipboard* clipboard = nullptr;
    // Biblioteca base: el emote consulta primero FaunaResources(), donde el viewport carga los transformados,
    // y después ésta. 0x4ea030 mira las acciones del tipo del héroe; si ninguna ofrece el modelo, corta.
    const Models::IModelLibrary* models = nullptr;
};

// 0x6a0c01 (atoi) → 0x6a0b76 (atol de la CRT) con el locale "C": los blancos (el espacio y de 0x09 a 0x0d), un '-' o
// un '+' y las cifras, con la cuenta en 32 bits que da la vuelta sin aviso; con '-', negada. Lo usan el Enter con el
// zen escrito (0x4b5574) y el OK del precio de la tienda (0x6261ea).
[[nodiscard]] std::int32_t AtoiLikeTheCrt(std::string_view text);

// Los WM_CHAR del paso, en su orden: Tab (TabChar) al WndProc (0x4b4f12), Enter (EnterChar) al WndProc
// (PressEnterInTheGame), y lo demás al EDIT con el foco (Backspace borra; sin un EDIT con el foco, la ventana del juego
// lo descarta). La subclase descarta Esc y los bytes 0x19 a 0x1c; TextField, los que no son imprimibles.
[[nodiscard]] Core::Expected<void, std::string> TypeInTheGameChat(const GameChatContext& context,
                                                                  std::string_view typed);

// Conserva el orden de SDL_TEXTINPUT, las teclas de edición y SDL_TEXTEDITING del paso de la escena.
[[nodiscard]] Core::Expected<void, std::string> TypeTheGameInput(const GameChatContext& context,
                                                                 std::span<const Input::TextInputEvent> events);

// 0x4b50b0..0x4b88e9: Enter en el WndProc, con el modo de los EDIT (0x6c7b8c en 1). Las compuertas (Chaos Castle
// fuera de la zona segura, los diálogos 0x7d, 0x95, 0x8a, 0x8d y 0x8b, 0x78abb00 en 3 y la tecla para los paneles,
// 0x657880(0xd)); con el campo vacío lo abre (con el largo del diálogo en 0x2a y sin el modo del zen) o lo cierra, y
// con el diálogo 0x73 vacía el EDIT de [0x5763504] y le da el foco al juego; si no, con el modo del zen (0x77f3b6a)
// guarda el atoi de lo escrito en 0x77f3b6c, vacía y oculta ese EDIT y vacía el campo, y con 0x78aba60 en 1 guarda lo
// escrito como el título de la tienda si 0x5ebbd0 lo deja (si no, el texto 1130) y vacía el campo; si no,
// lo lee al historial, corre los comandos (0x56c440),
// el filtro (0x622b40 con 1: el texto 570) y, con '/' adelante, sin nombre o sin el modo del susurro (F3), el chat
// (los emotes con sus C1 18, el freno, "webzen", /whisper y /move, y el C1 00); si no, el susurro (0x554a40 con el
// nivel 6, el C1 02 y el texto en el registro). Al final vacía el campo del mensaje, oculta los EDIT, corre el registro
// 100 y cierra el campo.
[[nodiscard]] Core::Expected<void, std::string> PressEnterInTheGame(const GameChatContext& context);

// 0x4b5734..0x4b6f54 (Enter), 0x5a1214..0x5a1e9e (las macros) y 0x4488d3..0x448ef8 (la ventana /move, en línea): el
// freno (0x5767940 y lo último dicho, 0x57676e4), "webzen" en el nombre del héroe, /whisper off y on (vivo) o /move
// (muriéndose), y el C1 00 con el nombre del héroe.
[[nodiscard]] Core::Expected<void, std::string> SayInTheChat(const GameChatContext& context, std::string_view said);

// 0x5a119f..0x5a2b47: con Alt (y el freno 0x77f3b74 en 0, fuera de Chaos Castle, que mira quien llama), cada macro
// de la tecla apretada de 1 a 9 y 0, en ese orden: los comandos (0x56c440 desde una macro), los emotes, el filtro y
// lo mismo que el chat de Enter, sin el historial y siempre con el C1 00. Cada una que pasa los comandos deja el
// freno en UI::MacroLockSteps.
[[nodiscard]] Core::Expected<void, std::string> SayTheMacros(const GameChatContext& context,
                                                             const std::array<bool, UI::MacroCount>& pressed);
} // namespace Client::Scenes
