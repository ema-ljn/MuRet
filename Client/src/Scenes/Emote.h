#pragma once

#include "Core/Expected.h"
#include "Scenes/ActionSend.h"
#include "UI/Emotes.h"

#include <string>

// El emote de un personaje (CL9 hito 1; 06 § El chat, "El emote"): 0x576a60 y lo que hace cada caso de 0x576eb0 con
// lo que encontró UI::FindTheEmote. Ninguna de las dos saca rand(), y un C1 no lo saca. *Estático*.
namespace Client::Scenes
{
struct ActorState;
class IModelLibrary;

struct EmoteContext
{
    // [0x743a170], el héroe: la dirección de todos los C1 18 sale de su ángulo (+0x24), sea cual sea el personaje.
    ActorState& hero;
    // Los modelos: 0x4ea030 mira las acciones del modelo del tipo del personaje (+2; 0x5769ad8 + tipo · 0xe0, su +0x26,
    // en 0x4ea036..0x4ea054), Player.bmd con un jugador y otro con un transformado (0x45).
    const IModelLibrary& models;
    ActionSender send;
};

// 0x576a60(personaje, personaje, animación, acción): con la acción de ahora del personaje (+0x111) de 1 a 12 o en 0x8a
// (las de parado de 0x4f2250, antes de las de caminar, 0x0d..0x21), pone la animación con 0x4ea030 (con un elfo,
// +0x1d4 & 7 == 2, la siguiente, salvo de 0xb7 a 0xc0) y manda el C1 18 de la acción; si no, nada. Devuelve si actuó.
// Los que la llaman (0x576eb0 y la guerra de gremios: 0x4d8260 y 0x4d8870, hito 8) pasan dos veces el mismo
// personaje, el de la clase y el de la acción; MuRet lo recibe una vez.
[[nodiscard]] Core::Expected<bool, std::string> PlayTheEmote(ActorState& actor, UI::EmoteCase emote,
                                                             const EmoteContext& context);

// Un caso de 0x576eb0 (0x57ca85 y los demás): 0x576a60 con el héroe y después, haya actuado o no, su propio C1 18 con
// la acción del caso. Con el héroe parado salen dos C1 18 iguales (Q-C776); caminando, pegando o en otra acción, solo
// el del caso, y el héroe no se anima.
[[nodiscard]] Core::Expected<void, std::string> DoTheEmote(UI::EmoteCase emote, const EmoteContext& context);
} // namespace Client::Scenes
