from typing import Final

from nicegui import ui

from mutools.adapters.palette import STUDIO

FONTS: Final = (
    '<link rel="preconnect" href="https://fonts.googleapis.com">'
    '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
    '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Alegreya:ital,wght@0,500;0,700;1,500'
    '&family=Alegreya+Sans:wght@400;500;700&display=swap">'
)
STYLE: Final = f"""
:root {{
  --page: {STUDIO.background}; --raised: {STUDIO.surface}; --well: {STUDIO.panel}; --well-hover: #292A2F;
  --ink: {STUDIO.foreground}; --muted: {STUDIO.secondary}; --faint: #6E7178; --accent: {STUDIO.primary};
  --accent-strong: #C3C9FF; --fill: {STUDIO.accent}; --fill-hover: #4855C4; --on-fill: #F2F1EC;
  --bad: {STUDIO.error}; --warn: {STUDIO.warning}; --good: {STUDIO.success};
  --line: rgba(228, 227, 221, 0.1); --line-strong: rgba(228, 227, 221, 0.2); --vitrine: #0B0C0E;
  --vitrine-text: #F2F1EC; --vitrine-muted: #B9BCC2; --display: Alegreya, Georgia, "Times New Roman", serif;
  --text: "Alegreya Sans", "Segoe UI", system-ui, sans-serif;
}}
body, body.body--dark {{ background: var(--page); color: var(--ink); font-family: var(--text); font-size: 14px;
  line-height: 1.45; font-variant-numeric: lining-nums tabular-nums; }}
.nicegui-content {{ padding: 0; gap: 0; }}
button {{ font: inherit; color: inherit; }}
button:focus-visible, input:focus-visible, [tabindex]:focus-visible {{ outline: 2px solid var(--accent);
  outline-offset: 2px; }}
@media (prefers-reduced-motion: reduce) {{ * {{ transition: none !important; }} }}

.mt-studio {{ min-height: 100vh; display: flex; flex-direction: column; width: 100%; }}
.mt-press {{ border: 0; background: transparent; cursor: pointer; padding: 0; text-align: left; }}
.mt-words {{ display: inline; }}
.mt-icon {{ display: inline-flex; line-height: 0; }}

.mt-header {{ display: flex; flex-wrap: wrap; align-items: baseline; gap: 12px 28px; padding: 16px 24px 12px; }}
.mt-brand {{ display: flex; align-items: baseline; gap: 8px; }}
.mt-wordmark {{ font-family: var(--display); font-style: italic; font-weight: 700; font-size: 26px; line-height: 1;
  letter-spacing: -0.01em; }}
.mt-brand-studio {{ font-size: 15px; color: var(--muted); }}
.mt-crumbs {{ display: flex; flex-wrap: wrap; align-items: baseline; gap: 8px; font-size: 15px; color: var(--muted);
  flex-grow: 1; min-width: 220px; }}
.mt-crumb-cut {{ color: var(--faint); }}
.mt-crumb-file {{ color: var(--ink); font-weight: 500; }}
.mt-crumb-folder {{ color: var(--muted); }}
.mt-crumb-folder:hover {{ color: var(--accent); }}
.mt-header-end {{ display: flex; align-items: center; gap: 14px; flex-wrap: wrap; }}
.mt-note {{ display: flex; align-items: center; gap: 6px; color: var(--muted); }}
.mt-find {{ height: 38px; display: flex; align-items: center; gap: 10px; padding: 0 8px 0 14px; border-radius: 19px;
  border: 1px solid rgba(228, 227, 221, 0.14); color: var(--muted); }}
.mt-find:hover {{ border-color: var(--ink); color: var(--ink); }}
.mt-find .mt-icon {{ order: -1; }}
.mt-key {{ font-size: 12px; padding: 2px 8px; border-radius: 10px; background: var(--well); color: var(--ink); }}

.mt-body {{ display: flex; flex-wrap: wrap; flex-grow: 1; min-height: 0; padding: 0 12px; }}
.mt-library {{ flex: 1 1 260px; max-width: 292px; padding: 4px 12px 16px; display: flex; flex-direction: column;
  gap: 14px; box-sizing: border-box; }}
.mt-main {{ flex: 999 1 560px; min-width: 0; display: flex; flex-direction: column; gap: 14px; padding: 4px 12px 0; }}
.mt-inspector {{ flex: 1 1 300px; max-width: 340px; padding: 4px 12px 16px; display: flex; flex-direction: column;
  gap: 16px; box-sizing: border-box; }}
.mt-footer {{ display: flex; flex-wrap: wrap; align-items: center; gap: 8px 24px; padding: 10px 24px 16px;
  font-size: 13px; color: var(--muted); }}
.mt-footer-gap {{ flex-grow: 1; }}

.mt-folder-head {{ display: flex; align-items: baseline; justify-content: space-between; gap: 8px; }}
.mt-folder-title {{ font-family: var(--display); font-weight: 700; font-size: 28px; line-height: 1.1; margin: 0; }}
.mt-folder-count {{ color: var(--muted); margin-left: 10px; }}
.mt-link {{ color: var(--accent); font-weight: 500; height: 32px; display: inline-flex; align-items: center;
  gap: 4px; }}
.mt-link:hover {{ color: var(--accent-strong); }}
.mt-index {{ background: var(--raised); border-radius: 14px; padding: 14px 16px 16px; width: 300px;
  border: 1px solid var(--line);
  box-shadow: 0 24px 48px -20px rgba(0, 0, 0, 0.7); display: flex; flex-direction: column; gap: 12px; }}
.q-menu:has(.mt-index) {{ background: transparent; box-shadow: none; border-radius: 14px; }}
.mt-index-row {{ display: flex; align-items: baseline; gap: 8px; height: 30px; font-size: 15px; width: 100%; }}
.mt-index-row::after {{ content: ""; order: 1; flex-grow: 1; border-bottom: 1px dotted rgba(228, 227, 221, 0.26);
  transform: translateY(-4px); }}
.mt-index-count {{ order: 2; }}
.mt-index-row.mt-on {{ color: var(--accent); font-weight: 700; }}
.mt-index-title {{ font-family: var(--display); font-weight: 700; font-size: 17px; }}
.mt-index-grid {{ display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 2px 10px; }}
.mt-index-small {{ display: flex; justify-content: space-between; gap: 4px; height: 26px; font-size: 13px; }}
.mt-index-small.mt-on {{ color: var(--accent); font-weight: 700; }}
.mt-index-small-count {{ color: var(--muted); }}
.mt-search {{ border-bottom: 1px solid rgba(228, 227, 221, 0.19); }}
.mt-search .q-field__control {{ height: 38px; }}
.mt-search .q-field__native {{ color: var(--ink); font-family: var(--text); font-size: 14px; }}
.mt-plates {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px 12px; }}
.mt-plate {{ display: flex; flex-direction: column; gap: 6px; font-size: 15px; font-weight: 500;
  overflow-wrap: anywhere; }}
.mt-plate.mt-on {{ color: var(--accent); font-weight: 700; }}
.mt-plate-well {{ order: -1; display: block; position: relative; height: 116px; border-radius: 14px; overflow: hidden;
  background: radial-gradient(ellipse 70% 70% at 50% 60%, var(--wash, #1E1F22) 0%, var(--wash-edge, #17181A) 100%); }}
.mt-plate.mt-on .mt-plate-well {{ outline: 2px solid var(--accent); outline-offset: 3px; }}
.mt-plate-image {{ position: absolute; inset: 4px; width: calc(100% - 8px); height: calc(100% - 8px);
  object-fit: contain; }}
.mt-plate-empty {{ position: absolute; inset: 0; display: flex; align-items: center; justify-content: center;
  font-size: 13px; font-weight: 400; color: var(--muted); }}
.mt-plate-facts {{ font-size: 13px; font-weight: 400; color: var(--muted); margin-top: -6px; }}
.mt-pager {{ display: flex; align-items: center; gap: 2px; font-variant-numeric: tabular-nums lining-nums; }}
.mt-page {{ min-width: 30px; height: 30px; padding: 0 6px; border-radius: 15px; display: inline-flex;
  align-items: center; justify-content: center; text-align: center; font-size: 14px; color: var(--muted); }}
.mt-page:hover {{ color: var(--ink); }}
.mt-page.mt-on {{ background: var(--fill); color: var(--on-fill); font-weight: 700; }}
.mt-page-gap {{ min-width: 18px; text-align: center; color: var(--faint); }}
.mt-page-turn {{ width: 30px; height: 30px; border-radius: 15px; display: inline-flex; align-items: center;
  justify-content: center; color: var(--ink); border: 1px solid rgba(228, 227, 221, 0.15); }}
.mt-page-turn:first-child {{ margin-right: 6px; }}
.mt-page-turn:last-child {{ margin-left: 6px; }}
.mt-page-turn:hover {{ border-color: var(--ink); }}
.mt-page-turn[disabled] {{ color: var(--faint); border-color: rgba(228, 227, 221, 0.08); cursor: default; }}
.mt-quiet {{ color: var(--muted); }}

@property --glow {{ syntax: "<color>"; inherits: true; initial-value: #2C2A2B; }}
.mt-vitrine {{ position: relative; height: clamp(420px, 60vh, 620px); border-radius: 22px; overflow: hidden;
  background: radial-gradient(ellipse 58% 62% at 50% 56%, var(--glow, #2C2A2B) 0%, #141517 62%, var(--vitrine) 100%);
  transition: --glow 900ms ease; color: var(--vitrine-text);
  box-shadow: inset 0 0 0 1px rgba(228, 227, 221, 0.07); }}
.mt-canvas {{ position: absolute; inset: 0; width: 100%; height: 100%; display: block; touch-action: none; }}
.mt-vitrine-top {{ position: absolute; left: 24px; top: 18px; right: 24px; display: flex; flex-wrap: wrap;
  align-items: center; justify-content: space-between; gap: 12px; pointer-events: none; }}
.mt-vitrine-top button {{ pointer-events: auto; }}
.mt-modes {{ display: flex; flex-wrap: wrap; align-items: center; gap: 18px; }}
.mt-mode {{ height: 34px; border-bottom: 2px solid transparent; font-size: 15px; color: var(--vitrine-muted); }}
.mt-mode:hover {{ color: var(--vitrine-text); }}
.mt-mode.mt-on {{ border-bottom-color: var(--accent); color: var(--vitrine-text); font-weight: 700; }}
.mt-modes-cut {{ width: 1px; height: 18px; background: rgba(228, 227, 221, 0.25); }}
.mt-toggle {{ display: flex; align-items: center; gap: 7px; height: 34px; font-size: 15px;
  color: var(--vitrine-muted); }}
.mt-toggle::before {{ content: ""; width: 12px; height: 12px; border-radius: 3px; box-sizing: border-box;
  border: 1.5px solid var(--vitrine-muted); }}
.mt-toggle.mt-on {{ color: var(--vitrine-text); }}
.mt-toggle.mt-on::before {{ border-color: var(--accent); background: var(--accent); }}
.mt-vitrine-tools {{ display: flex; gap: 6px; }}
.mt-round-light {{ width: 38px; height: 38px; border-radius: 19px; border: 1px solid rgba(228, 227, 221, 0.22);
  background: rgba(22, 23, 26, 0.35); color: var(--vitrine-text); display: flex; align-items: center;
  justify-content: center; }}
.mt-round-light:hover {{ border-color: rgba(228, 227, 221, 0.6); }}
.mt-label {{ position: absolute; left: 28px; bottom: 24px; display: flex; flex-direction: column; gap: 4px;
  max-width: min(460px, 60%); pointer-events: none; }}
.mt-model-name {{ margin: 0; font-family: var(--display); font-weight: 700; font-size: 44px; line-height: 1;
  letter-spacing: -0.01em; color: var(--vitrine-text); overflow-wrap: anywhere; }}
.mt-model-slot {{ font-family: var(--display); font-style: italic; font-size: 17px; color: #D5D4CD; }}
.mt-model-facts {{ font-size: 14px; color: var(--vitrine-muted); text-wrap: pretty; }}
.mt-vitrine-corner {{ position: absolute; right: 24px; bottom: 24px; display: flex; align-items: flex-end; gap: 16px;
  pointer-events: none; }}
.mt-state {{ display: flex; flex-direction: column; align-items: flex-end; gap: 2px; font-size: 13px;
  color: var(--vitrine-muted); text-align: right; }}
.mt-state-title {{ display: flex; align-items: center; gap: 8px; color: #E4E3DD; font-size: 14px; }}
.mt-dot {{ width: 8px; height: 8px; border-radius: 50%; display: inline-block; box-sizing: border-box;
  border: 1.5px solid var(--vitrine-muted); }}
.mt-dot.mt-dot-on {{ border: 0; background: var(--accent); }}
.mt-vitrine-message {{ position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center;
  justify-content: center; gap: 6px; padding: 24px; text-align: center; pointer-events: none; }}
.mt-vitrine-message-title {{ font-family: var(--display); font-weight: 700; font-size: 26px; }}
.mt-vitrine-message-text {{ color: var(--vitrine-muted); max-width: 420px; }}

.mt-actions {{ display: flex; flex-direction: column; gap: 12px; padding: 0 4px 8px; }}
.mt-actions-head {{ display: flex; align-items: baseline; gap: 8px 12px; flex-wrap: wrap; }}
.mt-section-title {{ margin: 0; font-family: var(--display); font-weight: 700; font-size: 20px; }}
.mt-rates {{ display: flex; gap: 4px; margin-left: auto; }}
.mt-rate {{ height: 28px; padding: 0 10px; border-radius: 14px; color: var(--muted); }}
.mt-rate.mt-on {{ background: var(--well); color: var(--ink); font-weight: 700; }}
.mt-strip {{ display: flex; gap: 4px; overflow-x: auto; padding-bottom: 2px; }}
.mt-segment {{ position: relative; min-width: max-content; height: 54px; flex-basis: 0; flex-shrink: 1; display: flex;
  flex-direction: column; justify-content: center; padding: 0 12px; border-radius: 10px; background: var(--well);
  font-weight: 500; white-space: nowrap; }}
.mt-segment:hover {{ background: var(--well-hover); }}
.mt-segment.mt-on {{ background: var(--fill); color: var(--on-fill); }}
.mt-segment-detail {{ font-size: 12px; font-weight: 400; color: var(--muted); }}
.mt-segment.mt-on .mt-segment-detail {{ color: #D9DCF5; }}
.mt-segment-lock {{ position: absolute; top: 9px; right: 9px; }}

.mt-transport {{ display: flex; align-items: center; gap: 18px; flex-wrap: wrap; }}
.mt-transport-buttons {{ display: flex; align-items: center; gap: 8px; }}
.mt-round {{ width: 40px; height: 40px; border-radius: 20px; border: 1px solid rgba(228, 227, 221, 0.15);
  background: transparent; display: flex; align-items: center; justify-content: center; cursor: pointer; }}
.mt-round:hover {{ border-color: var(--ink); }}
.mt-play {{ width: 52px; height: 52px; border-radius: 26px; border: 0; background: var(--fill);
  color: var(--on-fill); display: flex; align-items: center; justify-content: center; cursor: pointer; }}
.mt-play:hover {{ background: var(--fill-hover); }}
.mt-ruler {{ flex: 1 1 320px; min-width: 0; position: relative; height: 48px; cursor: ew-resize; touch-action: none; }}
.mt-ruler-line {{ position: absolute; left: 0; right: 0; top: 30px; height: 1px;
  background: rgba(228, 227, 221, 0.26); }}
.mt-tick {{ position: absolute; top: 21px; height: 9px; width: 2px; margin-left: -1px; border-radius: 1px;
  background: rgba(228, 227, 221, 0.29); }}
.mt-tick-major {{ top: 14px; height: 16px; }}
.mt-tick-passed {{ background: var(--accent); }}
.mt-head {{ position: absolute; top: 8px; width: 3px; height: 26px; margin-left: -1.5px; border-radius: 2px;
  background: var(--accent); }}
.mt-head-label {{ position: absolute; top: -10px; transform: translateX(-50%); font-size: 12px; font-weight: 700;
  color: var(--accent); }}
.mt-ruler-end {{ position: absolute; top: 34px; font-size: 12px; color: var(--muted); }}
.mt-ruler-start {{ left: 0; }}
.mt-ruler-last {{ right: 0; }}
.mt-readout {{ display: flex; flex-direction: column; min-width: 200px; }}
.mt-readout-key {{ font-size: 18px; font-weight: 500; }}
.mt-readout-speed {{ font-size: 13px; color: var(--muted); }}

.mt-editor {{ display: flex; flex-direction: column; gap: 10px; margin: 0 4px 8px; padding: 12px 16px 14px;
  background: var(--raised); border: 1px solid var(--line); border-radius: 16px; }}
.mt-editor-head {{ display: flex; align-items: baseline; gap: 8px 12px; flex-wrap: wrap; }}
.mt-editor-save {{ display: flex; align-items: center; gap: 6px; margin-left: auto; }}
.mt-editor-tools {{ display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }}
.mt-tools {{ display: flex; gap: 2px; padding: 2px; margin-right: 8px; border-radius: 17px; background: var(--well); }}
.mt-tool {{ height: 30px; padding: 0 16px; border-radius: 15px; color: var(--muted); }}
.mt-tool.mt-on {{ background: var(--fill); color: var(--on-fill); font-weight: 700; }}
.mt-edit-press {{ height: 32px; padding: 0 14px; border-radius: 16px; border: 1px solid var(--line-strong); }}
.mt-edit-press:hover {{ border-color: var(--ink); }}
.mt-save {{ height: 32px; padding: 0 16px; border-radius: 16px; background: var(--fill); color: var(--on-fill);
  font-weight: 700; }}
.mt-save:hover {{ background: var(--fill-hover); }}
.mt-edit-press[disabled], .mt-save[disabled] {{ color: var(--faint); background: transparent;
  border: 1px solid var(--line); cursor: default; }}
.mt-editor-note {{ display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }}
.mt-editor-answer {{ display: flex; flex-direction: column; gap: 2px; }}

.mt-tabs {{ display: flex; gap: 22px; border-bottom: 1px solid rgba(228, 227, 221, 0.11); }}
.mt-tab {{ height: 38px; color: var(--muted); border-bottom: 2px solid transparent; margin-bottom: -1px; }}
.mt-tab.mt-on {{ color: var(--ink); font-weight: 700; border-bottom-color: var(--accent); }}
.mt-section-head {{ display: flex; align-items: baseline; justify-content: space-between; gap: 8px; }}
.mt-column-name {{ font-size: 13px; color: var(--muted); }}
.mt-meshes {{ display: flex; flex-direction: column; gap: 2px; margin-top: -8px; }}
.mt-mesh {{ display: grid; grid-template-columns: 32px 22px minmax(0, 1fr) auto; align-items: center; gap: 10px;
  min-height: 46px; padding: 0 10px 0 6px; border-radius: 10px; }}
.mt-mesh:hover {{ background: var(--raised); }}
.mt-mesh.mt-on {{ background: var(--well); color: var(--accent); font-weight: 700; }}
.mt-mesh-index {{ order: -1; color: var(--muted); text-align: right; font-weight: 400; }}
.mt-mesh-count {{ text-align: right; }}
.mt-swatch {{ order: -2; width: 32px; height: 32px; border-radius: 6px; object-fit: cover; image-rendering: pixelated;
  background: var(--well-hover); display: block; }}
.mt-swatch-large {{ width: 88px; height: 88px; border-radius: 10px; flex-shrink: 0; }}
.mt-card {{ background: var(--raised); border: 1px solid var(--line); border-radius: 16px; padding: 16px;
  display: flex; flex-direction: column;
  gap: 12px; }}
.mt-card-head {{ display: flex; gap: 14px; align-items: flex-start; }}
.mt-card-titles {{ display: flex; flex-direction: column; gap: 2px; min-width: 0; }}
.mt-card-title {{ font-family: var(--display); font-weight: 700; font-size: 20px; line-height: 1.15; }}
.mt-small {{ font-size: 13px; color: var(--muted); overflow-wrap: anywhere; }}
.mt-fields {{ display: grid; grid-template-columns: auto minmax(0, 1fr); gap: 2px 14px; font-size: 13px; }}
.mt-field-name {{ color: var(--muted); }}
.mt-field-value {{ overflow-wrap: anywhere; }}
.mt-bad {{ color: var(--bad); }}
.mt-warn {{ color: var(--warn); }}
.mt-rows {{ display: flex; flex-direction: column; }}
.mt-row {{ display: flex; align-items: center; gap: 8px; min-height: 40px; padding: 0 4px;
  border-top: 1px solid rgba(228, 227, 221, 0.09); width: 100%; }}
.mt-row:last-child {{ border-bottom: 1px solid rgba(228, 227, 221, 0.09); }}
.mt-row.mt-on {{ color: var(--accent); font-weight: 700; }}
.mt-row-count {{ order: 2; margin-left: auto; color: var(--muted); font-weight: 400; }}
.mt-finding {{ display: flex; flex-direction: column; gap: 2px; padding: 10px 4px;
  border-top: 1px solid rgba(228, 227, 221, 0.09); width: 100%; }}
.mt-parts .q-tree__node-header {{ border-radius: 8px; }}
.mt-parts .q-tree__node--selected {{ color: var(--accent); font-weight: 700; }}

.mt-palette {{ width: min(560px, 92vw); background: var(--raised); border: 1px solid var(--line); border-radius: 18px;
  padding: 10px; }}
.mt-palette-results {{ display: flex; flex-direction: column; max-height: 50vh; overflow-y: auto; }}
.mt-palette-item {{ display: flex; align-items: baseline; justify-content: space-between; gap: 12px; padding: 8px 10px;
  border-radius: 10px; width: 100%; }}
.mt-palette-item:hover {{ background: var(--well); }}
.mt-palette-kind {{ font-size: 13px; color: var(--muted); }}
.mt-palette-input {{ margin: 0 6px 6px; }}
.mt-palette-none {{ padding: 10px; }}
"""


def paint() -> None:
    ui.colors(
        primary=STUDIO.primary,
        secondary=STUDIO.secondary,
        accent=STUDIO.accent,
        dark=STUDIO.surface,
        dark_page=STUDIO.background,
        positive=STUDIO.success,
        negative=STUDIO.error,
        warning=STUDIO.warning,
    )
    ui.add_head_html(FONTS)
    ui.add_css(STYLE)
