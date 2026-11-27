from collections.abc import Sequence
from dataclasses import dataclass
from typing import Final

from mubmd.core.formats.model_textures import texture_use
from mubmd.core.formats.model_view import MODEL_FORMAT_NAME
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.leaves import leaf_text
from mubmd.core.pointer import format_pointer, parse_pointer

GROUPS: Final = {"meshes": "mesh", "actions": "action", "bones": "bone"}
MESH_FIELDS: Final = ("texture_name", "texture")
MESH_COUNTS: Final = ("vertices", "normals", "uvs", "triangles")
BONE_FIELDS: Final = ("name", "parent")
PART_TOKENS: Final = len(("bones", "N"))
NO_PARENT: Final = -1
MODEL_TITLE: Final = "model"
RAW_NAME: Final = "⟨hex⟩"
NO_NAME: Final = "(no name)"
DUMMY_LABEL: Final = "(dummy)"


@dataclass(frozen=True, slots=True)
class Part:
    pointer: str
    label: str
    children: tuple["Part", ...] = ()


def counted(count: int, one: str, many: str | None = None) -> str:
    return f"{count} {one if count == 1 else many or f'{one}s'}"


def is_model(view: JsonObject) -> bool:
    return view.get("format") == MODEL_FORMAT_NAME


def _items(view: JsonObject, group: str) -> list[JsonValue]:
    items = view.get(group)
    return items if isinstance(items, list) else []


def _size(item: JsonObject, key: str) -> int:
    value = item.get(key)
    return len(value) if isinstance(value, list) else 0


def _name(value: JsonValue) -> str:
    text = leaf_text(value)
    return RAW_NAME if text is None else text or NO_NAME


def _mesh_label(mesh: JsonObject) -> str:
    words = [
        _name(mesh.get("texture_name")),
        counted(_size(mesh, "vertices"), "vertex", "vertices"),
        counted(_size(mesh, "triangles"), "triangle"),
    ]
    text = leaf_text(mesh.get("texture_name"))
    if text is not None:
        use = texture_use(text)
        words += [word for word in (use.slot, use.script) if word]
    return " · ".join(words)


def _action_label(action: JsonObject) -> str:
    keys = action.get("keys")
    label = counted(keys if isinstance(keys, int) else 0, "key")
    return f"{label} · locked" if action.get("lock") else label


def _bone_label(bone: JsonObject) -> str:
    return DUMMY_LABEL if "dummy" in bone else _name(bone.get("name"))


def _part(group: str, index: int, label: str, children: tuple[Part, ...] = ()) -> Part:
    return Part(f"/{group}/{index}", f"{index}  {label}", children)


def _parents(bones: Sequence[JsonValue]) -> list[int | None]:
    parents: list[int | None] = []
    for bone in bones:
        parent = bone.get("parent") if isinstance(bone, dict) and "dummy" not in bone else None
        parents.append(parent if isinstance(parent, int) else None)
    return parents


def _reach_the_root(parents: Sequence[int | None]) -> list[bool]:
    known: dict[int, bool] = {}
    for start in range(len(parents)):
        path: list[int] = []
        index: int | None = start
        while index != NO_PARENT and index not in known:
            if index is None or not 0 <= index < len(parents) or index in path:
                break
            path.append(index)
            index = parents[index]
        reached = index == NO_PARENT or (index is not None and known.get(index, False))
        known.update(dict.fromkeys(path, reached))
    return [known[index] for index in range(len(parents))]


def _bone_parts(bones: Sequence[JsonValue]) -> tuple[Part, ...]:
    parents = _parents(bones)
    rooted = _reach_the_root(parents)
    under: dict[int, list[int]] = {NO_PARENT: []}
    for index, parent in enumerate(parents):
        holder = parent if parent is not None and 0 <= parent < len(bones) and rooted[parent] else NO_PARENT
        under.setdefault(holder, []).append(index)
    order: list[int] = []
    pending = list(reversed(under[NO_PARENT]))
    while pending:
        index = pending.pop()
        order.append(index)
        pending.extend(reversed(under.get(index, ())))
    built: dict[int, Part] = {}
    for index in reversed(order):
        bone = bones[index]
        label = _bone_label(bone) if isinstance(bone, dict) else RAW_NAME
        built[index] = _part("bones", index, label, tuple(built[child] for child in under.get(index, ())))
    return tuple(built[index] for index in under[NO_PARENT])


def _group(name: str, parts: tuple[Part, ...], count: int) -> Part:
    return Part(f"/{name}", f"{name} ({count})", parts)


def model_parts(view: JsonObject) -> Part:
    meshes, actions, bones = (_items(view, group) for group in GROUPS)
    mesh_parts = tuple(
        _part("meshes", index, _mesh_label(mesh)) for index, mesh in enumerate(meshes) if isinstance(mesh, dict)
    )
    action_parts = tuple(
        _part("actions", index, _action_label(action))
        for index, action in enumerate(actions)
        if isinstance(action, dict)
    )
    groups = (
        _group("meshes", mesh_parts, len(meshes)),
        _group("actions", action_parts, len(actions)),
        _group("bones", _bone_parts(bones), len(bones)),
    )
    return Part("", _name(view.get("name")), groups)


def part_of(pointer: str) -> str:
    tokens = parse_pointer(pointer)
    return format_pointer(tokens[:PART_TOKENS]) if tokens and tokens[0] in GROUPS else ""


def part_title(part: str) -> str:
    tokens = parse_pointer(part)
    if not tokens:
        return MODEL_TITLE
    return tokens[0] if len(tokens) == 1 else f"{GROUPS[tokens[0]]} {tokens[1]}"


def _item(view: JsonObject, part: str) -> tuple[str, JsonObject] | None:
    tokens = parse_pointer(part)
    if len(tokens) != PART_TOKENS or tokens[0] not in GROUPS or not tokens[1].isdecimal():
        return None
    items = _items(view, tokens[0])
    index = int(tokens[1])
    item = items[index] if index < len(items) else None
    return (tokens[0], item) if isinstance(item, dict) else None


def part_fields(view: JsonObject, pointer: str) -> JsonObject:
    if pointer == "":
        return {"name": view["name"]} if "name" in view else {}
    found = _item(view, pointer)
    if found is None:
        return {}
    group, item = found
    if group == "meshes":
        return {key: item[key] for key in MESH_FIELDS if key in item}
    if group == "bones" and "dummy" not in item:
        return {key: item[key] for key in BONE_FIELDS if key in item}
    return {}


def _mesh_facts(mesh: JsonObject) -> tuple[tuple[str, str], ...]:
    facts = [(key, str(_size(mesh, key))) for key in MESH_COUNTS]
    text = leaf_text(mesh.get("texture_name"))
    if text is not None:
        use = texture_use(text)
        facts.append(("file", use.file))
        facts += [(label, word) for label, word in (("slot", use.slot), ("script", use.script)) if word]
    return tuple(facts)


def part_facts(view: JsonObject, pointer: str) -> tuple[tuple[str, str], ...]:
    if pointer == "":
        counts = tuple((group, str(len(_items(view, group)))) for group in GROUPS)
        return (("version", str(view.get("version"))), *counts)
    found = _item(view, pointer)
    if found is None:
        return ()
    group, item = found
    if group == "meshes":
        return _mesh_facts(item)
    if group == "actions":
        return tuple((key, str(item[key])) for key in ("keys", "lock") if key in item)
    if "dummy" in item:
        return (("dummy", str(item["dummy"])),)
    return (("actions", str(_size(item, "actions"))),)
