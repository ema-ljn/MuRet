---
name: frontend-design
description: Use when designing or reshaping a MuRet page or screen that people look at (the mutools studio, a mockup, an artifact, a new panel), to choose palette, type and layout that read as made for MU and not as a template. Anthropic's frontend-design guidance with the decisions MuRet already took.
license: Apache-2.0
metadata:
  upstream: Anthropic's public skill frontend-design, as shipped to Claude on 2026-11-27
---

# Frontend design in MuRet

This is Anthropic's `frontend-design` skill (Apache 2.0). Emanuel judged the first mutools studio mockup generic, "like
the styles seen everywhere on the web", and asked for a design skill (2026-11-27); the studio was redone with this one.
This file is MuRet's layer; the guidance itself is in [`references/upstream-skill.md`](references/upstream-skill.md).

## What takes precedence

1. What Emanuel asks for in the conversation. His standing preferences: **dark** pages, never a white or light page
   (2026-11-27), and nothing that looks templated.
2. The tool's spec and plan (for mutools,
   [`docs/superpowers/specs/2026-11-25-bmd-editor-design.md`](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md) and
   the stage plans in `docs/superpowers/plans/`).
3. This skill and, last, `references/`.

## What MuRet already decided

The mutools studio ("Vitrine", `Client/tools/mutools/src/mutools/adapters/web/theme.py`) is the reference for any new
screen of a MuRet tool:

- **The model is the memorable thing.** One vitrine holds it, and its glow takes the hue of the model's own textures;
  the library's plates get a dim wash of the same hue. Everything around it stays quiet.
- **Dark page**: page `#111214`, raised `#191A1D`, wells `#1F2023`, text `#E7E5DE`, muted `#A3A5AB`. Ultramarine is the
  only accent: `#9AA6FF` for text, lines and the playhead, `#3A47B0` for filled controls. Findings in `#E8867D`,
  warnings in `#E0B25E`.
- **Type**: Alegreya for names (model, section, folder), Alegreya Sans for the interface, tabular lining figures.
- **Not used**: monospace for data, ALL-CAPS labels, eyebrows over headings, strings joined with middle dots, stat-card
  grids, a `→` on buttons, decorative gradients.
- **Structure carries information**: the actions strip is proportional to each action's length at the client's speed;
  the scrubber is a ruler of keys; textures show as swatches; folders as an index with dot leaders.
- **Words** say what MU calls things and what the client does ("6.25 keys a second, as the client plays this monster"),
  in sentence case, with errors that say what failed and what to do.

## How it is used

- Plan the tokens (4 to 6 colors, type roles, a layout sketch), check them against the upstream's list of AI defaults,
  then build.
- Look at the result in a browser (Playwright with the preinstalled Chromium) before showing it, and once more after
  each change Emanuel asks for.
- A new screen reuses the studio's tokens; a new direction is a choice Emanuel makes, shown first as a mockup.

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of the `frontend-design` skill's
`SKILL.md` as Anthropic ships it to Claude (read on 2026-11-27), under the Apache License 2.0 in [`LICENSE`](LICENSE).
This `SKILL.md` is MuRet's own text. To update: copy the new upstream file over the reference and review the
"What MuRet already decided" section against it.
