#pragma once

#include "App/WindowState.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <optional>

// La imagen de la UI nueva del original (06 § La carga y § Las ventanas de la UI nueva): un objeto
// de 0xa8 bytes que arma 0x647050(w, h, slot, ..., fx, fy), ubica 0x6472e0(x, y, 3) y dibuja
// 0x647650 con un GL_TRIANGLE_FAN. No es el quad de 0x621b60: va en un espacio propio, con la y
// desde arriba y el alto de la pantalla pasado a ese espacio (H / fy), cada esquina multiplicada
// por fx y fy al dibujar, y las UV a medio téxel del borde. La usan la escena de carga, diseñada en
// 800 × 600 (fx = W / 800, fy = H / 600), y el fondo de cada ventana, los campos de la cuenta, el
// aviso y la barra de abajo, en píxeles (fx = fy = 1). Lo que no lleva todavía: el ancla de +0x9c
// (las imágenes de la carga y las de las ventanas que se leyeron van con 0) y el avance solo de una
// animación (0x6475d0). El punto de +0x58 y +0x5c, que 0x6472e0 pone donde se le pide, lo restan
// los que la ubican: x e y son siempre la esquina. Los cuadros, sí: los de un botón, uno abajo del
// otro (frameTop, CL5.5), y el de una imagen que empieza en otro lugar de su textura (frameLeft y
// frameTop, como las de 0x647260).
namespace Client::Render
{
struct UiImage
{
    // El slot de la textura, o ninguno: con -1, el original dibuja solo el color, sin textura.
    std::optional<TextureSlot> slot{};
    // Dónde la ubica 0x6472e0(x, y, 3): la esquina de arriba a la izquierda, en el espacio de la
    // imagen (los píxeles de la pantalla divididos por fx y fy) y con la y desde arriba.
    int x = 0;
    int y = 0;
    // Los dos primeros argumentos de 0x647050: el tamaño en ese espacio y, para las UV, en téxeles.
    int width = 0;
    int height = 0;
    // El tamaño del buffer de la textura (los floats de +0x20 y +0x24 de su slot, los lados
    // potencia de 2 que sube glTexImage2D), para las UV. Sin slot no se usa; con slot, mayor que 0.
    float textureWidth = 1.0f;
    float textureHeight = 1.0f;
    // fx y fy (+0x90 y +0x94): de ese espacio a los píxeles de la pantalla.
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // El de glColor4ub (+0x61 a +0x63, y el alfa en +0x60): blanco salvo que se cambie. Queda como
    // el color de GL después de dibujarla (Q-C75).
    Color color{};
    // 0x647370(w, h, modo) le cambió el ancho (modo 1) o el alto (modo 2) después de armarla: la u
    // de la derecha es width / tw (0x6473dd) o la v de abajo, height / th (0x647457), sin medio
    // téxel. Lo usa la barra de la carga (0x6402d0 y 0x640580). Las esquinas no cambian: 0x647370
    // las saca como 0x647050 y 0x6472e0.
    bool resizedWidth = false;
    bool resizedHeight = false;
    // El cuadro que eligió 0x647560, como la fila de téxeles de su borde de arriba: los botones
    // (0x63c330) apilan los suyos para abajo, el k en k · h. Sin cuadros, nada. Solo con slot.
    std::optional<int> frameTop{};
    // Y la columna de su borde de la izquierda, con frameTop: 0 en los botones, y el origen de la
    // imagen en su textura en las de 0x647260 (la lista de 0x647050 con un solo cuadro). Una imagen
    // con cuadros no sigue a 0x647370 (0x647050 pone +0x98 en 0): con frameTop, resizedWidth y
    // resizedHeight van en false.
    int frameLeft = 0;
    // El alto con que la armó 0x647050, si 0x647370 le cambió después el alto sin mover las UV
    // (una imagen con +0x98 en 0, como las que tienen cuadros): las UV salen de este alto, y las
    // esquinas, de height. La textura se estira. Sin él, las dos salen de height.
    std::optional<int> uvHeight{};
};

// Las cuatro esquinas, como 0x647050, 0x6472e0 y 0x647650. El borde de arriba es H / fy − y,
// guardado en float, y el de abajo, el de arriba sin redondear menos el alto de la imagen (que
// 0x6472e0 saca de las esquinas que dejó 0x647050); cada esquina va por fx o fy. Las UV van de
// (0,5 / tw, 0,5 / th) arriba a la izquierda a ((w − 0,5) / tw, (h − 0,5) / th) abajo a la
// derecha, o a w / tw y h / th del lado cambiado (resizedWidth, resizedHeight); sin slot, en 0,
// como las deja 0x647050. Con frameTop, 0x647560 corre la u de la izquierda a (frameLeft + 0,5) /
// tw y la v de arriba a (frameTop + 0,5) / th; la de la derecha es esa más lo que medía la imagen
// en u, y la de abajo, la de arriba más lo que medía en v (guardado en float).
[[nodiscard]] QuadCorners UiImageCorners(const UiImage& image, App::PixelSize screen);

// El factor de un eje de la pantalla para una UI diseñada en `designSize` píxeles: el tamaño por
// la inversa de `designSize` en float, como 0x65eeb0 con 800 y 600 (0x6ba884 y 0x6ba880).
[[nodiscard]] float DesignScale(int screenSize, int designSize);
} // namespace Client::Render
