import { THREE } from "nicegui-scene";

const RING_COLOR = 0xe4e3dd;
const RING_OPACITY = 0.14;
const RINGS = 7;
const RING_SEGMENTS = 128;
const RING_STEP = 0.3;
const SHADOW_SIZE = 128;
const SHADOW_OPACITY = 0.6;
const SHADOW_SPREAD = 1.4;
const LIFT = 0.001;
const FOG = 0x16171a;
const FOG_NEAR = 1.2;
const FOG_FAR = 3.4;
const SKY = 0xfff4e0;
const GROUND = 0x22252b;
const SKY_STRENGTH = 2.2;
const SUN = 0xffffff;
const SUN_STRENGTH = 1.6;
const SUN_FROM = new THREE.Vector3(-0.5, -1, 1.4);
const VIEW_DIRECTION = new THREE.Vector3(0.45, -0.8, 0.4).normalize();
const VIEW_DISTANCE = 1.15;
const NEAREST = 0.01;
const SMALLEST = 1;

export function measure(low, high) {
  const size = Math.max(Math.hypot(...high.map((value, axis) => value - low[axis])), SMALLEST);
  const center = new THREE.Vector3(...low.map((value, axis) => (value + high[axis]) / 2));
  return { size, center };
}

export function aim(camera, controls, low, high, room = VIEW_DISTANCE) {
  const { size, center } = measure(low, high);
  const distance = (size * room) / Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) / 2;
  camera.near = Math.max(distance / 1000, NEAREST);
  camera.far = Math.max(distance * 50, 1000);
  camera.position.copy(center).addScaledVector(VIEW_DIRECTION, distance);
  camera.updateProjectionMatrix();
  if (controls) {
    controls.target.copy(center);
    controls.update();
  } else {
    camera.lookAt(center);
  }
}

function shadow_texture() {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = SHADOW_SIZE;
  const context = canvas.getContext("2d");
  const half = SHADOW_SIZE / 2;
  const gradient = context.createRadialGradient(half, half, 0, half, half, half);
  gradient.addColorStop(0, "rgba(0,0,0,1)");
  gradient.addColorStop(1, "rgba(0,0,0,0)");
  context.fillStyle = gradient;
  context.fillRect(0, 0, SHADOW_SIZE, SHADOW_SIZE);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  return texture;
}

function rings(step) {
  const points = [];
  for (let ring = 1; ring <= RINGS; ring += 1) {
    const radius = ring * step;
    for (let segment = 0; segment < RING_SEGMENTS; segment += 1) {
      const from = (segment / RING_SEGMENTS) * Math.PI * 2;
      const to = ((segment + 1) / RING_SEGMENTS) * Math.PI * 2;
      points.push(Math.cos(from) * radius, Math.sin(from) * radius, 0, Math.cos(to) * radius, Math.sin(to) * radius, 0);
    }
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(points, 3));
  const material = new THREE.LineBasicMaterial({
    color: RING_COLOR,
    transparent: true,
    opacity: RING_OPACITY,
    depthWrite: false,
  });
  return new THREE.LineSegments(geometry, material);
}

export class Stage {
  scene = new THREE.Scene();
  floor = new THREE.Group();

  constructor() {
    this.scene.add(this.floor);
    this.hemisphere = new THREE.HemisphereLight(SKY, GROUND, SKY_STRENGTH);
    this.hemisphere.position.set(0, 0, 1);
    this.sun = new THREE.DirectionalLight(SUN, SUN_STRENGTH);
    this.scene.add(this.hemisphere, this.sun, this.sun.target);
    this.lit(false);
  }

  fit(low, high) {
    this.clear_floor();
    const { size, center } = measure(low, high);
    const floor = rings(size * RING_STEP);
    floor.position.set(center.x, center.y, low[2]);
    const footprint = Math.max(high[0] - low[0], high[1] - low[1], SMALLEST) * SHADOW_SPREAD;
    const texture = shadow_texture();
    const shadow = new THREE.Mesh(
      new THREE.PlaneGeometry(footprint, footprint),
      new THREE.MeshBasicMaterial({ map: texture, transparent: true, opacity: SHADOW_OPACITY, depthWrite: false }),
    );
    shadow.position.set(center.x, center.y, low[2] + size * LIFT);
    this.floor.add(floor, shadow);
    this.scene.fog = new THREE.Fog(FOG, size * FOG_NEAR, size * FOG_FAR);
    this.sun.position.copy(center).addScaledVector(SUN_FROM, size);
    this.sun.target.position.copy(center);
    this.disposed_floor = () => {
      floor.geometry.dispose();
      floor.material.dispose();
      shadow.geometry.dispose();
      shadow.material.dispose();
      texture.dispose();
    };
  }

  clear_floor() {
    this.floor.clear();
    this.disposed_floor?.();
    this.disposed_floor = null;
  }

  lit(on) {
    this.hemisphere.visible = on;
    this.sun.visible = on;
  }

  dispose() {
    this.clear_floor();
  }
}
