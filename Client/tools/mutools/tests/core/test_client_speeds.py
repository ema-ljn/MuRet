import re
from pathlib import Path

from mutools.core.client_speeds import MONSTER_COMMON_SPEEDS, NPC_MODELS, PLAYER_SPEED_WRITES

GAME = Path(__file__).resolve().parents[4] / "src" / "game"
STORE = re.compile(r"\{(\d+), (0x[0-9a-f]+)\}")


def test_the_player_writes_match_the_clients_table() -> None:
    source = (GAME / "Tables" / "PlayerActions.cpp").read_text(encoding="utf-8")

    writes = [
        (int(first), int(last), float(speed))
        for first, last, speed in re.findall(r"\{(\d+), (\d+), ([0-9.]+)f\},\s*// 0x", source)
    ]

    assert writes == [(write.first, write.last, write.speed) for write in PLAYER_SPEED_WRITES]


def test_the_common_monster_speeds_match_the_clients_table() -> None:
    source = (GAME / "Assets" / "MonsterModelTable.cpp").read_text(encoding="utf-8")
    common = source.split("CommonSpeeds{{")[1].split("}};")[0]

    assert [(int(action), int(bits, 16)) for action, bits in STORE.findall(common)] == list(MONSTER_COMMON_SPEEDS)


def test_every_npc_model_is_loaded_by_the_client() -> None:
    source = (GAME / "Assets" / "MonsterModelTable.cpp").read_text(encoding="utf-8")

    for model, _, _ in NPC_MODELS:
        assert f"constexpr NpcModelOp Npc{model:x}[]" in source
