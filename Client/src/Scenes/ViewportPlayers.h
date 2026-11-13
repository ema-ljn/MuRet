#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Protocol::Viewport
{
struct Players;
struct Leave;
struct Transformed;
struct TransformedRecord;
struct EquipmentShown;
struct Position;
} // namespace Client::Protocol::Viewport

// Los otros jugadores entran y salen del viewport (CL9 hito 1): el 0x12, el 0x14, el 0x45, el 0x25 y el 0xDF. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". Todo *estático*, del ASM. Lo
// que MuRet no tiene corta con error, con su dirección, después de lo que el original hace antes. Las búsquedas
// 0x5132d0 (el vivo con un nombre), 0x5178f0 (un int en un arreglo) y 0x517920 (los jugadores cerca de un punto) no
// tienen llamadores ni punteros en el binario: no se portan.
namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::Scenes
{
class WorldState;
struct ActorState;
struct WorldEntryResources;

// Lo que comparten las altas del viewport (los jugadores, el 45, el DF y los monstruos de MonsterViewport.h):
// Una escritura de la celda protegida (+0x41c o +0x420) con su acceso abierto: 4 rand() al cerrarse si queda sin
// accesos.
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Render::Models::CrtRandom& random);
// Una lectura, con su acceso (0x40b800 o el cierre en línea).
[[nodiscard]] std::uint8_t ReadCell(const std::uint8_t& cell, std::uint8_t& depth, Render::Models::CrtRandom& random);
// La X y después la Y de la celda protegida, y +0x39a/+0x39b con los mismos valores.
void WriteTheCells(ActorState& actor, std::uint8_t x, std::uint8_t y, Render::Models::CrtRandom& random);
// (nibble alto − 1) · 45 (0x6b8848, con el 1 de 0x6b87d4).
[[nodiscard]] float AngleOf(std::uint8_t directionAndPk);
// La X y después la Y, leídas de la tabla, al centro de la celda.
void PutInTheCellCenter(ActorState& actor, Render::Models::CrtRandom& random);
// La Y, la X y 0x4eac00(X, Y, destino, +0x3e8, 0); +0x378 en 1 solo si hay camino.
[[nodiscard]] Core::Expected<void, std::string>
WalkToTheTarget(ActorState& actor, std::uint8_t targetX, std::uint8_t targetY, const WorldEntryResources& resources);

// El dispatcher (0x4e372e..0x4e37b8) guarda el paquete entero en la cola (la sesión, con CopyToPacketRing, antes de
// leerlo) y 0x4c8860 recorre los registros. Cada uno: si el nombre (los diez bytes, hasta su 0) tiene "webzen"
// (0x576960), nada. Si no, con el estado 1 guarda de un vivo con la clave (0x5110d0) el byte de abajo de +0x1f2,
// +0x368, +0x369, +0x370, +0x360, +0x78 y +0x372; el alta (CreateActor, tipo 0x20a, ángulo 0), que con lo guardado los
// recupera (+0x1f2 sin el byte de arriba, Q-C800); la clase de +4, la piel en 0, +0x88 en 1 y el PK; por el estado, 1:
// 0x53d930 y el paquete otra vez a la cola (Q-C801), 2: la acción 0xc5 (0xc7 el elfo), 3: 0xcb (0xcc), 4: 0xc9 (0xca),
// 0x4ea030; las banderas (0x4e6380); la celda protegida y +0x39a/+0x39b; el ángulo, (dirección − 1) · 45. Recién
// llegado (el bit 15): la posición en el centro de la celda, el efecto 0x61f (0x518cf0, sin el dueño en +0x108, como la
// entrada del héroe) y el alpha en 0. Si no, el camino hasta +0x26/+0x27 (0x4eac00) y, si hay, +0x378 en 1. En Kalima
// (24 a 29 y 36) corta en 0x4c91dc (el joint 0x610 de 0x530860, CL11). Después, el equipo (ApplyCharacterSet con el
// lugar de 0x5110d0), el nombre y +0x1e3 en 0, con el estado 1 otra vez +0x360 y +0x78, Castle Siege (0x42ab40) y el
// panel (0x412140). Cada celda escrita o leída gasta 4 rand() al cerrarse; el alta, los 28 de 0x511290 y los de sus
// ayudantes.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Viewport::Players& players,
                                                                     const WorldEntryResources& resources);

