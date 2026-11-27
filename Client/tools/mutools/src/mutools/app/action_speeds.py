import re
import struct
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Final, Literal

from mutools.core.client_speeds import (
    FIRST_MONSTER_MODEL,
    MONSTER_ACTION_ONCE,
    MONSTER_CASE_SPEEDS,
    MONSTER_COMMON_SPEEDS,
    MONSTER_EARLY_SPEEDS,
    MONSTER_FIRST_ACTION_ONCE,
    MONSTER_SCALED_ACTIONS,
    MONSTER_SPEED_FACTORS,
    NPC_ACTION_SPEED,
    NPC_MODELS,
    PLAYER_ACTIONS_ONCE,
    PLAYER_SPEED_WRITES,
    STEPS_PER_SECOND,
)

type Source = Literal["player", "monster", "npc", "unknown"]

PLAYER_FILE: Final = PurePosixPath("player", "player.bmd")
MONSTER_FOLDER: Final = "monster"
MONSTER_FILE: Final = re.compile(r"monster(\d+)\.bmd")
MONSTER_ACTION_NAMES: Final = ("stop 1", "stop 2", "walk", "attack 1", "attack 2", "shock", "die")
FIRST_ACTION: Final = 1


@dataclass(frozen=True, slots=True)
class ActionSpeed:
    keys_per_step: float | None
    repeats: bool

    @property
    def keys_per_second(self) -> float | None:
        return None if self.keys_per_step is None else self.keys_per_step * STEPS_PER_SECOND


@dataclass(frozen=True, slots=True)
class ActionSpeeds:
    source: Source
    model: int | None
    speeds: tuple[ActionSpeed, ...]
    names: tuple[str | None, ...]


def _as_float(bits: int) -> float:
    return float(struct.unpack("<f", struct.pack("<I", bits))[0])


def _float32(value: float) -> float:
    return float(struct.unpack("<f", struct.pack("<f", value))[0])


def _lowered(relative: str) -> PurePosixPath:
    return PurePosixPath(relative.replace("\\", "/").lower())


def monster_number(relative: str) -> int | None:
    path = _lowered(relative)
    found = MONSTER_FILE.fullmatch(path.name)
    if path.parent.name != MONSTER_FOLDER or found is None or int(found.group(1)) < FIRST_ACTION:
        return None
    return int(found.group(1)) - FIRST_ACTION


def npc_model(relative: str) -> int | None:
    path = _lowered(relative)
    for model, folder, file in NPC_MODELS:
        if (path.parent.name, path.name) == (folder.lower(), file.lower()):
            return model
    return None


def _player(actions: int) -> list[ActionSpeed]:
    speeds: list[float | None] = [None] * actions
    for write in PLAYER_SPEED_WRITES:
        for action in range(write.first, min(write.last, actions - 1) + 1):
            speeds[action] = write.speed
    return [ActionSpeed(speed, action not in PLAYER_ACTIONS_ONCE) for action, speed in enumerate(speeds)]


def _stored(speeds: list[float | None], stores: Iterable[tuple[int, int]]) -> None:
    for action, bits in stores:
        if action < len(speeds):
            speeds[action] = _as_float(bits)


def _monster(number: int, actions: int) -> list[ActionSpeed]:
    speeds: list[float | None] = [None] * actions
    _stored(speeds, MONSTER_COMMON_SPEEDS)
    factor = MONSTER_SPEED_FACTORS.get(number)
    if factor is not None:
        for action in range(min(MONSTER_SCALED_ACTIONS, actions)):
            speed = speeds[action]
            speeds[action] = None if speed is None else _float32(speed * factor)
    _stored(speeds, MONSTER_EARLY_SPEEDS.get(number, ()))
    _stored(speeds, MONSTER_CASE_SPEEDS.get(number, ()))
    once = {MONSTER_ACTION_ONCE} | ({FIRST_ACTION} if number in MONSTER_FIRST_ACTION_ONCE else set())
    return [ActionSpeed(speed, action not in once) for action, speed in enumerate(speeds)]


def action_speeds(relative: str, actions: int) -> ActionSpeeds:
    number = monster_number(relative)
    if number is not None:
        names = tuple(
            MONSTER_ACTION_NAMES[action] if action < len(MONSTER_ACTION_NAMES) else None for action in range(actions)
        )
        return ActionSpeeds("monster", FIRST_MONSTER_MODEL + number, tuple(_monster(number, actions)), names)
    unnamed = (None,) * actions
    if _lowered(relative) == PLAYER_FILE:
        return ActionSpeeds("player", None, tuple(_player(actions)), unnamed)
    model = npc_model(relative)
    if model is not None:
        return ActionSpeeds("npc", model, (ActionSpeed(NPC_ACTION_SPEED, repeats=True),) * actions, unnamed)
    return ActionSpeeds("unknown", None, (ActionSpeed(None, repeats=True),) * actions, unnamed)
