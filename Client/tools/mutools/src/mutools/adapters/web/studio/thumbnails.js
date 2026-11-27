import { THREE } from "nicegui-scene";
import { Rig } from "./rig.js";
import { aim } from "./stage.js";

const WIDTH = 256;
const HEIGHT = 208;
const FOV = 35;
const ROOM = 0.78;
const MOST_ANISOTROPY = 4;
const DATABASE = "mutools-thumbnails";
const STORE = "pictures";
const VERSION = 1;
const AHEAD = "240px";

function opened_database() {
  return new Promise((resolve) => {
    try {
      const request = indexedDB.open(DATABASE, VERSION);
      request.onupgradeneeded = () => request.result.createObjectStore(STORE);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => resolve(null);
      request.onblocked = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
}

function asked_store(database, mode, work) {
  return new Promise((resolve) => {
    if (!database) {
      resolve(null);
      return;
    }
    try {
      const request = work(database.transaction(STORE, mode).objectStore(STORE));
      request.onsuccess = () => resolve(request.result ?? null);
      request.onerror = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
}

class Thumbnails {
  queue = [];
  watched = new Map();
  shown = new Map();
  busy = false;
  drawer = undefined;
  database = null;

  observer() {
    this.watcher ??= new IntersectionObserver(
      (entries) => entries.filter((entry) => entry.isIntersecting).forEach((entry) => this.wanted(entry.target)),
      { rootMargin: AHEAD },
    );
    return this.watcher;
  }

  watch(element, file, stamp, shown) {
    const known = this.shown.get(`${file}@${stamp}`);
    if (known) {
      shown(known);
      return;
    }
    this.watched.set(element, { file, stamp, shown });
    this.observer().observe(element);
  }

  forget(element) {
    this.watched.delete(element);
    this.watcher?.unobserve(element);
    this.queue = this.queue.filter((queued) => queued !== element);
  }

  wanted(element) {
    this.observer().unobserve(element);
    if (!this.watched.has(element)) return;
    this.queue.push(element);
    this.next();
  }

  async next() {
    if (this.busy) return;
    const element = this.queue.shift();
    if (!element) return;
    this.busy = true;
    try {
      const watched = this.watched.get(element);
      this.watched.delete(element);
      if (watched) watched.shown((await this.picture(watched.file, watched.stamp)) ?? { url: null, tint: null, failed: true });
    } finally {
      this.busy = false;
      this.next();
    }
  }

  async picture(file, stamp) {
    const name = `${file}@${stamp}`;
    if (this.shown.has(name)) return this.shown.get(name);
    this.database ??= opened_database();
    const database = await this.database;
    let kept = await asked_store(database, "readonly", (store) => store.get(name));
    if (!kept) {
      kept = await this.drawn(file);
      if (!kept) return null;
      await asked_store(database, "readwrite", (store) => store.put(kept, name));
    }
    const known = { url: kept.picture ? URL.createObjectURL(kept.picture) : null, tint: kept.tint };
    this.shown.set(name, known);
    return known;
  }

  renderer() {
    if (this.drawer !== undefined) return this.drawer;
    try {
      this.drawer = new THREE.WebGLRenderer({
        canvas: document.createElement("canvas"),
        antialias: true,
        alpha: true,
        preserveDrawingBuffer: true,
        powerPreference: "low-power",
      });
      this.drawer.setPixelRatio(1);
      this.drawer.setSize(WIDTH, HEIGHT, false);
      this.drawer.setClearColor(0x000000, 0);
    } catch {
      this.drawer = null;
    }
    return this.drawer;
  }

  async drawn(file) {
    const renderer = this.renderer();
    if (!renderer) return null;
    const rig = new Rig({ anisotropy: Math.min(renderer.capabilities.getMaxAnisotropy(), MOST_ANISOTROPY) });
    try {
      await rig.load(file, "own");
      if (!rig.state.corners && !rig.state.bones) return { picture: null, tint: null };
      await rig.add_textures();
      if (!rig.state.corners) rig.skeleton(true);
      const scene = new THREE.Scene();
      scene.add(rig.root);
      const camera = new THREE.PerspectiveCamera(FOV, WIDTH / HEIGHT, 0.1, 1000);
      camera.up.set(0, 0, 1);
      const { low, high } = rig.framed();
      aim(camera, null, low, high, ROOM);
      renderer.render(scene, camera);
      const picture = await new Promise((resolve) => renderer.domElement.toBlob(resolve, "image/png"));
      return { picture, tint: rig.tint };
    } catch {
      return null;
    } finally {
      rig.dispose();
      renderer.renderLists.dispose();
    }
  }
}

export const thumbnails = new Thumbnails();
