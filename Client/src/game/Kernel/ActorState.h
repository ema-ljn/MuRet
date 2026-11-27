#pragma once

#include "Kernel/CharacterAppearance.h"
#include "Kernel/ActorSpirit.h"
#include "Pathfinding/Path.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Kernel/CharacterHead.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// A record of the durable character pool, 0x438 bytes in the original (06, "Los personajes de la lista"). A projection
// of what MuRet reads today, not the layout. It starts zeroed, as WinMain's REP STOSD leaves it (0x4bafa6..0x4bafb3).
namespace Client::Kernel
{
inline constexpr std::int32_t ActorRemovalCountdownSentinel = -0x80;

// A part's model without an item.
inline constexpr std::uint16_t NoItemModel = 0xffff;

// The GMs' cape: the 0x1e8-byte object 0x413fe0 creates with +0x360 at 5 (0x414370 with MuSign, 0x12a). Only the
// animation changes between draws (+0x115 action, +0x116 previous, +0x118 frame, +0x11c previous key), which 0x414470
// advances with 0x4ec200; the place is recomputed every time.
struct GameMasterLayer
{
    Models::AnimationState animation;
    // The cape's +0x178 (its object's +0x174): 0x414370 sets it to 1 and only 0x5ffdf0 lowers it, in Chaos Castle with
    // the hero's +0x1a1.
    float alpha = 1.0f;
};

// A worn part, 0x20 bytes each: the body's five from +0x218, the hands at +0x2b8 and +0x2d8, the wings at +0x2f8 and
// the pet at +0x318 (06, "El equipo propio en el personaje").
struct ActorItemPart
{
    std::uint16_t model = 0; // +0, 0xffff without an item
    std::uint8_t level = 0;  // +2
    // +3 and +4: 0x512a30 copies the item's D+0x1b and D+0x1c bytes as they are.
    std::uint8_t flags1b = 0;
    std::uint8_t flags1c = 0;
    // +5: the body bone it hangs from. 0x511290 sets 0x21 and 0x2a on a 0x20a's hands (0x5126ce) and 0x50ba30 the
    // wings' (0x2f) and the pet's (0x22) before each 0x509340.
    std::uint8_t bone = 0;
    // +6 action, +7 previous, +8 frame and +0xc previous key of what hangs, which 0x509340 advances with 0x4ec200 every
    // frame. Nobody zeroes them on construction: the previous use's stay.
    Models::AnimationState animation{};
    // +0x10: the step per frame, which 0x50ba30 writes before each 0x509340.
    float speed = 0.0f;
};

struct ActorState
{
    // +0: alive. 0x511290 sets it and 0x510f70 clears it on loading another map (06, "La carga del mapa").
    bool alive = false;
    // +0x10, +0x14 and +0x18. 0x511290 computes Z with XY 50/50 and the current world, then 0x517270 writes the place's
    // X and Y (CL7.3 B).
    glm::vec3 position{};
    // Whether word318 (+0x318) is 0x1c15: what the selection writes of it and what the next 0x511290 reads to add the
    // pet's offset. The login leaves 0x1c16 in place 2. 0x512a30 writes the whole word in pet.model and updates this
    // too.
    bool previousHad1c15 = false;
    // MuRet's: how many times 0x511290 built it. The view rebuilds the visuals on each, even with the same bytes (06,
    // "Cada F3/00 aceptado y alta F3/01 exitosa reconstruye").
    std::uint64_t revision = 0;

