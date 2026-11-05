#pragma once

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterCloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::World
{
class FrameSprites;
class ScepterGlowCarry;
} // namespace Client::World

// El dibujo de un personaje en el ModelBatch, como 0x50ba30 en la selección: el cuerpo de
// Player.bmd posado, las partes con las matrices de esa pose (0x60cf70), lo que cuelga desde su
// hueso (0x509340, Render/Models/Attachment.h) y los pases de brillo de cada parte por su nivel
// (0x60b3d0, Render/Models/ItemGlow.h), cada malla con su textura y la mezcla de 0x4ecc30.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario" y "La selección
// de personaje"; el plan de la CL6 (CL6.3). Sin la luz por vértice (en la selección el personaje va
// con un color por malla), sin el color de 0x609640 (P-C14) y con las armas siempre en la mano: la
// zona segura que las manda a la espalda es de la CL7.
namespace Client::Scenes
{
// Los modelos que pide el dibujo: los del personaje y los de sus ítems, con su esqueleto y el slot
// de la textura de cada malla. Los carga la escena de carga (GameModels).
class IModelLibrary
{
public:
    virtual ~IModelLibrary() = default;
    [[nodiscard]] virtual const Data::Models::Model* Find(std::uint16_t model) const = 0;
    // El de Player.bmd para el 0x20a, con sus velocidades y su cabeza (MakePlayerSkeleton).
    [[nodiscard]] virtual const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const = 0;
    // Uno por malla, en su orden; vacío si no se cargaron sus texturas.
    [[nodiscard]] virtual std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const = 0;
    // El tamaño de la textura de `slot` (+0x20 y +0x24 de su lugar en la tabla, 0x7d3a1b8), si esta
    // biblioteca la subió: el de los sprites (0x549290).
    [[nodiscard]] virtual std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const
    {
        (void)slot;
        return std::nullopt;
    }
};

struct DrawnCharacter
{
    GameLogic::CharacterAppearance appearance;
    Render::Models::AnimationState body;
    Render::Models::ObjectTransform object;
    glm::vec2 headAngles{};
    // La mascota y las alas avanzan por tiempo (Render/Models/Animation.h).
    Render::Models::AnimationState pet;
    Render::Models::AnimationState wings;
    // La luz del personaje, canal por canal: la base de 0x50ba30 más +0xf4 (0,4 y 1,4 el elegido en la
    // selección; en el login, la del terreno en su lugar más 0,3).
    glm::vec3 light{0.4f, 0.4f, 0.4f};
    // El que está bajo el mouse (DAT_006c8e30 en 0x510d00): sus partes llevan el contorno.
    bool outlined = false;
    // La tela de la clase 3 o 4 (0x50d58b..0x50db57): la de este personaje, que vive entre frames, lo
    // global que lee (el viento, el contador y el rand()) y el mapa de la escena. Sin las dos, va sin
    // tela.
    CharacterCloth* cloth = nullptr;
    Render::Models::ClothWorld* clothWorld = nullptr;
    std::uint8_t map = 0;
    // La tabla de los sprites del frame (0x5491e0, World/Sprites.h): los 30 destellos de la tela, al
    // dibujarla, y los cinco del cetro (0x509340, caso 0x620, World/ScepterGlow.h) con el rand() de
    // `clothWorld`, en ese orden. Sin ella, no se piden.
    World::FrameSprites* sprites = nullptr;
    // El sorteo de los cinco del cetro entre frames de MuRet (World::ScepterGlowCarry): con él, una vez por
    // frame del original; sin él, en cada DrawCharacter.
    World::ScepterGlowCarry* scepter = nullptr;
    // Cómo cuentan los frames del original la tela y el cetro (Render::Models::OriginalFrameCarry): por el
    // reloj del frame, o uno por dibujo en el modo fiel (SceneContext::pacing).
    Render::Models::OriginalFramePace pace = Render::Models::OriginalFramePace::ByTheClock;
};

// Los slots de las texturas de los pases de brillo (06): 0x5fc el 0x8, 0x5c0 el 0x4 y 0x620 los
// 0x200, 0x800 y 0x1000.
inline constexpr Render::TextureSlot ShinyTextureSlot{0x5fc};
inline constexpr Render::TextureSlot ChromeTextureSlot{0x5c0};
inline constexpr Render::TextureSlot MetalTextureSlot{0x620};

// La piel de una clase, `modelo + 0x98` (0x50d41c..0x50d431): 2 · (clase & 7) + (clase >> 3).
[[nodiscard]] constexpr std::uint8_t SkinIndexOf(std::uint8_t characterClass)
{
    return static_cast<std::uint8_t>(2 * (characterClass & 7) + (characterClass >> 3));
}

// El slot que usa una malla: la de la piel (0x12e) va a 0x12e + la piel, la del pelo (0x137) a
// 0x12f + la piel, y la oculta (0x12d) no se dibuja (06 § El modelo en el binario).
[[nodiscard]] std::optional<Render::TextureSlot> MeshSlotOf(Render::TextureSlot assigned, std::uint8_t characterClass);

// 0x50ba30: el cuerpo posado con `body` y la cabeza; la sombra; las cinco partes de las botas al casco
// y la cabeza; la espalda, las alas y la mascota que se dibuja (Q-C98); y las manos. Cada modelo va
// pase por pase (RenderBody): cada malla con su textura, opaca o, con una OZT (de cuatro
// componentes), con la mezcla normal, y el color de la luz por el factor de su nivel; después, sus
// pases de brillo, aditivos. Con `outlined`, cada una de las seis partes lleva antes el contorno de
// 0x60cf70; lo que cuelga, no. Un modelo que la biblioteca no tiene no se dibuja.
// Lo que el objeto le pasa a RenderBody (Render/Models/ObjectState.h): en las partes, el del personaje,
// con la luz que escriben la clase 4 en el casco y 0x60b3d0 por el modelo; en lo que cuelga, el que
// 0x509340 le arma con 0x607b60. El script de la textura de cada malla (0x4ef0c0 y 0x4ecc30): con H no
// se dibuja; con R pasa su índice como el blendMesh, y la malla cuya textura es el blendMesh (todas,
// con −2) va aditiva con su textura y por la luz; con N, o si 0x60ff50 la marca, no brilla; S y el
// blendMesh corren las UV con la U y la V del objeto. La clase 4 lleva sus cascos con máscara con el
// MaskHelmMale de cada uno. Los casos por modelo de 0x60b3d0 que alcanza un personaje: la malla que
// algunos ítems ocultan en sus pases, el cromo de Spear10 antes y el set que se apaga malla a malla.
// Las saetas de la izquierda o las flechas de la derecha van a la espalda, del hueso de las alas.
void DrawCharacter(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnCharacter& character,
                   Core::Duration frameClock);

// Un objeto del mundo (0x606660): el modelo, su animación (+0x111 a +0x118), dónde va, lo que le pasa
// a RenderBody y el color del modelo (+0x48..+0x50, el que le pone 0x5ffce0 antes de posar).
struct DrawnObject
{
    std::uint16_t model = 0;
    Render::Models::AnimationState animation;
    Render::Models::ObjectTransform transform;
    Render::Models::ObjectState state;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    // +0x174, el alpha que se le pasa a RenderBody.
    float alpha = 1.0f;
    // +0xe8: la luz por vértice, que 0x5ffce0 copia a +0x44 del modelo.
    bool vertexLight = false;
};

// 0x602e90 con un objeto sin casos por tipo: 0x5ffdf0 no lo dibuja con el alpha por debajo de 0,01, y
// si no lo posa con su escala, sus ángulos y su posición, sin cabeza; 0x6002e0 llama a
// RenderBody(2): el pase base de cada malla, sin brillo y sin contorno, con el script de su textura y
// el blendMesh, la luz, la U, la V y la malla oculta del objeto. Con el alpha por debajo de 0,99 cada
// malla va con la mezcla normal y ese alpha (0x4ecc30); con la luz por vértice, cada esquina va con
// el color por la luz de su normal (Render::Models::NormalLight), salvo la del blendMesh. Un modelo
// que la biblioteca no tiene no se dibuja.
void DrawObject(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                Core::Duration frameClock);

// El punto `offset` del hueso `bone` de `object` posado como lo posa DrawObject, en el mundo (por la escala
// y más la posición del objeto, como los vértices). Es lo que lee del buffer global de la pose
// (0x68b181c) 0x4ec110 sin trasladar: 0x4eb860 lo deja en el mundo con su octavo argumento en 1. Sin el
// modelo, con menos huesos o con ese hueso dummy (0x4eb973 no lo escribe), nullopt.
[[nodiscard]] std::optional<glm::vec3> PosedBonePoint(const IModelLibrary& models, const DrawnObject& object,
                                                      std::size_t bone, const glm::vec3& offset);

// Un modelo que un dibujo posó en el buffer global de la pose (sin buffer propio, +0x11c en 0), con la
// biblioteca de la que sale.
struct PosedInTheBuffer
{
    const IModelLibrary* models = nullptr;
    DrawnObject object;
};

// El punto `offset` del hueso `bone` como lo dejó en el buffer global un dibujo que posó `posed`, en ese
// orden: el del último que tiene ese hueso sin ser dummy (los demás no lo escriben). Si ninguno, nullopt.
[[nodiscard]] std::optional<glm::vec3> LastPosedBonePoint(std::span<const PosedInTheBuffer> posed, std::size_t bone,
                                                          const glm::vec3& offset);

// 0x602e90 con el caso del remolino 90 del mapa 55 en 0x6002e0: 0x620b10 y 0x4ecc30(0, 0x80, 1, blendMesh,
// 1, U, V, −1), la malla 0 sola, en blanco y sin luz por vértice, con ModelBlend::Subtractive y la U y
// la V del objeto. Sin el blendMesh (antes de su primer paso), 0x4ecc30 no ata textura ni cambia la
// mezcla (Q-C135): no la dibuja. Un modelo que la biblioteca no tiene no se dibuja.
void DrawSubtractiveMesh(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                         Core::Duration frameClock);

// 0x602e90 con el caso del caballo oscuro (0xbf) de 0x6002e0: el pase base de DrawObject en blanco
// (pisa el color de 0x5ffce0); después el 0x44 (cromo aditivo, Render::Models::ShinePass::Chrome) de
// las mallas 12 a 15 que existen (en DarkHorse.bmd, la 12) en (1; 0,8; 0,3), por el alpha si es menor
// que 0,99; y la sombra de la malla 8 (0x4eff40 entre 8 y 9) en (0, 0, 0, 0,7), mezclada y aplastada
// sobre el piso desde su lugar. El original no dibuja la sombra en los mapas 10, 24 a 29 y 36, y la
// pone en 0,2 en el 7, que MuRet todavía no tiene. Las normales llevan la escala del objeto, para la
// luz y para las UV del cromo.
void DrawDarkHorse(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& horse,
                   Core::Duration frameClock);

// 0x602e90 con el caso del cuervo (0xc0) de 0x6002e0, con el nivel de la mascota en 0 (el A9 que lo
// trae no llega al login): glColor3f(1, 1, 1) y el pase base de la malla 3 (dksbody.tga) y de la 2
// (dkonebody.jpg, la del nivel por debajo de 20), con el color y la luz por vértice de 0x5ffce0; después
// la sombra de las cinco mallas (0x4eff40(−1, −1, −1, −1), también las que no se dibujan, Q-C132) en
// (0, 0, 0, 1), mezclada y aplastada sobre `groundHeight`, la altura del terreno bajo el cuervo
// (0x600a0b). El original no la dibuja en los mapas 10, 24 a 29 y 36, y la pone en 0,2 en el 7, que
// MuRet todavía no tiene.
void DrawDarkSpirit(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& spirit,
                    float groundHeight, Core::Duration frameClock);

// Los huesos de `object` posados como los posa DrawObject, en el mundo (la rotación por la escala; la
// traslación por la escala más la posición, en float, como PosedBonePoint): lo que deja 0x4eb860 con su
// octavo argumento en 1 en un buffer propio. Sin el modelo, vacío.
[[nodiscard]] std::vector<Render::Models::OriginalMatrix> PosedWorldBones(const IModelLibrary& models,
                                                                          const DrawnObject& object);
} // namespace Client::Scenes
