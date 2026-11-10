#include "Scenes/CharacterCloth.h"

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La tela de la clase 4" (§ Las telas en
// 0x50ba30); cada frame, contra el código real del main.exe emulado (tests/vectors/cloth).
namespace Client::Scenes
{
namespace
{
using Render::TextureSlot;
using Render::Models::Cloth;
using Render::Models::ClothShape;

constexpr std::uint8_t DarkLord = 4;
constexpr std::uint8_t MagicGladiator = 3;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint16_t ArmorClass05 = 0x221e;
constexpr std::uint16_t LordsCape = 0x1c30;
constexpr std::uint16_t ClothMonster = 0x37;
constexpr std::uint32_t Hidden = 0x4000;
constexpr std::uint32_t Still = 0x1000;
// 0x3ba3d70a, el dt de 0x50ba30.
constexpr float StepTime = 0.005f;
constexpr int StepsPerFrame = 5;
// Los mapas 0x12 a 0x17, donde no hay tela.
constexpr std::uint8_t FirstClothlessMap = 0x12;
constexpr std::uint8_t LastClothlessMap = 0x17;

// Las esferas de las telas del Dark Lord: las de las capas 0x256 (hueso 17) y las de la tercera.
struct Sphere
{
    glm::vec3 local;
    float radius;
    std::int32_t bone;
};
constexpr std::array<Sphere, 2> FrontSpheres{{{{-10.0f, 20.0f, 20.0f}, 27.0f, 17}, {{10.0f, 20.0f, 20.0f}, 27.0f, 17}}};
constexpr std::array<Sphere, 4> BackSpheres{{{{-10.0f, -10.0f, -10.0f}, 25.0f, 17},
                                             {{10.0f, -10.0f, -10.0f}, 25.0f, 17},
                                             {{-10.0f, -10.0f, 20.0f}, 27.0f, 17},
                                             {{10.0f, -10.0f, 20.0f}, 27.0f, 17}}};
constexpr Sphere FourthSphere{{0.0f, -15.0f, -20.0f}, 30.0f, 2};
constexpr std::array<Sphere, 3> MonsterSpheres{
    {{{-10.0f, -10.0f, -10.0f}, 35.0f, 17}, {{10.0f, -10.0f, -10.0f}, 35.0f, 17}, {{0.0f, -10.0f, -40.0f}, 50.0f, 19}}};

// Los argumentos de cada init, como los empuja 0x50ba30 (los números de los PUSH).
constexpr ClothShape LordsFrontCloth{.bone = 20,
                                     .anchor = {0.0f, 0.0f, 20.0f},
                                     .columns = 6,
                                     .rows = 5,
                                     .width = 30.0f,
                                     .height = 70.0f,
                                     .front = TextureSlot{0x256},
                                     .back = TextureSlot{0x256},
                                     .flags = 0xc0000205};
constexpr ClothShape LordsSecondCloth{.bone = 20,
                                      .anchor = {0.0f, 5.0f, 18.0f},
                                      .columns = 5,
                                      .rows = 5,
                                      .width = 30.0f,
                                      .height = 70.0f,
                                      .front = TextureSlot{0x256},
                                      .back = TextureSlot{0x256},
                                      .flags = 0x80002205};
constexpr ClothShape LordsCapeCloth{.bone = 19,
                                    .anchor = {0.0f, 8.0f, 10.0f},
                                    .columns = 10,
                                    .rows = 10,
                                    .width = 180.0f,
                                    .height = 180.0f,
                                    .front = TextureSlot{0x257},
                                    .back = TextureSlot{0x257},
                                    .flags = 0x1005};
// La cuarta: armada con el arreglo (y = 10, 0x1406) y vuelta a armar encima en los frames que siguen
// (y = 8, 0x1407).
constexpr ClothShape LordsFourthCloth{.bone = 18,
                                      .anchor = {0.0f, 10.0f, -5.0f},
                                      .columns = 5,
                                      .rows = 5,
                                      .width = 50.0f,
                                      .height = 90.0f,
                                      .front = TextureSlot{0x6ea},
                                      .back = TextureSlot{0x6ea},
                                      .flags = 0x1406};
constexpr ClothShape LordsFourthAgain{.bone = 18,
                                      .anchor = {0.0f, 8.0f, -5.0f},
                                      .columns = 5,
                                      .rows = 5,
                                      .width = 50.0f,
                                      .height = 90.0f,
                                      .front = TextureSlot{0x6ea},
                                      .back = TextureSlot{0x6ea},
                                      .flags = 0x1407};
constexpr ClothShape MonsterCloth{.bone = 19,
                                  .anchor = {0.0f, 10.0f, 0.0f},
                                  .columns = 10,
                                  .rows = 10,
                                  .width = 55.0f,
                                  .height = 140.0f,
                                  .front = TextureSlot{0x252},
                                  .back = TextureSlot{0x252},
                                  .flags = 0x1001};
constexpr ClothShape RobeCloth{.bone = 19,
                               .anchor = {0.0f, 10.0f, 0.0f},
                               .columns = 10,
                               .rows = 10,
                               .width = 75.0f,
                               .height = 120.0f,
                               .front = TextureSlot{0x250},
                               .back = TextureSlot{0x251},
                               .flags = 0x405};

template <std::size_t N> void AddSpheres(Cloth& cloth, const std::array<Sphere, N>& spheres)
{
    for (const Sphere& sphere : spheres)
    {
        cloth.AddSphere(sphere.local, sphere.radius, sphere.bone);
    }
}

Render::ModelBlend BlendOf(Render::Models::ClothBlend blend, Render::ModelBlend previous)
{
    switch (blend)
    {
    case Render::Models::ClothBlend::Opaque:
        return Render::ModelBlend::Opaque;
    case Render::Models::ClothBlend::Alpha:
        return Render::ModelBlend::Alpha;
    case Render::Models::ClothBlend::Additive:
        return Render::ModelBlend::Additive;
    case Render::Models::ClothBlend::Previous:
        return previous;
    }
    return previous;
}
} // namespace

bool WearsACloth(std::uint8_t characterClass, std::uint8_t map)
{
    const std::uint8_t base = characterClass & ClassMask;
    return (base == MagicGladiator || base == DarkLord) && !(map >= FirstClothlessMap && map <= LastClothlessMap);
}

void CharacterCloth::Build(const ClothWearer& wearer, const Render::Models::ClothBody& body)
{
    if ((wearer.characterClass & ClassMask) == DarkLord)
    {
        // 0x50d5b9: el arreglo de cuatro, tres armadas y la cuarta solo con ArmorClass05; +0x18c en 3.
        m_cloths.assign(4, Cloth{});
        m_cloths[0].Build(LordsFrontCloth, body);
        m_cloths[0].SetWindBytes(10, 50);
        AddSpheres(m_cloths[0], FrontSpheres);
        m_cloths[1].Build(LordsSecondCloth, body);
        m_cloths[1].SetWindBytes(8, 40);
        AddSpheres(m_cloths[1], FrontSpheres);
        m_cloths[2].Build(LordsCapeCloth, body);
        AddSpheres(m_cloths[2], BackSpheres);
        if (wearer.armor == ArmorClass05)
        {
            m_cloths[3].Build(LordsFourthCloth, body);
            m_cloths[3].AddSphere(FourthSphere.local, FourthSphere.radius, FourthSphere.bone);
        }
        m_count = 3;
        return;
    }
    // 0x50d812: una sola, la del monstruo 0x37 o la de la clase 3.
    m_cloths.assign(1, Cloth{});
    if (wearer.monster == ClothMonster)
    {
        m_cloths[0].Build(MonsterCloth, body);
        AddSpheres(m_cloths[0], MonsterSpheres);
    }
    else
    {
        m_cloths[0].Build(RobeCloth, body);
        AddSpheres(m_cloths[0], BackSpheres);
    }
    m_count = 1;
}

void CharacterCloth::Frame(Render::ModelBatch& batch, const ClothWearer& wearer, const Render::Models::ClothBody& body,
                           Render::Models::ClothWorld& world, std::uint8_t map, float worldTime, bool wears,
                           const glm::vec3& color)
{
    RunTheBlock(&batch, wearer, body, world, map, worldTime, wears, color);
}

void CharacterCloth::Advance(Render::ModelBatch& batch, const ClothWearer& wearer,
                             const Render::Models::ClothBody& body, Render::Models::ClothWorld& world, std::uint8_t map,
                             Core::Duration clock, bool wears, const glm::vec3& color,
                             Render::Models::OriginalFramePace pace)
{
    const auto worldTime = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(clock).count());
    const int frames = m_frames.FramesAt(clock, pace);
    if (frames == 0)
    {
        DrawTheCloths(batch, wearer, worldTime, wears, color);
        return;
    }
    for (int frame = 0; frame + 1 < frames; ++frame)
    {
        RunTheBlock(nullptr, wearer, body, world, map, worldTime, wears, color);
    }
    RunTheBlock(&batch, wearer, body, world, map, worldTime, wears, color);
}

