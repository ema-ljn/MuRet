#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Actors::Drawing
{
struct GameCharacterPass;
} // namespace Client::Actors::Drawing

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// Monsters and NPCs in the game's draw (0x50ba30 with a type other than 0x20a, CL9 milestone 2). Contract: 06 § El
// dibujo del juego, "El dibujo de los monstruos y los NPC".
namespace Client::Monsters::Drawing
{

// 0x50c671, 0x50c71b and 0x50c8ce: the +0x376 values with 0x60b130 shine passes (0x26, 0x2b, 0x34, 0x3b, 0x43, 0x4e to
// 0x53, 0x35, 0x36 and 0x87).
[[nodiscard]] bool MonsterShines(std::uint16_t monster);

// 0x50ba30 with a monster or an NPC (the type is not 0x20a) in slot `slot`, in the ASM's order:
// - with a +0x376 of WearsClothOnTheMesh (0x50bae7), 0x5ffdf0, the cloth on the mesh of pass.cloths[slot]
//   (MonsterMeshCloth, also if 0x5ffdf0 does not draw) and 0x6002e0, with the fourth argument at 0;
// - otherwise, 0x602e90(object, 1, outline, +0x376 from the 0x50bd05 list or 0): 0x5ffdf0 lowers the alpha 0.15f in
//   Chaos Castle with the hero's +0x1a1, below 0.01 it neither poses nor draws; it poses on the own bones (+0x120,
//   actor.bodyPose) with the head's angles, with the outline (the two 0x60cd60 passes with the colors and scales of
//   0x5fffbd..0x600135; 0xc5, only its mesh 0) and the body of 0x6002e0 over the model color of 0x5ffce0, with its
//   cases by type and by map, its chain and what it adds after the body (DrawMonsterBody, MonsterBody.h);
// - the shadow of 0x50bd89 with its conditions (DrawObjectShadow) and, in Blood Castle, the terrain height of those
//   dying; the 0x61e decal of 0xe8 and 0xe9 (0x5fb2f0, Terrain::DrawTerrainDecal), additive, with the terrain heights
//   of `pass` or of the world (without them, it cuts);
// - what 0x50bf45 adds by +0x376: 0x45 with its nine flare01 and the four rand() of its first 0x5c6/3 particle, which
//   cuts (0x50c0db, CL11; between original frames, without the particles); the flare01 of 0x46 on its bone 20; the
//   0x60b130 shine passes (MonsterShines: the color of 0x608e00 times the alpha and the factor, and RenderBody with
//   0x48, 0x44, 0x144 or 0x140 over the stored pose; Q-C863); 0x4d (0x50c177: its 0x44 pass with the clock's factor,
//   +0x64 at 0 and +0x68 stored, its body again with 0x602e90 in modes 2 and 3, at 1.2 and 1.4 around its bones, the
//   type + 1 (0x18a) with the outline, bones 24, 23 and 14 in WorldRuntime::phoenixBones and the 0x4d grid cloth of
//   pass.cloths[slot] (CharacterCloth::FrameTheMonsterCloth; without it, it cuts at 0x50c283), with the type at 0x18a;
//   Q-C864); the grid cloths of 0x47, 0x4a and 0x12d (0x50c59f), with the same slot cloth over the stored pose (without
//   it, it cuts at 0x50c59f); what 0x2a (0x50c762: 0xef from bone 9 and 0xf0 from 0x3d at 0.9f with the light at 1) and
//   the statues 0x84 to 0x86 (0x50c83b: 0xc1c, 0x225 or 0xa24 from bone 1 with the flag) hang from the wings slot, with
//   HangFromTheWingsSlot (GameHanging.h) and the fauna library; 0x49 (0x50c3ed: twenty-one flare01 and four 0x614/7
//   joints between its bones, the 0x44 pass and the cut at 0x60b300) and 0x4b (0x50c37e: the 0x5d9/0 of its bone 18 and
//   the 0x144 pass); the others, CheckMonsterDecorations;
// - the guild war and duel block (0x50c972..0x50cc29), which MuRet does not run for these draws: the duel does not
//   reach a monster and the war only an NPC with +0x1f2 overwritten (08 P-C352);
// - without +0x3e3, parts 5 down to 0 with 0x60cf70 (0x50d3e3: the parts' outline with mode 1, the pose on the own
//   bones and 0x60b3d0 with each one's level, excellent and set, with the light of +0x3b4); a model outside the records
//   0x60dbe0 sets to 0 cuts at 0x50d431, and the Chaos Castle Angel (+0x88 at 1) at 0x50d383;
// - the cloths (0x50d58b): with +0x376 at 0x37 outside maps 0x12 to 0x17 (WearsTheMonsterCloth), the CharacterCloth of
//   pass.cloths[slot] over the own bones (without it, it cuts), with its flares in pass.sprites;
// - 0x50db57: the rand() and the light of what hangs (HangingLightOf) and, outside the back, the hands
//   (DrawGameHanging);
// - the by-model switch of 0x50e9fa: the 0x50b870 and 0x50b920 sprites of 0x152 (with +0x1d6 at 1), 0x170, 0x186,
//   0x177, 0x174, 0x176, 0x173, 0x1d8 and 0x1f2; those of 0x1d9 (0x510763) with its rand() % 30 and, if it gives 0, its
//   three rand() % 100 and the 0x618/6 joint (which CreateFaunaJoint does not create yet: it cuts); the bone loop of
//   0x16e (0x5108b6, DrawTheBoneSparks), and the others go through the 0x510a1e chain: 0x43c150 and 0x42dfe0
//   (DrawKalimaSiegeHelpers, KalimaSiegeHelpers.h) and, if neither has the type, 0x441400, 0x434180 and 0x4271d0
//   (DrawTheMonsterHelpers, MonsterHelpers.h), with their CL11 effects cut;
// - 0x510ef0 in scene 5 with +0x88 at 1 and +0x369 (cuts at 0x510f4d, CL11); then the raven of +0x430 (DrawGameRaven,
//   with pass.polygon).
// `models` holds the items and Player.bmd (the hands); the type's model comes from Assets::AssetsState::fauna. Between
// original frames, everything on a copy of the character and the rand().
[[nodiscard]] Core::Expected<void, std::string>
DrawGameMonster(Render::ModelBatch& batch, const Models::IModelLibrary& models, Kernel::WorldState& world,
                std::size_t slot, const Actors::Drawing::GameCharacterPass& pass, bool originalFrame);
} // namespace Client::Monsters::Drawing
