import { THREE } from "nicegui-scene";
import { average_of, hue_of } from "./tint.js";

const POSITION_WIDTH = 3;
const TRIANGLE = 3;
const UV_WIDTH = 2;
const QUATERNION_WIDTH = 4;
const ALPHA_TEST = 0.25;
const TEXTURED = 0xffffff;
const FLAT = 0x9a978e;
const TINT = 0x9aa6ff;
const WIRE = 0xb9bcc2;
const SKELETON = 0xf2f1ec;
const MARK = 0x9aa6ff;
const MARK_SHARE = 0.012;
const NOTHING_MEASURED = { low: [-50, -50, 0], high: [50, 50, 100] };

export const MODES = ["textured", "lit", "wire"];

export function asked(route, params) {
  return fetch(`${route}?${new URLSearchParams(params)}`);
}

export async function texture_of(response, anisotropy = 1) {
  const kind = response.headers.get("Content-Type") ?? "";
  if (!response.ok) return null;
  let texture = null;
  if (kind.startsWith("image/jpeg")) {
    texture = new THREE.Texture(await createImageBitmap(await response.blob()));
  } else if (kind.startsWith("application/octet-stream")) {
    const width = Number(response.headers.get("X-Width"));
    const height = Number(response.headers.get("X-Height"));
    texture = new THREE.DataTexture(new Uint8Array(await response.arrayBuffer()), width, height, THREE.RGBAFormat);
    texture.generateMipmaps = true;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.magFilter = THREE.LinearFilter;
  }
  if (!texture) return null;
  texture.flipY = false;
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
  texture.anisotropy = anisotropy;
  texture.needsUpdate = true;
  return texture;
}

export class Rig {
  root = new THREE.Group();
  bones = [];
  parts = [];
  clips = new Map();
  current = null;
  wanted = null;
  helper = null;
  marker = null;
  view = "textured";
  blend = false;
  chosen = null;
  colors = [];
  disposed = false;

  constructor({ anisotropy = 1, changed = () => {} } = {}) {
    this.anisotropy = anisotropy;
    this.changed = changed;
  }

  async load(file, on) {
    const scene = await (await asked("/api/scene", { file, on })).json();
    if (!scene.bones) throw new Error(scene.message ?? "The scene was not answered");
    this.scene = scene;
    this.state = {
      ready: false,
      file: scene.file,
      skeleton: scene.skeleton,
      bones: scene.bones.length,
      meshes: scene.meshes.length,
      corners: scene.meshes.reduce((corners, mesh) => corners + mesh.bones.length, 0),
      textures: { shown: 0, flat: 0 },
      action: null,
      keys: 0,
      key: 0,
      playing: false,
      duration: 0,
      bounds: null,
    };
    const { low, high } = scene.bounds ?? NOTHING_MEASURED;
    this.low = low;
    this.high = high;
    this.size = Math.hypot(...high.map((value, axis) => value - low[axis]));
    this.add_bones(scene.bones);
    this.parts = scene.meshes.map((mesh) => this.add_part(mesh));
    this.mixer = new THREE.AnimationMixer(this.root);
    if (scene.actions.length) {
      await this.play(0);
      this.pause();
    }
    this.state.bounds = this.measured();
    return this;
  }

  add_bones(bones) {
    this.bones = bones.map((_, index) => Object.assign(new THREE.Bone(), { name: `bone${index}` }));
    bones.forEach((bone, index) => (this.bones[bone.parent] ?? this.root).add(this.bones[index]));
    this.skinned = new THREE.Skeleton(
      this.bones,
      this.bones.map(() => new THREE.Matrix4()),
    );
  }

  add_part(mesh) {
    const corners = mesh.bones.length;
    const geometry = new THREE.BufferGeometry();
    const skin = new Uint16Array(corners * 4);
    const weight = new Float32Array(corners * 4);
    mesh.bones.forEach((bone, corner) => {
      skin[corner * 4] = bone;
      weight[corner * 4] = 1;
    });
    geometry.setAttribute("position", new THREE.Float32BufferAttribute(mesh.positions, POSITION_WIDTH));
    geometry.setAttribute("normal", new THREE.Float32BufferAttribute(mesh.normals, POSITION_WIDTH));
    geometry.setAttribute("uv", new THREE.Float32BufferAttribute(mesh.uvs, UV_WIDTH));
    geometry.setAttribute("skinIndex", new THREE.BufferAttribute(skin, 4));
    geometry.setAttribute("skinWeight", new THREE.BufferAttribute(weight, 4));
    const part = new THREE.SkinnedMesh(geometry, this.material_for("textured", null));
    part.frustumCulled = false;
    part.userData = { base: FLAT, map: null, materials: { textured: part.material } };
    this.root.add(part);
    part.bind(this.skinned, new THREE.Matrix4());
    return part;
  }