void CharacterCloth::DrawCurrent(Render::ModelBatch& batch, const ClothWearer& wearer, Core::Duration clock, bool wears,
                                 const glm::vec3& color)
{
    const auto worldTime = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(clock).count());
    DrawTheCloths(batch, wearer, worldTime, wears, color);
}

void CharacterCloth::DrawTheCloths(Render::ModelBatch& batch, const ClothWearer& wearer, float worldTime, bool wears,
                                   const glm::vec3& color)
{
    m_flares.clear();
    if (!wears || (wearer.objectFlags & Hidden) != 0)
    {
        return;
    }
    const bool darkLord = (wearer.characterClass & ClassMask) == DarkLord;
    for (std::size_t index = 0; index < m_count && index < m_cloths.size(); ++index)
    {
        if (darkLord && index == 2 && wearer.wings != LordsCape && color == PlainClothColor)
        {
            continue;
        }
        Draw(batch, m_cloths[index], color, worldTime);
    }
}

void CharacterCloth::RunTheBlock(Render::ModelBatch* batch, const ClothWearer& wearer,
                                 const Render::Models::ClothBody& body, Render::Models::ClothWorld& world,
                                 std::uint8_t map, float worldTime, bool wears, const glm::vec3& color)
{
    m_flares.clear();
    if (!wears)
    {
        return;
    }
    const bool darkLord = (wearer.characterClass & ClassMask) == DarkLord;
    // El arreglo es de cuatro con la clase 4 y de una con las demás. Si la clase cambió sobre la misma
    // tela, el original leería fuera del arreglo: MuRet las suelta y las vuelve a armar.
    if (!m_cloths.empty() && m_cloths.size() != (darkLord ? 4u : 1u))
    {
        for (Cloth& released : m_cloths)
        {
            released.Release();
        }
        m_cloths.clear();
        m_count = 0;
    }
    if (m_cloths.empty())
    {
        Build(wearer, body);
    }
    else if (darkLord)
    {
        // 0x50d998..0x50da67: la cuarta se arma encima, sin soltarla (la esfera queda dos veces y las
        // banderas en 0x1406 | 0x1407), o se suelta con otra armadura.
        if (wearer.armor == ArmorClass05)
        {
            if (m_count == 3)
            {
                m_cloths[3].Build(LordsFourthAgain, body);
                m_cloths[3].AddSphere(FourthSphere.local, FourthSphere.radius, FourthSphere.bone);
                m_count = 4;
            }
        }
        else if (m_count == 4)
        {
            m_cloths[3].Release();
            m_count = 3;
        }
    }
    if ((wearer.objectFlags & Hidden) != 0)
    {
        return;
    }
    // 0x50da67..0x50db57: cada tela, sus cinco pasos y su dibujo; la tercera del Dark Lord, solo con la
    // capa o con el color de un duelo.
    for (std::size_t index = 0; index < m_count; ++index)
    {
        const bool withoutTheCape = darkLord && index == 2 && wearer.wings != LordsCape && color == PlainClothColor;
        if (withoutTheCape)
        {
            continue;
        }
        const float dt = (wearer.objectFlags & Still) != 0 ? 0.0f : StepTime;
        Cloth& cloth = m_cloths[index];
        if (!cloth.Simulate(dt, StepsPerFrame, body, world, map))
        {
            // 0x4fb1a0: las suelta todas; el frame siguiente, con +0x190 en 0, las vuelve a armar.
            for (Cloth& released : m_cloths)
            {
                released.Release();
            }
            m_cloths.clear();
            m_count = 0;
            break;
        }
        if (batch != nullptr)
        {
            Draw(*batch, cloth, color, worldTime);
        }
    }
}

