#pragma once

#include "Core/IClock.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/Models/Shine.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace Client::World
{
class FrameSprites;
class ScepterGlowCarry;
struct ObjectModelStepState;
struct FishEffectRequest;
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
    // Consulta raw32 de la misma tabla: ausencia explícita, sin truncar al slot16 de los modelos.
    [[nodiscard]] virtual std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const
    {
        (void)selector;
        return std::nullopt;
    }
    // Handle físico publicado por una carga exitosa del owner. La ausencia no autoriza truncar
    // el selector raw32 ni confundirlo con un nombre de GL. Las bibliotecas legacy no lo ofrecen.
    [[nodiscard]] virtual std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const
    {
        (void)selector;
        return std::nullopt;
    }
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
    // 0x50db94 repone el RGB antes de las armas, las alas y la mascota: el héroe no conserva ahí
    // el color de los bits del registro propio que usó para sus partes.
    std::optional<glm::vec3> hangingLight{};
    // +0x174 y las guardas de la sombra de 0x50bc05..0x50bc71.
    float alpha = 1.0f;
    bool castsShadow = true;
    bool safeZone = false;
    // +0x78: la tela no se simula ni se dibuja con 0x4000; con 0x1000 el dt es 0.
    std::uint32_t objectFlags = 0;
    // En el juego, los rand que pide el caso de 0x607b60 pertenecen al flujo global del cliente.
    Render::Models::CrtRandom* originalRandom = nullptr;
    // El que está bajo el mouse (DAT_006c8e30 en 0x510d00): sus partes llevan el contorno.
    bool outlined = false;
    // +0x88 del objeto: con 4 (un NPC), el contorno de 0x60cf70 va con otros colores; con 1 (un jugador), en
    // Chaos Castle lleva el Angel (DrawChaosCastleBody).
    std::uint8_t kind = 0;
    // La tela de la clase 3 o 4 (0x50d58b..0x50db57): la de este personaje, que vive entre frames, lo
    // global que lee (el viento, el contador y el rand()) y el mapa de la escena. Sin las dos, va sin
    // tela.
    CharacterCloth* cloth = nullptr;
    Render::Models::ClothWorld* clothWorld = nullptr;
    // +0x376: el número de monstruo (0xffff en un jugador). Con el 0x37, la tela de ese monstruo (0x50c951,
    // WearsTheMonsterCloth).
    std::optional<std::uint16_t> monster;
    // false entre frames del original: dibuja la tela actual sin crearla ni simularla.
    bool advanceCloth = true;
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
    // Publica matrices ya calculadas: el cuerpo con la cabeza (también el de Chaos Castle) y lo que cuelga, con su
    // raíz real. El cuerpo va al buffer propio del personaje (+0x120, 0x511bf7), no al global: quien lo recibe
    // decide dónde lo guarda.
    std::function<void(std::uint16_t, std::span<const Render::Models::BoneTransform>,
                       const Render::Models::ObjectTransform&, bool)>
        onPose{};
    std::function<std::span<const Render::Models::BoneTransform>(std::uint16_t)> poseBones{};
};

// 0x50db57 separa las partes y la tela de lo que cuelga con un rand y una nueva luz. Ambos
// caminos reutilizan el mismo render de modelos; All conserva el contrato del login y la selección.
enum class CharacterDrawPass : std::uint8_t
{
    All,
    Body,
    Hanging,
    Shadow,
    Parts,
    Cloth,
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
                   Core::Duration frameClock, CharacterDrawPass pass = CharacterDrawPass::All);