  material_for(view, map) {
    const shared = { side: THREE.DoubleSide, alphaTest: ALPHA_TEST, fog: false };
    if (view === "lit") return new THREE.MeshLambertMaterial({ ...shared, map, color: map ? TEXTURED : FLAT });
    if (view === "wire") return new THREE.MeshBasicMaterial({ ...shared, color: WIRE, wireframe: true });
    return new THREE.MeshBasicMaterial({ ...shared, map, color: map ? TEXTURED : FLAT });
  }

  async add_textures() {
    const drawn = this.parts.map((part, index) => [part, index]).filter(([part]) => part.geometry.attributes.position.count);
    await Promise.all(
      drawn.map(async ([part, index]) => {
        const texture = await asked("/api/texture", { file: this.scene.file, mesh: index })
          .then((response) => texture_of(response, this.anisotropy))
          .catch(() => null);
        if (this.disposed) {
          texture?.dispose();
          return;
        }
        this.state.textures[texture ? "shown" : "flat"] += 1;
        if (!texture) return;
        const rgb = average_of(texture.image);
        if (rgb) this.colors.push({ rgb, weight: part.geometry.attributes.position.count / TRIANGLE });
        part.userData.base = TEXTURED;
        part.userData.map = texture;
        for (const [view, material] of Object.entries(part.userData.materials)) {
          if (view === "wire") continue;
          material.map = texture;
          material.color.set(TEXTURED);
          material.needsUpdate = true;
        }
        this.paint(part, this.parts.indexOf(part));
        this.changed();
      }),
    );
    this.state.ready = true;
    this.changed();
  }

  get tint() {
    return hue_of(this.colors);
  }

  framed() {
    const measured = this.measured() ?? this.bone_bounds();
    return measured ?? { low: this.low, high: this.high };
  }

  bone_bounds() {
    if (!this.bones.length) return null;
    this.root.updateMatrixWorld(true);
    const box = new THREE.Box3();
    const point = new THREE.Vector3();
    for (const bone of this.bones) box.expandByPoint(bone.getWorldPosition(point));
    return box.isEmpty() ? null : { low: box.min.toArray(), high: box.max.toArray() };
  }

  measured() {
    this.root.updateMatrixWorld(true);
    const box = new THREE.Box3();
    for (const part of this.parts) {
      if (!part.geometry.attributes.position.count) continue;
      part.computeBoundingBox();
      box.union(part.boundingBox);
    }
    return box.isEmpty() ? null : { low: box.min.toArray(), high: box.max.toArray() };
  }

  clip(action, repeats) {
    const name = `${action}${repeats ? "" : " once"}`;
    if (!this.clips.has(name)) this.clips.set(name, this.asked_clip(action, repeats));
    return this.clips.get(name);
  }

  async asked_clip(action, repeats) {
    const poses = await (await asked("/api/poses", { file: this.scene.skeleton, action })).json();
    const keys = poses.keys ?? 0;
    const loops = repeats && (!this.scene.actions[action].lock || keys < 2);
    const times = Array.from({ length: keys + (loops ? 1 : 0) }, (_, key) => key);
    const looped = (values, width) => (loops ? [...values, ...values.slice(0, width)] : values);
    const tracks = [];
    (poses.bones ?? []).forEach((pose, index) => {
      if (!pose || !keys) return;
      if (pose.positions.length !== keys * POSITION_WIDTH || pose.quaternions.length !== keys * QUATERNION_WIDTH) return;
      const name = this.bones[index].name;
      tracks.push(new THREE.VectorKeyframeTrack(`${name}.position`, times, looped(pose.positions, POSITION_WIDTH)));
      tracks.push(
        new THREE.QuaternionKeyframeTrack(`${name}.quaternion`, times, looped(pose.quaternions, QUATERNION_WIDTH)),
      );
    });
    return { clip: new THREE.AnimationClip(`action${action}`, Math.max(times.at(-1) ?? 0, 1), tracks), keys };
  }

  async play(action, repeats = true) {
    if (!this.scene.actions[action]) return;
    this.wanted = action;
    const { clip, keys } = await this.clip(action, repeats);
    if (this.wanted !== action || this.disposed) return;
    this.current?.stop();
    this.current = this.mixer.clipAction(clip);
    this.current.setLoop(repeats ? THREE.LoopRepeat : THREE.LoopOnce, Infinity);
    this.current.clampWhenFinished = !repeats;
    this.current.reset().play();
    this.mixer.update(0);
    Object.assign(this.state, { action, keys, key: 0, playing: true, duration: clip.duration });
    this.changed();
  }

