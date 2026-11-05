#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/SceneTextures.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// La vista previa de la ventana de crear (CL6.5; 06 § La ventana de crear y "La vista previa"): los
// modelos que sube la selección (0x61ca30) y el objeto suelto 0x7437050, que 0x63e3e0 arma con el
// NewFace de la clase y 0x63e460 dibuja en su propio viewport con su propia cámara (por 0x50ba30,
// que con un modelo que no es el del personaje va a 0x602e90, sin sombra). *Estático*.
namespace Client::Scenes
{
// Logo05 en el 0xa4 y NewFace01 a 05 en 0xa8 a 0xac (0x60da30 con "Data\Logo\"); sus texturas, de
// Logo\ desde 0x60d530(0x6f8), las del 0xa4 con GL_LINEAR y las de los NewFace con GL_NEAREST, las dos
// repitiendo (0x60d5f0, con el quinto argumento en 1).
inline constexpr std::uint16_t SelectionLogoModel = 0xa4;
inline constexpr std::uint16_t FirstNewFaceModel = 0xa8;
inline constexpr std::size_t NewFaceCount = 5;
inline constexpr Render::TextureSlot SelectionModelsFirstSlot{0x6f8};

// En el orden en que los lee y les da texturas 0x61ca30.
[[nodiscard]] std::array<Data::Models::ModelFile, 1 + NewFaceCount> SelectionModelFiles();

// Los seis modelos con su esqueleto y el slot de cada malla. La velocidad de las acciones 0 y 1 de
// los NewFace es 0,3 (0x3e99999a, al final de 0x61ca30).
class SelectionModels final : public IModelLibrary
{
public:
    // Lee los seis desde `dataRoot` y les da sus texturas siguiendo a `gameSlots` (la tabla de la
    // carga, que es global en el original) desde 0x6f8, o una tabla vacía sin ella, y las sube. Uno
    // que falta se saltea con una línea en el log; una textura que no carga corta, como el original,
    // que se cierra.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot,
                                                         Render::ITextureStore& textures, Core::Log& log,
                                                         const Render::Models::ModelTextureSlots* gameSlots);
    // 0x61cc50: suelta las texturas.
    void Release();

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    // Los slots que repartió Load, si cargó.
    [[nodiscard]] const Render::Models::ModelTextureSlots* TextureSlots() const;

private:
    std::map<std::uint16_t, Data::Models::Model> m_models;
    std::map<std::uint16_t, Render::Models::Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_meshTextures;
    std::optional<Render::Models::ModelTextureSlots> m_slots;
    std::optional<SceneTextures> m_textures;
};

// El objeto de la vista previa: la clase (+0x1d4, la de la ventana) y su animación.
struct CreatePreview
{
    std::uint8_t characterClass = 1;
    Render::Models::AnimationState animation;
};

// 0x63e3e0 con la clase: 0x511290 lo arma de nuevo con el NewFace de la clase (la animación de cero)
// y 0x4ea030 le pone la acción 1.
void ChoosePreviewClass(CreatePreview& preview, std::uint8_t characterClass, const IModelLibrary& models);

// Un paso de 0x63e430: 0x4f9580 lo avanza (la velocidad de la acción por 2 con los NewFace) y, si la
// acción terminó, la 0. El original también lo avanza por paso desde la lógica de la escena
// (0x508ea0), y con la ventana abierta avanza dos veces: MuRet, una (D-C4).
void StepPreview(CreatePreview& preview, const IModelLibrary& models);

// Lo que dibuja 0x63e460 entre dos pasos (`stepAlpha` de un paso más): el NewFace en (50, 50, 0)
// (0x511290 con x = y = 0 · 100 + 50; la z del terreno, que MuRet no tiene, no se ve porque la
// cámara va con el objeto), con los ángulos, la escala y el corrimiento de su clase, con la luz por
// vértice (+0xe8, de 0x511290) y el color en 4,8 que le pone 0x6002e0.
[[nodiscard]] DrawnObject DrawnPreview(const CreatePreview& preview, const IModelLibrary& models, float stepAlpha);

// 0x6338a0(objeto, (10, −500, 48), (−90, 0, 0)) con el fov en 10 (0x63e460) y el near y el far de la
// escena 4. La posición, la del objeto de antes del corrimiento de su clase.
[[nodiscard]] Render::Camera3D PreviewCamera();

// 0x620db0(ftol(x / rx), ftol(y / ry), ftol(410 / rx), ftol(335 / ry)), con (x, y) la esquina de la
// caja de la ventana en píxeles.
[[nodiscard]] Render::ViewportRect PreviewViewport(const UI::Rect& box, App::PixelSize screen);
} // namespace Client::Scenes
