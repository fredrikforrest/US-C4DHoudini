# Test plan for the validation session

Prerequisites on the workstation: Houdini 21/22 with a Houdini Engine or FX
license, the Cinema 4D 2024/2025 C++ SDK and a Cinema seat that allows
`c4dpy`. The session needs tool access to run `hython`, `c4dpy` and shell
commands, and ideally to a running Cinema for screenshots.

## Phase A: core against Houdini (no Cinema needed)

1. Build: `cmake -S . -B build -DHAPI_INCLUDE_DIR="$HFS/toolkit/include" && cmake --build build -j && ctest --test-dir build`.
   Also regenerate `HapiFunctions.inc` from the installed headers and diff it
   against the committed one; commit the regenerated file if it differs.
2. Build the test HDAs: `hython tests/houdini/make_test_hdas.py tests/houdini/hda`.
   The script is written blind too; fix it until all seven assets save.
3. For each HDA run `build/hapi_probe tests/houdini/hda/<name>.hda` and record:

| HDA | Expect |
| --- | --- |
| facing_check | 1 object, 1 geo, 1 mesh part with 11 points (3 + 8). `--obj` dump imported in Cinema: triangle faces +Y, marker box at z = -200. If the triangle faces -Y the default must become `reverseWinding = true`; if the marker is at +200 the Z flip is wrong. |
| parm_zoo | Parm tree shows two tabs + collapsible folder, vector size 3, color size 3 with RGBA names, int and string menus with tokens, multiparm `points` with instances, two ramps flagged as ramps, hide/disable flags, node parm `extra_geo`. `--parm divisions=6` changes point count, `--parm smooth=1` makes `hidden_when_smooth` invisible on the post-cook refetch, `--parm reset=1` presses the button. |
| multi_object | OBJ asset: 3 objects with distinct transforms, `--parm spread=2` moves them. |
| packed_scatter | 1 instancer part with 200 instances of 1 part, the box part flagged instanced, `--parm count=50` gives 50. Time pipe vs `--session shm` on `count=5000`. |
| curves_out | 3 curve parts or one part with 3 curves: linear 24 cvs, NURBS, linear line 6 cvs. |
| deformer | Flagged time dependent. `--input` with a cube OBJ returns 6 faces; `--frame 1` vs `--frame 2` give different P. `--parm preset=b` triggers the Python callback: `amount` reads back 1.0. |
| named_parts | 3 `name` values, `c4d_material` per prim, 3 prim groups, uv vertex, N vertex, Cd point, materials uniform. |

4. Error paths: wrong library path, HDA path that does not exist, asset with
   a deliberate cook error (divide by zero in a wrangle), kill HARS mid
   session and call again. Each must surface a readable message, never a
   hang or crash.
5. Measure: HARS start-up time, cook + fetch time for `count=5000`, and the
   same over shared memory. Note numbers in this file.

## Phase B: plugin build

1. `tools/sync_plugin_sources.sh`, project tool, build. Fix signature drift
   and record every change in a short list in the README's "Known compile
   risks" (then delete that section once clean).
2. Load in Cinema, check the console shows the libHAPIL path and version
   on first use of a Houdini Asset object, and that *Session Info* works.

## Phase C: plugin behaviour

Run `tests/c4d/c4d_regression.py` from the Script Manager (or `c4dpy` with
the plugin path). It covers: facing and scale, description contents and
conditional visibility, recook on edit, child input round trip, time
dependence, multi-instance count, tags from attributes, save/reload with
stable ids. Then by hand:

- Undo a slider edit: value and geometry both revert.
- Copy and paste the object: both cook independently (different node ids
  in the console log).
- Reload HDA after editing it in Houdini: new parm shows up, old values kept.
- Restart Session command with three assets in the scene: all three recover.
- Link field input vs child input precedence.
- Auto Cook off: edits do nothing until the Cook button.
- Render (Standard/Redshift) with a texture tag matched by name from
  `named_parts`: material lands on the `mat_steel` selection.

## Phase D: decide and record

Update README "Build status" and "Coordinate conventions" with what was
observed, commit the measured numbers, then start roadmap item 1.
