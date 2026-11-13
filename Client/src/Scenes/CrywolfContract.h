#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Data::Text
{
class TextTable;
}

// CL9 hito 11, fase 4: el contrato de los altares de Crywolf (06 § La interfaz de Crywolf), *estático*. GS 0.97 no
// tiene Crywolf: el oráculo es el ASM del cliente. Lo que llena la ventana (0x723a44, sus dos renglones y 0x723be0) es
// 0x435b00, desde el BD 03 (0x4e1010, Scenes/EventPackets), el clic en un altar (0x567b00, dentro de 0x5650c0) y los
// botones de la misma ventana; su dibujo es 0x436240 (Scenes/CrywolfInterfaceDrawing).
namespace Client::Scenes
{
struct ActorState;
struct WorldRuntime;

// Los mensajes de 0x435b00: el texto 1950 + n de Text.bmd ([0x774bf04 + 300 · n]).
inline constexpr std::int32_t ContractRefusedMessage = 2; // el No (0x435ca4)
inline constexpr std::int32_t ContractSignedMessage = 3;  // el BD 03 con 1 (0x4e11a3), y el 4 abajo
inline constexpr std::int32_t ContractSignedSecondMessage = 4;
inline constexpr std::int32_t ContractWithPetMessage = 6;   // el Sí con la mascota (0x4361ac)
inline constexpr std::int32_t AltarTakenMessage = 0x34;     // el altar ya contratado (0x567c1b)
inline constexpr std::int32_t AltarSpentMessage = 0x35;     // el altar sin contratos (0x567be0)
inline constexpr std::int32_t ContractFailedMessage = 0x36; // el BD 03 con 0 y menos de 350 (0x4e1152), y el 0x37
inline constexpr std::int32_t ContractFailedSecondMessage = 0x37;
inline constexpr std::int32_t AltarOfferMessage = 0x38;    // wsprintfA con 2006 y los contratos que quedan
inline constexpr std::int32_t AltarQuestionMessage = 0x39; // la pregunta, que abre la ventana 1
inline constexpr std::int32_t ContractLevelMessage = 0x3a; // el BD 03 con 0 y 350 o más (0x4e117a)

// Los argumentos de 0x435b00(mensaje, renglón, valor, altar), en ese orden en la pila.
struct ContractMessage
{
    std::int32_t message = 0;
    // 0 o 1: 0x723a48 + 200 · renglón.
    std::int32_t line = 0;
    // A 0x723be0: la clave del altar que manda el Sí, o 0.
    std::int32_t value = 0;
    // Solo con 0x38: el altar de 0x6c73cc, de 0 a 4. Los demás pasan −1.
    std::int32_t altar = -1;
};

// 0x435b00: con la etapa en 3 o 4 (0x435a00), en el mapa 34 (0x4303c0) y sin la salida pedida (0x576793c). Con 0x38,
// wsprintfA(renglón, texto 2006, altar & 0xf) y la ventana 1; con otro, strcpy(renglón, texto 1950 + n) y la ventana
// 2, que el 0x39 pasa a 1. Con el renglón 0 vacía el 1; al final, 0x723be0 = valor. Fuera de su guarda no hace nada.
// Corta sin el texto, con un renglón o un altar fuera de sus tablas o con un texto de 200 bytes o más, que pisaría lo
// que sigue al renglón (el strcpy y wsprintfA no tienen tope).
[[nodiscard]] Core::Expected<void, std::string>
ShowTheContractMessage(WorldRuntime& runtime, const Data::Text::TextTable* texts, const ContractMessage& message);

// 0x435ad0(altar, valor): con la etapa en 3 o 4 en el mapa 34, el byte de 0x6c73cc + altar; no mira la salida pedida.
// El altar sale de la clave del BD 03 − 0x13c sin tope (Q-C1347): fuera de 0..4 corta.
[[nodiscard]] Core::Expected<void, std::string> SetTheAltar(WorldRuntime& runtime, std::int32_t altar,
                                                            std::uint8_t value);

struct ContractWindowContext
{
    WorldRuntime& runtime;
    const Data::Text::TextTable* texts = nullptr;
    // El word +0x318 del héroe (0x435cf7): con 0x1c14, 0x1c15 o 0x1c37 el Sí no manda nada.
    std::uint16_t heroPet = 0;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // El send() del paso, el del C1 06 BD 03. Vacío, el Sí corta después de 0x723bdc.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x435c5c..0x436220, el paso de la ventana del contrato dentro de 0x435bd0 (ya en el mapa 34), con el izquierdo abajo
// (0x7d36874) y sin los bordes de cada caja:
//   - la ventana 1: el No en (0x14a, 0x180) × (0xfa, 0x118), 0x723bdc en 1 y 0x435b00(2); después el Sí en (0xfa,
//     0x130) × (0xfa, 0x118): con la mascota, 0x435b00(6); si no, 0x723bdc en 2 y el C1 06 BD 03 de 0x723be0. Con el
//     botón abajo, en cualquier lugar, lo apaga con el flanco (0x7d366d4): se queda con todos los clics;
//   - la ventana 2: el OK en (0x122, 0x158) × (0xfa, 0x118) apaga el botón y el flanco y deja 0x723bdc en 3.
// No saca rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheContractWindow(const ContractWindowContext& context);

// Por dónde sigue 0x5650c0 después del clic en un altar.
enum class AltarClick : std::uint8_t
{
    // 0x5688ca (sin la etapa 3 o 4 en el 34) y 0x568288 (el tipo del blanco fuera de 0x204..0x208): no es el caso.
    NotAnAltar,
    // 0x567c2d: el héroe no es una elfa (+0x1d4 & 7 distinto de 2): el C1 30 de hablar con el blanco.
    Talk,
    // 0x435b00 con lo que tocó; 0x5650c0 sigue en 0x569573 (0x6eeda8 y 0x6c8e54 en −1).
    Contract,
};

// 0x567b00..0x567c23, la llegada al blanco 0x6c8e54 de 0x5650c0 con un altar (los tipos 0x204..0x208, el altar 0..4):
// con una elfa, el altar contratado (0x4365a0, el nibble alto en 1) da 0x435b00(0x34); sin contratos (el nibble bajo
// en 0), 0x435b00(0x35); con contratos, 0x435b00(0x38, 0, clave, altar) y 0x435b00(0x39, 1, clave, −1), con la clave
// +0x1f4 del blanco con signo. La llama la llegada a un NPC con Crywolf activo (0x5650c0 con +0x379 en 2,
// Scenes::TalkToTheNpc), antes del C1 30.
[[nodiscard]] Core::Expected<AltarClick, std::string> ClickTheCrywolfAltar(WorldRuntime& runtime,
                                                                           const Data::Text::TextTable* texts,
                                                                           const ActorState& hero,
                                                                           const ActorState& target);
} // namespace Client::Scenes
