#pragma once

#include "Data/Text/TextTable.h"
#include "Protocol/Character/CharacterList.h"
#include "UI/Notices.h"

#include <cstdint>
#include <optional>

// Las revisiones del botón de borrar de la selección (0x63edf0; Client/docs/research/06-render-
// escenas-y-ui.md, "Borrar un personaje"). *Estático*.
namespace Client::UI
{
// Si el grupo del servidor elegido deja borrar a uno de nivel alto: strcmp de su nombre
// (GroupNameText de código / 20, el que puso 0x4c1860 en 0x7d36bb0 + 0x21e · DAT_006d5ec4) con el
// texto 559. Un texto que falta cuenta como vacío. Sin servidor elegido (MuRet sin ConnectServer),
// sí: el original pasa siempre por la lista.
[[nodiscard]] bool FreeDeletionGroup(const Data::Text::TextTable* texts, std::optional<std::uint16_t> chosenServer);

// El aviso que abre el botón de borrar con `chosen`, en este orden: con gremio (+0x368 distinto de
// 0xff), el 0x92; de nivel 80 o más (0x6d5e24) fuera del grupo libre, el 0x83; con las banderas
// 0x12, el 0x61; si no, la confirmación 0x5e.
[[nodiscard]] NoticeId NoticeOfDeleteButton(const Protocol::Character::CharacterEntry& chosen, bool freeDeletionGroup);
} // namespace Client::UI
