#pragma once

#include "GameLogic/MovementSpeed.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/SceneId.h"
#include "World/GroundMarks.h"
#include "World/LeafWeather.h"
#include "World/ObjectModelStepState.h"
#include "World/TerrainCulling.h"

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

// Los globales en claro que sobreviven a los cambios de escena: el mundo en curso, el estado compartido
// de la velocidad y lo que hace falta para la espera de la entrada al mundo (06, "La entrada al mundo",
// pasos 1 a 4 y 6). Arrancan en 0, como la .bss del original; las cuentas de accesos de la tabla de
// protección, que el original guarda al lado de cada entrada, las pone MuRet en 0 sin haberlas leído
// (CL7.P, P-C22 c).
namespace Client::Scenes
{
// .data[0x6c8c5c] = 1e 00 00 00; los otros cuatro campos del clima están en .bss.
inline constexpr std::int32_t InitialLeafCount = 30;

struct WorldRuntime
{
    // Q-C180/Q-C27: primer aviso de datos que pidió WM_DESTROY. Durable incluso si Poll falla;
    // main muestra el modal antes de cerrar. Unsupported-map y errores sin aviso no lo escriben.
    std::optional<std::string> fatalDataMessage{};
    GameItemRenderState itemRender{};
    bool siegeActive = false; // 07239ec, separado del número de mapa (4294b0).
    GameMovementState movement{};
    // Los cinco selectores del mouse arrancan en -1 en la .data.
    std::array<std::int32_t, 5> selectedTargets{-1, -1, -1, -1, -1};
    std::int32_t cursorItemCount = 0;
    std::int32_t storm = 0;
    std::int32_t field78abb0c = 0;
    std::int32_t field78abb10 = 0;
    std::uint8_t byte6c8e4d = 0xff;
    std::optional<std::int32_t> lastSoundId{};
    World::ObjectModelStepState objectModelState{};
    // 7d365a0, .bss0.6061c0 lee el count retenido antes de60657a;6063d0 lo produce
    // sólo paraIcarus/Chaos/asedio. Kalima conserva0 después del reset del pase exitoso.
    std::int32_t objectSelectionCount = 0;
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
    // (0x4c2f39 con 2 y 0x4c38f9 con 1, que MuRet sigue: ApplyReturnToSelection). El login también lo
    // pone en 1 al cargar el 55 (0x6349ab); MuRet no lo escribe ahí porque el juego, su único lector
    // portado, llega siempre después de 0x633f20.
    bool drawingEnabled = false;
    // DAT_006d5dc8 escrito por un paquete: el F1 02 con 1 pone la 4 (0x4c363f) mientras se lee la red, y
    // el próximo paso del original ya es el de la selección. MuRet la deja acá y el SceneManager la toma
    // después del Poll (main.cpp) y entra en su próximo paso. Vacía, nadie la pidió.
    std::optional<SceneId> orderedScene{};
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
    // (0x4c44d4, la única escritura; 06, "La entrada al mundo", fila 3). Cada respuesta lo vuelve a
    // sortear.
    std::int32_t heroSlot = 0;
    // DAT_0743a170, el héroe publicado, como lugar del pool: WinMain lo deja en el 0 (0x4bafdf) y la
    // respuesta del F3 03 lo pasa al de heroSlot recién después de vestirlo con 0x512a30 (0x4c4725). No
    // lo escribe nadie más: en una segunda respuesta, hasta ahí es el héroe anterior, y después de la vuelta
    // a la selección sigue en el lugar del juego (Q-C213).
    std::size_t hero = 0;
    // [0x743a178] y [0x743a17c], lo que dejó el último 0x505820 (06, "El conjunto completo de 0x505820"): en
    // la .bss, 0 al arrancar. Lo escriben 0x512a30 con el héroe publicado y el dibujo de los personajes
    // (0x50ba30 en 0x50c91e), el del login y el de la selección (ScanTheDrawnCharacter). nullopt después de un
    // frame del juego que llega a los personajes (0x6374dd → 0x510d00), que MuRet no dibuja: no se sabe qué
    // dejó, y 0x550990 corta con error si lo lee.
    std::optional<Scan505820> fullSetScan = Scan505820{};
    // [0x78a6758], en la .bss: 0 al arrancar. 0x5bc740, después de poner un ítem en una de las grillas del
    // inventario, lo compara con signo con 12 (0x5bcd0b y 0x5bd08b) y, con menos, abre el registro propio
    // para las velocidades (0x54fe10). Lo escriben quince lugares de la interfaz del inventario y del juego
    // (0x44add8, 0x561b6d, 0x561bf7, 0x59bf3c, 0x5c2d5c, 0x5c5131, 0x5c5154, 0x5c51c3, 0x5c51f8, 0x5c5277,
    // 0x5c5301, 0x5c5330, 0x5c6c15, 0x5c8edd y 0x5c8f53; estático), y MuRet no tiene ninguno: queda en 0.
    std::int32_t slot78a6758 = 0;
    // El trapecio del último 0x5fb570, en celdas: las Y en 0x78ecbf0 y las X en 0x78ecc00, que solo escriben
    // 0x5fba55 y 0x5fba68 (por su dirección, en .text). A 0x5fb570 lo llama solo 0x5fbb00 (0x5fbeef), y a
    // 0x5fbb00 los dibujos del login (0x6351b7), de la selección (0x63475c) y del juego (0x637366): es uno
    // para las tres escenas. Lo lee solo 0x5fba90, con la escena de ese momento (0x6d5dc8): el primer paso del
    // juego después de la espera (0x508f00) lee el del último dibujo de la selección, y el primer paso del
    // login, el que haya. En la .bss (más allá de los 0x20000 bytes de la .data del archivo, desde 0x6c6000):
    // ceros, un trapecio sin área en (0, 0) con el que 0x5fba90 da 0 en los cuatro lados (*estático*, 06 § El
    // paso de los personajes y Q-C217).
    World::TerrainPolygon terrainPolygon{};
    // 0x7b4ccb8, el nivel efectivo del trapecio de un 0x5fb570 al siguiente, en las tres escenas: solo lo
    // escribe 0x5fb570 (0x5fb731). En la .bss: 0.
    World::TerrainPolygonState terrainPolygonState{};
    // Las marcas del suelo, 0x760cab0..0x760f990, en la .bss: todas apagadas al arrancar. Solo las tocan
    // 0x548fe0, 0x549090 y 0x549130, y ninguna carga las vacía: 0x6070a0 apaga las tablas de al lado y no esta
    // (Q-C228). Las llenan 0x505de0 con el modelo 0x182 (0x507486) y 0x52e810 (0x52e96c), desde el paso del
    // cuerpo (0x4fd5f0 → 0x4fcd00), y MuRet no corre ninguno de los dos: corta antes, en 0x505e0b y 0x4fcb87
    // (08 P-C49).
    World::GroundMarkTable groundMarks{};
    // 0x77f36d0, la capa 1 de la celda del héroe, en la .bss: 0 al arrancar. La escribe MoveHero (0x59e2ba),
    // después del contador. La leen el paso de los objetos
    // (los techos de 0x604ab0, 0x604bc4 y 0x604c06) y el de las hojas (0x637082..0x6370e5), entre otros.
    std::int32_t heroCellLayer = 0;
    // 0x75fdc38: meta del 0F. 0x75fdc3c es la cuenta actual, que ese paquete no cambia.
    // Estado durable de 0x530490 para el futuro binder de hojas; recibir 0F no crea lluvia.
    World::LeafWeather leafWeather{.firstWave = InitialLeafCount};
};

// 0x63722e y 0x59afd6: con el contador en más de esto, el juego no dibuja ni mueve al héroe.
inline constexpr std::int32_t WaitDrawThreshold = 30;
// 0x6357eb: lo que deja la carga del mundo en el contador, 0x98967f.
inline constexpr std::int32_t WorldLoadingWait = 9'999'999;
} // namespace Client::Scenes
