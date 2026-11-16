#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterSounds.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ConstructedBmdPrefix.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/MonsterModelTable.h"
#include "Scenes/TextureMetadata.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <glm/ext/vector_float2.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t IcarusMonster = 0x1f;
constexpr std::int32_t KalimaMonster = 0x21;
// Los sonidos de un modelo que no cargó nadie: el registro de 0x5769ad8 en 0 (0x60dbe0, 0x60dc80), no −1.
constexpr std::array<std::int16_t, 10> NoSounds{};
constexpr Data::Textures::Sampling MonsterSampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Repeat};
// El slot que el destructor del BMD no suelta (0x4f07b0..0x4f07c3).
constexpr std::uint16_t KeptSlot = 0x12e;
// NPC\BloodCastle01.bmd, con los huesos 43 y 44 colgados del 45 (Q-C108).
constexpr std::uint16_t BloodCastleNpc = 0x1f3;

Data::Textures::Sampling SamplingOf(std::uint32_t filter, std::uint32_t wrap)
{
    constexpr std::uint32_t GlLinear = 0x2601;
    constexpr std::uint32_t GlRepeat = 0x2901;
    return {filter == GlLinear ? Data::Textures::Filter::Linear : Data::Textures::Filter::Nearest,
            wrap == GlRepeat ? Data::Textures::Wrap::Repeat : Data::Textures::Wrap::Clamp};
}

// Un pedido de 0x41eac0 como lo guarda MuRet.
FaunaResourceSound SoundOf(const ModelSoundFile& sound)
{
    return {sound.id, sound.path, sound.buffers, sound.flags};
}

Core::Expected<void, std::string> SetSpeed(Render::Models::Skeleton& skeleton, std::uint8_t action, float speed,
                                           std::uint16_t model, std::string_view address)
{
    if (action >= skeleton.actions.size())
        return Core::Unexpected(std::format("{}: 0x612d90 escribe la velocidad de la acción {} del modelo {:#x}, que "
                                            "tiene {}: el original escribe fuera de su arreglo",
                                            address, action, model, skeleton.actions.size()));
    skeleton.actions[action].speed = speed;
    return {};
}
} // namespace
FaunaModels::~FaunaModels()
{
    Release();
}
void FaunaModels::Bind(const IModelLibrary* base)
{
    m_base = base;
}
Core::Expected<void, std::string> FaunaModels::InitializePrefix(std::uint16_t constructedBeforeBase)
{
    return m_prefix.Initialize(constructedBeforeBase);
}
const Render::Models::Skeleton* FaunaModels::PrefixModel() const
{
    return m_prefix.BorrowForSetAction();
}
std::optional<std::uint16_t> FaunaModels::PrefixOffset() const
{
    return m_prefix.Offset();
}
Core::Expected<void, std::string> FaunaModels::Prepare(std::int32_t monster, FaunaModelLoadContext& context)
{
    if (monster != IcarusMonster && monster != KalimaMonster)
        return Core::Unexpected(std::string("612d90: familia de recurso de fauna no admitida"));
    // La carpeta de las texturas solo cambia con el 0x46..0x48 y el 0x4a: la del 0x1f y el 0x21 es la de los monstruos
    // en todo mapa.
    constexpr std::int32_t AnyMap = 0;
    return LoadMonster(monster, AnyMap, context);
}

bool FaunaModels::HasActions(std::uint16_t model) const
{
    const auto* skeleton = SkeletonOf(model);
    return skeleton != nullptr && !skeleton->actions.empty();
}

bool FaunaModels::HasMeshes(std::uint16_t model) const
{
    const auto* found = Find(model);
    return found != nullptr && !found->meshes.empty();
}

void FaunaModels::AssignTextures(Entry& entry, std::string_view folder, FaunaModelLoadContext& context)
{
    const auto first = context.slots.ToLoad().size();
    entry.textures = context.slots.Assign(entry.model, folder, MonsterSampling);
    m_store = &context.textures;
    m_sharedTextures = context.sharedTextures;
    if (m_sharedTextures != nullptr)
        m_sharedTextures->Bind(context.textures);
    std::vector<Render::TextureSlot> uploadedHere;
    for (const auto& request : context.slots.ToLoad().subspan(first))
    {
        auto texture = Data::Textures::LoadTextureSet(context.dataRoot, std::span(&request, 1));
        if (!texture.HasValue())
        {
            context.log.Warning("una textura de un monstruo se saltea: {}", Data::Textures::Describe(texture.Error()));
            continue;
        }
        auto uploaded = m_sharedTextures != nullptr ? m_sharedTextures->Upload(texture.Value().front())
                                                    : context.textures.Load(texture.Value());
        if (!uploaded.HasValue())
        {
            context.log.Warning("una textura de un monstruo no sube: {}", uploaded.Error());
            continue;
        }
        m_uploaded.push_back(Render::TextureSlot{request.slot});
        uploadedHere.push_back(Render::TextureSlot{request.slot});
        m_metadata.insert_or_assign(request.slot, TextureMetadata(texture.Value().front().texture));
    }
    // 0x60d645: una coincidencia por nombre suma una referencia.
    if (m_sharedTextures != nullptr)
        for (const auto slot : entry.textures)
        {
            const auto uploaded = std::find(uploadedHere.begin(), uploadedHere.end(), slot);
            if (uploaded != uploadedHere.end())
                uploadedHere.erase(uploaded);
            else
                m_sharedTextures->Acquire(slot);
        }
}

