#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// La caída de Kundun (0x43eeb9..0x43f144, desde su cuerpo en la acción de morir, MonsterBodyChain): los ocho pedazos
// 0x11d..0x124 (Monster\cd71a..cd71h, que sólo carga Kalima) que suelta en el hueso 4 y el 0x128/2, que vuelve a entrar
// en 0x518cf0 como el 0x127 (0x518f51) con él de dueño. Las altas (0x51e234 y 0x51cf6d) y los pasos (0x52a17b y
// 0x5277ae). Su dibujo es el camino común (0x52da92 → 0x602e90): 0x6002e0 no tiene caso propio para ellos. 06, "Los
// efectos con modelo de los tipos altos".
namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::World
{
struct ModelEffect;
struct ModelEffectArguments;
class ModelEffects;
} // namespace Client::World

namespace Client::Scenes
{
struct HighEffectStepContext;

inline constexpr std::int16_t FirstKundunPieceEffect = 0x11d;
inline constexpr std::int16_t LastKundunPieceEffect = 0x124;
inline constexpr std::int16_t KundunEchoEffect = 0x127;

// La altura del terreno de 0x5f93c0 en (x, y), lo que deja en la pila de la x87.
using KundunTerrainHeight = std::function<double(float, float)>;

// 0x51e234..0x51e348, un pedazo después del prefijo, por su subtipo (la tabla de 0x51fbe8):
// - /1: vida 100 y escala 2.
// - /2 y /3: la partida (+0x17c) en la x y la y, su z en la altura del terreno menos la clave (+0x8a, MOVSX) menos 80
//   (un fstp); la velocidad en 0, vida 500, escala 2 y la x de los ángulos copiados (+0x28) en rand() % 2, que el /2
//   vuelve a poner en 0.
// - /4: la partida en la x y la y y la altura del terreno menos la clave; la velocidad en 0, la acción (+0x111) en 0,
//   vida 170 y escala 2.
// - /5: la acción en 0, vida 170 y escala 2.
// Todos, la clave en 0xffff al final. Sin la altura del terreno, el /2, el /3 y el /4 cortan (0x51e26e, 0x51e2ee).
[[nodiscard]] Core::Expected<void, std::string> BeginTheKundunPiece(World::ModelEffect& effect,
                                                                    Render::Models::CrtRandom& random,
                                                                    const KundunTerrainHeight& terrainHeight);

// 0x518cf0(pedazo, ...) sobre `effects`, como lo pide la caída de Kundun: el prefijo (World::ModelEffects::Begin) y
// BeginTheKundunPiece. Con la tabla llena, nada y sin rand(). Devuelve el lugar.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateTheKundunPiece(World::ModelEffects& effects, const World::ModelEffectArguments& arguments,
                     Render::Models::CrtRandom& random, const KundunTerrainHeight& terrainHeight);

// 0x51cf6d..0x51cf91 y 0x51d9ff, el 0x127 de cualquier subtipo: +0xd8 en 0,08f, vida 200, +0x64 en −2, escala 1,8f y
// la luz en 0,5. Sin rand().
void BeginTheKundunEcho(World::ModelEffect& effect);

// 0x518f51..0x518f90: el 0x128/2 del lugar `call` vuelve a entrar en 0x518cf0 como el 0x127/0 con su posición, sus
// ángulos y su luz, él de dueño (+0x108), la clave en −1, la escala 0 (0,9f del prefijo) y BeginTheKundunEcho. Con la
// tabla llena, nada.
std::optional<std::size_t> CreateTheKundunEcho(World::ModelEffects& effects, std::size_t call);

// 0x52a17b..0x52a683, el paso de un pedazo por su subtipo (la tabla de 0x52d0fc):
// - /1: el alpha 0,01f menos.
// - /4: con 140 de vida o más, el frame (+0x114) en 0; si no, el alpha 0,01f menos y, por encima de la partida, la z
//   de la velocidad 3,5 menos, o la velocidad en 0.
// - /5: con 150 de vida o más, el frame en 0; si no, el alpha 0,01f menos.
// - /2 y /3: mientras la vida no baje de (0x5e − +0x89) · 5, el /3 con más de 385 de vida tiembla: con rand() % 2 en
//   0, unos ángulos locales (0, 0, rand() % 360) y, con otro rand() % 2 en 0, el joint 0x6ee/8 sin dueño con ellos
//   (ancho 50, clave 0); después, con rand() % 3 en 0, la partícula 0x5f5/2 con los ángulos del efecto (luz 0,5,
//   escala ((rand() & 31) + 48) · 0,02f). Después cae:
//   por encima de 350, con la velocidad en (0, 0, 0) la x y la y en rand() % 2 · 2 − 2, y la z 6 menos; si no, con la
//   z de la velocidad distinta de −3, la onda de Kalima (0x437860(x · 0,01, y · 0,01, 2, −1000)) y cien partículas
//   0x6f4/2 (0x52a37d, la alta de 0x543d68) en un círculo de radio rand() % 30 + 30, 30 más abajo y con la z de los
//   ángulos rand() % 180 más; siempre, la partícula 0x607/4 a ±20 en x e y y 50 más abajo, la velocidad en
//   (0, 0, −3), el balanceo de la x de los ángulos entre −30 y 30 de a rand() % 3 + 1 y, con menos de 100 de vida, el
//   alpha 0,1f menos.
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunPiece(World::ModelEffect& effect,
                                                                   const HighEffectStepContext& context);

// 0x5277ae..0x527ad1, el paso del 0x127:
// - Con dueño (+0x108, el 0x128/2): con su clave (+0x8a) en 0, el frame en 0; si no, el dueño en 0.
// - Sin dueño y el frame pasado 6: la clave uno más y la z clave · 0,8f más; en la posición con rand() % 120 − 60
//   en x e y y rand() % 60 en z, tres partículas 0x5f2/0x14 en blanco de escala 10.
// - Sin dueño y el frame hasta 6: +0x89 uno más; la z de los ángulos hacia 45 de a +0x89 · 0,3f; la x y la y hacia el
//   héroe ([0x743a170]) más 300 en x y menos 300 en y de a +0x89 · 0,2f; el 0xc2/0 (0x527a97, clave 0) a ±600 en x e
//   y con los ángulos (0, 0, (rand() % 10) · 20) y la luz en blanco; la escala 0,02f más y el temblor de la cámara
//   (0x7d325c8) en (rand() % 8 − 8) · 0,1f.
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunEcho(World::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);
} // namespace Client::Scenes
