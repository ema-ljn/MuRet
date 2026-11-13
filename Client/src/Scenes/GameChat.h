#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "UI/ChatInput.h"

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

// La entrada del chat en el juego (CL8 hito 3; 06 § El chat): lo que el WndProc (0x4af4e0) hace con los WM_CHAR que
// le llegan o que le pasa la subclase de los EDIT (0x467b10: Tab y Enter), lo que los EDIT hacen con los demás, y las
// macros de 0x59ff10 (Alt y un número). Lo que se manda (el C1 00, el C1 02 y los C1 18 del emote, CL9 hito 1) va por
// `send`; lo que se avisa, por 0x555190 (AddGameMessage). Lo que el jugador escribe y a quién le susurra no va al log.
// *Estático*.
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;

// Quien manda los paquetes del chat (App::ServerSession::SendChat). Vacío, no se manda nada.
using ChatSender = std::function<void(Protocol::Outgoing)>;

struct GameChatContext
{
    WorldState& world;
    // El rand() de la tela: el registro propio de 0x554a40 lo gasta al cerrarse.
    Render::Models::CrtRandom& random;
    // Text.bmd: los comandos, los avisos y el "..." del filtro. Sin él, el primero que se lee es el error.
    const Data::Text::TextTable* texts = nullptr;
    // La cola de los nombres de 0x56c440 (UI::ChatCommandContext); sin ellas, no hay nombres.
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::ItemSetOptionTable* setOptions = nullptr;
    // Las palabras de Filter.bmd (0x622b40).
    std::span<const std::string> filter{};
    // Las de FilterName.bmd, que con Filter.bmd mira 0x622c70 en el nombre del gremio nuevo (0x654f80).
    std::span<const std::string> filterName{};
    // GetTextExtentPointA sobre el DC de los textos, que 0x555190 usa al cortar. Sin él, un aviso es el error.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    ChatSender send{};
    // Los modelos: 0x4ea030 mira las acciones del modelo del tipo del héroe al animar su emote (0x576a60). Sin ellos,
    // un emote es el error.
    const IModelLibrary* models = nullptr;
};

// Los WM_CHAR del paso, en su orden: Tab (TabChar) al WndProc (0x4b4f12), Enter (EnterChar) al WndProc
// (PressEnterInTheGame), y lo demás al EDIT con el foco (Backspace borra; sin un EDIT con el foco, la ventana del juego
// lo descarta). La subclase descarta Esc y los bytes 0x19 a 0x1c; TextField, los que no son imprimibles.
[[nodiscard]] Core::Expected<void, std::string> TypeInTheGameChat(const GameChatContext& context,
                                                                  std::string_view typed);

// 0x4b50b0..0x4b88e9: Enter en el WndProc, con el modo de los EDIT (0x6c7b8c en 1). Las compuertas (Chaos Castle
// fuera de la zona segura, los diálogos 0x7d, 0x95, 0x8a, 0x8d y 0x8b, 0x78abb00 en 3 y la tecla para los paneles,
// 0x657880(0xd)); con el campo vacío lo abre o lo cierra; si no, lo lee al historial, corre los comandos (0x56c440),
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