// 0x50d374..0x50d3d7: en Chaos Castle (18 a 23), un jugador (+0x88 en 1) no lleva sus seis partes sino el Angel
// (0x1f7, Player\Angel), sobre el cuerpo posado, con 0x60cf70: el contorno de esos mapas si está marcado, y
// 0x60b3d0 con el nivel 0, que le pone la luz (+0x3b4) y llama a 0x609640 (caso 0x1f7) con `flags`: 2, o 6 el
// héroe, con el cromo de la malla 0. Al 0x1f7 nadie le escribe la piel (+0x98 del modelo: sólo el lazo de las
// partes la pone en las suyas, y 0x60dbe0 la deja en 0), así que su malla skin_ va con la piel 0. Sin el modelo
// no dibuja nada, como una parte.
// `models` posa el cuerpo (0x20a) y `angelModels` tiene el 0x1f7, con las texturas de su carga.
inline constexpr std::uint16_t ChaosCastleBodyModel = 0x1f7;
[[nodiscard]] Core::Expected<void, std::string>
DrawChaosCastleBody(Render::ModelBatch& batch, const IModelLibrary& models, const IModelLibrary& angelModels,
                    const DrawnCharacter& character, Core::Duration frameClock, std::uint32_t flags);

// Los huesos del cuerpo de `character` como los posa DrawCharacter (0x4eb860 con la cabeza): el buffer
// propio del personaje, +0x120. Sin el esqueleto del 0x20a o si la pose falla, vacío.
[[nodiscard]] std::vector<Render::Models::BoneTransform> PoseCharacterBody(const IModelLibrary& models,
                                                                           const DrawnCharacter& character);

// Cuántos rand() saca 0x607b60 al armar el objeto de lo que cuelga con el tipo `model`: uno con Sword15
// (0x220) y Staff08 (0xc19), dos con Staff06 (0xc17) y Shield15 (0xe20), y ninguno con los demás.
[[nodiscard]] std::size_t HangingRandomCount(std::uint16_t model);

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
    // model+88: selección compartida publicada por el paso de objetos, sin luz y con UV corridas.
    std::optional<std::int16_t> unlitMesh{};
    // Scratch publicado por la pose del caller; los renderers especializados tampoco vuelven a posar.
    std::span<const Render::Models::SkinnedMesh> retainedMeshes{};
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

// RenderBody(2, …, `texture`) (0x4ef0c0) con un objeto del mundo: el pase base de DrawObject con `texture`
// forzada en cada malla, que es de tres componentes (como Chrome01): opaca, o con la mezcla normal y el
// alpha por debajo de 0,99, o aditiva y por la luz en la rama del blendMesh.
void DrawObjectWithTexture(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                           Core::Duration frameClock, Render::TextureSlot texture);

// RenderBody sobre el scratch ya transformado de 4ebc80: conserva los dummy y los vértices
// retenidos cuando otra familia no vuelve a posar. shine elige un pase adicional de 60b130.
void DrawObjectFromMeshes(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                          Core::Duration frameClock, std::span<const Render::Models::SkinnedMesh> meshes,
                          std::optional<Render::Models::ShinePass> shine = std::nullopt);

// 0x4ebc80 sobre `bones`, los huesos que posó 0x5ffdf0 en el buffer propio (+0x120): una por malla del modelo de
// `object`, en su orden, con su escala y su posición; también las que no tienen textura, que 0x4eff40 no mira. Una
// malla con índices fuera de rango queda vacía. Sin el modelo, ninguna. `boneScale` es el 0x6c881c que deja 0x5ffdf0
// por su modo: 1, o 1,2 con el 2 y 1,4 con el 3 (0x5fff82..0x5fffaf, Render::Models::SkinMeshAroundBones).
[[nodiscard]] std::vector<Render::Models::SkinnedMesh>
SkinObjectMeshes(const IModelLibrary& models, const DrawnObject& object,
                 std::span<const Render::Models::BoneTransform> bones, float boneScale = 1.0f);

// Un pase de 0x60cd60: la escala que 0x4ebc80 lee de 0x6c881c y el color del modelo (+0x48..+0x50).
struct ObjectOutlinePass
{
    float boneScale = 1.0f;
    Render::Color color;
};

// 0x60cd60(modelo, objeto, 0x40, …, escala) una vez por pase de `passes`, con `object` posado en `bones` (el
// contorno de 0x5ffdf0 en un monstruo o un NPC): cada malla pasada al mundo alrededor de sus huesos a la escala del
// pase y RenderBody(0x40) sin luz, sin textura, aditiva y sin escribir la profundidad, salvo la del blendMesh, con
// su textura y por su luz (como el contorno de las partes de DrawCharacter); sin la malla oculta. El caller mira
// antes el +0x78 & 0x4000 (0x60cd74) y el caso del 0xc5 (0x60cdd9).
void DrawObjectOutline(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                       Core::Duration frameClock, std::span<const Render::Models::BoneTransform> bones,
                       std::span<const ObjectOutlinePass> passes);

