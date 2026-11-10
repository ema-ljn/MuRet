#include "Scenes/CreatePreview.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Textures/Sampling.h"
#include "Render/ITextureStore.h"
#include "Render/Models/Animation.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/Text/TextLayout.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ActorConstruction.h"
#include "Render/Models/Cloth.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneTextures.h"
#include "UI/Widget.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
constexpr std::string_view LogoFolder = "Logo\\";
constexpr Data::Textures::Sampling LogoSampling{Data::Textures::Filter::Linear, Data::Textures::Wrap::Repeat};
constexpr Data::Textures::Sampling NewFaceSampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Repeat};
// 0x3e99999a en las acciones 0 y 1 de los NewFace (0x61ca30), por 2 en 0x4f9580 (0xa8 a 0xac).
constexpr float NewFaceActionSpeed = 0.3f;
constexpr float NewFaceSpeedFactor = 2.0f;
constexpr std::uint8_t StandingAction = 0;
constexpr std::uint8_t GreetingAction = 1;
// 0x511290(objeto, modelo, 0, 0, 0): x = y = 0 · 100 + 50.
constexpr glm::vec3 PreviewPosition{50.0f, 50.0f, 0.0f};
// 0x511290 prende la luz por vértice (+0xe8 en 1, 0x511be6), y 0x6002e0 les pisa el color a los
// 0xa8 a 0xac con 4,8 (0x4099999a, 0x6007d7) antes de RenderBody(2): cada esquina queda en
// 4,8 · max(0,2; 0,8 · L · n + 0,4), que el shader recorta a 1 salvo de espaldas a la luz.
constexpr float PreviewColor = 4.8f;
// 0x63e460: el desplazamiento de la cámara, sus ángulos y el fov.
constexpr glm::vec3 CameraOffset{10.0f, -500.0f, 48.0f};
constexpr glm::vec3 CameraAngles{-90.0f, 0.0f, 0.0f};
constexpr float PreviewFieldOfView = 10.0f;
// El viewport: 410 × 335 de 640 × 480 desde la esquina de la caja.
constexpr int PreviewWidth = 410;
constexpr int PreviewHeight = 335;

// Lo de 0x63e460 por clase: los ángulos (+0x1c), la escala (+0xc) y el corrimiento que suma a la
// posición (0x6b8bf0 en x con la clase 2 y 0x6b8bf8 en z con la 3).
struct ClassPose
{
    glm::vec3 angles;
    float scale;
    glm::vec3 offset;
};

constexpr std::array<ClassPose, NewFaceCount> ClassPoses{{
    {{0.0f, 0.0f, -40.0f}, 5.9f, {0.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, -12.0f}, 6.05f, {0.0f, 0.0f, 0.0f}},
    {{8.0f, 0.0f, 5.0f}, 9.1f, {4.8f, 0.0f, 0.0f}},
    {{8.0f, 0.0f, -13.0f}, 6.0f, {0.0f, 0.0f, 1.8f}},
    {{8.0f, 0.0f, -18.0f}, 6.0f, {0.0f, 0.0f, 0.0f}},
}};

std::uint16_t NewFaceOf(std::uint8_t characterClass)
{
    return static_cast<std::uint16_t>(FirstNewFaceModel + characterClass);
}
} // namespace

std::array<Data::Models::ModelFile, 1 + NewFaceCount> SelectionModelFiles()
{
    std::array<Data::Models::ModelFile, 1 + NewFaceCount> files{};
    files[0] = Data::Models::ModelFile{SelectionLogoModel, "Logo\\", "Logo", 5};
    for (std::size_t k = 0; k < NewFaceCount; ++k)
    {
        files[1 + k] = Data::Models::ModelFile{static_cast<std::uint16_t>(FirstNewFaceModel + k), "Logo\\", "NewFace",
                                               static_cast<int>(k + 1)};
    }
    return files;
}

Core::Expected<void, std::string> SelectionModels::Load(const std::filesystem::path& dataRoot,
                                                        Render::ITextureStore& textures, Core::Log& log,
                                                        const Render::Models::ModelTextureSlots* gameSlots)
{
    Release();
    m_models.clear();
    m_skeletons.clear();
    m_meshTextures.clear();
    // Las texturas apuntan a lo que piden los slots: se van antes que ellos.
    m_textures.reset();
    m_slots.emplace(gameSlots != nullptr ? gameSlots->ContinuedAt(SelectionModelsFirstSlot)
                                         : Render::Models::ModelTextureSlots(SelectionModelsFirstSlot));
    for (const Data::Models::ModelFile& file : SelectionModelFiles())
    {
        auto model = Data::Models::LoadModelFile(dataRoot, file);
        if (!model.HasValue())
        {
            log.Warning("el modelo {} de la selección no se puede leer y no se dibuja: {}", file.model,
                        model.Error().path);
            continue;
        }
        auto skeleton = Render::Models::MakeSkeleton(model.Value());
        if (!skeleton.HasValue())
        {
            log.Warning("el modelo {} de la selección no tiene un esqueleto que se pueda posar", file.model);
            continue;
        }
        Render::Models::Skeleton posed = std::move(skeleton).Value();
        if (file.model != SelectionLogoModel)
        {
            for (std::size_t action = 0; action < posed.actions.size() && action <= GreetingAction; ++action)
            {
                posed.actions[action].speed = NewFaceActionSpeed;
            }
        }
        m_meshTextures[file.model] = m_slots->Assign(model.Value(), LogoFolder,
                                                     file.model == SelectionLogoModel ? LogoSampling : NewFaceSampling);
        m_models.emplace(file.model, std::move(model).Value());
        m_skeletons.emplace(file.model, std::move(posed));
    }
    m_textures.emplace(textures, m_slots->ToLoad());
    return m_textures->Load(dataRoot);
}

