#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

// [0x5763534], el cartel con el nombre del mapa (vtable 0x6baad4): la imagen Interface\<mapa>.tga en el slot 0x66a
// que entra, se queda y sale encima del mundo al llegar a un mapa. Lo arma WinMain (0x4bb9b8, 0x65cd70 → 0x65d080);
// lo reinicia el primer paso de la escena 5 (0x63692b, 0x65dc00); lo disparan la entrada al mundo del F3 03
// (0x4c4b28), la reaparición del F3 04 (0x4c504e) y el 1C con gate (0x4c831c), con 0x65dc30; y lo dibuja el 19 del
// HUD (0x5a74ca, 0x65dd70), que corre el fundido (0x65dcd0). El resto de su tramo del .text son las plantillas del
// std::map de +8 de MSVC6 (el árbol rojo-negro, sus nodos de 0x24, operator[] y los destructores), que cubre std::map,
// y los destructores del cartel, que cubre el de MapBanner (triage de la referencia).
namespace Client::Render
{
class ITextureStore;
}

namespace Client::Hud
{
// +0x18, un int en el original (de 0 a 3).
enum class MapBannerState : std::uint8_t
{
    Off = 0,
    FadingIn = 1,
    Holding = 2,
    FadingOut = 3,
};

struct MapBanner
{
    // +8: el mapa → la imagen, que llena 0x65d080. El operator[] del árbol mete vacío el que no está.
    std::map<std::int32_t, std::string> files;
    // +0x14 (un word): el mapa de la imagen que está en el slot 0x66a; −1 desde 0x65dc00.
    std::int16_t shownMap = -1;
    MapBannerState state = MapBannerState::Off;
    // +0x1c: el timeGetTime() del último paso; +0x20: lo que lleva quieto, en ms.
    std::uint32_t last = 0;
    std::uint32_t elapsed = 0;
    // +0x24: el alfa del glColor4f del dibujo.
    float alpha = 0.0f;
    // MuRet: la imagen que 0x65dc30 carga en el slot 0x66a con 0x63bd00 (sin GPU en el paquete, la sube la escena
    // antes del dibujo, UploadTheMapBanner). Vacía, un mapa sin imagen: "File not exist" y el cliente se cierra.
    std::optional<std::string> pendingImage;
};

// 0x63bd00(imagen, 0x66a, GL_NEAREST, GL_CLAMP, 0, 1) con la imagen pendiente de 0x65dc30: el OZT de Data\ en el
// slot 0x66a. Una que falta, o el nombre vacío de un mapa sin imagen (sin punto, Q-C74), arma "File not exist", un
// MessageBox y WM_DESTROY con el sexto argumento en 1: corta (Q-C23). Una que está y no se lee no se sube y no avisa
// (Q-C14): el slot queda con la de antes. Sin una pendiente, nada.
[[nodiscard]] Core::Expected<void, std::string>
UploadTheMapBanner(MapBanner& banner, const std::filesystem::path& dataRoot, Render::ITextureStore& textures);

// 0x65cd70 → 0x65d080: la tabla, sin el estado (+0x14..+0x24 los deja sin escribir, y nadie los lee antes de
// 0x65dc00).
[[nodiscard]] MapBanner MakeTheMapBanner();

// 0x65dc00: apagado, +0x14 en −1, el reloj (`now`, timeGetTime), la cuenta en 0 y el alfa en 1.
void ResetTheMapBanner(MapBanner& banner, std::uint32_t now);

// 0x65dc30: entra (1) con el alfa en 0,2 (0x3e4ccccd) y la cuenta en 0. Con `map` (el mapa de 0x6c9dd0) distinto de
// +0x14 (el word con signo contra el int), operator[] da su imagen (sin cadena, la vacía de 0x6b85ec), que va al slot
// 0x66a con 0x63bd00(imagen, 0x66a, GL_NEAREST, GL_CLAMP, 0, 1), y +0x14 queda con el mapa.
void ShowTheMapBanner(MapBanner& banner, std::int32_t map);

// 0x65dcd0, en cada dibujo de 0x65dd70: entra de a 0,015 (0x6b8a24) hasta que la suma de la x87 llega a 1 (0x6b87d4),
// donde se queda (2) con el alfa en 1; se queda hasta pasar 5.000 ms de timeGetTime (sin signo) y sale (3) de a 0,015
// hasta que la suma llega a 0 (0x6b86a0), donde se apaga con el alfa en 0. Guarda el reloj en cada paso.
void StepTheMapBanner(MapBanner& banner, std::uint32_t now);

// 0x65dd70: el cartel de 166 × 90 con las UV 0,6484375 × 0,703125 (0x621b60 sin escalar el tamaño ni la posición),
// en x = (el ancho del mundo (0x5bb8d0) · rx − 166) · 0,5 e y = ry · 220, en píxeles de la pantalla.
struct MapBannerQuad
{
    float x = 0.0f;
    float y = 0.0f;
};
[[nodiscard]] MapBannerQuad MapBannerQuadOf(std::int32_t worldWidth, float rx, float ry);
inline constexpr std::uint16_t MapBannerSlot = 0x66a;
inline constexpr float MapBannerWidth = 166.0f;
inline constexpr float MapBannerHeight = 90.0f;
inline constexpr float MapBannerDu = 0.6484375f;
inline constexpr float MapBannerDv = 0.703125f;
} // namespace Client::Hud
