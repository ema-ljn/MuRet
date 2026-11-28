import { OrbitControls, THREE } from "nicegui-scene";
import { FrameLoop, Rig, Stage, aim, gizmo_for, glow_of, hear, tell } from "mutools-studio";

const FOV = 35;
const MOST_ANISOTROPY = 8;
const PICTURE = "image/png";

export default {
  template: `
    <div class="mt-vitrine" :style="{ '--glow': glow }">
      <canvas class="mt-canvas" aria-label="The model, which the mouse turns, pans and zooms"></canvas>
      <slot></slot>
    </div>`,

  data() {
    return { glow: glow_of(null) };
  },

  mounted() {
    this.settings = { view: "textured", skeleton: false, additive: false, speed: 8, mesh: null, bone: null };
    this.posing = { editing: false, bone: null, tool: "turn" };
    this.opened = Promise.resolve();
    const canvas = this.$el.querySelector("canvas");
    try {
      this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: "low-power" });
    } catch {
      this.$emit("failed", "This browser cannot draw WebGL.");
      return;
    }
    this.renderer.setClearColor(0x000000, 0);
    this.anisotropy = Math.min(this.renderer.capabilities.getMaxAnisotropy(), MOST_ANISOTROPY);
    this.stage = new Stage();
    this.camera = new THREE.PerspectiveCamera(FOV, 1, 0.1, 1000);
    this.camera.up.set(0, 0, 1);
    this.loop = new FrameLoop({
      renderer: this.renderer,
      draw: () => this.renderer.render(this.stage.scene, this.camera),
      animate: (seconds) => this.animate(seconds),
      animating: () => Boolean(this.rig?.playing),
      report: (stats) => tell("stats", stats),
    });
    this.controls = new OrbitControls(this.camera, canvas);
    this.controls.addEventListener("change", () => this.loop.request());
    this.controls.addEventListener("start", () => this.loop.start_moving());
    this.controls.addEventListener("end", () => this.loop.stop_moving());
    this.loop.watch(this.$el);
    this.resizer = new ResizeObserver(() => this.resize());
    this.resizer.observe(this.$el);
    this.unheard = [hear("command", (command) => this.command(command)), hear("ask", () => this.told())];
    window.mutoolsStudio = {
      frames: () => this.loop.drawn,
      gizmo: () => this.gizmo_state(),
      drag: (change) => this.drag(change),
    };
  },

  unmounted() {
    this.unheard?.forEach((unhear) => unhear());
    this.resizer?.disconnect();
    this.loop?.dispose();
    this.gizmo?.dispose();
    this.controls?.dispose();
    this.rig?.dispose();
    this.stage?.dispose();
    this.renderer?.dispose();
  },

  methods: {
    open(file, on) {
      this.opened = this.opened.then(() => this.load(file, on));
      return this.opened;
    },

    async load(file, on) {
      const rig = new Rig({ anisotropy: this.anisotropy, changed: () => this.loop.request() });
      try {
        await rig.load(file, on);
      } catch (error) {
        rig.dispose();
        this.$emit("failed", String(error?.message ?? error));
        return;
      }
      this.gizmo?.attach(null);
      this.rig?.dispose();
      this.rig = rig;
      window.mutoolsViewer = rig.state;
      this.stage.scene.add(rig.root);
      this.settings.mesh = this.settings.bone = this.posing.bone = null;
      rig.show(this.settings.view);
      rig.skeleton(this.settings.skeleton);
      rig.additive(this.settings.additive);
      rig.speed(this.settings.speed);
      this.frame();
      this.told();
      rig.add_textures().then(() => {
        if (this.rig !== rig) return;
        this.glow = glow_of(rig.tint);
        this.$emit("ready", { tint: rig.tint });
      });
    },

    async when_open(work) {
      await this.opened;
      if (!this.rig) return null;
      const answer = await work(this.rig);
      this.told();
      this.loop.request();
      return answer;
    },

    play(action, repeats = true, start = true) {
      return this.when_open(async (rig) => {
        await rig.play(action, repeats);
        if (!start) rig.pause();
      });
    },

    pause() {
      return this.when_open((rig) => rig.pause());
    },

    resume() {
      return this.when_open((rig) => rig.resume());
    },

    seek(key) {
      return this.when_open((rig) => rig.seek(key));
    },

    speed(keys_per_second) {
      this.settings.speed = keys_per_second;
      return this.when_open((rig) => rig.speed(keys_per_second));
    },

    mode(view) {
      this.settings.view = view;
      this.stage.lit(view === "lit");
      return this.when_open((rig) => rig.show(view));
    },

    skeleton(shown) {
      this.settings.skeleton = shown;
      return this.when_open((rig) => rig.skeleton(shown));
    },

    additive(on) {
      this.settings.additive = on;
      return this.when_open((rig) => rig.additive(on));
    },

    highlight(mesh) {
      return this.when_open((rig) => rig.highlight(mesh));
    },

    mark_bone(bone) {
      return this.when_open((rig) => rig.mark_bone(bone));
    },

    async edit(on) {
      this.posing.editing = on;
      if (on && !this.gizmo) {
        this.gizmo = await gizmo_for({
          camera: this.camera,
          canvas: this.renderer.domElement,
          scene: this.stage.scene,
          orbit: this.controls,
          changed: () => this.loop.request(),
          grabbed: () => this.grabbed(),
          released: (change) => this.released(change),
        });
      }
      this.posed_bone();
    },

    pose_bone(bone, tool) {
      Object.assign(this.posing, { bone, tool });
      return this.when_open(() => this.posed_bone());
    },

    posed_bone() {
      if (!this.gizmo) return;
      const { editing, bone, tool } = this.posing;
      this.gizmo.attach(editing ? (this.rig?.bones[bone] ?? null) : null, bone, tool);
      this.loop.request();
    },

    grabbed() {
      if (!this.rig?.current) return;
      this.rig.pause();
      this.rig.seek(this.rig.state.key);
      this.grabbed_at = { action: this.rig.state.action, key: this.rig.state.key };
      this.told();
    },

    released(change) {
      const at = this.grabbed_at ?? { action: this.rig?.state.action, key: this.rig?.state.key };
      this.grabbed_at = null;
      if (at.action === null || at.action === undefined) return;
      this.$emit("posed", { ...at, ...change });
    },

    drag({ quaternion, position }) {
      const bone = this.rig?.bones[this.posing.bone];
      if (!this.gizmo || !bone) return false;
      this.grabbed();
      if (quaternion) bone.quaternion.fromArray(quaternion);
      if (position) bone.position.fromArray(position);
      this.released(this.gizmo.change());
      this.loop.request();
      return true;
    },

    gizmo_state() {
      if (!this.gizmo) return null;
      const bone = this.rig?.bones[this.gizmo.bone];
      if (!bone) return { ...this.gizmo.state, at: null };
      const shown = bone.getWorldPosition(new THREE.Vector3()).project(this.camera);
      const box = this.renderer.domElement.getBoundingClientRect();
      const at = [box.left + ((shown.x + 1) / 2) * box.width, box.top + ((1 - shown.y) / 2) * box.height];
      return { ...this.gizmo.state, at };
    },

    reposed(action, actions) {
      return this.when_open((rig) => rig.replay(action, actions));
    },

    frame() {
      if (!this.rig) return;
      const { low, high } = this.rig.framed();
      this.stage.fit(low, high);
      aim(this.camera, this.controls, low, high);
      this.loop.request();
    },

    picture(name) {
      const draw = () => this.renderer.render(this.stage.scene, this.camera);
      this.gizmo ? this.gizmo.hidden(draw) : draw();
      this.renderer.domElement.toBlob((blob) => {
        if (!blob) return;
        const link = document.createElement("a");
        link.href = URL.createObjectURL(blob);
        link.download = `${name}.png`;
        link.click();
        setTimeout(() => URL.revokeObjectURL(link.href));
      }, PICTURE);
    },

    command({ name, value }) {
      if (!this.rig) return;
      if (name === "toggle") this.rig.playing ? this.rig.pause() : this.rig.resume();
      if (name === "pause") this.rig.pause();
      if (name === "step") {
        this.rig.pause();
        this.rig.step(value);
      }
      if (name === "seek") this.rig.seek(value);
      if (name === "frame") this.frame();
      this.told();
      this.loop.request();
    },

    animate(seconds) {
      if (!this.rig) return;
      const playing = this.rig.playing;
      this.rig.update(seconds);
      if (playing) this.told();
    },

    told() {
      const state = this.rig?.state;
      tell("state", state ? { ...state, time: this.rig.current?.time ?? 0 } : null);
    },

    resize() {
      const { clientWidth: width, clientHeight: height } = this.$el;
      if (!width || !height) return;
      this.renderer.setSize(width, height, false);
      this.camera.aspect = width / height;
      this.camera.updateProjectionMatrix();
      this.loop.request();
    },
  },
};
