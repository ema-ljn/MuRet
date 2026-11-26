#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Models/Model.h"
#include "EngineApi.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

// As 0x60da30 (Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd"). Which models a scene loads, and with
// which number, is the caller's choice.
namespace Client::Data::Models
{
// Over three times the lab's largest model (player\playerTest.bmd, 2,414,277 bytes): a larger file is not a model and
// is not read whole into memory (GoldenBmd.EveryModelFitsTheReadLimit).
inline constexpr std::uintmax_t MaxModelFileBytes = 8u * 1024u * 1024u;

// As 0x60da30 builds it: "%s.bmd" with -1, "%s0%d.bmd" with a number below 10 and "%s%d.bmd" with the rest. Signed,
// like the original: -2 gives "Name0-2.bmd".
[[nodiscard]] ENGINE_API std::string ModelFileName(std::string_view name, int number);

// The folder inside Data\ keeps its trailing slash ("Player\\").
struct ModelFile
{
    std::uint16_t model = 0;
    std::string_view folder;
    std::string_view name;
    int number = -1;
};

// The three that close the game if missing (0x60daec, 0x60db24 and 0x60db5c). Compares byte by byte, like the
// original, and ignores the number: if Monster50.bmd is missing, the original closes (Q-C23).
[[nodiscard]] ENGINE_API bool IsRequiredModel(std::string_view name);

// It must come without meshes (0x60dbe0, Q-C88).
inline constexpr std::string_view PlayerSkeletonName = "Player";

enum class ModelLoadErrorKind : std::uint8_t
{
    Missing,            // the original skips it silently, except IsRequiredModel (Q-C23)
    Unreadable,         // LoadBmd rejects it, or the system will not read it
    SkeletonWithMeshes, // "Player.bmd file error!!!" and WM_DESTROY (Q-C88)
};

struct ModelLoadError
{
    ModelLoadErrorKind kind = ModelLoadErrorKind::Missing;
    // As the original asks for it: "Data\\Player\\Player.bmd".
    std::string path;
    // With Unreadable, LoadBmd's error.
    std::optional<DataError> data;
    // With Missing or Unreadable, what the system said while finding or reading it, if anything.
    std::error_code system;
};

// The caller decides what a Missing means: stop for IsRequiredModel, go on without the model otherwise (plan
// question 8).
[[nodiscard]] ENGINE_API Core::Expected<Model, ModelLoadError> LoadModelFile(const std::filesystem::path& dataRoot,
                                                                             const ModelFile& file);
} // namespace Client::Data::Models
