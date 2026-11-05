#pragma once

// Los ángulos en grados del original, como los deja la x87 con 53 bits (Windows la pone así): el rumbo de
// un punto a otro, el giro de un ángulo hacia otro y la distancia entre dos. Los usan el cuervo
// (Scenes/DarkSpirit.h) y la cabeza de los personajes (Scenes/CharacterHead.h), verificados contra el código
// real emulado (tests/vectors/world55/raven.txt y head.txt).
namespace Client::Render::Models
{
// 0x4e93f0: el rumbo de (x1, y1) a (x2, y2), en grados, como lo deja en la pila de la x87.
[[nodiscard]] double CreateAngle(float x1, float y1, float x2, float y2);

// 0x4e9560: `from` hacia `to` a lo sumo `step` grados, como lo deja en la pila de la x87 (con sus ramas,
// que no son simétricas).
[[nodiscard]] double TurnAngle(float from, float to, float step);

// 0x4e96f0: de `from` a `to`, los dos pasados a [0, 360) si son negativos (y guardados en float), la resta
// `to − from`, o la vuelta por el otro lado si queda a más de media vuelta (360 − to + from, o 360 − from
// + to, con ramas que no son simétricas: con `from` en 180 o más mira `from − 180 > to`, y con menos, `from
// + 180 ≤ to`). Con `absolute`, sin el signo.
[[nodiscard]] double AngleDistance(float from, float to, bool absolute);
} // namespace Client::Render::Models