// El contorno del 0xc5 (0x60cdd9..0x60ce55): como DrawObjectOutline, pero solo la malla 0 con 0x4ecc30 (0x60ce4e), sin
// el script de su textura (oculta, o su propio blendMesh, que mira 0x4ef0c0) y sin la malla oculta: el +0x58 va de
// textura, que el contorno no usa salvo en la rama del blendMesh. El caller corta con +0x58 distinto de −1.
void DrawObjectOutlineOfTheFirstMesh(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                                     Core::Duration frameClock, std::span<const Render::Models::BoneTransform> bones,
                                     std::span<const ObjectOutlinePass> passes);

// La sombra de 0x60b3d0 con +0x98 en 1 (0x60b41a..0x60ccee), sobre `meshes` (SkinObjectMeshes): sin sombra en los
// mapas 10, 24 a 29 y 36; en el 7, 0x620a00(1) y glColor4f(0, 0, 0, 0,2), y en los demás 0x620980 y glColor3f(0, 0,
// 0). 0x4eff40(+0x64, +0x58, −1, −1): sin la malla oculta, las que no tienen triángulos y las que tienen la textura
// del blendMesh (+2 de la malla), cada vértice aplastado desde la posición del objeto (model+0x6c..+0x74, que copió
// 0x60cf70) con el abanico del mapa. El caller mira +0x1bc, +0x78 & 0x4000 y +0x92 (0x60b46f..0x60b49f).
void DrawObjectShadow(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                      std::span<const Render::Models::SkinnedMesh> meshes, std::int32_t map);

struct ItemSpecialDrawContext;
// Globales de60b3d0 prestados por el dueño del frame y su scratch proyectado.
struct ItemDrawContext
{
    const ItemSpecialDrawContext* special = nullptr;
    float glowPulse = 0;
    std::map<std::uint16_t, std::int32_t>* selections = nullptr;
    bool originalFrame = true;
    World::ObjectModelStepState* modelState = nullptr;
    std::int32_t map = 0;
    void* ownerContext = nullptr;
    std::function<std::span<const Render::Models::BoneTransform>()> poseBones{};
    std::function<Core::Expected<void, std::string>(const World::FishEffectRequest&)> createJoint{};
    std::function<Core::Expected<void, std::string>(std::uint16_t, std::int32_t, const DrawnObject&)> createEffect{};
    // Recibe sólo los stores nativos de alpha, separado del alpha temporal de dibujo.
    float* storedAlpha = nullptr;
};

// 60b3d0: cuerpo y opciones sobre los huesos/mallas que publicó el caller, sin volver a posar.
[[nodiscard]] Core::Expected<void, std::string>
DrawItemFromMeshes(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                   Core::Duration frameClock, std::span<const Render::Models::SkinnedMesh> meshes, std::uint8_t level,
                   std::uint8_t excellent, std::uint8_t ancient, std::uint8_t detail = 4,
                   const ItemDrawContext& context = {});

// 4ecc30/4ef0c0: mesh<0 recorre RenderBody; mesh>=0 pide esa malla directamente.
// Los pases especiales prestan las mismas mallas y no calculan otra pose.
// storedColor recibe sólo los FSTP de RGB, también antes de un error posterior.
// El bit0x20 proyecta al piso con spread2000 (4ed6f6, Kalima); no acredita el spread2500 del asedio.
// El 0x400 sin el 0x20 es la onda de 4ed775: cada esquina corrida por su normal girada en
// sin((931 · vértice + ftol(T)) · 0,007f) · 28, con T el reloj del mundo (`worldClock`, 0x5767b88); sin él, corta.
[[nodiscard]] Core::Expected<void, std::string>
DrawItemMeshPass(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                 Core::Duration frameClock, std::span<const Render::Models::SkinnedMesh> meshes, std::int16_t mesh,
                 std::uint32_t flags, std::optional<Render::TextureSlot> texture = std::nullopt,
                 glm::vec3* storedColor = nullptr, std::optional<float> worldClock = std::nullopt);

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

