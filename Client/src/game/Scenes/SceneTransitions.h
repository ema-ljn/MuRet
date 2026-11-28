#pragma once

#include "Scenes/SceneId.h"

#include <algorithm>
#include <array>

// Las transiciones de escena del original: los ocho `mov` a DAT_006d5dc8 del binario, todos con un
// número fijo (02 § Las cinco escenas y sus transiciones; leídos otra vez en el paso 1 de la
// CL5.4, *estático*). El SceneManager cambia de escena solo por una de estas; otra es un error en
// el log y no cambia nada.
namespace Client::Scenes
{
// En la .data de c32bbd5f, DAT_006d5dc8 vale 1: el juego arranca en la carga.
inline constexpr SceneId FirstScene = SceneId::Loading;

struct SceneTransition
{
    SceneId from = SceneId::Loading;
    SceneId to = SceneId::Loading;
};

// Por escena de origen, y dónde la escribe el original. Las cuatro de abajo son de 0x4c2710, la
// respuesta cifrada del F1 02: con pkt[4] en 1 va a la 4 (0x4c363f) y con 2, a la 2 (0x4c2f1a). El
// original no mira en qué escena está (solo si es la 5, para limpiar el juego): también la
// cambiaría en el login o en la carga del mundo. MuRet deja las que tienen sentido, desde el juego
// y desde la selección (Q-C79).
inline constexpr std::array<SceneTransition, 8> OriginalTransitions = {{
    {SceneId::Loading, SceneId::Login},                // 0x622f93, al final de 0x622e40
    {SceneId::Login, SceneId::CharacterSelect},        // 0x634dce, en 0x634c60: el login aceptado
    {SceneId::CharacterSelect, SceneId::WorldLoading}, // 0x633f05 (0x633c10), 0x634470 y 0x634638 (0x6342c0)
    {SceneId::WorldLoading, SceneId::Game},            // 0x635a91, en 0x6356f0
    {SceneId::Game, SceneId::CharacterSelect},         // 0x4c363f
    {SceneId::CharacterSelect, SceneId::CharacterSelect},
    {SceneId::Game, SceneId::Login}, // 0x4c2f1a
    {SceneId::CharacterSelect, SceneId::Login},
}};

// Si de `from` se puede ir a `to`.
[[nodiscard]] constexpr bool IsOriginalTransition(SceneId from, SceneId to)
{
    return std::any_of(OriginalTransitions.begin(), OriginalTransitions.end(),
                       [from, to](const SceneTransition& transition)
                       { return transition.from == from && transition.to == to; });
}
} // namespace Client::Scenes