// 0x455930: la mezcla, el color (el de 0x50ba30, o el del brillo) y las dos caras de cuadrados, cada
// uno en dos triángulos.
void CharacterCloth::Draw(Render::ModelBatch& batch, const Cloth& cloth, const glm::vec3& color, float worldTime)
{
    const Render::ModelBlend blend = BlendOf(Render::Models::FaceBlendOf(cloth.Flags()), m_lastBlend);
    m_lastBlend = blend;
    glm::vec3 faceColor = color;
    if ((cloth.Flags() & Render::Models::ClothGlows) != 0)
    {
        const float glow = Cloth::GlowAt(worldTime);
        const std::vector<Render::Models::ClothFlare> flares = cloth.Flares(glow);
        m_flares.insert(m_flares.end(), flares.begin(), flares.end());
        faceColor = glm::vec3(glow);
    }
    const Render::Color vertexColor{faceColor.r, faceColor.g, faceColor.b, 1.0f};
    const auto particles = cloth.Particles();
    for (const bool front : {true, false})
    {
        if (!front && !cloth.DrawsTheBack())
        {
            continue;
        }
        batch.SetState(Render::ModelDrawState{.texture = front ? cloth.Front() : cloth.Back(), .blend = blend});
        const std::vector<Render::Models::ClothCorner> corners = cloth.Corners(front);
        for (std::size_t q = 0; q + 3 < corners.size(); q += 4)
        {
            std::array<Render::ModelVertex, 4> square{};
            for (std::size_t c = 0; c < 4; ++c)
            {
                const Render::Models::ClothCorner& corner = corners[q + c];
                square[c] = Render::ModelVertex{
                    .position = particles[static_cast<std::size_t>(corner.particle)].position,
                    .uv = corner.uv,
                    .color = vertexColor,
                };
            }
            batch.Triangle({square[0], square[1], square[2]});
            batch.Triangle({square[0], square[2], square[3]});
        }
    }
}
} // namespace Client::Scenes
