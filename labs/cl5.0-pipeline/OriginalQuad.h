#pragma once

#include <array>

// Misión de la CL5.0 (docs/learning/ola-cl5.0-el-pipeline-grafico.md, actividad 4): los cuatro
// vértices y las cuatro UV del quad de 0x621b60, en el orden en que los manda al GL_TRIANGLE_FAN.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Primitivas 2D", y la fila "el quad" de
// la tabla del plan de la CL5.
//
// Las cuentas siguen el desensamblado (actividades 5 y 14): escalar multiplica por la inversa de
// 640 o de 480 guardada en float, no divide, y el borde de abajo se resta del de arriba sin
// redondear, porque el x87 lo deja en la pila. Van en double, como el x87 con la precisión de 53
// bits que Windows pone por defecto; con estos tamaños las restas son exactas, y con 64 es igual.
namespace Labs
{
// El tamaño de la ventana en píxeles: DAT_006d5d60 y DAT_006d5d64 (06).
struct Screen
{
    int width = 640;
    int height = 480;
};

// Los argumentos de 0x621b60, en su orden, sin el slot ni el alfa: el slot es la textura, y el alfa
// solo cambia el color de los vértices.
struct OriginalQuad
{
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    float u = 0;
    float v = 0;
    float du = 1;
    float dv = 1;
    bool scaleSize = false;
    bool scalePosition = false;
};

// Un vértice del fan: en píxeles de la ventana con la y para arriba, la de gluOrtho2D(0, W, 0, H)
// que pone 0x6219b0, y la UV de la textura.
struct QuadVertex
{
    float x = 0;
    float y = 0;
    float u = 0;
    float v = 0;
};

// 0x620d50: de x en 640 a la ventana, W · x · (1/640 en float).
[[nodiscard]] float ScaleX(float x, Screen screen);

// 0x620d80: de y en 480 a la ventana, H · y · (1/480 en float).
[[nodiscard]] float ScaleY(float y, Screen screen);

// Los cuatro vértices, en el orden del fan: (x, H − y), (x, H − y − h), (x + w, H − y − h) y
// (x + w, H − y), con las UV (u, v), (u, v + dv), (u + du, v + dv) y (u + du, v). La posición se
// escala con scalePosition, y el tamaño con scaleSize, cada uno por su lado.
[[nodiscard]] std::array<QuadVertex, 4> FanVertices(const OriginalQuad& quad, Screen screen);
} // namespace Labs
