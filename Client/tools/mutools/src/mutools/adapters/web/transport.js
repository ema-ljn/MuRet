import { hear, tell } from "mutools-studio";

const MAJOR_EVERY = 5;
const STEP_KEYS = { ArrowLeft: -1, ArrowRight: 1 };

export default {
  template: `
    <div class="mt-transport">
      <div class="mt-transport-buttons">
        <button type="button" class="mt-round" aria-label="Previous key" @click="step(-1)">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M6 5h2v14H6zM20 5v14L9 12z"></path></svg>
        </button>
        <button type="button" class="mt-play" :aria-label="playing ? 'Pause' : 'Play'" @click="toggle">
          <svg v-if="playing" width="16" height="16" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M6 4h4v16H6zM14 4h4v16h-4z"></path></svg>
          <svg v-else width="18" height="18" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M7 4v16l13-8z"></path></svg>
        </button>
        <button type="button" class="mt-round" aria-label="Next key" @click="step(1)">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M16 5h2v14h-2zM4 5v14l11-7z"></path></svg>
        </button>
      </div>
      <div ref="ruler" class="mt-ruler" role="slider" aria-label="Key" tabindex="0" aria-valuemin="0"
        :aria-valuemax="last" :aria-valuenow="key" @pointerdown="grab" @pointermove="drag" @keydown="keyed">
        <span class="mt-ruler-line"></span>
        <span v-for="tick in ticks" :key="tick.key" class="mt-tick"
          :class="{ 'mt-tick-major': tick.major, 'mt-tick-passed': tick.key <= key }"
          :style="{ left: tick.at + '%' }"></span>
        <span class="mt-head" :style="{ left: head + '%' }"></span>
        <span class="mt-head-label" :style="{ left: head + '%' }">{{ key }}</span>
        <span class="mt-ruler-end mt-ruler-start">0</span>
        <span class="mt-ruler-end mt-ruler-last">{{ last }}</span>
      </div>
      <div class="mt-readout">
        <span class="mt-readout-key">{{ keys ? 'Key ' + key + ' of ' + keys : 'No keys' }}</span>
        <span class="mt-readout-speed">{{ speed }}</span>
      </div>
    </div>`,

  props: { speed: { type: String, default: "" } },

  data() {
    return { keys: 0, key: 0, time: 0, playing: false };
  },

  computed: {
    last() {
      return Math.max(this.keys - 1, 0);
    },
    ticks() {
      const span = Math.max(this.last, 1);
      return Array.from({ length: this.keys }, (_, key) => ({ key, at: (key / span) * 100, major: key % MAJOR_EVERY === 0 }));
    },
    head() {
      return (Math.min(this.time, this.last) / Math.max(this.last, 1)) * 100;
    },
  },

  mounted() {
    this.unhear = hear("state", (state) => this.heard(state));
    tell("ask");
  },

  unmounted() {
    this.unhear?.();
  },

  methods: {
    heard(state) {
      this.keys = state?.keys ?? 0;
      this.key = state?.key ?? 0;
      this.time = state?.time ?? 0;
      this.playing = Boolean(state?.playing);
    },
    toggle() {
      tell("command", { name: "toggle" });
    },
    step(keys) {
      tell("command", { name: "step", value: keys });
    },
    seek(key) {
      tell("command", { name: "seek", value: Math.min(Math.max(key, 0), this.last) });
    },
    key_at(event) {
      const box = this.$refs.ruler.getBoundingClientRect();
      return Math.round(((event.clientX - box.left) / Math.max(box.width, 1)) * this.last);
    },
    grab(event) {
      if (!this.keys) return;
      this.$refs.ruler.setPointerCapture(event.pointerId);
      tell("command", { name: "pause" });
      this.seek(this.key_at(event));
    },
    drag(event) {
      if (this.$refs.ruler.hasPointerCapture(event.pointerId)) this.seek(this.key_at(event));
    },
    keyed(event) {
      if (event.key in STEP_KEYS) this.step(STEP_KEYS[event.key]);
      else if (event.key === "Home") this.seek(0);
      else if (event.key === "End") this.seek(this.last);
      else return;
      event.preventDefault();
    },
  },
};
