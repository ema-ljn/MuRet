const TOOLS = { turn: "rotate", move: "translate" };
const SIZE = 0.8;

function pointer_of(event, canvas) {
  const box = canvas.getBoundingClientRect();
  return {
    x: ((event.clientX - box.left) / Math.max(box.width, 1)) * 2 - 1,
    y: -((event.clientY - box.top) / Math.max(box.height, 1)) * 2 + 1,
    button: event.button,
  };
}

export async function gizmo_for(options) {
  const { TransformControls } = await import("./vendor/three/TransformControls.js");
  return new Gizmo(TransformControls, options);
}

export class Gizmo {
  bone = null;
  tool = "turn";
  moved = false;

  constructor(TransformControls, { camera, canvas, scene, orbit, changed, grabbed, released }) {
    this.canvas = canvas;
    this.orbit = orbit;
    this.controls = new TransformControls(camera, canvas);
    this.controls.setSpace("local");
    this.controls.setSize(SIZE);
    this.helper = this.controls.getHelper();
    scene.add(this.helper);
    this.controls.addEventListener("change", changed);
    this.controls.addEventListener("objectChange", () => (this.moved = true));
    this.controls.addEventListener("dragging-changed", ({ value }) => (orbit.enabled = !value));
    this.controls.addEventListener("mouseUp", () => {
      if (this.moved) released(this.change());
      this.moved = false;
    });
    this.before_grab = (event) => {
      if (!this.controls.object || event.button !== 0) return;
      this.controls.pointerHover(pointer_of(event, canvas));
      if (this.controls.axis === null) return;
      orbit.enabled = false;
      grabbed();
    };
    canvas.parentElement.addEventListener("pointerdown", this.before_grab, { capture: true });
  }

  attach(bone, index, tool) {
    this.tool = tool in TOOLS ? tool : "turn";
    this.controls.setMode(TOOLS[this.tool]);
    if (bone) {
      this.bone = index;
      this.controls.attach(bone);
    } else {
      this.bone = null;
      this.controls.detach();
    }
  }

  change() {
    const object = this.controls.object;
    const turned = this.tool === "turn";
    return {
      bone: this.bone,
      tool: this.tool,
      [turned ? "quaternion" : "position"]: (turned ? object.quaternion : object.position).toArray(),
    };
  }

  get state() {
    return { bone: this.bone, tool: this.tool, dragging: this.controls.dragging };
  }

  hidden(work) {
    const shown = this.helper.visible;
    this.helper.visible = false;
    work();
    this.helper.visible = shown;
  }

  dispose() {
    this.canvas.parentElement?.removeEventListener("pointerdown", this.before_grab, { capture: true });
    this.controls.detach();
    this.helper.removeFromParent();
    this.controls.dispose();
  }
}