    // What 0x512a30 and 0x4f2250 read and write (06, "El equipo propio en el personaje" and "Caminar").
    std::uint16_t type = 0;   // +2: 0x20a is a player
    std::int32_t subtype = 0; // +4: 0x4f2250's suffix compares it with 209 to 211, signed
    float scale = 0.0f;       // +0xc, from 0x512930
    // The character's box, +0x124 to +0x138, relative to its position: 0x511290 puts the floor at (−60, −60, 0)
    // (0x512270..0x5122b9) and the rest by type (tables 0x512700 and 0x5126e4, and 0x42d7f0 with 0x19b and 0x1f8).
    // Picking (0x59e700) tests the +0x13c box, which 0x505de0 builds from these values and the position.
    glm::vec3 boundsMin{};
    // +0x130 and +0x134, the other corner's X and Y.
    float boundsMaxX = 0.0f;
    float boundsMaxY = 0.0f;
    // +0x138, the box's top. 0x5133b0 adds 70 to it. The bubble reads it too (0x5a9050), 60 higher.
    float boundsTop = 0.0f;
    // +0x58, +0x60 and +0x64: 0x511290 sets the first and last to −1 (0x511d45, 0x51248d) and leaves the middle one;
    // 0x5141e0's cases write them by type. +0x58 at −2 is not drawn (0x42da90 and 0x42dfe0 write it on map 30); +0x60
    // is 0xc5's particle counter and the state of Kundun's attacks in 0x43c150 (KalimaSiegeHelpers.h), and the dying
    // one reads it (0x4fba6d, 0x192). Who reads the others, unread.
    std::int32_t dword58 = 0;
    std::int32_t dword60 = 0;
    std::int32_t dword64 = 0;
    // +0x92 (1), +0x93 (0xa), +0x94 (1.2f), +0x98 (0) and +0xe8 (1) from 0x511290 (0x511c70..0x511c88, 0x511bfe and
    // 0x511be6). The 13 puts the high type's bits 4..6 in +0x93 (0x4c9ee9), which 0x1a8's health bar reads (0x42a2c5,
    // Scenes/HealthBars). Who reads the others, unread.
    std::uint8_t byte92 = 0;
    std::uint8_t byte93 = 0;
    float float94 = 0.0f;
    std::uint8_t byte98 = 0;
    std::uint8_t byteE8 = 0;
    // +0xcc, +0xd0 and +0xe4: the push of type 0xf1 that the DF sets (0x4c6b40): 20 in +0xe4 and the cell difference
    // times 400 in the other two. +0xd8: 0x511290 sets it to 0 (0x511c56); Castle Siege's creator copies Z into it
    // (0x42d23e). +0xd4 and the five are also the flight of one who dies pushed (below, ActorDying.h).
    float floatCC = 0.0f;
    float floatD0 = 0.0f;
    // +0xd4: 0xf1's third turn, which 0x4fb910 adds to the yaw and shrinks by 0.8f. No path of the pool's character
    // writes it (nor 0x511290: the slot's previous use stays, 0 in MuRet; *static*).
    float floatD4 = 0.0f;
    float floatD8 = 0.0f;
    float floatE4 = 0.0f;
    // +0x38a (0, 0x511c0c), +0x394 (0, 0x511ceb) and +0x42d (bit 7 of the 13's high type, 0x4c9ee3): the monsters'
    // creations write them. Who reads them, unread. +0x3e4 (also written) is byte3e4, below. +0x38f (0x42d7f0 sets it
    // to 0 with 0x19b) is attackCount and +0x3a4 (tail 0x5140c0 copies [0x57678b0]'s WORD into it,
    // WorldRuntime::heroSlot) is attackTarget, below.
    std::uint8_t byte38a = 0;
    std::int32_t dword394 = 0;
    std::uint8_t byte42d = 0;
    // +0x110 (0x511290 sets it to 0, 0x511c69) and +0x100: 0x42d890 sets them to 1 and 0x12c when map 30's 286 or 287
    // ends action 5 (0x42d8fd and 0x42d904); 0x42da90 reads and writes them: open, and the time left, which drops by 1
    // each step (StepTheCastleObject, MonsterStepCases.h). 0x42dcb0 sets +0x110 to 3 on hitting 0x19e and 0x19f (CL9
    // milestone 3). 0x439d40 (the 1B with 0xc9 or 0xca, 0x4cbfc9 and 0x4cbfde) sets it to 0x21 or 0x22 if the type is
    // not 0x20a, and 0x439d90 (0x43f836 and 0x43fb99, the draw of 0x195/9 and 0x197) reads those two values, drops
    // mesh 6's stones and resets it to 0 (StrikeTheGlow, MonsterBodyChain.cpp).
    std::uint8_t byte110 = 0;
    std::int32_t dword100 = 0;
    // +0x428: −0x80 from 0x511290 (0x511c77); the 68 sets it to 0x96 (0x4e0b8a), and 0x4fd5f0 counts it down. The
    // default stands for a built actor; unbuilt BSS entries are not walked because they are dead.
    std::int32_t dword428 = ActorRemovalCountdownSentinel;
    // +0x7c: with 0x200000, 0x4f2250 sits the character down (action 0xca). Who writes it, unread.
    std::uint32_t flags7c = 0;
    // +0x78: 0x511290 sets it to 0 (0x511d51). The rider's is read by the horse's step (with 0x4000, no dust or breath:
    // 0x444414 and 0x44455a), its draw (with 0x4000, 0x602e90 with 10: 0x445310) and 0x5ffdf0 (with 0x1000, the horse's
    // frame at 0: 0x5ffefc). Kalima's guards set 0x100 or 0x8 (0x43aabf, Scenes::StepTheAttackCases). Who else writes
    // it on the character, unread.
    std::uint32_t flags78 = 0;
    // +8: at 1, the dark horse's step of this rider emits twelve 60f/0 (0x44468d) and resets it to 0 (0x444758),
    // whatever it is. 0x511290 does not write it and 0x5140c0 sets it to 0. Only the pets' A9 reply sets it to 1
    // (0x421db0, 0x4220a8..0x4220c1), on the hero and with its byte 4 at 0xfe (with 0xff, 2), unported.
    std::uint8_t byte8 = 0;
    // Action at +0x111, previous at +0x112, frame at +0x114 and previous key at +0x118: what 0x4ea030 sets.
    Models::AnimationState animation{};
    std::uint8_t characterClass = 0;                         // +0x1d4
    std::uint8_t skin = 0;                                   // +0x1d5
    std::uint16_t level = 0;                                 // +0x1d6, the level of the list or of F3 03
    std::uint8_t guildStatus = 0;                            // +0x368, 0xff from 0x511290
    std::uint16_t head = 0;                                  // +0x1f8, from 0x512930: 0xffff without a head
    std::array<ActorItemPart, Kernel::BodyPartCount> body{}; // +0x218, in Kernel::BodyPart's order
    std::array<ActorItemPart, 2> hands{};                    // +0x2b8 the right one and +0x2d8 the left one
    ActorItemPart wings;                                     // +0x2f8
    ActorItemPart pet;                                       // +0x318
    // +0x358: 0x511290 sets it to 0 (0x511c99). On the published hero, 0x40d010 sets it to 0, and to 1 with a full set
    // (06, "Los stats del registro propio"); in the F3 03 reply that published hero is still the previous one (Q-C197).
    // The selection's list copies b[10]'s bit 0 into it, unported.
    std::uint8_t byte358 = 0;
    // +0x35c: with +0x358, 0x50fd74 counts it on each draw and resets it to 0 with effect 0x5ac/3 every 100. 0x511290
    // sets it to 0 (0x511ca0); nothing else writes it.
    std::int32_t counter35c = 0;
    // +0x376: with 0x101, 0x4f2250 picks the flying action. Who writes it, unread.
    std::uint16_t word376 = 0;
    std::uint8_t run = 0; // +0x38c, Caminar's Run
    // +0x1c, +0x20 and +0x24: 0x511290 sets the first two to 0 and the third to the angle it receives.
    glm::vec3 angles{};
    // +0x40, +0x44 and +0x48: the point of staff 0xc1e that 0x509340 leaves on each draw (0x50b4ca). Joint 0x618/0x11,
    // released there, takes its position from here on creation (0x5321f7) and each step (0x53822f). 0x511290 does not
    // write it: the record's previous use stays (Q-C363).
    glm::vec3 effectOrigin{};
    // +0x4c, +0x50 and +0x54: the point of 0x505bc0's first bone (the second goes to +0x40), written by 0x505de0's
    // cases of 0x179, 0x17b to 0x17f, 0x1aa and 0x1ab. 0x511290 does not write it. Who reads it, unread.
    glm::vec3 bonePoint4c{};
    // +0x17c, +0x180 and +0x184: the position 0x4fcee0 copies before frame 3 and outside action 0x8d
    // (0x4fcf06..0x4fcf1b). 0x4f5b70's 0x49 and 0x4b write it too on the 13 (0x4f7ab9, Scenes::StepTheAttackCases);
    // 0x511290 leaves it: the record's previous use stays (Q-C363). Joint 0x610/4 of sword 0x223 follows the character
    // from here (0x537d55).
    glm::vec3 trailOrigin{};
    // +0x194, +0x198 and +0x19c: the hand bone's point that 0x4f97b0's skill 0x2b leaves with +0x381 up to 8, moved 300
    // along the heading (0x4f9a1e..0x4f9a80, Scenes::StepTheFrameSkill). The step of the joints with this owner reads
    // it (0x535ea9; the same skill's 0x10f/2, CL11). 0x511290 does not write it.
    glm::vec3 skillHandPoint{};
    // +0x1a0: skill 0x2b lights 35 steps of sparks on the target (4f9ba2); 511c62 initializes it to 0.
    std::uint8_t byte1a0 = 0;
    // +0x28 and +0x2c, the head's angles, and +0x34 and +0x38, where it turns: 0x505de0 moves them
    // (0x5068eb..0x5069ab, Kernel::StepCharacterHead) and the pose reads them (0x4eb860). 0x511290 does not write them:
    // WinMain's or the record's previous use stay. The hero's target is written by MoveHero with the mouse
    // (0x59b1c2..0x59b287, CL7.4), unported.
    CharacterHead headTurn{};
    // +0x120: the body's bones in the world as the game's last draw left them (0x509340, 0x4eb860's pose with the
    // head). 0x511290 frees the buffer and asks for an uninitialized one (0x5123c4..0x5123ff): empty before the first
    // draw (Q-C318). Atlans's bubble reads it (0x506d25).
    std::vector<Math::BoneTransform> bodyPose;
    // +0x430: the raven 0x420d60 creates and 0x420dd0 moves inside this owner's body.
    std::optional<Kernel::DarkSpirit> darkSpirit;
    // +0x360: 5 is the GMs' cape, which 0x413fe0 sets on each draw of a GM; 1 to 3, Blood Castle's event weapon
    // (0x50de84). 0x513430, 0x5139f0, 0x5bc740 and 0x5be150 write 4 (and 0), and the packets 6 to 13 (0x12 in 0x4c89e1,
    // 0x40c140, and 0x42ab40, 0x42ad50 and 0x42ae60 with +0x78 flags), unported. 0x511290 leaves it: the record's
    // previous use stays.
    std::uint8_t byte360 = 0;
    // +0x364: the cape 0x413fe0 creates with +0x360 between 5 and 13. 0x511290 sets it to 0 without freeing it.
    std::optional<GameMasterLayer> layer;
    // +0xf4, +0xf8 and +0xfc, its own light: 0x511290 sets it to 0. The arrival effect reads it (0x518cf0).
    glm::vec3 light{};
    // +0x3b4..+0x3bc: 0x50ba30's draw light, not +0xf4's own light.
    glm::vec3 drawnLight{};
    // +0x170 and +0x174: alpha's target and alpha. 0x511290 sets them to 1 and the F3 03 reply lowers the hero's alpha
    // to 0 (0x4c4981): it enters transparent.
    float alphaTarget = 0.0f;
    float alpha = 0.0f;
    // +0x16d: how 0x4e9980 takes alpha to its target: by 0.05 with 1, a tenth of what is left with 0. 0x511290 sets it
    // to 1 (0x511bdf).
    std::uint8_t alphaMode = 0;
    // +0x68: the blendMesh's light, which 0x4e9980 lowers to alpha when greater and never raises (Q-C112). 0x511290
    // sets it to 1.0 (0x512499).
    float blendMeshLight = 0.0f;
    // +0x6c and +0x70: the U and V of 0x4ef0c0's and 0x4ecc30's passes of the character and its parts (0x60cf70).
    // 0x511290 sets them to 0 (0x511d7b and 0x511d7e); 0x433b2f (0x1af on map 34) stores U, and 0x505de0's cases store
    // U for 0x186 and 0x16c and V for 0x189, 0x16f, 0x16d and 0x161 (Scenes::StepTheModelEffects).
    float textureU = 0.0f;
    float textureV = 0.0f;
    // +0x188: the logic step counter (0x7d3a0f0) when 0x511290 built it.
    std::int32_t stepStamp = 0;
    // +0x16c: whether it is seen. 0x508f00 writes it each step with the last draw's trapezoid (0x5fba90) and, alive,
    // 0x508ea0 again. 0x511290 sets it to 0 (0x511bd8).
    bool seen = false;
    // +0x13c..+0x168: a box, the corner (+0x13c..+0x144) and three axes of three floats. 0x510d00 sets +0x13c, +0x148,
    // +0x158 and +0x168 to 1000, 1, 1 and 1 on the hero with +0x1d8 & 4 in scene 5 (0x510e57) instead of drawing it;
    // 0x505e15 builds a model without actions' box from +0x124's and the position. Who reads it, unread.
    float field13c = 0.0f;
    float field140 = 0.0f;
    float field144 = 0.0f;
    float field148 = 0.0f;
    float field14c = 0.0f;
    float field150 = 0.0f;
    float field154 = 0.0f;
    float field158 = 0.0f;
    float field15c = 0.0f;
    float field160 = 0.0f;
    float field164 = 0.0f;
    float field168 = 0.0f;
    // +0x389: nonzero, it is dying and 0x508f00 does not put it in the TerrainWall. 0x511290 sets it to 0 (0x511c05);
    // who else writes it, unread.
    std::uint8_t dying = 0;
    // +0x88, the object's class: 0x511290 and the F3 03 reply set it to 1 (a player); with 8, 0x508f00 does not put it
    // in the TerrainWall (0x508f60). 0x60cf70 outlines the 4 with other colors and 0x50d374 dresses the 1 with the
    // Angel in Chaos Castle.
    std::uint8_t byte88 = 0;
    // +0x89 and +0x8a (a short): the pulse of Kundun's 0x62d (0x192) in action 5 (0x43c7bb..0x43c8e3), which sets +0x8a
    // to 0 outside it. 0x511290 leaves them; the others that write +0x8a (0x4f9f6f, 0x4fdc10, 0x5ff9ec) are paths MuRet
    // does not run with a monster (*static*). 0x4fdc10 leaves 0x558eb0's chosen key in +0x8a (−1 without one) on each
    // step of the skill (CL9 milestone 3, phase 2b); 0x4fd5f0's skill cases read it (5 passes it to 0x518cf0 at
    // 0x4fdf6b), milestone 4.
    std::uint8_t byte89 = 0;
    std::int16_t word8a = 0;
    std::uint8_t controlCode = 0; // +0x1d8, the F3 03 reply's CtlCode
    // +0x1d9: the name. The F3 03 reply copies the own record's ten bytes there (0x4c47a2).
    std::array<std::byte, Protocol::FrontEnd::NameSize> name{};
    // +0x1a1: only 0x4d07c0's and 0x4d0a30's packets write it (CL9.5); in Chaos Castle, set on the hero, 0x5ffdf0
    // lowers the alpha of each object it poses (0x5ffe09).
    std::uint8_t byte1a1 = 0;
    std::uint8_t byte1e3 = 0; // +0x1e3: the F3 03 reply sets it to 0 (0x4c4844); what it is, unread
    // +0x1e4..+0x1f1: what a monster name longer than ten bytes overwrites after +0x1e3 (0x5141e0's unbounded strcpy
    // and the 1F's strcat, Q-C847). What the original keeps there, unread.
    std::array<std::byte, 14> nameSpill{};
    // +0x1f4: the character's key. 0x517270 sets the selection's place (0x5172a8) and the F3 03 reply the player's
    // (0x57678bc, 0x4c4516). 0x510f70 kills those without the hero's.
    std::uint16_t key = 0;
    std::uint8_t pk = 0; // +0x375: 3 from 0x511290, then the F3 03 reply's
    // Caminar: +0x3e8.., +0x39a/+0x39b (target, apart from the advanced cell), +0x388 and +0x3e5.
    Pathfinding::Path path;
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    std::uint8_t direction = 0;
    std::uint8_t byte369 = 0;
    // +0x36c: the 5D sets it to 0 (0x4e4348); what it is, unread in milestone 8.
    std::uint32_t dword36c = 0;
    std::uint8_t byte370 = 0;
    // +0x371: 0x511cc1 starts it at 0x43; 0x652940 and 0x6529e0 pick the siege skill.
    std::uint8_t byte371 = 0;
    // +0x372: 0x511290 sets it to 0 (0x511ccf), the state-1 0x12 keeps the previous record's (0x4c895f, 0x4c89ed) and
    // B8 01 (0x4e0700) writes pkt[4] into the hero's. 0x652330 (the siege skill on the bar, milestone 13) compares it
    // with a cap: *inferred*, the siege's kill count.
    std::uint8_t byte372 = 0;
    // +0x373: 0x511290 sets it to 0xff (0x511cc8); B8 01 with 0 returns it to the skill (+0x425) if not 0xff and leaves
    // 0xff. 0x652330 writes it too (milestone 13).
    std::uint8_t byte373 = 0;
    std::uint8_t byte374 = 0;
    // +0x42c: 0x511290 sets it to 1 (0x511c92); without it, 0x59e700's picking skips it (0x59e95e). On map 30,
    // 0x42d890, 0x42da90 and 0x42dfe0 clear and set it on the siege's objects.
    std::uint8_t byte42c = 0;
    // +0x381: the active skill inhibits the standing facing (59b3e4). Kept by 511290.
    std::uint8_t byte381 = 0;
    std::uint16_t word1f2 = 0;
    std::uint16_t damage = 0; // +0x384
    // CL9 milestone 3 (attack, damage and death). +0x380 and +0x382: 0x16 without bit 15 sets 2 and the damage on the
    // hero (0x4cf2f9). +0x3a4: the target's place, as a WORD: 0x16 (on the hero) and the attack on arrival (0x5659ab)
    // write it; 0x505de0 looks at it with +0x380 at 2 (CharacterHead.h). +0x3a8..+0x3b0: the target's position on
    // hitting (0x5653ec). +0x38f: how many times it hit, which 0x4f4a10 raises and uses to alternate the action.
    // +0x391: the skill that killed it (0x4d0a7e) and +0x398, which the pushes' 0x17 sets to 0. Who else writes them,
    // unread. Arriving skills (CL9 milestone 4): +0x3a4 is the skill's target (0x4cc530 and 0x4cc290; 1E sets it to
    // 0xffff and +0x380 to 0, 0x4ce000), +0x38f also alternates 0x4cc530's skill 0x17 (even or odd) and 0x3a sets
    // +0x1bc to 0 (0x4cde26).
    std::uint8_t byte380 = 0;
    std::uint16_t word382 = 0;
    // o+0x8a: the chosen key 0x558eb0 leaves (−1 without one) on each step of the skill (0x4fdc10, CL9 milestone 3,
    // phase 2b). 0x4fd5f0's skill cases read it (0x2a passes it to 0x518cf0 at 0x4fdf6b and 5 at 0x500509, CL9
    // milestone 4).
    // o+0x8c and o+0x8e: the right-hand weapon (+0x2b8, MOVSX) − 0x212 and its level (+0x2ba), left by the attack
    // step's skills 0x29, 0x2a, 0x37 and 0x38 (0x4fdf2b, 0x4fdfb3, 0x4fdfe4 and 0x4fec30, CL9 milestone 4) for the
    // effects that read them through their owner (ModelEffect::itemType and itemLevel: 0x2a's 0xfa, 0x52d43a).
    std::int16_t word8c = 0;
    std::uint8_t byte8e = 0;
    std::uint16_t attackTarget = 0;
    glm::vec3 attackPosition{};
    std::uint8_t attackCount = 0;
    std::uint8_t deathSkill = 0;
    std::uint8_t byte398 = 0;
    // +0x30: the pushes' 0x17 copies the heading toward the killer into it (0x4d0b8f). What reads it, unread.
    float angle30 = 0.0f;
    // The flight of one who dies pushed (0x17 with 0x28 or 0x3b, 0x4d0ac1..0x4d0b38, and 0x4d07c0 in Blood Castle and
    // Chaos Castle), which 0x4fb9e0 moves with 0x51fd20 and 0x4fb2b0 (ActorDying.h): +0x1bc, +0xcc, +0xd0, +0xd4, +0xd8
    // and +0xe4, the push of +0x1a4..+0x1ac and the start position at +0x1b0..+0x1b8.
    std::uint8_t byte1bc = 0;
    // +0x1bd: the key of the last magic (0x57cf90, Skill57cf90.h) this character sent with the 1E: 0x57cfc0's branches
    // (the knight's 0x4c, 0x589238) and 0x518dcb write it, and 0x501c25 and 0x680aca read it, unported.
    std::uint8_t magicKey = 0;
    // +0x3c8: how long it has been dying, which 0x4fb9e0 raises by 0.02 (ActorDying.h). 0x511290 sets it to 0
    // (0x51247a).
    float dyingTime = 0.0f;
    // +0xcc..+0xe4: floatCC, floatD0, floatD4, floatD8 and floatE4, above.
    glm::vec3 push{};
    glm::vec3 pushOrigin{};
    std::uint8_t byte399 = 0;
    std::uint8_t skillIndex = 0;          // +0x425
    std::uint8_t firstFootstepSound = 0;  // +0x3e0, kept until the animation ends
    std::uint8_t secondFootstepSound = 0; // +0x3e1
    std::uint8_t byte80 = 0;              // +0x80
    std::uint8_t movementCooldown = 0;
    std::uint8_t movementType = 0;       // +0x379
    float movementPhase = 0.0f;          // +0x84
    std::uint16_t baseMovementSpeed = 0; // +0x386, 10 from 0x511290
    std::uint8_t movement = 0;           // +0x378, Caminar's Movement
    // The protected logical cell (+0x41c/+0x420), which MovePath advances. The server's target is targetX/Y
    // (+0x39a/b); 0x511290 initializes both pairs with the same value.
    std::uint8_t cellX = 0;
    std::uint8_t cellY = 0;
    // How many accesses hold +0x41c's and +0x420's entries open (Models::LogicalAccess).
    std::uint8_t cellXAccesses = 0;
    std::uint8_t cellYAccesses = 0;
    // The four bytes of +0x41c and +0x420 as 0x511290's last access leaves them: if its close left the count at 0,
    // 0x412890's rand() & 0xff (Q-C125); otherwise the plain cell. Only 0x511290 keeps them: the only reader that does
    // not open them, the 13 (0x4c9d01..0x4c9d36), reads them right after the creation (Q-C849).
    std::uint32_t cellXLeft = 0;
    std::uint32_t cellYLeft = 0;
    // Depth of the one-byte entry +0x38e, which the draw checks before each hand.
    std::uint8_t byte38eAccesses = 0;
    // +0x38e: the logical value. With ')' (0x29), 0x50ba30's hands loop skips the right one in action 0x3d, and with
    // '4' (0x34) 0x509340 sets the frame of what hangs to 3.2 in actions 0x22 to 0x97. 0x4cc530 and 0x4f5b70 write it,
    // unported, and the 1A (0x4ceee4, Scenes::ApplyMagicAttackCount).
    std::uint8_t byte38e = 0;
    // What the arriving skills write (CL9 milestone 4, Scenes/SkillReplies.h). What 0x511290 puts in each, unread:
    // they stay 0 until the first arrives.
    // +0x38d: whether this character's last skill succeeded: bit 15 of the target of 19 (0x4cc530) and 69 (0x4cc290).
    // 0x4cc530's branches 0x33, 0x37 and 0x44 to 0x48 look at it. The A8 sets it to 0 on the raven's object, not here.
    std::uint8_t skillSucceeded = 0;
    // +0x434: the place of skill 0x4c's target (0x4ccef1).
    std::int32_t skillTarget4c = 0;
    // +0x39c and +0x39d: the cell of the skill with a duration (1E, 0x4ce000).
    std::uint8_t skillCellX = 0;
    std::uint8_t skillCellY = 0;
    // +0x42e, protected like +0x38e (the close that leaves it without accesses spends a rand(), 0x40b3f0): the
    // monster's skill that 0x69 sends (0x4cc290, SkillReplies), which 0x4f92f0 looks up in MonsterSkill.bmd and leaves
    // at 0xff (ActorCombat). 0x511290 leaves it: a new pool slot has 0 and a used one what the previous left.
    std::uint8_t byte42eAccesses = 0;
    std::uint8_t byte42e = 0;
    // +0x390: 0x511290 sets it to 0 (0x51186b), and the quests' state sets it to 1 (0x418e20 and 0x418ee0, with quest 3
    // done, CL9.1), unported: it stays 0. With 1, the tooltip of a level 220+ Dark Knight's skill writes "Combo"
    // (0x5b99d1).
    std::uint8_t byte390 = 0;
    // +0x3e2, the safe zone: any nonzero byte. 0x511290 and 0x505de0 copy bit 0x01 of the cell's .att.
    std::uint8_t safeZone = 0;
    // +0x3e3: nonzero, 0x512a30 and 0x512930 leave the clothes. 0x511290 sets it to 0 (0x511d68).
    std::uint8_t byte3e3 = 0;
    // +0x3e4: 1 on the letters' portrait character (0x4959a1), and 0 on 0x5141e0's monsters (0x5143d1). Nonzero,
    // 0x50ba30 draws no shadow (0x50bc16) and does not run 0x505820 (0x50c913).
    std::uint8_t byte3e4 = 0;
};

// 0x414340: with a cape at +0x364, frees it and returns +0x360 to 0; without one, touches nothing.
inline void ReleaseTheLayer(ActorState& actor)
{
    if (actor.layer.has_value())
    {
        actor.layer.reset();
        actor.byte360 = 0;
    }
}
} // namespace Client::Kernel
