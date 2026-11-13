#pragma once

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ClothCarry.h"
#include "Scenes/MonsterCloth.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Las telas de un personaje: el bloque 0x50d58b..0x50db57 de 0x50ba30, que las arma, las simula y las
// dibuja en cada frame dibujado, después de las partes del cuerpo y antes de lo que cuelga (06 § La
// tela de la clase 4). Las del Dark Lord son tres, la capa y sus dos telas de adelante, y una cuarta
// con su armadura de la clase; la de la clase 3, una.
namespace Client::Scenes
{
// Lo que el bloque lee del personaje.
struct ClothWearer
{
    // +0x1d4: la clase en los bits 0 a 2.
    std::uint8_t characterClass = 0;
    // +0x238: la armadura. Con ArmorClass05 (0x221e), el Dark Lord lleva la cuarta tela.
    std::uint16_t armor = 0;
    // +0x2f8: las alas. Sin la capa 0x1c30, la tercera tela del Dark Lord no va.
    std::optional<std::uint16_t> wings;
    // +0x376: el número de monstruo (−1 en un jugador); el 0x37 tiene su tela.
    std::optional<std::uint16_t> monster;
    // objeto + 0x78: con 0x4000 no se simula ni se dibuja; con 0x1000, el dt es 0.
    std::uint32_t objectFlags = 0;
};

// bVar23 para un jugador (0x50c929..): la clase 3 o 4 con el modelo 0x20a y +0x3e3 en 0, salvo en
// los mapas 0x12 a 0x17. Lo de los duelos y las guerras de guild, que la prende en cualquier clase,
// no está en MuRet.
[[nodiscard]] bool WearsACloth(std::uint8_t characterClass, std::uint8_t map);

// La otra mitad de bVar23 (0x50c951..0x50c96e), con cualquier tipo: el +0x376 0x37, salvo en los mapas 0x12 a 0x17.
// Arma la tela de ClothMonster, o las del Dark Lord si +0x1d4 dice la clase 4 (0x50d5a7 no mira el tipo).
[[nodiscard]] bool WearsTheMonsterCloth(std::uint16_t monster, std::uint8_t map);

// 0x50c177 y 0x50c59f: los +0x376 que arman en objeto + 0x190 una tela de la grilla propia (0x6a0210 con 0x4548b0, la
// vtable 0x6b8cd8, y su +4, 0x454a50), con cualquier tipo y en cualquier mapa: el 0x4d (0x50c2ca: el hueso 10, (0, −10,
// 0), 5 · 12, 15 · 240, la textura 0x629 y 0x1100, con 0x455cd0 en (0, 0, 40) de radio 30 en el hueso 10) y el 0x47
// (la textura 0x253), el 0x4a y el 0x12d (la 0x255) (0x50c5fa: el hueso 0x13, (0, 10, 0), 5 · 15, 30 · 300 y 0x1100,
// sin esferas).
[[nodiscard]] bool WearsAGridClothOfItsOwn(std::uint16_t monster);

// El color de 0x50ba30 sin duelo ni guerra: blanco.
inline constexpr glm::vec3 PlainClothColor{1.0f, 1.0f, 1.0f};

// Lo más que corre Advance de una vez: 3 s del original.
inline constexpr int MaxClothFramesPerAdvance = Render::Models::MaxOriginalFramesPerDraw;

class CharacterCloth
{
public:
    // Un frame del bloque: si no hay telas (objeto + 0x190 en 0) las arma desde la pose de ahora; con
    // el arreglo armado, la cuarta del Dark Lord se arma encima o se suelta según la armadura; y cada
    // tela da sus cinco pasos de 0,005 y se dibuja en `batch` antes de pasar a la siguiente. Si un paso
    // la rompe, las suelta todas (0x4fb1a0) y el frame siguiente las vuelve a armar. `wears` es
    // bVar23 y `color` el de 0x50ba30.
    void Frame(Render::ModelBatch& batch, const ClothWearer& wearer, const Render::Models::ClothBody& body,
               Render::Models::ClothWorld& world, std::uint8_t map, float worldTime, bool wears,
               const glm::vec3& color = PlainClothColor);

    // Lo que corre en un frame de MuRet (DrawCharacter). Q-C124 corregido como Q-C20: el bloque corre una
    // vez por cada frame del original (Render::Models::OriginalFrameTime, 46,875 ms) que pasó desde el
    // frame anterior, el último con su dibujo, y sin ninguno se dibuja el estado de ahora; el primero
    // corre uno. Así la tela flamea como en el original a cualquier FPS. En el modo fiel (`pace`
    // OncePerDraw), uno por dibujo, como el original (Render::Models::OriginalFrameCarry). Un salto de más
    // de MaxClothFramesPerAdvance frames corre esos y descarta el resto. `clock` es también el WorldTime
    // del brillo. Sin ninguno, el dibujo es el de DrawCurrent con `body`.
    void Advance(Render::ModelBatch& batch, const ClothWearer& wearer, const Render::Models::ClothBody& body,
                 Render::Models::ClothWorld& world, std::uint8_t map, Core::Duration clock, bool wears,
                 const glm::vec3& color = PlainClothColor,
                 Render::Models::OriginalFramePace pace = Render::Models::OriginalFramePace::ByTheClock);

