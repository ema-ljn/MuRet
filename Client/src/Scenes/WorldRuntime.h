#pragma once

#include <cstdint>

// Los globales en claro que sobreviven a los cambios de escena. Hoy solo el mundo en curso.
namespace Client::Scenes
{
struct WorldRuntime
{
    // DAT_006c9dd0, el mundo en curso. Quien llama a 0x61a560 lo escribe justo antes (05, "La carpeta
    // de cada mapa"): el login con 55 (06, "El login, al entrar") y la selección con 54 en 0x633f20
    // (06, "Al entrar"). Ese 54 sigue en la carga del mundo y en la espera de la escena 5, que corre
    // con lo que dejó la selección hasta que la respuesta del F3 03 escribe el del juego (06, "La
    // entrada al mundo", paso 1 y fila 1).
    // El −1 de antes de la primera escritura es el que MuRet ya usaba; el inicial del original no
    // está leído. Con −1, 0x511290 no suma el corrimiento de la mascota (06, CL7.3 B).
    std::int32_t map = -1;
};
} // namespace Client::Scenes
