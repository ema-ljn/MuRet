#pragma once

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Actors/Drawing/ClothCarry.h"
#include "Actors/Drawing/MonsterCloth.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// A character's cloths: the block 0x50d58b..0x50db57 of 0x50ba30, which builds, simulates and draws them in each drawn
// frame, after the body parts and before what hangs (06 § La tela de la clase 4). The Dark Lord has three, the cape and
// its two front cloths, and a fourth with its class armor; class 3, one.
namespace Client::Actors::Drawing
{
// What the block reads from the character.
struct ClothWearer
{
    // +0x1d4: the class in bits 0 to 2.
    std::uint8_t characterClass = 0;
    // +0x238: the armor. With ArmorClass05 (0x221e), the Dark Lord wears the fourth cloth.
    std::uint16_t armor = 0;
    // +0x2f8: the wings. Without the 0x1c30 cape, the Dark Lord's third cloth does not go.
    std::optional<std::uint16_t> wings;
    // +0x376: the monster number (−1 for a player); 0x37 has its cloth.
    std::optional<std::uint16_t> monster;
    // object + 0x78: with 0x4000 it is neither simulated nor drawn; with 0x1000, the dt is 0.
    std::uint32_t objectFlags = 0;
};

// bVar23 for a player (0x50c929..) without looking at +0x3e3: class 3 or 4, except on maps 0x12 to 0x17. The game
// builds all of it, with +0x3e3, the guild wars, Battle Soccer and the duel (GameCharacters,
// DrawnCharacter::wearsCloth).
[[nodiscard]] bool WearsACloth(std::uint8_t characterClass, std::uint8_t map);

// The other half of bVar23 (0x50c951..0x50c96e), with any type: +0x376 at 0x37, except on maps 0x12 to 0x17. It builds
// the cloth of ClothMonster, or the Dark Lord's if +0x1d4 says class 4 (0x50d5a7 does not look at the type).
[[nodiscard]] bool WearsTheMonsterCloth(std::uint16_t monster, std::uint8_t map);

// 0x50c177 and 0x50c59f: the +0x376 values that build at object + 0x190 a cloth of their own grid (0x6a0210 with
// 0x4548b0, vtable 0x6b8cd8, and its +4, 0x454a50), with any type and on any map: 0x4d (0x50c2ca: bone 10, (0, −10, 0),
// 5 · 12, 15 · 240, texture 0x629 and 0x1100, with 0x455cd0 at (0, 0, 40) of radius 30 on bone 10) and 0x47 (texture
// 0x253), 0x4a and 0x12d (0x255) (0x50c5fa: bone 0x13, (0, 10, 0), 5 · 15, 30 · 300 and 0x1100, without spheres).
[[nodiscard]] bool WearsAGridClothOfItsOwn(std::uint16_t monster);

// The color of 0x50ba30 without a duel or a war: white.
inline constexpr glm::vec3 PlainClothColor{1.0f, 1.0f, 1.0f};

// The most Advance runs at once: 3 s of the original.
inline constexpr int MaxClothFramesPerAdvance = Models::MaxOriginalFramesPerDraw;

class CharacterCloth
{
public:
    // One frame of the block: if there are no cloths (object + 0x190 at 0) it builds them from the current pose; with
    // the array built, the Dark Lord's fourth is built on top or released by the armor; and each cloth takes its five
    // steps of 0.005 and is drawn in `batch` before the next one. If a step breaks it, it releases them all (0x4fb1a0)
    // and the next frame builds them again. `wears` is bVar23 and `color` that of 0x50ba30.
    void Frame(Render::ModelBatch& batch, const ClothWearer& wearer, const Models::ClothBody& body,
               Models::ClothWorld& world, std::uint8_t map, float worldTime, bool wears,
               const glm::vec3& color = PlainClothColor);

    // What runs in a MuRet frame (DrawCharacter). Q-C124 fixed as Q-C20: the block runs once per original frame
    // (Models::OriginalFrameTime, 46.875 ms) that passed since the previous frame, the last one with its draw, and with
    // none the current state is drawn; the first runs one. So the cloth flaps as in the original at any FPS. In the
    // faithful mode (`pace` OncePerDraw), one per draw, like the original (Models::OriginalFrameCarry). A jump of more
    // than MaxClothFramesPerAdvance frames runs those and drops the rest. `clock` is also the glow's WorldTime. With
    // none, the draw is DrawCurrent's with `body`.
    void Advance(Render::ModelBatch& batch, const ClothWearer& wearer, const Models::ClothBody& body,
                 Models::ClothWorld& world, std::uint8_t map, Core::Duration clock, bool wears,
                 const glm::vec3& color = PlainClothColor,
                 Models::OriginalFramePace pace = Models::OriginalFramePace::ByTheClock);