    // Un dibujo entre frames del original: solo las telas que ya existen y sus destellos, sin
    // armarlas, simularlas ni avanzar su reloj. También vale antes del primer Advance. Cada tela va
    // llevada con su hueso del cuerpo de su última simulación a `body`, el de ahora (ClothCarry, #307):
    // con el personaje que camina, la capa va con él en vez de quedarse atrás hasta el frame siguiente.
    // Con el cuerpo quieto, se dibuja como quedó.
    void DrawCurrent(Render::ModelBatch& batch, const ClothWearer& wearer, const Render::Models::ClothBody& body,
                     Core::Duration clock, bool wears, const glm::vec3& color = PlainClothColor);

    // Un frame de la tela del +0x376 `monster` (WearsAGridClothOfItsOwn, 0x50c283..0x50c35c y 0x50c59f..0x50c669): con
    // +0x190 en 0 la arma sobre `body` (una, +0x18c en 1); después la primera da 0x455190(0,005f, 5), sin mirar +0x78;
    // si se rompe, 0x4fb1a0 las suelta y el frame siguiente la vuelve a armar; si no, su +0x18 (0x455930) la dibuja en
    // `batch` con el color nulo, (1, 1, 1). Un arreglo ya armado sigue con la forma que tenga, como el original.
    void FrameTheMonsterCloth(Render::ModelBatch& batch, std::uint16_t monster, const Render::Models::ClothBody& body,
                              Render::Models::ClothWorld& world, std::uint8_t map, float worldTime);
    // Entre frames del original: la primera tela como quedó, sin simularla, llevada a `body` (#307). Sin tela, nada.
    void DrawTheMonsterCloth(Render::ModelBatch& batch, const Render::Models::ClothBody& body, float worldTime);

    // Advance vuelve a contar desde cero, como en el primer frame: el próximo corre un frame del bloque y no
    // pone al día el tiempo que pasó sin dibujo. Las telas quedan como estaban. Es para un frame retenido
    // (Q-C85): el original no dibuja mientras conecta, y en el frame siguiente corre uno.
    void RestartTheClock()
    {
        m_frames = {};
    }

    // objeto + 0x190: las telas armadas (cuatro con la clase 4, una con las demás), o ninguna.
    [[nodiscard]] std::span<const Render::Models::Cloth> Cloths() const
    {
        return m_cloths;
    }
    // objeto + 0x18c: cuántas recorre el lazo.
    [[nodiscard]] std::uint8_t Count() const
    {
        return m_count;
    }
    // objeto + 0x190 de un monstruo con la tela sobre la malla (la vtable 0x6b8d04, 0x50bae7): la que arma y
    // simula DrawGameMonster. Con el mismo +0x376 no lleva la de arriba.
    [[nodiscard]] MonsterMeshCloth& OnTheMesh()
    {
        return m_onTheMesh;
    }

    // Los sprites del brillo del último frame (Effect\flare01.jpg, 0x5491e0). DrawCharacter los pide en
    // la tabla del frame (DrawnCharacter::sprites), que dibuja 0x549440 → 0x549290.
    [[nodiscard]] std::span<const Render::Models::ClothFlare> Flares() const
    {
        return m_flares;
    }

private:
    // El bloque, con su dibujo en `batch`, o sin dibujar con nullptr.
    void RunTheBlock(Render::ModelBatch* batch, const ClothWearer& wearer, const Render::Models::ClothBody& body,
                     Render::Models::ClothWorld& world, std::uint8_t map, float worldTime, bool wears,
                     const glm::vec3& color);
    // Las telas como quedaron, con las reglas del lazo, sin simularlas, llevadas a `body`.
    void DrawTheCloths(Render::ModelBatch& batch, const ClothWearer& wearer, const Render::Models::ClothBody& body,
                       float worldTime, bool wears, const glm::vec3& color);
    void Build(const ClothWearer& wearer, const Render::Models::ClothBody& body);
    // Una tela y sus destellos, con sus partículas llevadas por `carry` si hay uno.
    void Draw(Render::ModelBatch& batch, const Render::Models::Cloth& cloth, const glm::vec3& color, float worldTime,
              const std::optional<ClothCarry>& carry = std::nullopt);

    std::vector<Render::Models::Cloth> m_cloths;
    // El cuerpo de la última simulación, del que DrawTheCloths lleva cada tela.
    std::optional<SimulatedClothBody> m_simulatedBody;
    std::uint8_t m_count = 0;
    std::vector<Render::Models::ClothFlare> m_flares;
    Render::ModelBlend m_lastBlend = Render::ModelBlend::Opaque;
    Render::Models::OriginalFrameCarry m_frames;
    MonsterMeshCloth m_onTheMesh;
};
} // namespace Client::Scenes