// 0x602e90 con el caso del 0x69 de Lorencia en 0x6002e0 (0x600a4a..0x600ae6): entre 0x4ecc10 y 0x4ecc20
// (glPushMatrix y glPopMatrix), cuatro 0x4ecc30(malla, 2, alpha, blendMesh, luz, U, V, −1) sin RenderBody:
// las mallas 0 a 2 con el blendMesh y la luz del objeto y U = V = 0, y la 3 con el blendMesh 3, la luz y
// la U y la V del objeto. Cada una, el pase base de DrawObject, sin la malla oculta (+0x58) ni los scripts
// R y H, que mira RenderBody y no 0x4ecc30; Waterspout01 no tiene ninguno. Sin el glColor de RenderBody:
// con la luz por vértice, cada esquina lleva el suyo. Un modelo que la biblioteca no tiene no se dibuja.
void DrawWaterspout(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock);

// 0x602e90 con el 0x51 de Tarkan en 0x6002e0 (0x600c81..0x600cdc): entre 0x4ecc10 y 0x4ecc20, dos
// 0x4ecc30 sobre la malla 0 sin RenderBody. Primero el pase base (2) con el blendMesh, la luz, la U y la V
// del objeto, sin la malla oculta (+0x58) ni los scripts R y H, que mira RenderBody y no 0x4ecc30 (Object82
// no tiene ninguno); después el 0x44 con Chrome01 (0x5c0): las UV del cromo con la normal en el mundo,
// aditivo y con el color del modelo (el de 0x5ffce0, sin la luz por vértice), por el alpha si es menor que
// 0,99 (Q-C95). Sin el cromo si la malla tiene N. Un modelo que la biblioteca no tiene no se dibuja.
void DrawTarkanChrome(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                      Core::Duration frameClock);

// 0x425260 (desde 0x602ed0) con el 0x29 (0x4255f3) y el 0x41/0x42 (0x42588d) de Aida, entre 0x4ecc10 y
// 0x4ecc20: el model+0x88 en 0 (la malla 0 sin luz por vértice, con el color del modelo y la U y la V
// corridas) y un 0x4ecc30 sobre la malla 0 sola, con el alpha, el blendMesh, la luz y la V del objeto y la U
// (−ftol(T) % 100.000) · paso, de `worldClock` (0x5767b88). El 0x29: banderas 0x42 (aditivo), el color del
// modelo en 0,52 (0x3f051eb8, que queda en el modelo) y paso 0,00001f (0x6b8a2c); el 0x41/0x42: banderas 2,
// el color que le dejó el cuerpo y paso 0,00005f (0x6b8a28). Después el +0x88 vuelve a 0xff, que es lo que
// esos tipos tienen en Aida. Los demás tipos, o un modelo que la biblioteca no tiene, no dibujan nada. No
// saca rand() ni toca el objeto: va en cada dibujo de MuRet, no sólo en los frames del original.
[[nodiscard]] Core::Expected<void, std::string> DrawAidaMeshPass(Render::ModelBatch& batch, const IModelLibrary& models,
                                                                 const DrawnObject& object, std::int16_t type,
                                                                 float worldClock, Core::Duration frameClock);

// 0x602e90 con el caso del caballo oscuro (0xbf) de 0x6002e0: el pase base de DrawObject en blanco
// (pisa el color de 0x5ffce0); después el 0x44 (cromo aditivo, Render::Models::ShinePass::Chrome) de
// las mallas 12 a 15 que existen (en DarkHorse.bmd, la 12) en (1; 0,8; 0,3), por el alpha si es menor
// que 0,99; y, en el mundo `map` (0x6c9dd0), la sombra de la malla 8 (0x4eff40 entre 8 y 9) en (0, 0, 0,
// 0,7), mezclada y aplastada sobre el piso desde su lugar: sin sombra en los mapas 10, 24 a 29 y 36, y
// en 0,2 en el 7 (0x60073c..0x600787). Las normales llevan la escala del objeto, para la luz y para las
// UV del cromo.
void DrawDarkHorse(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& horse, std::int32_t map,
                   Core::Duration frameClock);

