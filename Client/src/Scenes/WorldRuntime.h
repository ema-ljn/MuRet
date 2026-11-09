#pragma once

#include "GameLogic/MovementSpeed.h"

#include <cstddef>
#include <cstdint>

// Los globales en claro que sobreviven a los cambios de escena: el mundo en curso, el estado compartido
// de la velocidad y lo que hace falta para la espera de la entrada al mundo (06, "La entrada al mundo",
// pasos 1 a 4 y 6). Arrancan en 0, como la .bss del original; las cuentas de accesos de la tabla de
// protección, que el original guarda al lado de cada entrada, las pone MuRet en 0 sin haberlas leído
// (CL7.P, P-C22 c).
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
    // 0x7d3a0d0 y 0x7d3a0d4, los de Fenrir: uno solo para todos los personajes (06, "Caminar", la
    // velocidad). 0x4f2250 pone el primero en 0 (0x4f22a4). Los ceros del DTO no afirman el inicial del
    // original.
    GameLogic::MovementSpeedSharedState movementSpeed;

    // DAT_07d3a101, el mundo cargado: en 0, el paso del juego sale en 0x636db5 y su dibujo devuelve
    // 0 en 0x6371a6, sin leer el contador. Lo ponen en 1 la carga del 54 de la selección (0x633f30,
    // dentro de 0x633f20) y el paso del juego con el estado 0x3d (0x636dac), y en 0 solo el F1 02
    // (0x4c2f39, 0x4c38f9). El login también lo pone en 1 al cargar el 55 (0x6349ab); MuRet no lo
    // escribe ahí porque el juego, su único lector portado, llega siempre después de 0x633f20.
    bool drawingEnabled = false;
    // DAT_07d3a13e: si la carga del mundo (0x6356f0) ya armó el contador y subió sus texturas. La
    // baja la selección justo antes de pasar a la escena 3 (0x633efe, 0x634469, 0x634631) y la sube
    // 0x6356f0 (0x635980). Nadie más la escribe.
    std::uint8_t loadingOnceFlag = 0;
    // DAT_077f3b04, el contador de la espera y de los gates, que vive en la tabla de protección
    // (06, "La tabla de protección"): 9.999.999 en la escena 3 y en un pedido de gate, 30 en la
    // respuesta del F3 03, y baja de a 1 en MoveHero (0x59af01). Con más de 30 el dibujo del juego no
    // presenta (0x63722e).
    std::int32_t warpCounter = 0;
    // Cuántos accesos tienen abierto el contador: el byte que sigue a su copia cifrada.
    std::uint8_t warpDepth = 0;
    // Cuántos accesos tienen abierto el registro propio ([0x7681eb4], 0xf08 bytes).
    std::uint8_t ownRecordDepth = 0;
    // DAT_057678b0: el lugar del pool que la respuesta del F3 03 elige para el héroe, rand() % 400
    // (0x4c44d4; 06, "La entrada al mundo", fila 3).
    std::int32_t heroSlot = 0;
    // DAT_0743a170, el héroe publicado, como lugar del pool: WinMain lo deja en el 0 (0x4bafdf) y la
    // respuesta del F3 03 lo pasa al de heroSlot recién después de vestirlo con 0x512a30 (0x4c4725).
    std::size_t hero = 0;
};

// 0x63722e y 0x59afd6: con el contador en más de esto, el juego no dibuja ni mueve al héroe.
inline constexpr std::int32_t WaitDrawThreshold = 30;
// 0x6357eb: lo que deja la carga del mundo en el contador, 0x98967f.
inline constexpr std::int32_t WorldLoadingWait = 9'999'999;
} // namespace Client::Scenes