    // A draw between original frames: only the cloths that already exist and their flares, without building or
    // simulating them or advancing their clock. Also valid before the first Advance. Each cloth is carried with its
    // bone from the body of its last simulation to `body`, the current one (ClothCarry): with the character walking,
    // the cape goes with it instead of staying behind until the next frame. With the body still, it is drawn as it was
    // left.
    void DrawCurrent(Render::ModelBatch& batch, const ClothWearer& wearer, const Models::ClothBody& body,
                     Core::Duration clock, bool wears, const glm::vec3& color = PlainClothColor);

    // One frame of the cloth of +0x376 `monster` (WearsAGridClothOfItsOwn, 0x50c283..0x50c35c and 0x50c59f..0x50c669):
    // with +0x190 at 0 it builds it on `body` (one, +0x18c at 1); then the first runs 0x455190(0.005f, 5), without
    // looking at +0x78; if it breaks, 0x4fb1a0 releases them and the next frame builds it again; otherwise its +0x18
    // (0x455930) draws it in `batch` with the null color, (1, 1, 1). An array already built keeps whatever shape it
    // has, like the original.
    void FrameTheMonsterCloth(Render::ModelBatch& batch, std::uint16_t monster, const Models::ClothBody& body,
                              Models::ClothWorld& world, std::uint8_t map, float worldTime);
    // Between original frames: the first cloth as it was left, not simulated, carried to `body`. Without a cloth,
    // nothing.
    void DrawTheMonsterCloth(Render::ModelBatch& batch, const Models::ClothBody& body, float worldTime);

    // 0x4fb1a0: releases the resources of the character's cloths, without restarting the block's clock or its earlier
    // blend.
    void ReleaseResources();

    // Advance counts from zero again, as in the first frame: the next one runs one frame of the block and does not
    // catch up the time that passed without a draw. The cloths stay as they were. It is for a held frame (Q-C85): the
    // original does not draw while it connects, and in the next frame it runs one.
    void RestartTheClock()
    {
        m_frames = {};
    }

    // object + 0x190: the built cloths (four with class 4, one with the others), or none.
    [[nodiscard]] std::span<const Models::Cloth> Cloths() const
    {
        return m_cloths;
    }
    // object + 0x18c: how many the loop walks.
    [[nodiscard]] std::uint8_t Count() const
    {
        return m_count;
    }
    // object + 0x190 of a monster with the cloth on the mesh (vtable 0x6b8d04, 0x50bae7): the one DrawGameMonster
    // builds and simulates. With the same +0x376 it does not wear the one above.
    [[nodiscard]] Actors::Drawing::MonsterMeshCloth& OnTheMesh()
    {
        return m_onTheMesh;
    }

    // The glow sprites of the last frame (Effect\flare01.jpg, 0x5491e0). DrawCharacter asks for them in the frame's
    // table (DrawnCharacter::sprites), which 0x549440 → 0x549290 draws.
    [[nodiscard]] std::span<const Models::ClothFlare> Flares() const
    {
        return m_flares;
    }

private:
    // The block, with its draw in `batch`, or undrawn with nullptr.
    void RunTheBlock(Render::ModelBatch* batch, const ClothWearer& wearer, const Models::ClothBody& body,
                     Models::ClothWorld& world, std::uint8_t map, float worldTime, bool wears, const glm::vec3& color);
    // The cloths as they were left, with the loop's rules, not simulated, carried to `body`.
    void DrawTheCloths(Render::ModelBatch& batch, const ClothWearer& wearer, const Models::ClothBody& body,
                       float worldTime, bool wears, const glm::vec3& color);
    void Build(const ClothWearer& wearer, const Models::ClothBody& body);
    // A cloth and its flares, with its particles carried by `carry` if there is one.
    void Draw(Render::ModelBatch& batch, const Models::Cloth& cloth, const glm::vec3& color, float worldTime,
              const std::optional<ClothCarry>& carry = std::nullopt);

    std::vector<Models::Cloth> m_cloths;
    // The body of the last simulation, from which DrawTheCloths carries each cloth.
    std::optional<SimulatedClothBody> m_simulatedBody;
    std::uint8_t m_count = 0;
    std::vector<Models::ClothFlare> m_flares;
    Render::ModelBlend m_lastBlend = Render::ModelBlend::Opaque;
    Models::OriginalFrameCarry m_frames;
    Actors::Drawing::MonsterMeshCloth m_onTheMesh;
};
} // namespace Client::Actors::Drawing
