import { hear } from "mutools-studio";

export default {
  template: `
    <div v-if="variant === 'vitrine'" class="mt-state">
      <span class="mt-state-title"><span class="mt-dot" :class="{ 'mt-dot-on': drawing }"></span>{{ title }}</span>
      <span class="mt-state-detail">{{ detail }}</span>
    </div>
    <span v-else class="mt-footer-state">{{ sentence }}</span>`,

  props: { variant: { type: String, default: "vitrine" } },

  data() {
    return { fps: 0, playing: false, triangles: 0, calls: 0, drawn: 0 };
  },

  computed: {
    drawing() {
      return this.playing || this.fps > 0;
    },
    title() {
      if (this.playing) return `Playing at ${this.fps} frames a second`;
      return this.fps > 0 ? `Drawing at ${this.fps} frames a second` : "Still";
    },
    detail() {
      if (this.playing) return "Poses are interpolated between keys";
      return this.fps > 0 ? "Only while the camera moves" : "Nothing is drawn until something changes";
    },
    sentence() {
      const last = this.drawn ? ` The last frame had ${this.triangles} triangles in ${this.calls} draws.` : "";
      if (this.playing) return `Drawing ${this.fps} frames a second while the action plays.${last}`;
      return `Drawing only when something changes.${last}`;
    },
  },

  mounted() {
    this.unheard = [
      hear("stats", (stats) => Object.assign(this, { fps: stats.fps, triangles: stats.triangles, calls: stats.calls, drawn: stats.drawn })),
      hear("state", (state) => (this.playing = Boolean(state?.playing))),
    ];
  },

  unmounted() {
    this.unheard.forEach((unhear) => unhear());
  },
};
