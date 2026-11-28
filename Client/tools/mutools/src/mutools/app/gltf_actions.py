from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from mutools.app.action_speeds import action_speeds
from mutools.app.actions import ActionEdit, open_model
from mutools.app.documents import save_view
from mutools.app.gltf_animation import animation_action, chosen_animation
from mutools.app.gltf_rig import JPEG, PNG, ChosenAction, Rig, RigImage, SkinnedMesh, model_rig
from mutools.app.model_scene import borrows_skeleton, model_scene
from mutools.app.textures import mesh_texture
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import checked_actions, read_action_file, with_action
from mutools.core.formats.record_fields import invalid
from mutools.core.formats.texture_images import RgbaImage, png_of
from mutools.core.json_types import JsonObject
from mutools.core.ports import FileStore

GLB_SUFFIX: Final = ".glb"


@dataclass(frozen=True, slots=True)
class Untextured:
    model: Path
    mesh: int
    reason: str


@dataclass(frozen=True, slots=True)
class GltfExport:
    output: Path
    dry_run: bool
    joints: int
    meshes: int
    animations: int
    untextured: tuple[Untextured, ...]
    size: int


@dataclass(frozen=True, slots=True)
class GltfImport:
    animation: str
    keys: int
    edit: ActionEdit


def _not_glb(path: Path, message: str) -> None:
    if path.suffix.casefold() != GLB_SUFFIX:
        raise MutoolsError(ErrorCode.USAGE, message)


def _count(view: JsonObject) -> int:
    actions = view["actions"]
    assert isinstance(actions, list)
    return len(actions)


def _image(store: FileStore, model: Path, view: JsonObject, mesh: int) -> RigImage:
    content = mesh_texture(store, model, view, mesh).content
    return RigImage(png_of(content), PNG) if isinstance(content, RgbaImage) else RigImage(content, JPEG)


def _skinned(
    store: FileStore, model: Path, view: JsonObject, skeleton: JsonObject | None
) -> tuple[list[SkinnedMesh], list[Untextured]]:
    meshes: list[SkinnedMesh] = []
    untextured: list[Untextured] = []
    for index, mesh in enumerate(model_scene(view, skeleton).meshes):
        if not mesh.bones:
            continue
        image = None
        try:
            image = _image(store, model, view, index)
        except MutoolsError as error:
            untextured.append(Untextured(model, index, error.message))
        meshes.append(SkinnedMesh(f"{model.stem} {index}", mesh, image))
    return meshes, untextured


def _part_view(store: FileStore, part: Path, skeleton: JsonObject, model: Path) -> JsonObject:
    view = open_model(store, part).view
    if not borrows_skeleton(view, skeleton):
        raise invalid(
            f"{part.name} does not hang from {model.name}'s skeleton: the bones its meshes use are not the same",
            "/bones",
        )
    return view


def export_gltf(
    store: FileStore,
    write: Callable[[Rig], bytes],
    path: Path,
    output: Path,
    *,
    parts: Sequence[Path] = (),
    actions: Sequence[int] | None = None,
    dry_run: bool,
) -> GltfExport:
    _not_glb(output, f"mutools writes glTF binary files: give --output a {GLB_SUFFIX} name, not {output.name}")
    view = open_model(store, path).view
    count = _count(view)
    chosen = range(count) if actions is None else actions
    for action in chosen:
        checked_actions(view, action)
    meshes, untextured = _skinned(store, path, view, None)
    for part in parts:
        part_meshes, part_untextured = _skinned(store, part, _part_view(store, part, view, path), view)
        meshes += part_meshes
        untextured += part_untextured
    names = action_speeds(f"{path.parent.name}/{path.name}", count).names
    named = [ChosenAction(action, (names[action] or f"action {action}").capitalize()) for action in chosen]
    rig = model_rig(view, meshes, named)
    data = write(rig)
    if not dry_run:
        store.write(output, data, expected_sha256=store.sha256(output) if store.exists(output) else None)
    return GltfExport(
        output, dry_run, len(rig.joints), len(rig.meshes), len(rig.animations), tuple(untextured), len(data)
    )


def import_gltf(
    store: FileStore,
    read: Callable[[bytes], Rig],
    path: Path,
    source: Path,
    *,
    animation: str | None = None,
    at: int | None = None,
    dry_run: bool,
) -> GltfImport:
    _not_glb(source, f"{source.name} is not a {GLB_SUFFIX}: in Blender, export the animation as glTF Binary (.glb)")
    opened = open_model(store, path)
    if at is not None:
        checked_actions(opened.view, at, "leave --at out to add the action after the last one")
    rig = read(store.read(source))
    index = chosen_animation(rig, animation, source.name)
    action = read_action_file(animation_action(rig, index, opened.view, at))
    view = with_action(opened.view, action, at)
    result = save_view(store, opened, view, dry_run=dry_run)
    keys = action["keys"]
    assert isinstance(keys, int)
    edit = ActionEdit(_count(opened.view) if at is None else at, _count(view), result)
    return GltfImport(rig.animations[index].name, keys, edit)
