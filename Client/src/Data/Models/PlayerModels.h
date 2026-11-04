#pragma once

#include "Data/Models/ModelFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Los modelos del personaje y de los ítems, como los carga el original: 0x60dbe0 (el avance 1 de la
// carga, 0x61ccb0) y 0x60ff50 (el avance 3) (Client/docs/research/06-render-escenas-y-ui.md, "El
// modelo en el binario"). Cada fila es una llamada a 0x60da30, en el orden del original y con su
// dirección: las tablas salen de emular esos dos cargadores sobre el main.exe de c32bbd5f.
namespace Client::Data::Models
{
// El número de la tabla de modelos de Player.bmd, el esqueleto (0x60e90d).
inline constexpr std::uint16_t PlayerSkeletonModel = 0x20a;
// La cantidad de modelos de la tabla del original: 0x489d de 0xe0 bytes (0x60dc23).
inline constexpr std::size_t ModelTableSize = 0x489d;

// 0x60ff50: el modelo de un ítem es el 0x212 más 512 por su grupo más su índice.
inline constexpr std::uint16_t FirstItemModel = 0x212;
inline constexpr std::uint16_t ItemModelsPerGroup = 512;
[[nodiscard]] constexpr std::uint16_t ItemModel(std::uint8_t group, std::uint16_t index)
{
    return static_cast<std::uint16_t>(FirstItemModel + group * ItemModelsPerGroup + index);
}

// Los huesos que 0x60dbe0 escribe en +0x54, +0x58 y +0x5c del esqueleto (0x60f301..0x60f32b): la
// cabeza, y dos que serían los pies (*inferido*).
inline constexpr std::int16_t PlayerHeadBone = 20;
inline constexpr std::array<std::int16_t, 2> PlayerFootBones = {6, 13};

// Las 213 cargas de 0x60dbe0 en el modo Data\: Player.bmd, las partes de cada clase, las del equipo
// y Shadow01.bmd. Todas están en el Data\ del laboratorio.
[[nodiscard]] std::span<const ModelFile> PlayerModelFiles();

// Las 243 de 0x60ff50, cada una con un modelo distinto: las armas, los escudos, los ítems y las
// cuatro mascotas de Player\ (Helper01 a 04). En el laboratorio faltan solo los tres giftbox (09),
// que el original saltea sin avisar.
[[nodiscard]] std::span<const ModelFile> ItemModelFiles();
} // namespace Client::Data::Models