// 0x4ca780: por cada clave, con la información del asedio (0x5767924), la entera 0x13d pone sus bytes 4..6 en 0 y
// cualquier otra los 0x1f..0x21 (Q-C802); 0x510ff0 (RemoveActor) con la clave sin el bit 15; y 0x451630 busca la
// tienda personal de esa clave en el mapa de 0x7245c8, que solo llena el 0x3F (hito 7): vacío en MuRet, nunca llega a
// 0x451430.
void ApplyViewportLeave(const Protocol::Viewport::Leave& leave, WorldState& world);

// 0x4c92a0: con "webzen" en el nombre, el registro no hace nada. Si no, la clase del héroe publicado (0x4c9328), el
// alta del transformado o del monstruo de tipo +4 (CreateMonster, 0x5141e0, sin mirar si devuelve 0) y
// FinishTheTransformed.
[[nodiscard]] Core::Expected<void, std::string> ApplyTransformed(const Protocol::Viewport::Transformed& transformed,
                                                                 const WorldEntryResources& resources);

// 0x4c9356..0x4c9762, lo que 0x4c92a0 hace con el personaje `index` que devolvió 0x5141e0: con +0x376 en 7 la escala
// en 0,8; la clase del héroe publicado que leyó antes del alta (`heroClass`, 0x4c9328), el PK, +0x88 y +0x3e3 en 1;
// las banderas (0x4e6380 y 0x42ab40); la celda protegida, +0x39a/+0x39b y el ángulo. Recién llegado, la posición, el
// alpha en 0 y corta en 0x4c965c (el efecto 0xec de 0x518cf0 y el joint 0x5d5/2 de 0x53fe70, CL11); si no, el camino
// (0x4eac00) con la Y leída antes que la X. Después, el nombre, +0x1e3 en 0 y 0x512a30 sobre el héroe publicado.
[[nodiscard]] Core::Expected<void, std::string>
FinishTheTransformed(std::size_t index, const Protocol::Viewport::TransformedRecord& record, std::uint8_t heroClass,
                     const WorldEntryResources& resources);

// 0x4c8380: el personaje de la clave entera (0x5110d0, el 400 si no hay) y el ítem de pkt[5..]: el tipo (0x54b3a0) en
// el lugar del nibble de arriba de pkt[6], con 0x1fff sin ítem. Las manos (0, 1) con el nivel (0x513340 del nibble de
// abajo: de 8 a 15, 0) y el excelente (pkt[8] & 0x3f); vaciar la izquierda suelta el cuervo del héroe publicado, no el
// de este (Q-C803), y llenarla lo arma otra vez (0x420d60). Las cinco del cuerpo (2 a 6) con el set (pkt[9]); vacías,
// la parte de la clase. Las alas (7) con el nivel en 0. La mascota (8): vacía, 0x443d40; si no, el ayudante por tipo
// (0x1a00, 0x1a02, 0x1a03, 0x1a04 y Fenrir 0x1a25 con +0x31b en el excelente) con 0x443fa0. Del 9 al 15, nada. Al
// final, 0x4053f0 y 0x512930.
[[nodiscard]] Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Viewport::EquipmentShown& shown,
                                                                    const WorldEntryResources& resources);

// 0x4c6b40: el personaje de la clave entera (0x5110d0). Con el tipo 0xf1 (el que crea 0x5141e0 para los tipos 160,
// 180, 188, 196, 266, 274 y 337): +0xe4 en 20, la X y la Y de la celda leídas con su acceso, +0xd0 y +0xcc con las
// diferencias por 400 y el sonido 0x1b (WorldRuntime::lastSoundId, CL10); corta en 0x4c6c9d (los joints 0x5c5 de
// 0x51fe10, CL11). Si no, la celda protegida en pkt[5] y pkt[6], +0x39a/+0x39b y +0x399 en 1.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportPosition(const Protocol::Viewport::Position& position,
                                                                      const WorldEntryResources& resources);
} // namespace Client::Scenes
