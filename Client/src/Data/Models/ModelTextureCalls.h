#pragma once

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <cstdint>
#include <span>
#include <string_view>

// Las texturas de los modelos del arranque, como las piden 0x60f7d0 (el avance 2 de la carga,
// 0x61ccb0) y 0x611850 (el avance 4) (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en
// el binario"). No salen de las tablas de los modelos: cada cargador elige a qué modelos les da
// texturas, en qué orden y de qué carpeta, que no siempre es la del .bmd (los Helper de Player\ las
// toman de Item\; 0x1c16, de Skill\). Las listas repiten los lazos del decompilado, y una emulación
// de los dos cargadores sobre el main.exe de c32bbd5f da las mismas 505 llamadas, en el mismo orden.
namespace Client::Data::Models
{
// Una llamada a 0x60d5f0: el modelo y la carpeta donde se buscan sus texturas, con la barra del
// final.
struct ModelTextureCall
{
    std::uint16_t model = 0;
    std::string_view folder;
};

// El muestreo de todas esas llamadas: 0x2901 y 0x2600, repitiendo y sin filtro. Las OZT van siempre
// sin filtro (0x60d5f0 le pasa 0x2600 a 0x63bd00).
inline constexpr Textures::Sampling ModelTextureSampling{Textures::Filter::Nearest, Textures::Wrap::Repeat};

// Las 214 de 0x60f7d0, después de su 0x60d530(0x138), todas con Player\.
[[nodiscard]] std::span<const ModelTextureCall> PlayerTextureCalls();

// Las 291 de 0x611850, después de su 0x60d530(0x25a): con Item\, salvo 0x1c16, 0x1c17 y 0x1c15
// (Skill\), 0x224c (Monster\), 0x1c2f (Npc\) y 0xdb (Item\Bow\, un modelo que no carga ninguna de
// las dos tablas). 26 modelos se piden dos veces, y la segunda encuentra los nombres de la primera.
[[nodiscard]] std::span<const ModelTextureCall> ItemTextureCalls();

// Las cuatro primeras de 0x615700 después de su 0x60d530(0x3b8), con Skill\: las de 0x145, 0x146 y
// 0x147 y las del caballo oscuro (Data::Models::SkillModelFiles). Las otras, que van antes y después,
// cuando entren las habilidades y los efectos.
[[nodiscard]] std::span<const ModelTextureCall> SkillTextureCalls();

// Lo que 0x60f7d0 sube a mano con 0x63b870 y 0x63bd00, sin filtro y sin repetir, en slots fijos que
// no guardan el nombre ni corren el próximo: la piel y el segundo nivel de cada clase y el pelo
// (0x12e a 0x137; 0x12d y 0x135 quedan vacíos) y las cuatro capas (0x250, 0x251, 0x252 y 0x257).
[[nodiscard]] std::span<const Textures::TextureSetEntry> PlayerFixedTextures();
} // namespace Client::Data::Models
