#pragma once

#include "Data/Models/Model.h"
#include "Models/MeshTraits.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace Client::Assets
{
// What the original's table keeps per model slot: the file, its skeleton, each mesh's texture slot and what each
// mesh's script says.
struct LoadedModel
{
    Data::Models::Model model;
    std::optional<Models::Skeleton> skeleton;
    std::vector<Render::TextureSlot> meshTextures;
    std::vector<Models::MeshTraits> meshes;
};

// The mesh traits are read here, once per load. The skeleton holds the animation keys, so the file keeps its bones
// without them.
[[nodiscard]] LoadedModel MakeLoadedModel(Data::Models::Model model, std::optional<Models::Skeleton> skeleton);

class ModelTable
{
public:
    LoadedModel& Insert(std::uint16_t slot, Data::Models::Model model, std::optional<Models::Skeleton> skeleton);
    void Clear();

    [[nodiscard]] const LoadedModel* Find(std::uint16_t slot) const;
    [[nodiscard]] LoadedModel* Find(std::uint16_t slot);
    [[nodiscard]] const Data::Models::Model* ModelOf(std::uint16_t slot) const;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t slot) const;
    [[nodiscard]] Models::Skeleton* SkeletonOf(std::uint16_t slot);
    // Empty if the slot has no model or its textures were not assigned.
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t slot) const;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(std::uint16_t slot) const;
    [[nodiscard]] std::size_t Count() const;
    [[nodiscard]] bool Empty() const;

    [[nodiscard]] auto begin() const
    {
        return m_models.begin();
    }
    [[nodiscard]] auto end() const
    {
        return m_models.end();
    }
    [[nodiscard]] auto begin()
    {
        return m_models.begin();
    }
    [[nodiscard]] auto end()
    {
        return m_models.end();
    }

private:
    std::map<std::uint16_t, LoadedModel> m_models;
};
} // namespace Client::Assets