Core::Expected<void, std::string> FaunaModels::LoadFixedTexture(const Data::Textures::TextureSetEntry& texture,
                                                                FaunaModelLoadContext& context)
{
    auto loaded = Data::Textures::LoadTextureSet(context.dataRoot, std::span(&texture, 1));
    if (!loaded.HasValue())
        return Core::Unexpected(std::format("0x612d90: {} (el original cierra con un archivo que falta, Q-C23)",
                                            Data::Textures::Describe(loaded.Error())));
    auto uploaded = m_sharedTextures != nullptr ? m_sharedTextures->Upload(loaded.Value().front())
                                                : context.textures.Load(loaded.Value());
    if (!uploaded.HasValue())
        return Core::Unexpected(uploaded.Error());
    m_uploaded.push_back(Render::TextureSlot{texture.slot});
    m_metadata.insert_or_assign(texture.slot, TextureMetadata(loaded.Value().front().texture));
    return {};
}

Core::Expected<void, std::string> FaunaModels::ApplyMonsterTables(std::int32_t number, Entry& entry,
                                                                  FaunaModelLoadContext& context)
{
    Render::Models::Skeleton& skeleton = entry.skeleton;
    const auto model = static_cast<std::uint16_t>(FirstMonsterModel + number);
    // 0x612e5a..0x612e9f.
    for (const ActionSpeedStore& store : CommonMonsterSpeeds())
        if (auto set = SetSpeed(skeleton, store.action, std::bit_cast<float>(store.bits), model, "0x612e5a");
            !set.HasValue())
            return set;
    constexpr std::size_t NotRepeatedAction = 6;
    skeleton.actions[NotRepeatedAction].repeats = false;
    // 0x612ea3..0x612efb: las acciones 0 a 5 (0x10 · 6 = 0x60).
    constexpr std::size_t ScaledActions = 6;
    if (const auto factor = MonsterSpeedFactorOf(number))
        for (std::size_t action = 0; action < ScaledActions; ++action)
            skeleton.actions[action].speed *= *factor;
    // 0x612efd..0x613103.
    for (const ActionSpeedStore& store : EarlyMonsterSpeedsOf(number))
        if (auto set = SetSpeed(skeleton, store.action, std::bit_cast<float>(store.bits), model, "0x612f16");
            !set.HasValue())
            return set;
    // 0x613109..0x613123.
    if (MonsterFirstActionStops(number))
        skeleton.actions[1].repeats = false;
    // 0x613127: el tercer switch. Lo que escribe en otro modelo (el 0xc y el 0xd escriben los dos) solo vale si ese ya
    // está: su propio caso escribe lo mismo cuando se carga.
    for (const MonsterModelOp& op : MonsterModelCaseOf(number).ops)
    {
        Entry* target = &entry;
        if (op.model != 0 && op.model != model)
        {
            const auto other = m_models.find(op.model);
            if (other == m_models.end())
                continue;
            target = &other->second;
        }
        switch (op.kind)
        {
        case MonsterModelOpKind::Sound:
            m_sounds.push_back(SoundOf({static_cast<std::int32_t>(op.value), op.path}));
            break;
        case MonsterModelOpKind::SoundTable:
            target->sounds = op.sounds;
            break;
        case MonsterModelOpKind::Speed:
            if (auto set = SetSpeed(target->skeleton, op.index, std::bit_cast<float>(op.value), model, "0x613130");
                !set.HasValue())
                return set;
            break;
        case MonsterModelOpKind::HeadBone:
            target->skeleton.headBone = static_cast<std::int16_t>(op.value);
            break;
        case MonsterModelOpKind::ModelByte:
            target->bytes.insert_or_assign(op.index, static_cast<std::uint8_t>(op.value));
            break;
        case MonsterModelOpKind::JpegTexture:
        case MonsterModelOpKind::TgaTexture:
        {
            const Data::Textures::TextureSetEntry texture{op.path, static_cast<std::uint16_t>(op.value),
                                                          SamplingOf(op.filter, op.wrap)};
            if (auto loaded = LoadFixedTexture(texture, context); !loaded.HasValue())
                return loaded;
            break;
        }
        }
    }
    return {};
}

