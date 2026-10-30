# Blender source architecture (5.2 LTS)

A tour of the tree for people who want to change Blender, not just build it.
Paths are relative to the Blender checkout. Line counts are from `v5.2.3`.

| Area | Lines of code | What it is |
| --- | --- | --- |
| `source/blender/` | ~2.8M | Blender proper: data model, kernel, editors, nodes, render, Python API |
| `intern/` | ~360k | Blender Foundation libraries with their own API: Cycles, GHOST, guardedalloc, libmv, mantaflow, ... |
| `extern/` | ~560k | Third-party code vendored in-tree (Bullet, Audaspace, Mantaflow, gtest, ...) |
| `scripts/` | ~280k | Python: the whole UI layout, operators written in Python, presets, bundled add-ons |

## 1. The layering

Blender is a layered C/C++ program with a Python layer on top. Lower layers
must not include higher layers.

```
scripts/ (Python: UI, Python operators, add-ons)        <- bpy / RNA
source/blender/python/        Python C API bindings (bpy, mathutils, gpu, bmesh ...)
source/blender/windowmanager/ event loop, operators, keymaps, file read/write, jobs
source/blender/editors/       one directory per editor + shared tools (transform, object, mesh ...)
source/blender/draw/          viewport draw engines: EEVEE, Workbench, Overlay, Grease Pencil, Compositor
source/blender/render/        render pipeline glue (engines plug in via RenderEngine)
source/blender/nodes/         node definitions and evaluation (geometry, shader, compositor, function)
source/blender/modifiers/     mesh/curve modifiers (ModifierTypeInfo)
source/blender/depsgraph/     dependency graph: what to re-evaluate, and in what order
source/blender/blenkernel/    "BKE": the kernel. Everything that operates on data blocks, independent of UI
source/blender/makesrna/      "RNA": reflection layer that exposes DNA to UI, Python and animation
source/blender/makesdna/      "DNA": the struct definitions that ARE the .blend file format
source/blender/blenloader/    reads/writes .blend, runs versioning when old files are opened
source/blender/blenlib/       "BLI": containers, math, strings, paths, threading, no Blender data types
source/blender/gpu/           GPU abstraction (OpenGL, Vulkan, Metal backends, shader compilation)
intern/ghost/                 window system / input abstraction (X11, Wayland, Win32, Cocoa)
intern/guardedalloc/          MEM_* allocator used everywhere
```

Prefixes tell you the layer from the function name:
`BLI_` blenlib, `BKE_` blenkernel, `DNA_`/`DEG_`/`RNA_`/`WM_`/`ED_`/`GPU_`/`DRW_`/`NOD_`/`MOD_`/`BLO_`/`IMB_`.

## 2. DNA and RNA, the two halves of the data model

**DNA** (`source/blender/makesdna/DNA_*.h`, 97 headers). Plain C structs such as
`Object`, `Mesh`, `Scene`, `ModifierData`. The `makesdna` tool parses these
headers at build time and embeds a description of every struct in the binary.
That description is written into every `.blend` file, which is why old files
open in new versions and new files mostly open in old versions. Rules that
follow from this:

- Members must be 8-byte aligned and the struct size a multiple of 8. Use `char _pad[N]` to pad.
- Renaming a member needs an entry in `source/blender/makesdna/intern/dna_rename_defs.h`.
- Any new member needs a default. Since DNA became C++, defaults are written as default
  member initializers in the DNA header itself (`float crease_threshold = DEG2RAD(140.0f);`
  in `DNA_object_types.h:404`); old files get theirs in
  `source/blender/blenloader/intern/versioning_*.cc`.
- `source/blender/makesdna/intern/makesdna.cc` lists every DNA header it processes.

**RNA** (`source/blender/makesrna/intern/rna_*.cc`, 108 files). A reflection
layer built on top of DNA. `rna_modifier.cc`, `rna_object.cc` etc. declare
properties with `RNA_def_property_*`. Everything that goes through RNA gets, for
free: the Python API (`bpy.data`, `bpy.types`), UI buttons via `layout.prop()`,
animation (F-Curves drive RNA paths), drivers, overrides, and the undo system's
knowledge of what changed. If you add a struct member and want it visible
anywhere, you define it in RNA.

## 3. Versioning: how files survive your changes

