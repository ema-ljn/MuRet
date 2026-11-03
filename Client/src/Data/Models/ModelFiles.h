#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Models/Model.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

// De un modelo al archivo que lo trae, como 0x60da30 (Client/docs/research/05-formatos-de-datos.md,
// "Modelos .bmd"): el nombre del archivo, los tres que no pueden faltar (Q-C23) y la lectura con
// LoadBmd. Qué modelos carga cada escena y con qué número es de quien llama (PlayerModelFiles, PR B).
namespace Client::Data::Models
{
// Más del triple del modelo más grande del laboratorio (player\playerTest.bmd, 2.414.277 bytes): un
// archivo más grande no es un modelo, y no se lee entero a memoria (GoldenBmd.EveryModelFitsTheReadLimit).
inline constexpr std::uintmax_t MaxModelFileBytes = 8u * 1024u * 1024u;

// El nombre del archivo, como lo arma 0x60da30: "%s.bmd" con -1, "%s0%d.bmd" con un número menor que
// 10 y "%s%d.bmd" con los demás. Con signo, como el original: -2 da "Nombre0-2.bmd".
[[nodiscard]] std::string ModelFileName(std::string_view name, int number);

// Un modelo como lo pide el original: el número de su tabla, la carpeta dentro de Data\ (con la barra
// del final: "Player\\"), el nombre y el número del archivo.
struct ModelFile
{
    std::uint16_t model = 0;
    std::string_view folder;
    std::string_view name;
    int number = -1;
};

// Los tres que cierran el juego si faltan (0x60daec, 0x60db24 y 0x60db5c): Monster, Player y
// PlayerTest. Compara byte a byte, como el original, y no mira el número: falta Monster50.bmd y el
// original se cierra (Q-C23).
[[nodiscard]] bool IsRequiredModel(std::string_view name);

// El esqueleto del personaje, que tiene que venir sin mallas (0x60dbe0, Q-C88).
inline constexpr std::string_view PlayerSkeletonName = "Player";

enum class ModelLoadErrorKind : std::uint8_t
{
    Missing,            // no está; el original lo saltea sin avisar, salvo IsRequiredModel (Q-C23)
    Unreadable,         // está y no se lee: LoadBmd no lo acepta, o el sistema no lo deja leer
    SkeletonWithMeshes, // Player.bmd con mallas: "Player.bmd file error!!!" y WM_DESTROY (Q-C88)
};

struct ModelLoadError
{
    ModelLoadErrorKind kind = ModelLoadErrorKind::Missing;
    // Como la pide el original: "Data\\Player\\Player.bmd".
    std::string path;
    // Con Unreadable, lo que dijo LoadBmd.
    std::optional<DataError> data;
    // Con Missing o Unreadable, lo que dijo el sistema al buscarlo o leerlo, si dijo algo.
    std::error_code system;
};

// Busca `file.folder` + ModelFileName(file.name, file.number) en `dataRoot` (el Data\ del cliente) sin
// mirar mayúsculas, con ResolveDataPath, y lo lee con LoadBmd. Quien llama decide qué hacer con un
// Missing: con IsRequiredModel, cortar; con los demás, seguir sin el modelo (pregunta 8 del plan).
[[nodiscard]] Core::Expected<Model, ModelLoadError> LoadModelFile(const std::filesystem::path& dataRoot,
                                                                  const ModelFile& file);
} // namespace Client::Data::Models
