#pragma once

#include "Assets/ModelTable.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Render/ITextureStore.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/ModelId.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Models/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The models of a world's objects: what 0x616ec0 reads for the map. First each family's specials, with their textures
// from 0x60d530(0x69); Lorencia then uses the 112 names of 0x619a21..0x619ea1. The others follow the generic loop
// (0x61a0c4..0x61a0e1): k from 0 to 159 is Object<N>\Object<k + 1>.bmd ("%s0%d.bmd" up to 9). Their textures, from
// Object<N>\, follow the table from 0x60d530(0x41c), with GL_NEAREST and repeating (0x60d5f0(k, "Object<N>\\", 0x2901,
// 0x2600, 1)). Contract: 05 § Los modelos de cada mundo and 06 § El mundo del login. MapLoad creates the specials'
// effects and fixed objects.
namespace Client::Assets
{
struct WorldSpecialModelSet;
struct WorldModelTextureRequests;
class ModelTextureTable;
inline constexpr std::size_t WorldModelCount = 160;
// The dragon that flies in the login (06 § El dragón de fondo): only on map 55.
inline constexpr std::uint16_t DragonModel = 0xb4;
// The siege spearman: 0x612d90(0x4d) loads it into model 0x152 + 0x4d (Monster78) with map 30 (0x42aa6b).
inline constexpr std::uint16_t SpearmanModel = 0x19f;
inline constexpr Render::TextureSlot WorldModelsFirstSlot{0x41c};

class WorldModels final : public Models::IModelLibrary
{
public:
    // Reads those of `map` from `dataRoot` and gives them their textures following `gameSlots` (the load's table,
    // global in the original) from 0x41c, or an empty table without it, and uploads them. A family whose specials need
    // a missing service returns an error after releasing the previous one. A missing model is skipped silently, as in
    // the original (Q-C23). A texture that is missing or does not upload is skipped with a warning, as in GameModels:
    // the original finds it by name in the table, loaded by another (fire01 and fire02 of type 89, from 0x615700, which
    // MuRet does not load whole yet), and its mesh comes out without a texture. `globalModels`, if lent, lives until
    // Release and does not change: Blood Castle requires it before consuming resources; missing Gate01/02 are skipped
    // as optional files. The map overlays only its texture bindings, without copying or changing those models. Its own
    // slots and metadata take precedence over the global ones, and are only published after an effective upload.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         Render::ITextureStore& textures, Core::Log& log,
                                                         const Models::ModelTextureSlots* gameSlots,
                                                         const Models::IModelLibrary* globalModels = nullptr,
                                                         ModelTextureTable* sharedTextures = nullptr);
    void Release();

    [[nodiscard]] const Data::Models::Model* Find(Models::ModelId model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(Models::ModelId model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;

    [[nodiscard]] std::size_t Count() const;
    // The slots Load handed out, if it loaded: the next one is where the logo goes on (0x61c7c0).
    [[nodiscard]] const Models::ModelTextureSlots* TextureSlots() const;
    // 0x612d90 continues the global cursor with the textures of Monster32/34.
    [[nodiscard]] Models::ModelTextureSlots* TextureSlots();

private:
    void LoadFile(const std::filesystem::path& dataRoot, const Data::Models::ModelFile& file, Core::Log& log,
                  std::size_t& missing);
    void AssignSpecialTexture(const Data::Models::ModelFile& file, const WorldSpecialModelSet& specials,
                              WorldModelTextureRequests& textureRequests);
    [[nodiscard]] Core::Expected<void, std::string> LoadSpecials(const std::filesystem::path& dataRoot,
                                                                 const WorldSpecialModelSet& specials, Core::Log& log,
                                                                 std::size_t& missing,
                                                                 WorldModelTextureRequests& textureRequests);
    void LoadObjects(const std::filesystem::path& dataRoot, std::uint8_t map, Core::Log& log, std::size_t& missing,
                     WorldModelTextureRequests& textureRequests);
    [[nodiscard]] Core::Expected<void, std::string> ConfigureActions(std::uint8_t map,
                                                                     const WorldSpecialModelSet& specials);
    [[nodiscard]] Core::Expected<void, std::string> LoadTheSpearman(const std::filesystem::path& dataRoot,
                                                                    Core::Log& log, std::size_t& missing,
                                                                    WorldModelTextureRequests& textureRequests);
    void UploadTextures(const std::filesystem::path& dataRoot, Render::ITextureStore& textures, Core::Log& log,
                        const WorldModelTextureRequests& textureRequests);
    // The slots of an own model's meshes, or those this map binds to a global one.
    [[nodiscard]] std::vector<Render::TextureSlot>& BindingsOf(std::uint16_t model);
    void ForEachBinding(const std::function<void(Render::TextureSlot)>& visit) const;

    std::string m_folder;
    ModelTable m_models;
    // Blood Castle's Gates: global models whose textures this map binds again (0x60d617).
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_globalBindings;
    std::optional<Models::ModelTextureSlots> m_slots;
    Render::ITextureStore* m_store = nullptr;
    ModelTextureTable* m_sharedTextures = nullptr;
    std::size_t m_requestStart = 0;
    std::vector<Render::TextureSlot> m_uploaded;
    std::map<std::uint32_t, Models::TextureMetadata> m_textureMetadata;
    // Not owning: the global models and textures live until Release; they are not copied or changed.
    const Models::IModelLibrary* m_globalModels = nullptr;
};
} // namespace Client::Assets
