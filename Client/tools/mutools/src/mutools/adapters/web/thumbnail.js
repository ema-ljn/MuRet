import { thumbnails, wash_of } from "mutools-studio";

export default {
  template: `
    <span class="mt-plate-well" :style="wash">
      <img v-if="url" :src="url" alt="" class="mt-plate-image" decoding="async">
      <span v-else-if="empty" class="mt-plate-empty">Nothing to draw</span>
    </span>`,

  props: { file: { type: String, required: true }, stamp: { type: String, default: "" } },

  data() {
    return { url: null, tint: null, empty: false };
  },

  computed: {
    wash() {
      const [center, edge] = wash_of(this.tint);
      return { "--wash": center, "--wash-edge": edge };
    },
  },

  mounted() {
    thumbnails.watch(this.$el, this.file, this.stamp, (shown) => {
      this.url = shown.url;
      this.tint = shown.tint;
      this.empty = !shown.url && !shown.failed;
    });
  },

  unmounted() {
    thumbnails.forget(this.$el);
  },
};
