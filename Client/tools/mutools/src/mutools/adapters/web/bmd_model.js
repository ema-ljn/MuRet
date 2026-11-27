import { THREE } from "nicegui-scene";

const POSITION_WIDTH = 3;
const UV_WIDTH = 2;
const QUATERNION_WIDTH = 4;
const ALPHA_TEST = 0.25;
const TEXTURED = 0xffffff;
const FLAT = 0x8c6a2f;
const TINT = 0xe6c76a;
const MARK = 0xd9822b;
const MARK_SHARE = 0.012;
const GRID_STEP = 100;
const GRID_DIVISIONS = 20;
const GRID_OPACITY = 0.2;
const NOTHING_MEASURED = { low: [-50, -50, 0], high: [50, 50, 100] };
const KEY_READOUT = ".mutools-key";

function asked(route, params) {
  return fetch(`${route}?${new URLSearchParams(params)}`);
}

async function texture_of(response) {
  const kind = response.headers.get("Content-Type") ?? "";
  if (!response.ok) return null;
  let texture = null;
  if (kind.startsWith("image/jpeg")) {
    texture = new THREE.Texture(await createImageBitmap(await response.blob()));
  } else if (kind.startsWith("application/octet-stream")) {
    const width = Number(response.headers.get("X-Width"));
    const height = Number(response.headers.get("X-Height"));
    texture = new THREE.DataTexture(new Uint8Array(await response.arrayBuffer()), width, height, THREE.RGBAFormat);
    texture.magFilter = THREE.LinearFilter;
  }
  if (!texture) return null;
  texture.flipY = false;
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
  texture.needsUpdate = true;
  return texture;
}

export default class BmdModel {
  root = new THREE.Group();
  bones = [];
  parts = [];
  clips = new Map();
  current = null;
  wanted = null;
  helper = null;
  marker = null;
  attached = false;

  async create_mesh(file, on) {
    const scene = await (await asked("/api/scene", { file, on })).json();
    if (!scene.bones) throw new Error(scene.message ?? "The scene was not answered");
    this.scene = scene;
    this.state = window.mutoolsViewer = {
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
    this.size = Math.hypot(...high.map((value, axis) => value - low[axis]));
    this.add_grid();
    this.add_bones(scene.bones);
    this.parts = scene.meshes.map((mesh) => this.add_part(mesh));
    this.mixer = new THREE.AnimationMixer(this.root);
    if (scene.actions.length) {
      await this.play(0);
      this.pause();
    }
    this.state.bounds = this.measured();
    if (!this.state.corners) this.skeleton(true);
    this.add_textures().then(() => (this.state.ready = true));
    return this.root;
  }

  created() {
    let last = performance.now();
    const tick = (now) => {
      this.attached ||= this.root.parent !== null;
      if (this.attached && this.root.parent === null) return;
      this.mixer.update((now - last) / 1000);
      last = now;
      this.report();
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  add_grid() {
    const side = Math.max(Math.ceil((this.size * 2) / GRID_STEP), 1) * GRID_STEP;
    const grid = new THREE.GridHelper(side, GRID_DIVISIONS);
    grid.material.transparent = true;
    grid.material.opacity = GRID_OPACITY;
    grid.rotateX(Math.PI / 2);
    this.root.add(grid);
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
    geometry.setAttribute("uv", new THREE.Float32BufferAttribute(mesh.uvs, UV_WIDTH));
    geometry.setAttribute("skinIndex", new THREE.BufferAttribute(skin, 4));
    geometry.setAttribute("skinWeight", new THREE.BufferAttribute(weight, 4));
    const material = new THREE.MeshBasicMaterial({ color: FLAT, side: THREE.DoubleSide, alphaTest: ALPHA_TEST });
    const part = new THREE.SkinnedMesh(geometry, material);
    part.frustumCulled = false;
    part.userData.base = FLAT;
    this.root.add(part);
    part.bind(this.skinned, new THREE.Matrix4());
    return part;
  }

  async add_textures() {
    const drawn = this.parts.map((part, index) => [part, index]).filter(([part]) => part.geometry.attributes.position.count);
    await Promise.all(
      drawn.map(async ([part, index]) => {
        const texture = await asked("/api/texture", { file: this.scene.file, mesh: index })
          .then(texture_of)
          .catch(() => null);
        this.state.textures[texture ? "shown" : "flat"] += 1;
        if (!texture) return;
        const tinted = part.material.color.getHex() !== part.userData.base;
        part.userData.base = TEXTURED;
        part.material.map = texture;
        if (!tinted) part.material.color.set(TEXTURED);
        part.material.needsUpdate = true;
      }),
    );
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

  clip(action) {
    if (!this.clips.has(action)) this.clips.set(action, this.asked_clip(action));
    return this.clips.get(action);
  }

  async asked_clip(action) {
    const poses = await (await asked("/api/poses", { file: this.scene.skeleton, action })).json();
    const keys = poses.keys ?? 0;
    const loops = !this.scene.actions[action].lock || keys < 2;
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

  async play(action) {
    if (!this.scene.actions[action]) return;
    this.wanted = action;
    const { clip, keys } = await this.clip(action);
    if (this.wanted !== action) return;
    this.current?.stop();
    this.current = this.mixer.clipAction(clip);
    this.current.reset().play();
    this.mixer.update(0);
    Object.assign(this.state, { action, keys, key: 0, playing: true, duration: clip.duration });
  }

  pause() {
    if (this.current) this.current.paused = true;
    this.report();
    return this.state.key;
  }

  resume() {
    if (this.current) this.current.paused = false;
    this.report();
  }

  seek(key) {
    if (!this.current) return;
    this.current.time = Math.min(Math.max(key, 0), this.current.getClip().duration);
    this.mixer.update(0);
    this.report();
  }

  speed(keys_per_second) {
    this.mixer.timeScale = keys_per_second;
  }

  skeleton(shown) {
    if (shown && !this.helper) {
      this.helper = new THREE.SkeletonHelper(this.root);
      this.root.add(this.helper);
    }
    if (this.helper) this.helper.visible = shown;
  }

  additive(on) {
    for (const part of this.parts) {
      part.material.blending = on ? THREE.AdditiveBlending : THREE.NormalBlending;
      part.material.depthWrite = !on;
      part.material.transparent = on;
      part.material.needsUpdate = true;
    }
  }

  highlight(mesh) {
    this.parts.forEach((part, index) => part.material.color.set(index === mesh ? TINT : part.userData.base));
  }

  mark_bone(bone) {
    this.marker?.removeFromParent();
    this.marker = null;
    if (!this.bones[bone]) return;
    const radius = Math.max(this.size * MARK_SHARE, 0.5);
    const material = new THREE.MeshBasicMaterial({ color: MARK, depthTest: false });
    this.marker = new THREE.Mesh(new THREE.SphereGeometry(radius, 12, 8), material);
    this.marker.renderOrder = 1;
    this.bones[bone].add(this.marker);
  }

  report() {
    if (!this.state) return;
    const keys = Math.max(this.state.keys, 1);
    this.state.key = this.current ? Math.floor(this.current.time) % keys : 0;
    this.state.playing = Boolean(this.current) && !this.current.paused;
    const readout = document.querySelector(KEY_READOUT);
    const text = this.current ? `${this.state.key} / ${keys}` : "";
    if (readout && readout.textContent !== text) readout.textContent = text;
  }

  apply_material() {}
}
