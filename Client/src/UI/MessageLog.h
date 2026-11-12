#pragma once

#include "Core/Expected.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// La entrada de los mensajes del juego (CL8 hito 3; 06 § El chat): 0x555190(nombre, texto, tipo), que llaman 98
// lugares del original, y su reloj (0x5554b0). Primero va al registro nuevo (0x45bb20 por la vtable + 0x94, con el
// cuarto argumento en 0) y después, si es para ella, al área vieja.
namespace Client::UI
{
// Lo que tocan 0x555190 y su reloj: las dos áreas, los modos del registro, el DC para medir y la opción del sonido
// de los avisos (0x77f3b78, que pone el F3 30 y cambia el menú de Esc).
struct MessageSink
{
    ChatLogState& log;
    MessageAreaState& area;
    ChatModes modes;
    ChatMeasure measure;
    bool noticeSound = false;
};

// 0x555190. Devuelve el sonido que pide el registro (0x26 con el filtro, ChatLog.h) o el error del corte.
[[nodiscard]] Core::Expected<std::optional<std::int32_t>, std::string>
AddMessage(const MessageSink& sink, std::string_view name, std::string_view text, std::int32_t type);

// 0x5554b0, por paso: con el reloj del área vieja en 0 o menos antes de bajarlo, 150 y un mensaje vacío
// (0x555190 con 0x77f3bcc y 0x77f3bc8, dos textos vacíos de la .bss), que envejece el registro y corre el área.
[[nodiscard]] Core::Expected<std::optional<std::int32_t>, std::string> StepTheMessageClock(const MessageSink& sink);
} // namespace Client::UI
