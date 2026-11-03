#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Un juego de texturas: las que sube una escena al entrar, cada una con su slot de la tabla del
// original y su muestreo (Client/docs/research/05-formatos-de-datos.md, "Texturas"; 06, la carga y
// el login). Es la primera lectura del disco de la capa de datos: los cargadores de la CL1 reciben
// los bytes, y esto los busca y los lee.
//
// Un juego es un dato (una tabla por escena o por mundo), nunca una lista en el código que lo usa:
// la carga y el login tienen el suyo (Scenes/TextureSets.h), y el mundo N el de su carpeta (CL7).
namespace Client::Data::Textures
{
// Qué cargador del original lee una textura: 0x63b870, el del OZJ, o 0x63bd00, el del OZT. Quien
// llama elige uno según lo que pide: "<nombre>.jpg" al primero y "<nombre>.tga" al segundo.
enum class TextureLoader : std::uint8_t
{
    Ozj,
    Ozt,
};

// Una textura de un juego: como la pide el original ("Logo\\Login_Back01.jpg"), su slot y cómo se
// muestrea (el tercer y cuarto argumento del cargador).
struct TextureSetEntry
{
    std::string_view path;
    std::uint16_t slot = 0;
    Sampling sampling{};
};

enum class TextureSetErrorKind : std::uint8_t
{
    UnknownFormat, // la entrada no es .jpg ni .tga: un error de la tabla, no del disco
    Missing,       // no está; el original arma "File not exist", MessageBox y WM_DESTROY (Q-C23)
    Unreadable,    // está y no se lee; el original no la sube y no avisa (Q-C14, Q-C29)
};

struct TextureSetError
{
    TextureSetErrorKind kind = TextureSetErrorKind::Missing;
    // La ruta como la pide el original: la de la entrada.
    std::string path;
    // Con Unreadable, lo que dijo el cargador de la CL1.
    std::optional<DataError> data;
    // Con Missing o Unreadable, lo que dijo el sistema al buscarla o leerla, si dijo algo: una
    // carpeta sin permiso, una falla de E/S.
    std::error_code system;
};

// Una textura leída, con lo que hace falta para subirla.
struct LoadedTexture
{
    std::uint16_t slot = 0;
    Sampling sampling{};
    Texture texture;
};

// El cargador de `originalPath` por su extensión, sin mirar mayúsculas: ".jpg" el OZJ y ".tga" el
// OZT. Con otra, o sin extensión, std::nullopt.
[[nodiscard]] std::optional<TextureLoader> TextureLoaderOf(std::string_view originalPath);

// El archivo que abre el cargador, como en 0x63b870 y 0x63bd00 con Data\: el nombre hasta el
// primer punto, con el punto, y la extensión del cargador. "Monster\\bossmap1_R.jpg.jpg" abre
// "Monster\\bossmap1_R.OZJ" (09 § El Data\). Sin punto, el original no escribe el byte que va antes
// de la extensión y queda lo que había en la pila (Q-C74); acá va un punto.
[[nodiscard]] std::string TextureFileName(std::string_view originalPath, TextureLoader loader);

// Lee todas las de `entries`, en orden, desde la carpeta `dataRoot` (el Data\ del cliente): cada
// una con el archivo de TextureFileName, buscado con ResolveDataPath (sin mayúsculas), y leída con
// el cargador de su extensión. La primera que no es .jpg ni .tga, que falta o que no se lee corta.
[[nodiscard]] Core::Expected<std::vector<LoadedTexture>, TextureSetError>
LoadTextureSet(const std::filesystem::path& dataRoot, std::span<const TextureSetEntry> entries);

// El error en palabras, para el log: qué pasó y con qué ruta. Sin bytes del archivo.
[[nodiscard]] std::string Describe(const TextureSetError& error);
} // namespace Client::Data::Textures
