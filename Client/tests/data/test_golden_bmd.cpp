#include "Data/Models/Bmd.h"

#include "Core/ByteReader.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextures.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Los .bmd de modelos reales, todos los de la carpeta golden (Client/tests/support/GoldenDir.h).
namespace
{
using Client::Core::ByteReader;
using Client::Data::Models::BmdEncryptedOffset;
using Client::Data::Models::BmdEncryptedSizeOffset;
using Client::Data::Models::BmdVersionOffset;
using Client::Data::Models::Bone;
using Client::Data::Models::EncryptedBmdVersion;
using Client::Data::Models::LoadBmd;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::MaxModelFileBytes;
using Client::Data::Models::MaxPosedBoneActions;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Data::Models::ModelFile;
using Client::Data::Models::TextureScript;
using Client::Data::Models::TextureScriptOf;
using Client::Data::Models::Triangle;
using Client::Tests::HasExtension;
using Client::Tests::ReadFileBytes;

namespace fs = std::filesystem;

// Los modelos del Data\ del laboratorio, recorridos por el script del research (05, 09).
constexpr std::size_t ModelsOfTheLab = 1962;

// Un modelo del laboratorio: su ruta dentro de Data\, su archivo y lo que leyó LoadBmd.
struct LabModel
{
    fs::path path;
    std::vector<char> file;
    Model model;
};

// Si `file` es un modelo: los .bmd de las tablas (Item, Skill, Gate) no empiezan con "BMD".
bool IsModelFile(const std::vector<char>& file)
{
    return file.size() >= 3 && file[0] == 'B' && file[1] == 'M' && file[2] == 'D';
}

class GoldenBmd : public Client::Tests::GoldenDataTest
{
protected:
    // Cada .bmd de la carpeta golden, en orden de ruta: el recorrido de una carpeta no tiene orden.
    std::vector<fs::path> BmdPaths() const
    {
        std::vector<fs::path> paths;
        std::error_code error;
        for (auto it = fs::recursive_directory_iterator(m_dataRoot, error); !error && it != fs::end(it);
             it.increment(error))
        {
            if (it->is_regular_file() && HasExtension(it->path(), ".bmd"))
            {
                paths.push_back(it->path());
            }
        }
        EXPECT_FALSE(error) << "no se pudo recorrer la carpeta golden: " << error.message();
        std::ranges::sort(paths);
        return paths;
    }

    // Cada modelo, leído con LoadBmd. Uno que no se lee hace fallar el test y no está en la lista.
    std::vector<LabModel> LabModels() const
    {
        std::vector<LabModel> models;
        for (const fs::path& path : BmdPaths())
        {
            std::optional<std::vector<char>> file = ReadFileBytes(path);
            if (!file.has_value())
            {
                ADD_FAILURE() << "no se pudo leer " << Relative(path);
                continue;
            }
            if (!IsModelFile(*file))
            {
                continue;
            }
            auto loaded = LoadBmd(std::as_bytes(std::span(*file)));
            if (!loaded.HasValue())
            {
                ADD_FAILURE() << Relative(path) << " no cargó";
                continue;
            }
            models.push_back({fs::relative(path, m_dataRoot), std::move(*file), std::move(loaded).Value()});
        }
        return models;
    }

