import pytest

from mutools.app.action_speeds import action_speeds, monster_number, npc_model

PLAYER_ACTIONS = 205
MONSTER_ACTIONS = 7


def keys_per_second(file: str, actions: int) -> list[float | None]:
    return [speed.keys_per_second for speed in action_speeds(file, actions).speeds]


def repeats(file: str, actions: int) -> list[bool]:
    return [speed.repeats for speed in action_speeds(file, actions).speeds]


@pytest.mark.parametrize(
    ("file", "number"),
    [
        ("Monster/Monster01.bmd", 0),
        ("monster/MONSTER12.BMD", 11),
        ("Monster\\Monster04.bmd", 3),
        ("Monster/Monster00.bmd", None),
        ("Item/Monster01.bmd", None),
        ("Monster/Monster01a.bmd", None),
    ],
)
def test_a_monster_file_gives_its_number(file: str, number: int | None) -> None:
    assert monster_number(file) == number


def test_the_player_takes_the_later_write() -> None:
    speeds = keys_per_second("player/Player.bmd", PLAYER_ACTIONS)

    assert speeds[0] is None
    assert speeds[1:5] == pytest.approx([7.0, 7.0, 6.5, 6.0])
    assert speeds[194] == pytest.approx(10.0)


def test_some_player_actions_play_once() -> None:
    once = [action for action, again in enumerate(repeats("player/Player.bmd", PLAYER_ACTIONS)) if not again]

    assert once == [68, 193, 195, 196]


def test_a_monster_plays_the_common_speeds() -> None:
    speeds = action_speeds("Monster/Monster01.bmd", MONSTER_ACTIONS)

    assert (speeds.source, speeds.model) == ("monster", 0x152)
    assert speeds.names[:3] == ("stop 1", "stop 2", "walk")
    assert keys_per_second("Monster/Monster01.bmd", MONSTER_ACTIONS) == pytest.approx(
        [6.25, 5.0, 8.5, 8.25, 8.25, 12.5, 13.75]
    )
    assert repeats("Monster/Monster01.bmd", MONSTER_ACTIONS) == [True] * 6 + [False]


def test_a_factor_scales_the_first_six_actions_as_float32() -> None:
    speeds = keys_per_second("Monster/Monster04.bmd", MONSTER_ACTIONS)

    assert speeds[0] == 7.500000298023224
    assert speeds[6] == pytest.approx(13.75)


def test_some_monsters_play_their_second_action_once() -> None:
    assert repeats("Monster/Monster15.bmd", MONSTER_ACTIONS) == [True, False, True, True, True, True, False]


def test_an_npc_plays_every_action_at_a_quarter_key_a_step() -> None:
    speeds = action_speeds("NPC/BloodCastle01.bmd", 2)

    assert (speeds.source, speeds.model, npc_model("npc/bloodcastle01.bmd")) == ("npc", 0x1F3, 0x1F3)
    assert keys_per_second("NPC/BloodCastle01.bmd", 2) == [6.25, 6.25]


def test_a_model_the_client_does_not_name_has_no_speed() -> None:
    speeds = action_speeds("Item/Sword01.bmd", 1)

    assert (speeds.source, speeds.model, speeds.names) == ("unknown", None, (None,))
    assert keys_per_second("Item/Sword01.bmd", 1) == [None]
