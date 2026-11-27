#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Textures/TextureSet.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Assets/ConstructedBmdPrefix.h"
#include "Models/TextureMetadata.h"
#include "Assets/ModelTextureTable.h"
#include "Sound/MonsterSounds.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/ext/vector_float2.hpp>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Audio
{
class IPlayback;
}
namespace Client::Core
{
class Log;
}
namespace Client::Render
{
class ITextureStore;
}
namespace Client::Models
{
class ModelTextureSlots;
}
namespace Client::Assets
{
struct FaunaModelLoadContext
{
    const std::filesystem::path& dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
    Models::ModelTextureSlots& slots;
    ModelTextureTable* sharedTextures = nullptr;
    // WorldRuntime::modelSounds: 0x612d90 writes the model's sound table (0x612c80, 0x613127) after loading it with
    // meshes. With nullptr it does not write it.
    Sound::ModelSoundTable* sounds = nullptr;
    Audio::IPlayback* audio = nullptr;
};
struct FaunaResourceSound
{
    std::int32_t id;
    std::string_view path;
    std::int32_t buffers = 2;
    std::int32_t flags = 1;
};
// The models the original loads when a creation asks for them (0x612d90 and 0x612270): the fauna's (0x1f and 0x21,
// models 0x171 and 0x173, from StepGameFauna) and the monsters' and NPCs' (from 0x5141e0, P-C176). In the original
// they go to the global array at 0x5769ad8, which is not released on a map change: the owner outlives map changes,
// and Bind only changes the borrowed library (the map's, which stays with the global one).
class FaunaModels final : public Models::IModelLibrary
{
public:
    FaunaModels() = default;
    FaunaModels(const FaunaModels&) = delete;
    FaunaModels& operator=(const FaunaModels&) = delete;
    FaunaModels(FaunaModels&&) = delete;
    FaunaModels& operator=(FaunaModels&&) = delete;
    ~FaunaModels() override;
    void Bind(const Models::IModelLibrary* base);
    // 0x60dc4c: the caller hands over its CRT choice in the global model initialization.
    [[nodiscard]] Core::Expected<void, std::string> InitializePrefix(std::uint16_t constructedBeforeBase);
    [[nodiscard]] const Models::Skeleton* PrefixModel() const;
    [[nodiscard]] std::optional<std::uint16_t> PrefixOffset() const;
    // 0x612d90(0x1f/0x21) from the fauna: other numbers fail.
    [[nodiscard]] Core::Expected<void, std::string> Prepare(std::int32_t monster, FaunaModelLoadContext& context);
    // The whole 0x612d90(n), with the current map (0x6c9dd0) choosing the texture folder. With model 0x152 + n already
    // holding actions or meshes (here or in the borrowed library), nothing (0x612dbc/0x612dc7). Reads Monster{n +
    // 1}.bmd: without it the original closes (0x612dd6..0x612de1, Q-C23) and MuRet fails (P-C173). Without meshes,
    // nothing else (0x612dee). With them, its textures (0x60d5f0) and the MonsterModelTable tables: the seven common
    // speeds, the factor, the first switch, action 1 that does not repeat and the third switch, with their .wav
    // (SoundFiles), the ten action sounds (SoundActions), the head bone (+0x54), byte +0x88 and the fixed textures. A
    // speed outside the model's actions (the original writes outside its array) fails.
    [[nodiscard]] Core::Expected<void, std::string> LoadMonster(std::int32_t number, std::int32_t map,
                                                                FaunaModelLoadContext& context);
    // 0x612270(model): with the model already holding actions, nothing (0x612298..0x61229f). Otherwise its case (the
    // .bmd files of the Npc and Object35 folders, the parts of 0x1cb..0x1cd, their textures and their .wav) and the
    // tail: its actions at 0.25 and, with meshes, 0x60d5f0 again. A missing .bmd is skipped silently (0x60da30 only
    // closes the game for Monster, Player and PlayerTest). NPC\BloodCastle01.bmd, with parents after their children
    // (Q-C108), fails: MuRet's pose does not allow it (08, P-C240).
    [[nodiscard]] Core::Expected<void, std::string> LoadNpc(std::uint16_t model, FaunaModelLoadContext& context);
    // What LoadMonster and LoadNpc use from a creation: the own texture table with its cursor and the backend the game
    // bound to it (GameScene::Enter); without a backend, nullopt.
    [[nodiscard]] std::optional<FaunaModelLoadContext> LoadContextOf(const std::filesystem::path& dataRoot,
                                                                     Core::Log& log);
    // The speed (+4) of action `action` of an own model, which the camera tour script writes (0x403a96..0x403abe) after
    // the creation. A model that is not here (one of the borrowed library, which MuRet does not let anyone write) or an
    // action outside its array (the original writes outside it) fails with `address`.
    [[nodiscard]] Core::Expected<void, std::string> SetActionSpeed(std::uint16_t model, std::uint8_t action,
                                                                   float speed, std::string_view address);
    // Byte `offset` of a model 0x612d90 wrote (0x88 of 0x16d, 0x613f97), if it wrote it.
    [[nodiscard]] std::optional<std::uint8_t> ModelByte(std::uint16_t model, std::uint8_t offset) const;
    void Release();
    void ResetSoundRegistrationStamps();
    [[nodiscard]] ModelTextureTable& TextureTable();
    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(std::uint16_t model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;
    [[nodiscard]] std::span<const FaunaResourceSound> SoundFiles() const;
    // +0xaa..+0xbc of the model: what 0x612c80 wrote, or the zeros of 0x60dbe0 (also without the model: the original's
    // record at 0).
    [[nodiscard]] std::span<const std::int16_t, 10> SoundActions(std::uint16_t model) const;
    // +0xa4/+0xa8 of the model: the frame and key that pose (0x4eb860) and advance (0x4ec200) publish. 0x60dbe0 leaves
    // them at zero; they are not reset when the models are released (*inferred*).
    [[nodiscard]] float PoseFrame(std::uint16_t model) const;
    [[nodiscard]] std::int16_t PoseKey(std::uint16_t model) const;
    void RecordPoseKey(std::uint16_t model, std::int16_t key);
    void RecordAnimation(std::uint16_t model, float frame, std::int16_t key);

private:
    struct Entry
    {
        Data::Models::Model model;
        Models::Skeleton skeleton;
        std::vector<Render::TextureSlot> textures;
        std::vector<Models::MeshTraits> meshes;
        // +0xaa: 0x60dbe0 leaves the array at 0 (0x60dc80) and only 0x612c80 writes it.
        std::array<std::int16_t, 10> sounds{};
        std::map<std::uint8_t, std::uint8_t> bytes;
    };
    // The model with actions or meshes, here or in the borrowed library: what the guards of 0x612d90 and 0x612270
    // check.
    [[nodiscard]] bool HasActions(std::uint16_t model) const;
    [[nodiscard]] bool HasMeshes(std::uint16_t model) const;
    // 0x60d5f0(model, folder, …) on an own model: its slots and the upload of what is new.
    void AssignTextures(Entry& entry, std::string_view folder, FaunaModelLoadContext& context);
    // The file, its skeleton and its traits as a loaded model keeps them (MakeLoadedModel).
    static void SetLoaded(Entry& entry, Data::Models::Model model, Models::Skeleton skeleton);
    // 0x63b870 and 0x63bd00 on a fixed slot.
    [[nodiscard]] Core::Expected<void, std::string> LoadFixedTexture(const Data::Textures::TextureSetEntry& texture,
                                                                     FaunaModelLoadContext& context);
    [[nodiscard]] Core::Expected<void, std::string> ApplyMonsterTables(std::int32_t number, Entry& entry,
                                                                       FaunaModelLoadContext& context);
    void RegisterModelSounds(std::uint16_t model, Audio::IPlayback* audio);
    // Not reset when leaving a map or releasing its textures.
    ConstructedBmdPrefix m_prefix;
    ModelTextureTable m_textureTable;
    ModelTextureTable* m_sharedTextures = nullptr;
    const Models::IModelLibrary* m_base = nullptr;
    Render::ITextureStore* m_store = nullptr;
    std::map<std::uint16_t, Entry> m_models;
    std::map<std::uint32_t, Models::TextureMetadata> m_metadata;
    std::vector<Render::TextureSlot> m_uploaded;
    std::vector<FaunaResourceSound> m_sounds;
    struct ModelSoundRegistration
    {
        std::vector<FaunaResourceSound> sounds;
        bool registered = false;
    };
    std::map<std::uint16_t, ModelSoundRegistration> m_modelSoundRegistrations;
    struct AnimationMetadata
    {
        float frame = 0.0f;
        std::int16_t key = 0;
    };
    std::map<std::uint16_t, AnimationMetadata> m_animationMetadata;
};

// What a reader without the models sees: no model, the zero sounds and the zero pose.
[[nodiscard]] const FaunaModels& FaunaModelsOrEmpty(const FaunaModels* models);
} // namespace Client::Assets
