#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Textures/TextureSet.h"
#include "Models/IModelLibrary.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ConstructedBmdPrefix.h"
#include "Models/TextureMetadata.h"
#include "Scenes/ModelTextureTable.h"
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
namespace Client::Scenes
{
struct FaunaModelLoadContext
{
    const std::filesystem::path& dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
    Models::ModelTextureSlots& slots;
    ModelTextureTable* sharedTextures = nullptr;
    // WorldRuntime::modelSounds: 0x612d90 escribe la tabla de sonidos del modelo (0x612c80, 0x613127) después de
    // cargarlo con mallas. nullptr, no la escribe.
    ModelSoundTable* sounds = nullptr;
    Audio::IPlayback* audio = nullptr;
};
struct FaunaResourceSound
{
    std::int32_t id;
    std::string_view path;
    std::int32_t buffers = 2;
    std::int32_t flags = 1;
};
// Los modelos que el original carga cuando los pide un alta (0x612d90 y 0x612270): los de la fauna (0x1f y 0x21, el
// modelo 0x171 y el 0x173, desde StepGameFauna) y los de los monstruos y los NPC (desde 0x5141e0, CL9 hito 2, P-C176).
// En el original van al arreglo global de 0x5769ad8, que no se suelta al cambiar de mapa: el dueño dura más que los
// cambios de mapa, y Bind solo cambia la biblioteca prestada (la del mapa, que sigue con la global).
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
    // 60dc4c: el caller entrega su elección CRT en la inicialización global de modelos.
    [[nodiscard]] Core::Expected<void, std::string> InitializePrefix(std::uint16_t constructedBeforeBase);
    [[nodiscard]] const Models::Skeleton* PrefixModel() const;
    [[nodiscard]] std::optional<std::uint16_t> PrefixOffset() const;
    // 612d90(1f/21) desde la fauna: los otros números cortan.
    [[nodiscard]] Core::Expected<void, std::string> Prepare(std::int32_t monster, FaunaModelLoadContext& context);
    // 0x612d90(n) entero, con el mapa en curso (0x6c9dd0), que elige la carpeta de las texturas. Con el modelo 0x152 +
    // n ya con acciones o mallas (aquí o en la biblioteca prestada), nada (0x612dbc/0x612dc7). Lee Monster{n + 1}.bmd:
    // sin él, el original cierra (0x612dd6..0x612de1, Q-C23) y MuRet corta (P-C173). Sin mallas, nada más (0x612dee).
    // Con ellas, sus texturas (0x60d5f0) y las tablas de MonsterModelTable: las siete velocidades comunes, el factor,
    // el primer switch, la acción 1 que no se repite y el tercer switch, con sus .wav (SoundFiles), los diez sonidos
    // de las acciones (SoundActions), el hueso de la cabeza (+0x54), el byte +0x88 y las texturas fijas. Una velocidad
    // que cae fuera de las acciones del modelo (el original escribe fuera de su arreglo) corta.
    [[nodiscard]] Core::Expected<void, std::string> LoadMonster(std::int32_t number, std::int32_t map,
                                                                FaunaModelLoadContext& context);
    // 0x612270(modelo): con el modelo ya con acciones, nada (0x612298..0x61229f). Si no, su caso (los .bmd de las
    // carpetas Npc y Object35, las partes de 0x1cb..0x1cd, sus texturas y sus .wav) y la cola: sus acciones a 0,25 y,
    // con mallas, 0x60d5f0 otra vez. Un .bmd que falta se saltea sin avisar (0x60da30 solo cierra con Monster, Player
    // y PlayerTest). NPC\BloodCastle01.bmd, con padres después de sus hijos (Q-C108), corta: la pose de MuRet no lo
    // admite (08, P-C240).
    [[nodiscard]] Core::Expected<void, std::string> LoadNpc(std::uint16_t model, FaunaModelLoadContext& context);
    // Lo que usan LoadMonster y LoadNpc desde un alta: la tabla de texturas propia con su cursor y el backend que le
    // ligó el juego (GameScene::Enter); sin backend, nullopt.
    [[nodiscard]] std::optional<FaunaModelLoadContext> LoadContextOf(const std::filesystem::path& dataRoot,
                                                                     Core::Log& log);
    // La velocidad (+4) de la acción `action` de un modelo propio, la que escribe el guion del recorrido de la cámara
    // (0x403a96..0x403abe, CL9 hito 11) después del alta. Un modelo que no está acá (el de la biblioteca prestada, que
    // MuRet no deja escribir) o una acción fuera de su arreglo (el original escribe afuera) cortan con `address`.
    [[nodiscard]] Core::Expected<void, std::string> SetActionSpeed(std::uint16_t model, std::uint8_t action,
                                                                   float speed, std::string_view address);
    // El byte `offset` de un modelo que escribió 0x612d90 (el 0x88 del 0x16d, 0x613f97), si lo escribió.
    [[nodiscard]] std::optional<std::uint8_t> ModelByte(std::uint16_t model, std::uint8_t offset) const;
    void Release();
    void ResetSoundRegistrationStamps();
    [[nodiscard]] ModelTextureTable& TextureTable();
    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;
    [[nodiscard]] std::span<const FaunaResourceSound> SoundFiles() const;
    // +0xaa..+0xbc del modelo: lo que escribió 0x612c80, o los ceros de 0x60dbe0 (también sin el modelo: el registro en
    // 0 del original).
    [[nodiscard]] std::span<const std::int16_t, 10> SoundActions(std::uint16_t model) const;
    // +0xa4/+0xa8 del modelo: frame/key publicados por pose (4eb860) y avance (4ec200).
    // 0x60dbe0 los deja en cero; no se reinician al soltar los modelos (*inferido*).
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
        // +0xaa: 0x60dbe0 deja el arreglo en 0 (0x60dc80) y solo 0x612c80 lo escribe.
        std::array<std::int16_t, 10> sounds{};
        std::map<std::uint8_t, std::uint8_t> bytes;
    };
    // El modelo con acciones o mallas, aquí o en la biblioteca prestada: lo que miran las guardas de 0x612d90 y
    // 0x612270.
    [[nodiscard]] bool HasActions(std::uint16_t model) const;
    [[nodiscard]] bool HasMeshes(std::uint16_t model) const;
    // 0x60d5f0(modelo, carpeta, …) sobre un modelo propio: sus slots y la subida de lo nuevo.
    void AssignTextures(Entry& entry, std::string_view folder, FaunaModelLoadContext& context);
    // 0x63b870 y 0x63bd00 en un slot fijo.
    [[nodiscard]] Core::Expected<void, std::string> LoadFixedTexture(const Data::Textures::TextureSetEntry& texture,
                                                                     FaunaModelLoadContext& context);
    [[nodiscard]] Core::Expected<void, std::string> ApplyMonsterTables(std::int32_t number, Entry& entry,
                                                                       FaunaModelLoadContext& context);
    void RegisterModelSounds(std::uint16_t model, Audio::IPlayback* audio);
    // No se reinicia al salir de un mapa ni al soltar sus texturas.
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
} // namespace Client::Scenes
