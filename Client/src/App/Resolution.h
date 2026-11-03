#pragma once

#include <array>
#include <compare>
#include <span>
#include <string>

// Las resoluciones que acepta la ventana (decidido con Emanuel el 2026-11-03): las cinco del
// original o un modo real del monitor. El original elige una de cinco con el valor Resolution del
// registro (0x4b90a0, 02 § La resolución) y su ventana no cambia de tamaño; MuMain ofrece los
// modos que da el monitor (SDL_GetFullscreenDisplayModes). Un tamaño cualquiera, como 7 × 3, no
// es ninguna de las dos cosas, y la UI se arma sobre 640 × 480 (D-C11).
namespace Client::App
{
struct Resolution
{
    int width = 0;
    int height = 0;

    // Primero por el ancho y después por el alto: el orden de la lista de DescribeValidResolutions.
    friend constexpr auto operator<=>(const Resolution&, const Resolution&) = default;
};

// Las del registro, de 0 a 4. 640 × 480 es la de referencia de la UI.
inline constexpr std::array<Resolution, 5> OriginalResolutions{
    {{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200}}};

// true si es una de las cinco del original o uno de los modos del monitor, con el mismo ancho y
// el mismo alto.
[[nodiscard]] bool IsValidResolution(Resolution resolution, std::span<const Resolution> monitorModes);

// Las que valen, para el mensaje de error: de la más chica a la más grande, sin repetir, como
// "640 × 480, 800 × 600".
[[nodiscard]] std::string DescribeValidResolutions(std::span<const Resolution> monitorModes);
} // namespace Client::App
