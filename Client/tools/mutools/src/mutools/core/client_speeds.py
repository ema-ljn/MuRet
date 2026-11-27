from dataclasses import dataclass
from typing import Final


@dataclass(frozen=True, slots=True)
class SpeedWrite:
    first: int
    last: int
    speed: float


STEPS_PER_SECOND: Final = 25
# 0x60dbe0, in its order: a later write wins (Client/src/game/Tables/PlayerActions.cpp).
PLAYER_SPEED_WRITES: Final = (
    SpeedWrite(1, 12, 0.28),
    SpeedWrite(3, 3, 0.26),
    SpeedWrite(4, 4, 0.24),
    SpeedWrite(5, 5, 0.24),
    SpeedWrite(7, 7, 0.22),
    SpeedWrite(8, 8, 0.22),
    SpeedWrite(13, 33, 0.3),
    SpeedWrite(21, 21, 0.35),
    SpeedWrite(29, 29, 0.35),
    SpeedWrite(152, 194, 0.32),
    SpeedWrite(195, 196, 0.45),
    SpeedWrite(197, 204, 0.4),
    SpeedWrite(194, 194, 0.4),
    SpeedWrite(171, 171, 0.28),
    SpeedWrite(172, 172, 0.28),
    SpeedWrite(201, 201, 0.2),
    SpeedWrite(202, 202, 0.2),
    SpeedWrite(64, 64, 0.2),
    SpeedWrite(65, 65, 0.2),
    SpeedWrite(138, 138, 0.24),
    SpeedWrite(139, 139, 0.3),
    SpeedWrite(140, 140, 0.3),
    SpeedWrite(141, 141, 0.24),
    SpeedWrite(67, 67, 0.45),
    SpeedWrite(72, 72, 0.3),
    SpeedWrite(73, 73, 0.3),
    SpeedWrite(74, 74, 0.3),
    SpeedWrite(75, 75, 0.3),
    SpeedWrite(76, 76, 0.2),
    SpeedWrite(77, 77, 0.28),
    SpeedWrite(78, 78, 0.3),
    SpeedWrite(79, 79, 0.3),
    SpeedWrite(80, 80, 0.28),
    SpeedWrite(81, 81, 0.3),
    SpeedWrite(82, 82, 0.3),
    SpeedWrite(83, 83, 0.2),
    SpeedWrite(84, 84, 1.0),
    SpeedWrite(85, 85, 1.0),
    SpeedWrite(86, 125, 0.45),
    SpeedWrite(106, 117, 0.71),
    SpeedWrite(118, 121, 0.4),
    SpeedWrite(93, 93, 0.3),
    SpeedWrite(88, 88, 0.3),
    SpeedWrite(90, 90, 0.3),
    SpeedWrite(89, 89, 0.28),
    SpeedWrite(91, 91, 0.3),
    SpeedWrite(137, 137, 0.3),
)
PLAYER_ACTIONS_ONCE: Final = frozenset({195, 196, 193, 68})

