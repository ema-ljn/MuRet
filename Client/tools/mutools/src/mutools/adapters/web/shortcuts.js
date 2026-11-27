import { tell } from "mutools-studio";

const TYPED = new Set(["INPUT", "TEXTAREA", "SELECT", "BUTTON"]);
const COMMANDS = { " ": "toggle", f: "frame", F: "frame" };

export default {
  template: "<span hidden></span>",

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
      const typing = TYPED.has(target?.tagName) || target?.isContentEditable || target?.closest?.(".q-dialog");
      const command = COMMANDS[event.key];
      if (!command || typing || event.ctrlKey || event.metaKey || event.altKey || event.repeat) return;
      event.preventDefault();
      tell("command", { name: command });
    },
  },
};