// 0x602e90 con el caso del cuervo (0xc0) de 0x6002e0, con el nivel de la mascota en 0 (el A9 que lo
// trae no llega al login): glColor3f(1, 1, 1) y el pase base de la malla 3 (dksbody.tga) y de la 2
// (dkonebody.jpg, la del nivel por debajo de 20), con el color y la luz por vértice de 0x5ffce0; después
// la sombra de las cinco mallas (0x4eff40(−1, −1, −1, −1), también las que no se dibujan, Q-C132) en
// (0, 0, 0, 1), mezclada y aplastada sobre `groundHeight`, la altura del terreno bajo el cuervo
// (0x600a0b). En el mapa `map` 10, 24 a 29 y 36 no hay sombra, y en el 7 va con alpha 0,2 (0x6009a9..0x6009e5).
void DrawDarkSpirit(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& spirit,
                    float groundHeight, Core::Duration frameClock, std::int32_t map);

// 0x602e90(objeto, 0, 0, 10) en 0x6002e0 (0x60033c..0x600435), con `object` como lo posó 0x5ffdf0 (que no mira el
// 10): lo que reciben el ayudante (0x445310) y el cuervo (0x420e11) con el +0x78 & 0x4000 de su jinete o de su dueño.
// El color del modelo en 1 (0x60039d) y, de la malla 0 a la última, 0x4ecc30(malla, 0x4040, 0,5, blendMesh, luz,
// U · 8, V · 2, 0x620) suelto, sin RenderBody: el pase de brillo 0x4000 (Render::Models::ShinePass::Chrome5), que
// 0x4ecc30 mira antes que el blendMesh y el 0x40, con Effect\Chrome02 (0x620) forzada, aditivo, sin la luz por
// vértice y con el alpha 0,5 en vez del del objeto. Cada malla que lo dibuja multiplica antes en el lugar el color
// del modelo por 0,5 (0x4ed3fd, Q-C95): la k-ésima que dibuja, desde 0, va en 0,5 a la k + 1. Como 0x4ecc30 suelto,
// no saltea la malla oculta (+0x58) ni las del script H; sí, sin multiplicar, las mallas sin triángulos (0x4ecc5b),
// las de la textura oculta 0x12d y las que tienen N o el +0 de 0x60ff50 (0x4ed07a). Ni sombra ni el pase de cada tipo
// (DrawDarkHorse, DrawDarkSpirit, RenderBody(2), DrawFenrirBody). Un modelo que la biblioteca no tiene no dibuja nada.
[[nodiscard]] Core::Expected<void, std::string> DrawChromeShell(Render::ModelBatch& batch, const IModelLibrary& models,
                                                                const DrawnObject& object, Core::Duration frameClock);

// 0x602e90 con los Fenrir (0x14a a 0x14c) en 0x6002e0 (0x60277b..0x60292b), entre 0x4ecc10 y 0x4ecc20: el color del
// modelo en 1 y +0x88 en 0 (sin la luz por vértice), 0x4ecc30(0, 2, …) y 0x4ecc30(1, 2, …) con el alpha, el blendMesh,
// su luz y la U y la V del objeto y, con `charging` (la acción 4 del Fenrir, +0x111), la malla 1 otra vez con 0x46
// (el cromo aditivo) y Effect\eff_lighting (0x68f). Después, salvo en los mapas 10, 24 a 29 y 36, la sombra de todas
// las mallas con triángulos (0x4eff40(−1, −1, −1, −1)) en (0, 0, 0, 0,7), o 0,2 en el 7, mezclada y aplastada desde
// su lugar con el abanico del mapa. Sin el modelo, nada; el error es el de 0x4ecc30.
[[nodiscard]] Core::Expected<void, std::string> DrawFenrirBody(Render::ModelBatch& batch, const IModelLibrary& models,
                                                               const DrawnObject& fenrir, std::int32_t map,
                                                               bool charging, Core::Duration frameClock);

