#include "Assets/ModelTable.h"

#include "Data/Models/Model.h"
#include "Models/MeshTraits.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace Client::Assets
{
LoadedModel MakeLoadedModel(Data::Models::Model model, std::optional<Models::Skeleton> skeleton)
{
    LoadedModel loaded;
    loaded.meshes = Models::ReadMeshTraits(model);
    if (skeleton.has_value())
    {
        for (Data::Models::Bone& bone : model.bones)
            bone.actions = {};
    }
    loaded.model = std::move(model);
    loaded.skeleton = std::move(skeleton);
    return loaded;
}

LoadedModel& ModelTable::Insert(std::uint16_t slot, Data::Models::Model model, std::optional<Models::Skeleton> skeleton)
{
    return m_models.insert_or_assign(slot, MakeLoadedModel(std::move(model), std::move(skeleton))).first->second;
}

void ModelTable::Clear()
{
    m_models.clear();
}

const LoadedModel* ModelTable::Find(std::uint16_t slot) const
{
    const auto found = m_models.find(slot);
    return found == m_models.end() ? nullptr : &found->second;
}

LoadedModel* ModelTable::Find(std::uint16_t slot)
{
    const auto found = m_models.find(slot);
    return found == m_models.end() ? nullptr : &found->second;
}

const Data::Models::Model* ModelTable::ModelOf(std::uint16_t slot) const
{
    const LoadedModel* loaded = Find(slot);
    return loaded == nullptr ? nullptr : &loaded->model;
}

const Models::Skeleton* ModelTable::SkeletonOf(std::uint16_t slot) const
{
    const LoadedModel* loaded = Find(slot);
    return loaded == nullptr || !loaded->skeleton.has_value() ? nullptr : &*loaded->skeleton;
}

Models::Skeleton* ModelTable::SkeletonOf(std::uint16_t slot)
{
    LoadedModel* loaded = Find(slot);
    return loaded == nullptr || !loaded->skeleton.has_value() ? nullptr : &*loaded->skeleton;
}

std::span<const Render::TextureSlot> ModelTable::MeshTextures(std::uint16_t slot) const
{
    const LoadedModel* loaded = Find(slot);
    return loaded == nullptr ? std::span<const Render::TextureSlot>{} : loaded->meshTextures;
}

std::span<const Models::MeshTraits> ModelTable::MeshTraitsOf(std::uint16_t slot) const
{
    const LoadedModel* loaded = Find(slot);
    return loaded == nullptr ? std::span<const Models::MeshTraits>{} : loaded->meshes;
}

std::size_t ModelTable::Count() const
{
    return m_models.size();
}

bool ModelTable::Empty() const
{
    return m_models.empty();
}
} // namespace Client::Assets