void SelectionModels::Release()
{
    if (m_textures)
    {
        m_textures->Release();
    }
}

const Data::Models::Model* SelectionModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found == m_models.end() ? nullptr : &found->second;
}

const Render::Models::Skeleton* SelectionModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_skeletons.find(model);
    return found == m_skeletons.end() ? nullptr : &found->second;
}

std::span<const Render::TextureSlot> SelectionModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_meshTextures.find(model);
    return found == m_meshTextures.end() ? std::span<const Render::TextureSlot>{} : found->second;
}

const Render::Models::ModelTextureSlots* SelectionModels::TextureSlots() const
{
    return m_slots ? &*m_slots : nullptr;
}

void ChoosePreviewClass(CreatePreview& preview, std::uint8_t characterClass, const IModelLibrary& models)
{
    preview.characterClass = characterClass;
    preview.type = NewFaceOf(characterClass);
    preview.alive = true;
    preview.position = PreviewPosition;
    preview.animation = {};
    if (const Render::Models::Skeleton* skeleton = models.SkeletonOf(NewFaceOf(characterClass)))
    {
        Render::Models::SetAction(preview.animation, *skeleton, GreetingAction);
    }
}

Core::Expected<void, std::string> RebuildPreview(CreatePreview& preview, std::uint8_t characterClass,
                                                 const IModelLibrary& models,
                                                 const ActorConstructionContext& construction,
                                                 Render::Models::ClothWorld& cloth)
{
    preview.characterClass = characterClass;
    const auto built = ConstructActor(preview, NewFaceOf(characterClass), {}, construction, cloth);
    if (!built.HasValue())
    {
        return built;
    }
    preview.byte88 = 0;
    if (const auto* skeleton = models.SkeletonOf(preview.type))
    {
        Render::Models::SetAction(preview.animation, *skeleton, GreetingAction);
    }
    return {};
}

void StepPreview(CreatePreview& preview, const IModelLibrary& models)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(NewFaceOf(preview.characterClass));
    if (skeleton == nullptr || preview.animation.action >= skeleton->actions.size())
    {
        return;
    }
    const Render::Models::ActionInfo& action = skeleton->actions[preview.animation.action];
    if (Render::Models::AdvanceAnimation(preview.animation, action, action.speed * NewFaceSpeedFactor,
                                         Render::Models::AnimationScene::CharacterSelect))
    {
        Render::Models::SetAction(preview.animation, *skeleton, StandingAction);
    }
}

DrawnObject DrawnPreview(CreatePreview& preview, const IModelLibrary& models, float stepAlpha, int originalFrames)
{
    const ClassPose& pose = ClassPoses[preview.characterClass < NewFaceCount ? preview.characterClass : 1];
    preview.angles = pose.angles;
    preview.scale = pose.scale;
    preview.light = glm::vec3(1.0f);
    for (int frame = 0; frame < originalFrames; ++frame)
    {
        preview.position += pose.offset;
    }
    DrawnObject drawn;
    drawn.model = NewFaceOf(preview.characterClass);
    drawn.animation = preview.animation;
    if (const Render::Models::Skeleton* skeleton = models.SkeletonOf(drawn.model);
        skeleton != nullptr && preview.animation.action < skeleton->actions.size())
    {
        const Render::Models::ActionInfo& action = skeleton->actions[preview.animation.action];
        drawn.animation =
            Render::Models::Interpolated(preview.animation, action, action.speed * NewFaceSpeedFactor, stepAlpha);
    }
    drawn.transform.position = preview.position;
    drawn.transform.angles = preview.angles;
    drawn.transform.scale = preview.scale;
    drawn.color = glm::vec3(PreviewColor);
    drawn.vertexLight = true;
    return drawn;
}

Render::Camera3D PreviewCamera()
{
    Render::Camera3D camera = SelectionCamera();
    camera.position = PreviewPosition + CameraOffset;
    camera.angles = CameraAngles;
    camera.skipXRotation = false;
    camera.fieldOfView = PreviewFieldOfView;
    return camera;
}

Render::Camera3D PreviewCamera(const CreatePreview& preview)
{
    Render::Camera3D camera = PreviewCamera();
    camera.position = preview.position + CameraOffset;
    return camera;
}

Render::ViewportRect PreviewViewport(const UI::Rect& box, App::PixelSize screen)
{
    return Render::ViewportRect{Render::Text::DesignX(box.x, screen), Render::Text::DesignY(box.y, screen),
                                Render::Text::DesignX(PreviewWidth, screen),
                                Render::Text::DesignY(PreviewHeight, screen)};
}
} // namespace Client::Scenes