// 0x600cf2..0x600daa, el 0x1c y el 0x1d de los mapas 11 a 17 después de su RenderBody: la sombra
// (0x4eff40(blendMesh, 2, −1, −1)) de las mallas con triángulos, salvo la 2 y las de la textura del blendMesh,
// en (0, 0, 0, 1) (0x620a00(1) y glColor4f), mezclada y aplastada sobre `groundHeight`, la altura del terreno
// bajo el objeto (0x600d80).
void DrawBloodCastleShadow(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                           float groundHeight, Core::Duration frameClock);

// Lo que 0x42cc10 (la cadena de 0x6002e0 en el mapa 30, después de 0x438fe0) lee del objeto y de afuera.
struct CastleSiegeBodyPass
{
    // +0x2: el tipo del objeto.
    std::int16_t type = 0;
    // +0x4: 0x606660 lo deja en 0; solo las puertas lo miran.
    std::int32_t field04 = 0;
    // 0x4294b0: el byte del asedio (0x7239ec).
    bool siegeActive = false;
    // 0x5767b88, el reloj del mundo en ms.
    float worldClock = 0.0f;
};

// 0x42cc10 con un objeto del mapa 30 que 0x6002e0 dibuja (+0x58 distinto de −2), con `object` como lo dejó
// 0x5ffce0 (su color es el del modelo). Devuelve true con los tipos que tienen su caso, que dibujan lo suyo y
// cortan la cadena (0x6002e0 no hace su RenderBody); false con los demás, que van por 0x42e3d0 (solo tipos de
// monstruo) y siguen. Los casos:
// - el 0xc: el color del modelo en 1 y dos 0x4ecc30 sueltos con las banderas 2, la malla 0 con su textura y
//   la 1 con Object31\Flag.tga (0x69c);
// - el 0x11: el color en 0,45 (0x3ee66666), el model+0x88 en 0 y RenderBody(2) con la V en
//   (ftol(T) % 10.000) · 0,0002f;
// - el 0x33: el model+0x88 en 0 y RenderBody(0x42) con la U en (−ftol(T) % 10.000) · 0,0002f;
// - el 0x42: el color en (0,1; 0,4; 0,6) y RenderBody(0x44), el cromo, con la luz en sin(T · 0,001f) · 0,2f +
//   0,3f;
// - el 0x32, el 0x53, el 0x54 y el 0x55: RenderBody(2) y, sin el asedio, la sombra negra y opaca de 0x4eff40
//   (0x620980 y glColor3f(0, 0, 0)), sin la malla oculta ni las de la textura del blendMesh, aplastada desde
//   su lugar con el spread 2500 del 30;
// - el 0x13, sin el asedio: RenderBody(2); con el color en 0,3, RenderBody(0x44) con Chrome01 (0x5c0) y el
//   blendMesh en 0; con (0,2; 0,2; 0,5), RenderBody(0x48) igual. Con el asedio corta en 0x42ce09;
// - del 0x3d al 0x40: con +0x4 en 0, RenderBody(2); si no, corta en 0x42cfcb (0x4ee770).
// Después de cada caso, el model+0x88 vuelve a 0xff, que en el 30 es lo que esos tipos ya tienen, y el color del
// modelo lo vuelve a poner 0x5ffce0. Un modelo que la biblioteca no tiene no dibuja nada.
[[nodiscard]] Core::Expected<bool, std::string>
DrawCastleSiegeBody(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                    const CastleSiegeBodyPass& pass, Core::Duration frameClock);

// Los huesos de `object` posados como los posa DrawObject, en el mundo (la rotación por la escala; la
// traslación por la escala más la posición, en float, como PosedBonePoint): lo que deja 0x4eb860 con su
// octavo argumento en 1 en un buffer propio. Sin el modelo, vacío.
[[nodiscard]] std::vector<Render::Models::OriginalMatrix> PosedWorldBones(const IModelLibrary& models,
                                                                          const DrawnObject& object);
} // namespace Client::Scenes
