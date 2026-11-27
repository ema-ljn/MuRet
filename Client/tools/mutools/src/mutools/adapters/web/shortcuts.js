import { tell } from "mutools-studio";

const WRITTEN = new Set(["INPUT", "TEXTAREA", "SELECT"]);
const TYPED = new Set([...WRITTEN, "BUTTON"]);
const COMMANDS = { " ": "toggle", f: "frame", F: "frame" };
const EDITS = { z: "undo", y: "redo", s: "save" };

export default {
  template: "<span hidden></span>",

  props: { edit: { type: Boolean, default: false } },

  mounted() {
    this.pressed = (event) => this.key(event);
    document.addEventListener("keydown", this.pressed);
  },

  unmounted() {
    document.removeEventListener("keydown", this.pressed);
  },

  methods: {
    key(event) {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        this.$emit("find");
        return;
      }
      const target = event.target;
      if (this.edit && (event.ctrlKey || event.metaKey) && !event.altKey) {
        const letter = event.key.toLowerCase();
        const edit = letter === "z" && event.shiftKey ? "redo" : EDITS[letter];
        const writing = WRITTEN.has(target?.tagName) || target?.isContentEditable;
        if (edit && (edit === "save" || !writing)) {
          event.preventDefault();
          if (!event.repeat) this.$emit(edit);
          return;
        }
      }
      const typing = TYPED.has(target?.tagName) || target?.isContentEditable || target?.closest?.(".q-dialog");
      const command = COMMANDS[event.key];
      if (!command || typing || event.ctrlKey || event.metaKey || event.altKey || event.repeat) return;
      event.preventDefault();
      tell("command", { name: command });
    },
  },
};
