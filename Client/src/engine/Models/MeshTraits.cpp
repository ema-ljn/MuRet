#include "Models/MeshTraits.h"

#include "Data/Models/Model.h"
#include "Data/Models/ModelTextures.h"

#include <optional>
#include <vector>

namespace Client::Models
{
std::vector<MeshTraits> ReadMeshTraits(const Data::Models::Model& model)
{
    std::vector<MeshTraits> traits;
    traits.reserve(model.meshes.size());
    for (const Data::Models::Mesh& mesh : model.meshes)
    {
        MeshTraits read;
        if (const std::optional<Data::Models::TextureScript> script = Data::Models::TextureScriptOf(mesh.textureName))
        {
            read.hidden = script->hidden;
            read.blendScript = script->blendMesh;
            read.scrollScript = script->scrollUv;
            read.shines = !script->withoutShine;
        }
        traits.push_back(read);
    }
    return traits;
}
} // namespace Client::Models
