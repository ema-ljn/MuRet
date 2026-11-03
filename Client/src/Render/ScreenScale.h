#pragma once

#include "App/WindowState.h"

// De las coordenadas de 640 × 480 a los píxeles de la pantalla, cada eje por su lado: 0x620d50 y
// 0x620d80 (06 § Primitivas 2D). El quad de 0x621b60 los usa para la posición y el tamaño cuando
// sus banderas lo piden, y la arena de 0x622000 y el quad sin textura de 0x621aa0, siempre. Con
// una pantalla que no es 4:3, lo escalado se estira, como lo haría el original (pregunta 10 del
// plan de la CL5).
//
// No dividen: el binario carga W o H como entero, multiplica por la coordenada y después por la
// inversa de 640 o de 480 guardada en float (0x3acccccd en 0x6ba070 y 0x3b088889 en 0x6ba06c), con
// la x87 en los 53 bits que pone Windows, y el que llama guarda el resultado en float. Por eso 25
// escalado en 480 da 25,000002 y no 25 (la CL5.0 lo leyó en el desensamblado: el lab
// labs/cl5.0-pipeline/ usa estas mismas).
namespace Client::Render
{
// 0x620d50: W · x · (1/640 en float).
[[nodiscard]] float ScaleX(float x, App::PixelSize screen);

// 0x620d80: H · y · (1/480 en float).
[[nodiscard]] float ScaleY(float y, App::PixelSize screen);
} // namespace Client::Render
