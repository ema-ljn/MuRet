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
// Shadow01.bmd, la sombra que 0x50ba30 dibuja debajo de cada personaje (0x60f2f4).
inline constexpr std::uint16_t PlayerShadowModel = 0x20b;
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

// Los cascos que 0x50ba30 dibuja con su máscara en la clase 4: el modelo + 0x328b, los
// MaskHelmMale01, 06, 07, 09 y 10 de 0x60dbe0.
inline constexpr std::uint16_t MaskHelmOffset = 0x328b;
inline constexpr std::array<std::uint16_t, 5> HelmsWithMask{0x1012, 0x1017, 0x1018, 0x101a, 0x101b};

// Las 213 cargas de 0x60dbe0 en el modo Data\: Player.bmd, las partes de cada clase, las del equipo
// y Shadow01.bmd. Todas están en el Data\ del laboratorio.
[[nodiscard]] std::span<const ModelFile> PlayerModelFiles();

// Las 243 de 0x60ff50, cada una con un modelo distinto: las armas, los escudos, los ítems y las
// cuatro mascotas de Player\ (Helper01 a 04). En el laboratorio faltan solo los tres giftbox (09),
// que el original saltea sin avisar.
[[nodiscard]] std::span<const ModelFile> ItemModelFiles();

// Lo que MuRet carga de 0x615700, el cargador de las habilidades y los efectos, que va después de
// 0x60ff50 en el mismo avance de la carga: las monturas de las mascotas (0xc6, 0x110 y los Fenrir 0x14a..0x14c),
// el caballo oscuro (0xbf, Skill\DarkHorse.bmd), el rayo de los Fenrir (0x14d, Effect\lightning_type01.bmd), el
// cuervo (0xc0, Skill\DarkSpirit.bmd), HellGate (0xc5), los dos Gate de Object12 (0x10c/0x10d) y los tres modelos que
// piden sus texturas antes del caballo (0x145, 0x146 y 0x147), para conservar los slots del original. Los otros,
// cuando entren las habilidades y los efectos.
[[nodiscard]] std::span<const ModelFile> SkillModelFiles();

// Una malla de un modelo de la tabla.
struct ModelMesh
{
    std::uint16_t model = 0;
    std::uint16_t mesh = 0;

    friend bool operator==(const ModelMesh&, const ModelMesh&) = default;
};

// Las cinco mallas a las que 0x60ff50, al final (0x6117fd..0x611847), les prende el byte +0, con el
// que 0x4ecc30 no les corre los pases de brillo: la 1 de Spear01, Sword11, Mace07 y MagicBox07 y la 2
// de Staff07.
inline constexpr std::array<ModelMesh, 5> MeshesWithoutShine{{
    {0x812, 1},  // Spear01
    {0x21c, 1},  // Sword11
    {0xc18, 2},  // Staff07
    {0x618, 1},  // Mace07
    {0x2243, 1}, // MagicBox07
}};
} // namespace Client::Data::Models