Core::Expected<void, std::string> FaunaModels::LoadMonster(std::int32_t number, std::int32_t map,
                                                           FaunaModelLoadContext& context)
{
    const auto model = static_cast<std::uint16_t>(FirstMonsterModel + number);
    // 0x612dbc/0x612dc7: con [0x6c9e0c] en 1, acciones o mallas ya presentes.
    if (HasActions(model) || HasMeshes(model))
        return {};
    // 0x612de1: 0x60da30(0x152 + n, "Data\Monster\", "Monster", n + 1).
    auto loaded = Data::Models::LoadModelFile(context.dataRoot, {model, "Monster\\", "Monster", number + 1});
    if (!loaded.HasValue())
        return Core::Unexpected(std::format("0x612d90 no encuentra {} y el original cierra (0x612dd6..0x612de1, "
                                            "Q-C23, P-C173)",
                                            loaded.Error().path));
    auto skeleton = Render::Models::MakeSkeleton(loaded.Value());
    if (!skeleton.HasValue())
        return Core::Unexpected(std::format("0x612de1: el hueso {} de Monster\\{} cuelga de uno de después (Q-C108)",
                                            skeleton.Error().bone, Data::Models::ModelFileName("Monster", number + 1)));
    auto& entry = m_models[model];
    entry.model = std::move(loaded).Value();
    entry.skeleton = std::move(skeleton).Value();
    // 0x612dee: sin mallas, ni texturas ni acciones.
    if (entry.model.meshes.empty())
        return {};
    AssignTextures(entry, MonsterTextureFolderOf(number, map), context);
    if (auto applied = ApplyMonsterTables(number, entry, context); !applied.HasValue())
        return applied;
    // 0x613127: el caso del switch de `number` con su tabla de sonidos (0x612c80) también en la tabla de los pasos del
    // ataque (WorldRuntime::modelSounds, CL9 hito 3).
    if (context.sounds != nullptr)
        WriteMonsterSounds(number, *context.sounds);
    return {};
}

Core::Expected<void, std::string> FaunaModels::LoadNpc(std::uint16_t model, FaunaModelLoadContext& context)
{
    // 0x61228d..0x61229f: con [0x6c9e0c] en 1, solo las acciones.
    if (HasActions(model))
        return {};
    for (const NpcModelOp& op : NpcModelCaseOf(model).ops)
    {
        switch (op.kind)
        {
        case NpcModelOpKind::Load:
        {
            auto loaded = Data::Models::LoadModelFile(context.dataRoot, {op.model, op.folder, op.name, op.number});
            if (!loaded.HasValue())
            {
                // 0x60da30 solo cierra con Monster, Player y PlayerTest: los demás se saltean (Q-C23).
                if (loaded.Error().kind != Data::Models::ModelLoadErrorKind::Missing)
                    context.log.Warning("un modelo de NPC no se puede leer y no se dibuja: {}", loaded.Error().path);
                break;
            }
            if (op.model == BloodCastleNpc)
                return Core::Unexpected(std::string("0x6127a4: NPC\\BloodCastle01.bmd cuelga los huesos 43 y 44 "
                                                    "del 45, que va después (Q-C108): la pose de MuRet no lo admite "
                                                    "(P-C240)"));
            auto skeleton = Render::Models::MakeSkeleton(loaded.Value());
            if (!skeleton.HasValue())
                return Core::Unexpected(std::format("0x612270: el hueso {} de {} cuelga de uno de después (Q-C108)",
                                                    skeleton.Error().bone,
                                                    Data::Models::ModelFileName(op.name, op.number)));
            auto& entry = m_models[op.model];
            entry.model = std::move(loaded).Value();
            entry.skeleton = std::move(skeleton).Value();
            entry.textures.clear();
            break;
        }
        case NpcModelOpKind::Textures:
        {
            // 0x60d5f0 sobre un modelo sin mallas no pide nada.
            const auto found = m_models.find(op.model);
            if (found != m_models.end() && !found->second.model.meshes.empty())
                AssignTextures(found->second, op.folder, context);
            break;
        }
        case NpcModelOpKind::Sound:
            m_sounds.push_back(SoundOf(op.sound));
            break;
        }
    }
    // 0x612b4f..0x612b8b. Un modelo de la biblioteca prestada (un objeto del mapa) sin acciones con mallas solo
    // volvería a encontrar sus nombres en la tabla: MuRet no lo toca.
    const auto found = m_models.find(model);
    if (found == m_models.end())
        return {};
    for (Render::Models::ActionInfo& action : found->second.skeleton.actions)
        action.speed = NpcActionSpeed;
    if (!found->second.model.meshes.empty())
        AssignTextures(found->second, NpcTextureFolder, context);
    return {};
}