`source/blender/blenkernel/BKE_blender_version.h` holds
`BLENDER_VERSION` (502), `BLENDER_FILE_SUBVERSION` (45) and the minimum
readable version. Every time you change DNA in a way that needs fixing up old
files, you:

1. Bump `BLENDER_FILE_SUBVERSION`.
2. Add a block to `source/blender/blenloader/intern/versioning_520.cc` guarded by
   `if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, <new subversion>)) { ... }`.
3. Set defaults for new scenes in `versioning_defaults.cc` and for new
   preferences in `versioning_userdef.cc`.

A fork should pick its own subversion range (see `03-fork-workflow.md`) so
that files saved by the fork still open in upstream Blender.

## 4. The window manager and operators

`source/blender/windowmanager/` owns the event loop (`wm_event_system.cc`), the
operator system (`wm_operators.cc`, `wm_operator_type.cc`), keymaps
(`wm_keymap.cc`), file I/O entry points (`wm_files.cc`: reading the startup
file, recover, autosave), jobs/threads (`wm_jobs.cc`) and the splash screen
(`wm_splash_screen.cc`).

An **operator** is the unit of user action. In C++ it is a `wmOperatorType`
with `exec`, `invoke`, `modal`, `poll` callbacks and RNA properties; in Python
it is a `bpy.types.Operator` subclass. Both are registered under an idname
such as `OBJECT_OT_add` and both appear as `bpy.ops.object.add()`.

- Example: `OBJECT_OT_add` in `source/blender/editors/object/object_add.cc:741`.
- Registration: `source/blender/editors/object/object_ops.cc:103`
  (`WM_operatortype_append`), called from
  `source/blender/editors/space_api/spacetypes.cc` via `ED_operatortypes_object()`.

## 5. Editors

`source/blender/editors/` has one `space_*` directory per editor (view3d,
outliner, node, image, sequencer, ...) and shared tool directories (`transform`,
`object`, `mesh`, `sculpt_paint`, `uvedit`, `interface`, `io`). Each space
registers a `SpaceType` with region types and draw callbacks; the actual panel
layout is Python in `scripts/startup/bl_ui/space_*.py`.

`editors/interface/` is the UI toolkit: buttons, layouts, templates
(`interface_templates.cc`), icons, text drawing. Icons are SVG files in
`release/datafiles/icons_svg/` compiled into the binary by
`source/blender/editors/datafiles/CMakeLists.txt`.

## 6. Nodes

`source/blender/nodes/` holds four node systems that share one data model
(`bNodeTree` in DNA): `geometry/` (270 node files), `shader/`, `composite/`,
`function/` (field/math nodes). Each node is a single file with a declaration
callback (sockets), an execute callback, and a registration callback hooked up
with `NOD_REGISTER_NODE`. See `02-adding-features.md` for the recipe.

Geometry nodes execute through the lazy-function framework
(`NOD_geometry_nodes_lazy_function.hh`) and the `functions/` + `geometry/`
directories under `source/blender/`, which hold the actual geometry algorithms
(`GEO_*` headers) so nodes stay thin.

## 7. Modifiers

`source/blender/modifiers/intern/MOD_*.cc` (94 files). Each file fills a
`ModifierTypeInfo` (`MOD_wireframe.cc:156` is a compact example), including
`panel_register` for its UI, and is wired into the enum
`eModifierType_*` in `DNA_modifier_types.h`, the init table in
`MOD_util.cc:204`, and RNA in `rna_modifier.cc` (`rna_def_modifier_wireframe` at
line 7404). New modifiers upstream are increasingly implemented as geometry
node groups instead; consider that path first.

## 8. Depsgraph, evaluation and the draw engines

`source/blender/depsgraph/` builds a graph of what depends on what and
evaluates copies of the data ("CoW" / evaluated copies). Code that reads
evaluated data uses `DEG_get_evaluated_*`. Changing an object must be followed
by `DEG_id_tag_update()` and a `WM_event_add_notifier()` so the UI refreshes.

`source/blender/draw/engines/`: `eevee` (real-time renderer), `workbench`
(solid mode), `overlay`, `gpencil`, `image`, `compositor`, `select`, `external`
(for third-party engines). Cycles lives in `intern/cycles/` and talks to
Blender through `intern/cycles/blender/`. Shaders are GLSL under
`source/blender/draw/engines/*/shaders/` and `source/blender/gpu/shaders/`,
compiled at build time into the binary via `gpu_shader_create_info`.

