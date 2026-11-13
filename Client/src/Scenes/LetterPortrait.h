#pragma once

#include "Core/Expected.h"
#include "GameLogic/CharacterAppearance.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/HelperTable.h"
#include "UI/OldWindows.h"

#include <cstdint>
#include <string>

// El personaje del retrato de las cartas (vtable 0x6ba008, 0x495e50): el objeto que la ventana de leer (+0xb0) y la de
// escribir (+0xb4) llevan, con su personaje propio (+0x7c), su ayudante propio (+0x4b4) y lo que lo arma, lo viste, lo
// copia del héroe y le cambia la acción. Lo de la ventana (el lugar, el mouse, el ángulo, el zoom y la acción de
// +0x6a4) es de UI (UI::OldPortraitPart). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El retrato de las
// cartas". Todo *estático*, del ASM.
namespace Client::Scenes
{
struct WorldEntryResources;

struct LetterPortrait
{
    // +0x7c: un registro de 0x438 bytes como los del pool, fuera de él. Nunca es el héroe publicado.
    ActorState actor{};
    // +0x4b4: un lugar de 0x1d4 bytes como los de la tabla de los ayudantes (0x7d335d0), fuera de ella. El jinete
    // (+0x108) es el personaje de arriba: en MuRet, `rider` en ActorPoolSize.
    HelperState helper{};
    // +0x688: la escala del ayudante, por la del personaje (0x497810 y la cola de 0x496530) y el zoom (0x495760).
    float helperScale = 0.0f;
    // +0x698: la acción quieta del dibujo (0x497c10); 0x497910 lo pone en 1.
    std::int32_t idle = 0;
    // +0x6a0: la última acción que mostró 0x495a20.
    std::int32_t shownAction = 0;
    // +0x6a8: el frame entero que guarda el dibujo (0x497c10); 0x497910 lo pone en 0.
    std::int32_t frameMark = 0;
    // Las telas del personaje (objeto + 0x18c/+0x190), que suelta 0x4fb1a0 y arma el dibujo (0x50ba30).
    CharacterCloth cloth{};
};

// 0x4961d0 (la vtable + 0x30): el ayudante en cero; las entradas de la celda del personaje (+0x41c y +0x420) abiertas
// mientras borra el personaje y cerradas, la X y después la Y (4 rand() cada una si quedan en 0); la celda lógica del
// héroe publicado, la Y y después la X (otros 4 y 4); 0x511290(personaje, 0x20a, X, Y, 0), con sus 28 rand(); y el
// lugar en (−300, −300, −300). Desde cero, 44 rand(). Quien llama pone +0x68c en 1.
[[nodiscard]] Core::Expected<void, std::string> BuildThePortrait(LetterPortrait& portrait,
                                                                 const WorldEntryResources& resources);

// 0x4977d0 (la vtable + 0x34), armado: la clase (+0x1d4) y 0x513100.
[[nodiscard]] Core::Expected<void, std::string>
SetThePortraitClass(LetterPortrait& portrait, std::uint8_t characterClass, const WorldEntryResources& resources);

// 0x497810 (la vtable + 0x38), armado: 0x5139f0(0, equipo, personaje, ayudante) (ApplyCharacterSetTo), +0x688 con la
// escala del ayudante por 0,7f (0x6b8a5c) entre la del héroe publicado ([0x743a170] + 0xc), y la zona segura (+0x3e2)
// en 1 con alas y la acción `action` (+0x6a4) pasada de 0x82, en 0 si no.
[[nodiscard]] Core::Expected<void, std::string> DressThePortrait(LetterPortrait& portrait,
                                                                 GameLogic::Equipment equipment, std::int32_t action,
                                                                 const WorldEntryResources& resources);

// 0x496530 (la vtable + 0x3c), armado: la clase del héroe publicado (con 0x513100 si cambia) y, por grupos (la cabeza
// con el cuerpo, las manos, las alas y la mascota), lo que difiere: del héroe tal cual con +0x3e3 en 0, o del registro
// propio con +0x3e3 distinto de 0 (con su acceso y los 3.848 rand() de su cierre al comparar y en cada grupo que
// copia). Con la mascota distinta, 0x495a20(0x7a, 0) y `action` (+0x6a4) en 0x7a; si no, 0x495a20 con la que mostró.
// Con la mascota o las manos distintas, el ayudante de nuevo. Devuelve si algo difería.
[[nodiscard]] Core::Expected<bool, std::string> CopyTheHeroToThePortrait(LetterPortrait& portrait, std::int32_t& action,
                                                                         const WorldEntryResources& resources);

// 0x495a20(acción, corrimiento): la acción de la lista (la del ayudante montado si lo hay, la de los gestos si no) que
// queda a `delta` de `action`, puesta en el personaje (0x4f2250, 0x4f4a10 o 0x4f37c0 con el mundo en 0, o 0x4ea030 con
// la tabla de los gestos), y 0x4fd5f0 y 0x505de0 con el mundo en curso. Devuelve la acción y la deja en +0x6a0.
[[nodiscard]] Core::Expected<std::int32_t, std::string> ShowThePortraitAction(LetterPortrait& portrait,
                                                                              std::int32_t action, std::int32_t delta,
                                                                              const WorldEntryResources& resources);

// La parte del personaje de 0x497890 (la vtable + 0x40), armado: los ángulos (+0x1c..+0x24) en (−20, 5, 60).
void TurnThePortrait(LetterPortrait& portrait);

// El gancho de UI::OldWindowStepView::portrait con el juego de `resources`, que vive más que él: el primer pedido arma
// el LetterPortrait de UI::OldPortraitPart::actor. Build (0x4961d0) pone +0x68c en 1; SetClass, SetEquipment,
// CopyTheHero y SetAngle no hacen nada sin armar (0x4977d9, 0x49781e, 0x496548, 0x49789b); SetAction (0x4978f0) y
// TurnAction (0x497910: la acción que da 0x495a20 en +0x6a4, +0x6a8 en 0 y +0x698 en 1) no lo miran; Step copia +0x360
// del héroe (0x4979c0).
[[nodiscard]] UI::OldPortraitHook LetterPortraitHook(const WorldEntryResources& resources);
} // namespace Client::Scenes