  async replay(action, actions) {
    this.scene.actions = actions;
    const names = [...this.clips.keys()].filter((name) => action === null || name.split(" ")[0] === `${action}`);
    const forgotten = names.map((name) => this.clips.get(name));
    names.forEach((name) => this.clips.delete(name));
    const shown = this.state.action;
    if (shown !== null && (action === null || shown === action) && actions[shown]) {
      const key = this.state.key;
      await this.play(shown, this.current?.loop !== THREE.LoopOnce);
      this.pause();
      this.seek(Math.min(key, Math.max(this.state.keys - 1, 0)));
    }
    for (const clip of await Promise.all(forgotten)) this.mixer.uncacheClip(clip.clip);
  }

  get playing() {
    return Boolean(this.current) && !this.current.paused;
  }

  pause() {
    if (this.current) this.current.paused = true;
    this.report();
    this.changed();
    return this.state.key;
  }

  resume() {
    if (!this.current) return;
    if (this.current.loop === THREE.LoopOnce && this.current.time >= this.current.getClip().duration) this.current.reset();
    this.current.paused = false;
    this.report();
    this.changed();
  }

  seek(key) {
    if (!this.current) return;
    this.current.time = Math.min(Math.max(key, 0), this.current.getClip().duration);
    this.mixer.update(0);
    this.report();
    this.changed();
  }

  step(keys) {
    if (!this.current) return;
    const duration = this.current.getClip().duration;
    const next = Math.round(this.current.time) + keys;
    this.seek(((next % duration) + duration) % duration);
  }

  speed(keys_per_second) {
    this.mixer.timeScale = keys_per_second;
  }

  update(seconds) {
    this.mixer?.update(seconds);
    this.report();
  }

  show(view) {
    this.view = view;
    this.parts.forEach((part, index) => {
      const kept = part.userData.materials;
      kept[view] ??= this.material_for(view, part.userData.map);
      part.material = kept[view];
      this.paint(part, index);
    });
    this.additive(this.blend);
    this.changed();
  }

  skeleton(shown) {
    if (shown && !this.helper) {
      this.helper = new THREE.SkeletonHelper(this.root);
      this.helper.material.vertexColors = false;
      this.helper.material.color?.set(SKELETON);
      this.root.add(this.helper);
    }
    if (this.helper) this.helper.visible = shown;
    this.changed();
  }

  additive(on) {
    this.blend = on;
    for (const part of this.parts) {
      part.material.blending = on ? THREE.AdditiveBlending : THREE.NormalBlending;
      part.material.depthWrite = !on;
      part.material.transparent = on;
      part.material.needsUpdate = true;
    }
    this.changed();
  }

  paint(part, index) {
    if (this.view === "wire") part.material.color.set(index === this.chosen ? TINT : WIRE);
    else part.material.color.set(index === this.chosen ? TINT : part.userData.base);
  }

  highlight(mesh) {
    this.chosen = mesh;
    this.parts.forEach((part, index) => this.paint(part, index));
    this.changed();
  }

  mark_bone(bone) {
    if (this.marker) {
      this.marker.removeFromParent();
      this.marker.geometry.dispose();
      this.marker.material.dispose();
    }
    this.marker = null;
    if (this.bones[bone]) {
      const radius = Math.max(this.size * MARK_SHARE, 0.5);
      const material = new THREE.MeshBasicMaterial({ color: MARK, depthTest: false });
      this.marker = new THREE.Mesh(new THREE.SphereGeometry(radius, 12, 8), material);
      this.marker.renderOrder = 1;
      this.bones[bone].add(this.marker);
    }
    this.changed();
  }

  report() {
    if (!this.state) return;
    const keys = Math.max(this.state.keys, 1);
    this.state.key = this.current ? Math.floor(this.current.time + 1e-6) % keys : 0;
    this.state.playing = this.playing;
  }

  dispose() {
    this.disposed = true;
    this.current?.stop();
    this.mixer?.stopAllAction();
    this.mixer?.uncacheRoot(this.root);
    this.mark_bone(null);
    this.helper?.dispose();
    for (const part of this.parts) {
      part.geometry.dispose();
      for (const material of Object.values(part.userData.materials)) material.dispose();
      part.userData.map?.dispose();
    }
    this.skinned?.dispose();
    this.root.removeFromParent();
  }
}
