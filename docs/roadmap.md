# Roadmap

Effort estimates assume someone who knows both SDKs. Items are ordered by
how much they unblock.

## 0. Validate the proof of concept (days)

0. Licensing first: run `hapi_probe` under the Indie license (does the
   engine accept the plugin?), email SideFX, and run `tools/hom_probe.py`
   on the same HDAs. Decide which backend is primary. See
   `docs/indie-licensing-and-alternatives.md`.

1. Run `hapi_probe` on a machine with Houdini against three HDAs: a simple
   SOP asset, an OBJ subnet asset with several objects, one with packed
   primitive instancing. Check cook time, parm tree, OBJ facing/scale.
2. Compile the plugin against the Cinema 4D 2025 SDK, fix signature
   drift, register real plugin ids.
3. Load the same HDAs in Cinema, confirm: UI builds, slider edits cook,
   children feed inputs, time-dependent assets animate, scene save/load
   restores values, copy/paste of the object instantiates a new node.
4. Decide the winding default from what you see (switch is in the UI).

## 1. Make it usable day to day (2 to 4 weeks)

- HOM backend in the plugin: a `HomBackend` that fills `ParmSet`,
  `AssetOutput` and consumes `InputMesh` through the bridge protocol
  (spawn hython, or attach to a Houdini GUI running `start_in_ui`), with
  a preference to choose HAPI or HOM. Shelf tool for the attached mode.

- Multiparm add/remove in the UI (`InsertMultiparmInstance` /
  `RemoveMultiparmInstance`, then rebuild description).
- Float and color ramps as `SplineData` / `Gradient` custom GUIs.
- Cinema n-gons for faces with more than four vertices (Modeling kernel).
- Input normals and vertex colors; incremental input updates instead of
  delete and recreate.
- `name` attribute and `c4d_split_attr` splitting into separate objects
  with stable identity across cooks (Unreal's output identifier idea).
- `lod<N>`, `collision_geo`, `main_geo` group semantics (hide/visibility).
- Attribute instancers resolving `c4d_instance` to scene objects by name.
- HARS death detection and automatic restart (`HAPI_IsSessionValid` on
  failure, then `InvalidateAllAssets`).
- Preferences dialog (library path, session type, port, log file, OTL path)
  instead of the world-container only.
- Proper undo for parameter edits (already works for container values;
  verify buttons and reloads).

## 2. Production features (1 to 3 months)

- Materials: create Cinema/Redshift materials from Principled Shader
  parameters and textures (`HAPI_RenderTextureToImage`,
  `HAPI_ExtractImageToFile`), matched by node path; keep the by-name
  Texture Tag path as the simple option.
- Volumes: VDB parts to Cinema volume objects, heightfields to displaced
  planes or Fields.
- Bake command: collapse the generator into plain objects; optional Alembic
  / VDB cache export so render nodes need no Houdini Engine license.
- Point clouds / particles as Thinking Particles or Matrix objects.
- Session Sync launcher (`houdini -hess=pipe:<name>` from the plugin, like
  Max) with viewport sync.
- Parm tags: units (so Houdini metre parms show as cm), `asset_ref`,
  `file_readonly`.
- Python API (`c4d` module helpers) mirroring the Max MaxScript interface:
  create asset, set parm, cook, bake.
- PDG cooking for TOP networks.

## 3. Keep the door open

- `hapi_core` has no Cinema dependency: a command-line baker for the farm
  and a Blender/other-host port are both possible from the same code.
- Regenerate `HapiFunctions.inc` per Houdini major; keep the `has()` checks
  for anything newer than Houdini 20.5 (shared memory, input curves).