# 0x612d90 (Client/src/game/Assets/MonsterModelTable.cpp): speeds as the bits of their floats.
FIRST_MONSTER_MODEL: Final = 0x152
MONSTER_COMMON_SPEEDS: Final = (
    (0, 0x3E800000),
    (1, 0x3E4CCCCD),
    (2, 0x3EAE147B),
    (3, 0x3EA8F5C3),
    (4, 0x3EA8F5C3),
    (5, 0x3F000000),
    (6, 0x3F0CCCCD),
)
MONSTER_ACTION_ONCE: Final = 6
MONSTER_SCALED_ACTIONS: Final = 6
MONSTER_SPEED_FACTORS: Final = {0x3: 1.2, 0x5: 0.7, 0x19: 0.7, 0x25: 0.4, 0x2A: 0.4}
MONSTER_EARLY_SPEEDS: Final = {
    0x2: ((2, 0x3F333333),),
    0x6: ((2, 0x3F19999A),),
    0x8: ((2, 0x3F333333),),
    0x9: ((2, 0x3F99999A),),
    0xA: ((2, 0x3E8F5C29),),
    0xC: ((2, 0x3E99999A),),
    0xD: ((2, 0x3E8F5C29),),
    0x11: ((2, 0x3F000000),),
    0x13: ((2, 0x3F19999A),),
    0x14: ((2, 0x3ECCCCCD),),
    0x15: ((2, 0x3F000000),),
    0x1C: ((2, 0x3E99999A),),
    0x27: ((2, 0x3E6147AE),),
    0x29: ((2, 0x3E3851EC),),
    0x2A: ((3, 0x3EB33333), (4, 0x3EB33333)),
    0x2C: ((6, 0x3E99999A),),
    0x3F: ((6, 0x3DCCCCCD),),
    0x40: ((3, 0x3E99999A), (4, 0x3E800000), (5, 0x3E19999A), (6, 0x3E800000)),
    0x42: ((6, 0x3DCCCCCD),),
    0x43: ((3, 0x3E4CCCCD), (4, 0x3E99999A)),
    0x45: ((6, 0x3DCCCCCD),),
    0x46: ((3, 0x3F000000), (4, 0x3F000000), (2, 0x3E99999A)),
    0x47: ((3, 0x3F000000), (4, 0x3F000000), (2, 0x3E99999A)),
    0x48: ((3, 0x3ECCCCCD), (4, 0x3ECCCCCD), (2, 0x3E99999A)),
    0x59: ((7, 0x3EA8F5C3), (8, 0x3EA8F5C3), (9, 0x3EA8F5C3)),
    0x5C: ((7, 0x3EA8F5C3), (8, 0x3EA8F5C3)),
    0x5E: ((2, 0x3E4CCCCD),),
    0x63: ((3, 0x3EBD70A4), (4, 0x3EBD70A4), (6, 0x3E19999A)),
    0x64: ((6, 0x3E4CCCCD),),
    0x65: ((6, 0x3E4CCCCD), (0, 0x3E19999A), (1, 0x3E19999A), (3, 0x3E6B851F), (4, 0x3E6B851F), (2, 0x3E99999A)),
    0x66: ((0, 0x3E19999A), (1, 0x3E19999A), (3, 0x3E6B851F), (4, 0x3E6B851F), (2, 0x3E99999A)),
    0x69: ((2, 0x3E19999A),),
}
MONSTER_FIRST_ACTION_ONCE: Final = frozenset({0xE, 0x1F, 0x27, 0x28})
MONSTER_CASE_SPEEDS: Final = {
    0xE: ((1, 0x3EB33333),),
    0x1F: ((3, 0x3F000000), (4, 0x3F333333), (1, 0x3F4CCCCD), (7, 0x3F4CCCCD)),
    0x20: ((8, 0x3ECCCCCD), (9, 0x3ECCCCCD), (7, 0x3ECCCCCD), (10, 0x3ECCCCCD)),
    0x22: ((3, 0x3F000000), (4, 0x3F000000)),
    0x25: ((3, 0x3E19999A), (4, 0x3E19999A), (6, 0x3E4CCCCD)),
    0x31: ((6, 0x3E6147AE),),
    0x32: ((6, 0x3E6147AE),),
    0x33: ((6, 0x3E6147AE),),
    0x34: ((6, 0x3E6147AE),),
    0x35: ((6, 0x3E6147AE),),
    0x36: ((6, 0x3E6147AE),),
    0x37: ((6, 0x3E6147AE),),
    0x38: ((6, 0x3E6147AE),),
    0x56: ((0, 0x3D4CCCCD), (1, 0x3D4CCCCD), (2, 0x3DCCCCCD)),
}

# 0x612270 loads these as NPC models and 0x612b4f sets all their actions to 0.25.
NPC_ACTION_SPEED: Final = 0.25
NPC_MODELS: Final = (
    (0x1CB, "Npc", "Female01.bmd"),
    (0x1CC, "Npc", "Man01.bmd"),
    (0x1CD, "Npc", "Girl01.bmd"),
    (0x1CE, "Npc", "Smith01.bmd"),
    (0x1CF, "Npc", "Wizard01.bmd"),
    (0x1D0, "Npc", "SnowMerchant01.bmd"),
    (0x1D1, "Npc", "SnowSmith01.bmd"),
    (0x1D2, "Npc", "SnowWizard01.bmd"),
    (0x1D3, "Npc", "ElfWizard01.bmd"),
    (0x1D4, "Npc", "ElfMerchant01.bmd"),
    (0x1D5, "Npc", "Master01.bmd"),
    (0x1D6, "Npc", "Storage01.bmd"),
    (0x1D7, "Npc", "Tournament01.bmd"),
    (0x1D8, "Npc", "MixNpc01.bmd"),
    (0x1D9, "Npc", "DevilNpc01.bmd"),
    (0x1F2, "Npc", "Sevina01.bmd"),
    (0x1F3, "Npc", "BloodCastle01.bmd"),
    (0x1F4, "Npc", "BloodCastle02.bmd"),
    (0x1F5, "Npc", "DeviasTrader01.bmd"),
    (0x1F6, "Npc", "Breeder.bmd"),
    (0x1F8, "Npc", "Model_Npc_Catapult_Att.bmd"),
    (0x1F9, "Npc", "Model_Npc_Catapult_Def.bmd"),
    (0x1FA, "Npc", "NpcSenatus.bmd"),
    (0x1FB, "Npc", "NpcGateSwitch.bmd"),
    (0x1FC, "Npc", "NpcCrown.bmd"),
    (0x1FD, "Npc", "NpcCheckFloor.bmd"),
    (0x1FE, "Npc", "NpcClerk.bmd"),
    (0x1FF, "Npc", "NpcBarrier.bmd"),
    (0x200, "Npc", "npc_mulyak.bmd"),
    (0x203, "Object35", "Object82.bmd"),
    (0x204, "Object35", "Object57.bmd"),
    (0x205, "Object35", "Object57.bmd"),
    (0x206, "Object35", "Object57.bmd"),
    (0x207, "Object35", "Object57.bmd"),
    (0x208, "Object35", "Object57.bmd"),
)
