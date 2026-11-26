#include "Data/Models/ModelFiles.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Data/Models/Bmd.h"
#include "Data/Models/Model.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Data::Models
{
namespace
{
using Core::Unexpected;

// The number 0x60da30 writes as nothing ("%s.bmd"), and the first one without the leading 0 ("%s%d.bmd").
constexpr int NoNumber = -1;
constexpr int FirstTwoDigitNumber = 10;
// The original asks for "Data\\Player\\Player.bmd".
constexpr std::string_view DataFolder = "Data\\";
constexpr std::string_view BmdExtension = ".bmd";

// 0x6c9e54, 0x6c9e4c and 0x6c9e40: the names 0x60da30 compares with the missing one's.
constexpr std::array<std::string_view, 3> RequiredModels = {"Monster", "Player", "PlayerTest"};

ModelLoadError MakeError(ModelLoadErrorKind kind, std::string path)
{
    return ModelLoadError{kind, std::move(path), std::nullopt, {}};
}
} // namespace

std::string ModelFileName(std::string_view name, int number)
{
    std::string fileName(name);
    if (number != NoNumber)
    {
        // "%s0%d" with any number below 10, negatives too: -2 gives "0-2".
        if (number < FirstTwoDigitNumber)
        {
            fileName += '0';
        }
        fileName += std::to_string(number);
    }
    fileName += BmdExtension;
    return fileName;
}

bool IsRequiredModel(std::string_view name)
{
    for (const std::string_view required : RequiredModels)
    {
        if (name == required)
        {
            return true;
        }
    }
    return false;
}

Core::Expected<Model, ModelLoadError> LoadModelFile(const std::filesystem::path& dataRoot, const ModelFile& file)
{
    std::string relative(file.folder);
    relative += ModelFileName(file.name, file.number);
    std::string originalPath(DataFolder);
    originalPath += relative;

    std::error_code error;
    const std::optional<std::filesystem::path> path = ResolveDataPath(dataRoot, relative, error);
    if (!path.has_value())
    {
        ModelLoadError missing = MakeError(ModelLoadErrorKind::Missing, std::move(originalPath));
        missing.system = error;
        return Unexpected(std::move(missing));
    }
    const Core::Expected<std::vector<char>, std::error_code> bytes = ReadDataFile(*path, MaxModelFileBytes);
    if (!bytes.HasValue())
    {
        ModelLoadError unreadable = MakeError(ModelLoadErrorKind::Unreadable, std::move(originalPath));
        unreadable.system = bytes.Error();
        return Unexpected(std::move(unreadable));
    }
    Core::Expected<Model, DataError> model = LoadBmd(std::as_bytes(std::span(bytes.Value())));
    if (!model.HasValue())
    {
        ModelLoadError unreadable = MakeError(ModelLoadErrorKind::Unreadable, std::move(originalPath));
        unreadable.data = model.Error();
        return Unexpected(std::move(unreadable));
    }
    if (file.name == PlayerSkeletonName && !model.Value().meshes.empty())
    {
        return Unexpected(MakeError(ModelLoadErrorKind::SkeletonWithMeshes, std::move(originalPath)));
    }
    return std::move(model).Value();
}
} // namespace Client::Data::Models
