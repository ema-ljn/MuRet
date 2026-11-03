#pragma once

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/QuadCorner.h"
#include "Render/ScreenScale.h"

// Misión de la CL5.0 (docs/learning/ola-cl5.0-el-pipeline-grafico.md, actividad 4): los cuatro
// vértices y las cuatro UV del quad de 0x621b60, en el orden en que los manda al GL_TRIANGLE_FAN.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Primitivas 2D", y la fila "el quad" de
// la tabla del plan de la CL5.
//
// Las cuentas siguen el desensamblado (actividades 5 y 14): escalar multiplica por la inversa de
// 640 o de 480 guardada en float, no divide, y el borde de abajo se resta del de arriba sin
// redondear, porque la x87 lo deja en la pila. Desde la CL5.2 viven en el cliente
// (Client/src/Render/ScreenScale.cpp y OriginalQuad.cpp), que las necesita para el SpriteBatch, y
// el lab las usa de ahí en vez de tener su copia (CODING_RULES §4), como hizo la CL5.1 con la tabla
// de GL. Los nombres del lab quedan, para que sus tests y su guía se lean igual.
namespace Labs
{
// El tamaño de la ventana en píxeles: DAT_006d5d60 y DAT_006d5d64 (06).
using Screen = Client::App::PixelSize;

// Los argumentos de 0x621b60, en su orden. El del cliente trae también el slot y el alfa, que el
// lab no usa: el slot es la textura, y el alfa solo cambia el color de los vértices.
using OriginalQuad = Client::Render::OriginalQuad;

// Un vértice del fan: en píxeles de la ventana con la y para arriba, la de gluOrtho2D(0, W, 0, H)
// que pone 0x6219b0, y la UV de la textura.
using QuadVertex = Client::Render::QuadCorner;

// 0x620d50 y 0x620d80: de x en 640 y de y en 480 a la ventana, W · x · (1/640 en float) y
// H · y · (1/480 en float).
using Client::Render::ScaleX;
using Client::Render::ScaleY;

// Los cuatro vértices, en el orden del fan: (x, H − y), (x, H − y − h), (x + w, H − y − h) y
// (x + w, H − y), con las UV (u, v), (u, v + dv), (u + du, v + dv) y (u + du, v). La posición se
// escala con scalePosition, y el tamaño con scaleSize, cada uno por su lado.
[[nodiscard]] inline Client::Render::QuadCorners FanVertices(const OriginalQuad& quad, Screen screen)
{
    return Client::Render::FanCorners(quad, screen);
}
} // namespace Labs