    std::string Relative(const fs::path& path) const
    {
        return fs::relative(path, m_dataRoot).generic_string();
    }
};

// Si los primeros `polygon` índices de un juego entran en `count`.
bool IndicesFit(const std::array<std::int16_t, 4>& indices, std::uint8_t polygon, std::size_t count)
{
    const auto used = std::span(indices).first(std::min<std::size_t>(polygon, indices.size()));
    return std::ranges::all_of(used, [count](std::int16_t index)
                               { return index >= 0 && static_cast<std::size_t>(index) < count; });
}

// Un hueso del laboratorio, por el archivo y su número.
struct BonePlace
{
    std::string path;
    std::size_t bone = 0;
    bool operator==(const BonePlace&) const = default;
};

// GoogleTest lo usa para mostrar una lista distinta de la esperada.
[[maybe_unused]] void PrintTo(const BonePlace& place, std::ostream* out)
{
    *out << place.path << " #" << place.bone;
}

// Los 1.962 se leen y ninguno deja bytes de más: ni después del último hueso ni, en los cifrados,
// después del bloque de +8.
TEST_F(GoldenBmd, ReadsEveryModelOfTheLab)
{
    const std::vector<LabModel> models = LabModels();

    EXPECT_EQ(models.size(), ModelsOfTheLab);
    for (const LabModel& lab : models)
    {
        EXPECT_EQ(lab.model.trailingBytes, 0u) << lab.path.generic_string();
        if (lab.model.version != EncryptedBmdVersion)
        {
            continue;
        }
        ByteReader size(std::as_bytes(std::span(lab.file)).subspan(BmdEncryptedSizeOffset));
        EXPECT_EQ(BmdEncryptedOffset + size.ReadU32Le().value_or(0), lab.file.size()) << lab.path.generic_string();
    }
}

// 816 cifrados, 1.142 en claro con 0x0A y los cuatro de 0x00 de Object3, que también van en claro
// (05, Q-C86).
TEST_F(GoldenBmd, CountsTheVersionsOfTheLab)
{
    std::map<int, std::size_t> versions;
    std::vector<std::string> versionZero;
    for (const LabModel& lab : LabModels())
    {
        EXPECT_EQ(lab.model.version, static_cast<std::uint8_t>(lab.file[BmdVersionOffset]));
        ++versions[lab.model.version];
        if (lab.model.version == 0)
        {
            versionZero.push_back(lab.path.generic_string());
        }
    }

    EXPECT_EQ(versions, (std::map<int, std::size_t>{{0x00, 4}, {0x0A, 1142}, {0x0C, 816}}));
    EXPECT_EQ(versionZero, (std::vector<std::string>{"Object3/Object25.bmd", "Object3/Object27.bmd",
                                                     "Object3/Object33.bmd", "Object3/Object90.bmd"}));
}

// El esqueleto del personaje, pedido como 0x60dbe0 ("Data\\Player\\Player.bmd", en player\ en el
// laboratorio): 60 huesos, 10 dummy, 205 acciones y ninguna malla (05, Q-C88).
TEST_F(GoldenBmd, ThePlayerIsASkeleton)
{
    const auto player = LoadModelFile(m_dataRoot, ModelFile{0x20a, "Player\\", "Player", -1});

    ASSERT_TRUE(player.HasValue());
    EXPECT_TRUE(player.Value().meshes.empty());
    EXPECT_EQ(player.Value().bones.size(), 60u);
    EXPECT_EQ(std::ranges::count_if(player.Value().bones, &Bone::dummy), 10);
    EXPECT_EQ(player.Value().actions.size(), 205u);
}

// El padre de cada hueso va antes que él, y la pose de 0x4eb860, que los recorre en orden, ya lo
// posó. Salvo en NPC\BloodCastle01.bmd, donde el 43 y el 44 cuelgan del 45 (Q-C106).
TEST_F(GoldenBmd, ParentsComeBeforeChildren)
{
    std::vector<BonePlace> parentAfter;
    for (const LabModel& lab : LabModels())
    {
        const std::vector<Bone>& bones = lab.model.bones;
        for (std::size_t k = 0; k < bones.size(); ++k)
        {
            if (bones[k].dummy)
            {
                continue;
            }
            const std::int16_t parent = bones[k].parent;
            EXPECT_TRUE(parent >= -1 && parent < static_cast<std::int16_t>(bones.size()))
                << lab.path.generic_string() << " #" << k << ": " << parent;
            if (parent >= 0 && static_cast<std::size_t>(parent) >= k)
            {
                parentAfter.push_back({lab.path.generic_string(), k});
            }
        }
    }

    EXPECT_EQ(parentAfter, (std::vector<BonePlace>{{"NPC/BloodCastle01.bmd", 43}, {"NPC/BloodCastle01.bmd", 44}}));
}

// Cada triángulo es de tres vértices, y sus índices de vértices, normales y UV entran en su malla.
TEST_F(GoldenBmd, EveryIndexOfATriangleIsInItsMesh)
{
    std::size_t triangles = 0;
    for (const LabModel& lab : LabModels())
    {
        for (std::size_t k = 0; k < lab.model.meshes.size(); ++k)
        {
            const Mesh& mesh = lab.model.meshes[k];
            for (const Triangle& triangle : mesh.triangles)
            {
                ++triangles;
                EXPECT_EQ(triangle.polygon, 3) << lab.path.generic_string() << ", malla " << k;
                EXPECT_TRUE(IndicesFit(triangle.vertices, triangle.polygon, mesh.vertices.size()) &&
                            IndicesFit(triangle.normals, triangle.polygon, mesh.normals.size()) &&
                            IndicesFit(triangle.texCoords, triangle.polygon, mesh.texCoords.size()))
                    << lab.path.generic_string() << ", malla " << k;
            }
        }
    }
    EXPECT_EQ(triangles, 450338u);
}

// El int16 de +16 de cada normal es un vértice de su malla (P-C01 d: los 301.367 entran).
TEST_F(GoldenBmd, EveryNormalPointsAtAVertexOfItsMesh)
{
    std::size_t normals = 0;
    for (const LabModel& lab : LabModels())
    {
        for (std::size_t k = 0; k < lab.model.meshes.size(); ++k)
        {
            const Mesh& mesh = lab.model.meshes[k];
            for (const auto& normal : mesh.normals)
            {
                ++normals;
                EXPECT_TRUE(normal.vertex >= 0 && static_cast<std::size_t>(normal.vertex) < mesh.vertices.size())
                    << lab.path.generic_string() << ", malla " << k << ": " << normal.vertex;
            }
        }
    }
    EXPECT_EQ(normals, 301367u);
}

// Ningún modelo pasa del tope con que LoadModelFile los lee del disco.
TEST_F(GoldenBmd, EveryModelFitsTheReadLimit)
{
    std::uintmax_t largest = 0;
    for (const LabModel& lab : LabModels())
    {
        largest = std::max<std::uintmax_t>(largest, lab.file.size());
    }
    EXPECT_LE(largest, MaxModelFileBytes);
    EXPECT_EQ(largest, 2414277u) << "el más grande del laboratorio, player\\playerTest.bmd";
}

// Ningún modelo pasa del tope de huesos que se posan por acciones con que LoadBmd los lee.
TEST_F(GoldenBmd, EveryModelFitsThePosedBoneLimit)
{
    std::size_t largest = 0;
    fs::path largestPath;
    for (const LabModel& lab : LabModels())
    {
        const auto posed = static_cast<std::size_t>(
            std::ranges::count_if(lab.model.bones, [](const Bone& bone) { return !bone.dummy; }));
        if (posed * lab.model.actions.size() > largest)
        {
            largest = posed * lab.model.actions.size();
            largestPath = lab.path;
        }
    }
    EXPECT_LE(largest, MaxPosedBoneActions);
    EXPECT_EQ(largest, 10250u) << "Player.bmd, con 50 de sus 60 huesos que se posan por 205 acciones";
    EXPECT_EQ(largestPath.generic_string(), "player/Player.bmd");
}

// 05: 142 mallas del laboratorio tienen script (0x459940), 135 con R, 6 con H (las de Object11\Object01
// a 06) y 1 con S (Item\Quest03), y ninguna con N. 17 son de partes del personaje, todas con R: el
// pelo de los cascos (hair_R.jpg) y otras tres texturas (CL6.1; antes 05 decía que ninguna).
TEST_F(GoldenBmd, TheTextureScriptsOfTheLab)
{
    std::size_t scripts = 0;
    std::map<char, std::size_t> letters;
    std::vector<std::string> hidden;
    std::vector<std::string> scrolling;
    std::size_t player = 0;
    for (const LabModel& lab : LabModels())
    {
        for (const Mesh& mesh : lab.model.meshes)
        {
            const std::optional<TextureScript> script = TextureScriptOf(mesh.textureName);
            if (!script.has_value())
            {
                continue;
            }
            ++scripts;
            letters['R'] += script->blendMesh ? 1u : 0u;
            letters['H'] += script->hidden ? 1u : 0u;
            letters['S'] += script->scrollUv ? 1u : 0u;
            letters['N'] += script->withoutShine ? 1u : 0u;
            if (script->hidden)
            {
                hidden.push_back(lab.path.generic_string());
            }
            if (script->scrollUv)
            {
                scrolling.push_back(lab.path.generic_string());
            }
            if (lab.path.begin()->generic_string() == "player")
            {
                ++player;
                EXPECT_TRUE(script->blendMesh) << lab.path.generic_string();
            }
        }
    }

    EXPECT_EQ(scripts, 142u);
    EXPECT_EQ(letters, (std::map<char, std::size_t>{{'H', 6}, {'N', 0}, {'R', 135}, {'S', 1}}));
    EXPECT_EQ(hidden,
              (std::vector<std::string>{"Object11/Object01.bmd", "Object11/Object02.bmd", "Object11/Object03.bmd",
                                        "Object11/Object04.bmd", "Object11/Object05.bmd", "Object11/Object06.bmd"}));
    EXPECT_EQ(scrolling, (std::vector<std::string>{"Item/Quest03.bmd"}));
    EXPECT_EQ(player, 17u);
}
} // namespace
