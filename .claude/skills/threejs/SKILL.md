---
name: threejs
description: Use when writing or reviewing the three.js code of the mutools studio (Client/tools/mutools/src/mutools/adapters/web, its Vue components and studio/*.js) - the scene, skinned models and their actions, the camera, picking, loaders, and keeping the page light (render on demand). CloudAI-X's three.js skills with the rules MuRet adds.
license: MIT
metadata:
  upstream: https://github.com/CloudAI-X/threejs-skills/tree/0e181866f095b5f06d005133876e926515390d95/skills
---

# three.js in MuRet

These are five of the thirteen skills of [CloudAI-X/threejs-skills](https://github.com/CloudAI-X/threejs-skills)
(MIT): [fundamentals](references/fundamentals.md), [animation](references/animation.md),
[performance](references/performance.md), [interaction](references/interaction.md) and [loaders](references/loaders.md).
Emanuel asked for a WebGL studio that is light without losing quality, and for skills and techniques from the internet
(2026-11-27). This file is MuRet's layer; it wins over `references/` where they differ.

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Fidelity**: what the client does with a model (bone order, the Half-Life angle order of the rotations, lock
   actions, the client's speeds) is computed in Python (`app/model_scene.py`, `app/action_speeds.py`) and served by
   `/api/scene` and `/api/poses`. The browser plays it; it never recomputes MU's math.
3. The tool's spec and plans, then this skill, then `references/`.

## Rules the references do not have

- **Which three.js**: the one NiceGUI bundles (`import { THREE, OrbitControls } from "nicegui-scene"`, r187 in NiceGUI
  4.1). No CDN, no npm, no second copy. What the bundle lacks is a decision for Emanuel before it is added. He chose
  `TransformControls` (2026-11-27): it is vendored byte for byte from three 0.187.0 in
  `adapters/web/studio/vendor/three/`, with three's license, and its `import ... from 'three'` reaches the bundled
  three through `adapters/web/three/index.js`, which re-exports those names from `nicegui-scene`; `test_vendored.py`
  pins its SHA-256 and checks the names. A new three in NiceGUI means vendoring the same release's file again.
  `GLTFExporter` still waits for his decision.
- **Render on demand, never a running loop**: `renderer.setAnimationLoop(animate)` in the references is replaced by
  `studio/frame_loop.js`, which draws only when something asks (`request()`), caps a playing action at 30 frames a
  second, uses a pixel ratio of 2 at rest and 1 while the camera moves, and stops when the page is hidden or the
  vitrine is off screen. A still model draws 0 frames a second; the browser tests check it.
- **Two WebGL contexts at most**: the vitrine's and one shared renderer for every library thumbnail
  (`studio/thumbnails.js`), whose pictures are cached in IndexedDB by file and content stamp.
- **Free what is replaced**: a model that is closed disposes its geometries, materials and textures (`Rig.dispose`).
- **MU's space**: Z is up (`camera.up.set(0, 0, 1)`), units are the client's. Each corner hangs from one bone with
  weight 1; bones are `THREE.Bone`s named `bone<N>` and the clips are built from the served poses, one key per second of
  clip time with the mixer's `timeScale` as keys a second.
- **Not used here**: React Three Fiber, post-processing and custom shaders, unless Emanuel asks.

## Origin and license

`references/*.md` are verbatim copies of `skills/threejs-<name>/SKILL.md` at commit `0e181866` of CloudAI-X/threejs-skills,
under the MIT license in [`LICENSE`](LICENSE). This `SKILL.md` is MuRet's own text. To update: copy the files from a new
commit, change the commit in the frontmatter and here, and check the rules above against them.
