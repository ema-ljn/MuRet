const PLAYING_FPS = 30;
const FRAME_SLACK_MS = 2;
const MAX_PIXEL_RATIO = 2;
const MOVING_PIXEL_RATIO = 1;
const STATS_EVERY_MS = 1000;

export class FrameLoop {
  frames = 0;
  drawn = 0;
  rate = 0;
  calls = 0;
  triangles = 0;
  moving = false;
  visible = true;
  onscreen = true;
  pending = null;
  last = 0;
  measured = 0;

  constructor({ renderer, draw, animate, animating, report = () => {} }) {
    this.renderer = renderer;
    this.draw = draw;
    this.animate = animate;
    this.animating = animating;
    this.report = report;
    this.full_ratio = Math.min(window.devicePixelRatio || 1, MAX_PIXEL_RATIO);
    renderer.setPixelRatio(this.full_ratio);
    this.shown = () => {
      this.visible = document.visibilityState !== "hidden";
      this.request();
    };
    document.addEventListener("visibilitychange", this.shown);
  }

  watch(element) {
    this.observer = new IntersectionObserver(([entry]) => {
      this.onscreen = entry.isIntersecting;
      this.request();
    });
    this.observer.observe(element);
  }

  request() {
    if (this.pending !== null || !this.visible || !this.onscreen || this.stopped) return;
    this.pending = requestAnimationFrame((now) => this.frame(now));
  }

  frame(now) {
    this.pending = null;
    const playing = this.animating();
    if (playing && this.last && now - this.last < 1000 / PLAYING_FPS - FRAME_SLACK_MS) {
      this.request();
      return;
    }
    const seconds = this.last ? Math.min((now - this.last) / 1000, 1 / PLAYING_FPS * 4) : 0;
    this.last = playing ? now : 0;
    this.animate(seconds);
    this.draw();
    this.count(now);
    if (playing) this.request();
  }

  count(now) {
    this.frames += 1;
    this.drawn += 1;
    this.calls = this.renderer.info.render.calls;
    this.triangles = this.renderer.info.render.triangles;
    this.measure(now);
  }

  measure(now) {
    if (now - this.measured < STATS_EVERY_MS) return;
    const elapsed = this.measured ? now - this.measured : STATS_EVERY_MS;
    this.rate = Math.round((this.frames * 1000) / elapsed);
    this.frames = 0;
    this.measured = now;
    this.report(this.stats());
    clearTimeout(this.settle);
    this.settle = setTimeout(() => this.idle(), STATS_EVERY_MS + 100);
  }

  idle() {
    if (this.pending !== null) return;
    this.rate = 0;
    this.frames = 0;
    this.measured = 0;
    this.report(this.stats());
  }

  stats() {
    return {
      fps: this.rate,
      drawn: this.drawn,
      calls: this.calls,
      triangles: this.triangles,
      playing: this.animating(),
      geometries: this.renderer.info.memory.geometries,
      textures: this.renderer.info.memory.textures,
    };
  }

  start_moving() {
    this.moving = true;
    if (this.renderer.getPixelRatio() !== MOVING_PIXEL_RATIO) this.renderer.setPixelRatio(MOVING_PIXEL_RATIO);
    this.request();
  }

  stop_moving() {
    this.moving = false;
    if (this.renderer.getPixelRatio() !== this.full_ratio) this.renderer.setPixelRatio(this.full_ratio);
    this.request();
  }

  dispose() {
    this.stopped = true;
    if (this.pending !== null) cancelAnimationFrame(this.pending);
    clearTimeout(this.settle);
    document.removeEventListener("visibilitychange", this.shown);
    this.observer?.disconnect();
  }
}