std::optional<FaunaModelLoadContext> FaunaModels::LoadContextOf(const std::filesystem::path& dataRoot, Core::Log& log)
{
    Render::ITextureStore* store = m_textureTable.Store();
    if (store == nullptr)
        return std::nullopt;
    return FaunaModelLoadContext{dataRoot, *store, log, m_textureTable.Slots(), &m_textureTable};
}

std::optional<std::uint8_t> FaunaModels::ModelByte(std::uint16_t model, std::uint8_t offset) const
{
    const auto found = m_models.find(model);
    if (found == m_models.end())
        return std::nullopt;
    const auto byte = found->second.bytes.find(offset);
    return byte != found->second.bytes.end() ? std::optional(byte->second) : std::nullopt;
}

void FaunaModels::Release()
{
    if (m_sharedTextures != nullptr)
    {
        for (const auto& [model, entry] : m_models)
            for (const auto slot : entry.textures)
                if (slot.value != KeptSlot)
                    m_sharedTextures->Release(slot);
    }
    else if (m_store != nullptr)
        for (const auto slot : m_uploaded)
            m_store->Release(slot);
    m_models.clear();
    m_metadata.clear();
    m_uploaded.clear();
    m_sounds.clear();
    m_store = nullptr;
    m_sharedTextures = nullptr;
    m_base = nullptr;
}
ModelTextureTable& FaunaModels::TextureTable()
{
    return m_textureTable;
}
const Data::Models::Model* FaunaModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? &found->second.model : m_base != nullptr ? m_base->Find(model) : nullptr;
}
const Render::Models::Skeleton* FaunaModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? &found->second.skeleton : m_base != nullptr ? m_base->SkeletonOf(model) : nullptr;
}
std::span<const Render::TextureSlot> FaunaModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    if (found != m_models.end())
        return found->second.textures;
    return m_base != nullptr ? m_base->MeshTextures(model) : std::span<const Render::TextureSlot>{};
}
std::optional<TextureMetadata> FaunaModels::TextureMetadataOf(std::uint32_t selector) const
{
    if (m_sharedTextures != nullptr)
        return m_sharedTextures->Metadata(selector);
    const auto found = m_metadata.find(selector);
    if (found != m_metadata.end())
        return found->second;
    return m_base != nullptr ? m_base->TextureMetadataOf(selector) : std::nullopt;
}
std::optional<Render::TextureSlot> FaunaModels::TextureSlotOf(std::uint32_t selector) const
{
    if (m_sharedTextures != nullptr)
        return m_sharedTextures->Metadata(selector)
                   ? std::optional(Render::TextureSlot{static_cast<std::uint16_t>(selector)})
                   : std::nullopt;
    if (m_base != nullptr)
        if (auto slot = m_base->TextureSlotOf(selector))
            return slot;
    const auto found = m_metadata.find(selector);
    return found != m_metadata.end() ? std::optional(Render::TextureSlot{static_cast<std::uint16_t>(selector)})
                                     : std::nullopt;
}
std::optional<glm::vec2> FaunaModels::TextureSize(Render::TextureSlot slot) const
{
    const auto metadata = TextureMetadataOf(slot.value);
    return metadata ? std::optional(metadata->size) : std::nullopt;
}
std::span<const FaunaResourceSound> FaunaModels::SoundFiles() const
{
    return m_sounds;
}
std::span<const std::int16_t, 10> FaunaModels::SoundActions(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? std::span<const std::int16_t, 10>(found->second.sounds)
                                   : std::span<const std::int16_t, 10>(NoSounds);
}

std::int16_t FaunaModels::PoseKey(std::uint16_t model) const
{
    const auto found = m_poseKeys.find(model);
    return found != m_poseKeys.end() ? found->second : std::int16_t{0};
}

void FaunaModels::RecordPoseKey(std::uint16_t model, std::int16_t key)
{
    m_poseKeys[model] = key;
}
} // namespace Client::Scenes
