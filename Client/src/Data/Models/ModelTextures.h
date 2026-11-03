#pragma once

#include "Data/Textures/TextureSet.h"

#include <cstdint>
#include <optional>
#include <string_view>

// Lo que dice el nombre de la textura de una malla, como lo leen el lector de modelos y el que le
// asigna los slots (Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd"; 06, "El modelo en
// el binario"). Las texturas de las escenas siguen otra regla: Textures::TextureLoaderOf.
namespace Client::Data::Models
{
// El cargador de una textura de modelo, como 0x60d5f0: la letra después del primer punto, con tolower
// (0x6a0cce); 't' es el OZT, y cualquier otra el OZJ. Sin punto, el índice del punto queda en 0 y
// mira la segunda letra del nombre. "Monster\\bossmap1_R.jpg.jpg" es un OZJ, y "x.Tga.jpg", un OZT.
[[nodiscard]] Textures::TextureLoader ModelTextureLoaderOf(std::string_view textureName);

// Los slots a los que 0x60d5f0 manda una malla por el principio del nombre de su textura, después de
// cargarla: "ski" y "level" la piel (0x12e), "hid" la malla que no se dibuja (0x12d) y "hair" el
// pelo (0x137). Byte a byte y en minúsculas: "Hair" no cuenta. Con otro, std::nullopt.
inline constexpr std::uint16_t SkinTextureSlot = 0x12e;
inline constexpr std::uint16_t HiddenTextureSlot = 0x12d;
inline constexpr std::uint16_t HairTextureSlot = 0x137;
[[nodiscard]] std::optional<std::uint16_t> SkinSlotOf(std::string_view textureName);

// El script de una malla (0x459940): lo que sigue al primer '_' del nombre de su textura, hasta el
// primer punto. Cada letra prende un byte de malla + 0x24, que lee el dibujo (06).
struct TextureScript
{
    bool blendMesh = false;    // 'R', el byte 0: la malla es el blendMesh (0x4ef0c0)
    bool hidden = false;       // 'H', el 1: no se dibuja (0x4ef0c0)
    bool scrollUv = false;     // 'S', el 2: corre sus UV (0x4ecc30)
    bool withoutShine = false; // 'N', el 3: sin pases de brillo (0x4ecc30)
};

// El script de `textureName`, o std::nullopt si no tiene: sin '_', sin letras después, o con una que
// no es R, H, S ni N en mayúscula entre las cuatro que mira (0x459a2f; el llamador, 0x4f17f3, solo lo
// guarda si vuelve 1). "Monster_R.jpg" es el blendMesh; "skin_barbarian_01.jpg" no tiene script.
[[nodiscard]] std::optional<TextureScript> TextureScriptOf(std::string_view textureName);
} // namespace Client::Data::Models