## 9. Python

- `source/blender/python/intern/` implements `bpy` (`bpy.cc`, `bpy_app.cc`,
  `bpy_rna.cc` turns RNA into Python classes). `mathutils`, `bmesh`, `gpu`
  live in sibling directories.
- `scripts/modules/` is the pure-Python side (`bpy/utils.py`, `bpy_extras`,
  `addon_utils.py`, keymap utilities).
- `scripts/startup/bl_ui/` is the entire default UI: 80 modules listed in
  `bl_ui/__init__.py`. `scripts/startup/bl_operators/` are Python operators.
- `scripts/presets/keyconfig/keymap_data/blender_default.py` is the default
  keymap, as data.
- `scripts/addons_core/` holds the add-ons that ship inside Blender (glTF,
  FBX, Rigify, Node Wrangler, the extensions client `bl_pkg`, ...). Everything
  else is an extension installed from a remote repository (default
  `https://extensions.blender.org`, stored per user in the preferences).
- `scripts/startup/bl_app_templates_system/` are the app templates on the
  splash screen (2D Animation, Sculpting, VFX, ...): each is a folder with an
  `__init__.py` and a `startup.blend`. This is the lightest way to ship a
  customized "mode" of Blender without touching C++.

## 10. Startup, data files and branding

- `source/creator/creator.cc:332` is `main()`. Argument parsing is `creator_args.cc`.
- `release/datafiles/startup.blend` is compiled into the binary (datatoc) and
  is what "Load Factory Settings" loads. `preview.blend`, `splash.png`,
  `bfont.pfb` are embedded the same way (see
  `source/blender/editors/datafiles/CMakeLists.txt:1072-1084`).
- `release/datafiles/colormanagement/` (OpenColorIO config), `studiolights/`,
  `fonts/`, `icons_svg/`, `locale/` are installed next to the binary.
- User paths come from `source/blender/blenkernel/intern/appdir.cc`
  (`~/.config/blender/5.2/`, `BLENDER_USER_*` environment variables).
- The window title is set in `source/blender/windowmanager/intern/wm_window.cc:694`.
- Desktop integration: `release/freedesktop/blender.desktop`,
  `org.blender.Blender.metainfo.xml`, `icons/`.
- Links in the Help menu and splash: `scripts/startup/bl_ui/space_topbar.py`
  (lines 609-622) and `scripts/startup/bl_operators/wm.py` (lines 1156, 1377, 3479-3551).

## 11. Build system

Top-level `CMakeLists.txt` declares every `WITH_*` option (~2650 lines).
`build_files/cmake/platform/platform_unix.cmake` is where libraries are found
on Linux. `build_files/cmake/config/*.cmake` are preset option sets
(`blender_lite`, `blender_release`, `blender_full`, `blender_headless`,
`bpy_module`). `GNUmakefile` is a convenience wrapper: `make`, `make lite`,
`make update`, `make test`, `make format`, `make check_*`.

`build_files/build_environment/` builds every third-party dependency from
source (`make deps`) and `versions.cmake` in there is the authoritative list of
the versions upstream Blender is tested against. Official builds use the
pre-built results of that, checked in as Git LFS submodules under
`lib/<platform>`.

## 12. Tests and code quality

- `tests/python/` (Python regression tests run through Blender), `tests/gtests/`
  (C++ unit tests, `WITH_GTESTS=ON`), `tests/files/` (test data, Git LFS).
  `make test` runs `ctest` on the build directory.
- `.clang-format`, `.clang-tidy`, `.editorconfig` and `pyproject.toml` define
  style; `make format` applies clang-format and autopep8. Upstream will not
  take patches that are not formatted.
- `tools/utils_maintenance/` has the checkers behind `make check_*`.

## 13. Where to read more

- Developer handbook: https://developer.blender.org/docs/ (building, code style, submitting patches)
- Architecture notes in-tree: `source/blender/blenkernel/BKE_*.hh` headers are
  well commented; `source/blender/blenloader/intern/versioning_xxx_template.cc`
  documents the versioning conventions.
- Python API reference matches the version: https://docs.blender.org/api/5.2/
