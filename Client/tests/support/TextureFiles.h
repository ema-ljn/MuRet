// Ayudas para los tests de las texturas: armar un OZT sintético como lo deja el exportador del
// original, y los píxeles que se esperan de él.

#pragma once

#include "Data/Textures/OztTexture.h"
#include "support/LittleEndian.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Client::Tests
{

// La cabecera de un TGA sin comprimir, sin id y sin paleta: 18 bytes.
inline constexpr std::size_t TgaHeaderSize = 18;
// El tipo de imagen de un TGA de color sin comprimir.
inline constexpr std::uint8_t TgaUncompressedColor = 2;
// El descriptor de los OZT del laboratorio: 8 bits de alfa y el origen abajo a la izquierda.
inline constexpr std::uint8_t TgaDescriptorOfTheLab = 0x08;

// El alfa de todos los píxeles de FilePixel: no sale de ningún otro canal, así que se ve si queda
// cruzado con uno, y no es 0xFF, así que se ve si el cargador lo inventa.
inline constexpr std::uint8_t FileAlpha = 0x5A;

// Los 4 bytes de un píxel BGRA del archivo para el píxel (x, y) de la imagen, con y = 0 la primera
// fila del archivo. Hasta 512 × 512, cada píxel sale distinto: el azul y el verde son los bytes
// bajos de x y de y, y el rojo lleva sus bits altos con el bit 7 prendido, para que ningún píxel
// sea todo ceros como lo que sobra de la textura.
inline std::array<std::uint8_t, 4> FilePixel(int x, int y)
{
    const auto blue = static_cast<std::uint8_t>(x & 0xFF);
    const auto green = static_cast<std::uint8_t>(y & 0xFF);
    const auto red = static_cast<std::uint8_t>(0x80 | (x >> 8) | ((y >> 8) << 1));
    return {blue, green, red, FileAlpha};
}

// Un OZT de `width` × `height` con FilePixel y `bitsPerPixel` en la cabecera: el TGA, con sus 4
// primeros bytes repetidos delante, como lo arma el exportador (0x63b280). Los píxeles son siempre
// de 4 bytes, sean cuales sean los bits por píxel.
inline std::vector<std::byte> PlainOzt(std::int16_t width, std::int16_t height, std::uint8_t bitsPerPixel = 32)
{
    std::vector<std::byte> tga{std::byte{0}, std::byte{0}, std::byte{TgaUncompressedColor}};
    tga.resize(Data::Textures::OztWidthOffset - Data::Textures::OztPrefixSize); // la paleta y el origen, en cero
    AppendLittleEndian(tga, static_cast<std::uint16_t>(width), sizeof(width));
    AppendLittleEndian(tga, static_cast<std::uint16_t>(height), sizeof(height));
    tga.push_back(std::byte{bitsPerPixel});
    tga.push_back(std::byte{TgaDescriptorOfTheLab});
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (const std::uint8_t channel : FilePixel(x, y))
            {
                tga.push_back(std::byte{channel});
            }
        }
    }
    std::vector<std::byte> ozt(tga.begin(), tga.begin() + Data::Textures::OztPrefixSize);
    ozt.insert(ozt.end(), tga.begin(), tga.end());
    return ozt;
}

} // namespace Client::Tests
