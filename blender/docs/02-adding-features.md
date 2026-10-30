# Adding features to the fork

Recipes, cheapest first. Every file path was checked against `v5.2.3`.
Rebuild with `ninja -C <build dir>`; only changed targets recompile.

## Rule of thumb: pick the lowest layer that does the job

| You want to... | Do it in | Rebuild needed? |
| --- | --- | --- |
| Add menus, panels, buttons, operators that drive existing functionality | Python (`scripts/startup/bl_ui`, `bl_operators`) or an add-on | No |
| Ship a preconfigured "mode" (own startup file, UI layout, keymap) | App template (`scripts/startup/bl_app_templates_system/<Name>/`) | No |
| Change default keymap or preferences | `scripts/presets/keyconfig/keymap_data/blender_default.py`, `versioning_userdef.cc` | No / yes |
| New geometry/shader/compositor node | C++ in `source/blender/nodes/<system>/nodes/` | Yes |
| New operator that needs internal data access or speed | C++ in `source/blender/editors/<area>/` | Yes |
| New data on objects/scenes that must be saved in .blend | DNA + RNA + versioning | Yes |
| New modifier | `source/blender/modifiers/intern/MOD_*.cc` + DNA + RNA | Yes |
| New file format import/export | `source/blender/io/<format>/` + operator in `editors/io/` | Yes |
| Branding / splash / default scene | `release/datafiles/`, `wm_window.cc`, `release/freedesktop/` | Yes |

Python changes take effect on restart (or `F3 > Reload Scripts`); nothing to
compile. That is where most product customization should start.

## Recipe 1: a Python operator and menu entry (no compile)

1. Create `scripts/startup/bl_operators/fork_tools.py`:

```python
import bpy
from bpy.types import Operator


class FORK_OT_hello(Operator):
    """Say hello from the fork"""
    bl_idname = "fork.hello"
    bl_label = "Fork Hello"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        self.report({'INFO'}, f"Hello from {bpy.app.version_string}")
        return {'FINISHED'}


classes = (FORK_OT_hello,)
```

2. Add `"fork_tools"` to the `_modules` list in
   `scripts/startup/bl_operators/__init__.py` (line 13). That file registers
   every `classes` tuple it finds.
3. Put it in a menu: in `scripts/startup/bl_ui/space_view3d.py`, inside
   `VIEW3D_MT_object.draw()` (class at line 2806), add `layout.operator("fork.hello")`.
4. Test headless: `blender -b --python-expr "import bpy; bpy.ops.fork.hello()"`.

Prefer a new module over editing upstream modules: it keeps your diff out of
files that change every release.

## Recipe 2: an app template (no compile)

An app template is a folder under `scripts/startup/bl_app_templates_system/`
with an `__init__.py` (register/unregister, usually hooking
`bpy.app.handlers.load_factory_startup_post`) and an optional `startup.blend`.
Look at `Sculpting/__init__.py` for a 30-line example. Templates can also
override UI by replacing `bl_ui` classes (`scripts/modules/bl_app_override/`).
Users pick them on the splash screen or with `--app-template <Name>`.

Templates are the right tool when the fork is "Blender configured for our
pipeline" rather than "Blender with new algorithms".

## Recipe 3: a C++ operator

Operators live next to the data they act on, e.g. object operators in
`source/blender/editors/object/`.

1. Implement it (pattern from `object_add.cc:700-760`):

```cpp
static wmOperatorStatus fork_hello_exec(bContext *C, wmOperator *op)
{
  Object *ob = CTX_data_active_object(C);
  if (ob == nullptr) {
    return OPERATOR_CANCELLED;
  }
  BKE_reportf(op->reports, RPT_INFO, "Active object: %s", ob->id.name + 2);
  /* If you changed data: DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
   * WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob); */
  return OPERATOR_FINISHED;
}

void OBJECT_OT_fork_hello(wmOperatorType *ot)
{
  ot->name = "Fork Hello";
  ot->description = "Report the active object";
  ot->idname = "OBJECT_OT_fork_hello";
  ot->exec = fork_hello_exec;
  ot->poll = ED_operator_objectmode;
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
```

2. Declare it in `source/blender/editors/object/object_intern.hh` and register
   it in `object_ops.cc` next to `WM_operatortype_append(OBJECT_OT_add)` (line 103).
3. Expose it in a menu from Python: `layout.operator("object.fork_hello")`.
   The idname `OBJECT_OT_fork_hello` becomes `bpy.ops.object.fork_hello`.
4. Properties go on `ot->srna` with `RNA_def_float`, `RNA_def_enum`, ... and
   are read with `RNA_float_get(op->ptr, "name")`.

The fork patch `patches/0002-example-fork-operator.patch` is exactly this and
was built and run headless against this tree (see `../README.md`).

## Recipe 4: a geometry node

Each node is one file, e.g. `source/blender/nodes/geometry/nodes/node_geo_mesh_primitive_cube.cc`:

```cpp
namespace blender::nodes::node_geo_fork_example_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Geometry>("Geometry");
  b.add_input<decl::Float>("Scale").default_value(1.0f).min(0.0f);
  b.add_output<decl::Geometry>("Geometry").propagate_all();
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry = params.extract_input<GeometrySet>("Geometry");
  const float scale = params.extract_input<float>("Scale");
  /* ... modify geometry, see GEO_* headers in source/blender/geometry/ ... */
  params.set_output("Geometry", std::move(geometry));
}

static void node_register()
{
  static bke::bNodeType ntype;
  geo_node_type_base(&ntype, "GeometryNodeForkExample", GEO_NODE_FORK_EXAMPLE);
  ntype.ui_name = "Fork Example";
  ntype.ui_description = "Example node added by the fork";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_fork_example_cc
```

Then:

1. Add the file to `source/blender/nodes/geometry/CMakeLists.txt` (alphabetical list, line ~192).
2. Add a legacy type number in `source/blender/blenkernel/BKE_node_legacy_types.hh`
   (`GEO_NODE_MESH_PRIMITIVE_CUBE` is 1032; forks should pick a high, unused
   range such as 9000+ and never reuse a number).
3. Add it to the Add menu: `scripts/startup/bl_ui/node_add_menu_geometry.py`
   (`self.node_operator(layout, "GeometryNodeMeshCube")` at line 567 shows the pattern).
4. Nodes with their own settings need a DNA storage struct (`DNA_node_types.h`)
   plus `ntype.initfunc`, `node_type_storage` and RNA via `NOD_rna_define.hh`
   helpers; grep an existing node with `storage` for the pattern.

Shader and compositor nodes follow the same file shape under `nodes/shader/nodes/`
and `nodes/composite/nodes/`, with GPU/material callbacks instead of `geometry_node_execute`.

## Recipe 5: new data in the .blend file (DNA + RNA + versioning)

Example: add `float fork_weight` to `Object`.

1. `source/blender/makesdna/DNA_object_types.h`: add the member, keep 8-byte
   alignment (replace padding or add `char _pad_fork[4]`).
2. Default value: write it as a default member initializer right in the DNA
   header (`float fork_weight = 1.0f;`, the way `DNA_object_types.h:404` does
   for `crease_threshold`). Files saved before the member existed read it as
   zero, so set the value for old files in
   `source/blender/blenloader/intern/versioning_520.cc` after bumping
   `BLENDER_FILE_SUBVERSION` in `BKE_blender_version.h` (see `01-architecture.md` section 3).
3. RNA: in `source/blender/makesrna/intern/rna_object.cc`,
   `RNA_def_property(srna, "fork_weight", PROP_FLOAT, PROP_FACTOR)` with
   `RNA_def_property_ui_text` and `RNA_def_property_update` (which tags the
   depsgraph). It is now `obj.fork_weight` in Python, animatable, and
   `layout.prop(obj, "fork_weight")` draws it.
4. If evaluation depends on it, read it from the evaluated object in the
   modifier/node/draw code and make sure the update callback tags
   `ID_RECALC_GEOMETRY` or `ID_RECALC_TRANSFORM`.

Forks must coordinate DNA changes carefully: a .blend saved by the fork keeps
the extra member (DNA is self-describing, upstream Blender ignores unknown
members), but structs that upstream later extends in the same spot will
conflict at merge time. Prefer appending at the end of structs and keep the
fork's subversion numbers far from upstream's (see `03-fork-workflow.md`).

## Recipe 6: a modifier

Modifiers are being superseded by geometry node assets; a node group saved in
`assets/nodes/*.blend` and marked as a modifier asset appears in the Add
Modifier menu with zero C++. Use that unless you need something nodes cannot do.

For a real C++ modifier copy `source/blender/modifiers/intern/MOD_wireframe.cc`
(250 lines): fill `ModifierTypeInfo` (`idname`, `struct_name`, `type`, `flags`,
`modify_mesh`, `panel_register`, `blend_write`/`blend_read`), add
`eModifierType_Fork = 87` in `DNA_modifier_types.h` (enum at lines 39-127), the
`ForkModifierData` struct, `INIT_TYPE(Fork)` in `MOD_util.cc:204`, the RNA
struct in `rna_modifier.cc` and the file in `modifiers/CMakeLists.txt`.

## Recipe 7: an import/export format

`source/blender/io/` has one directory per format (`wavefront_obj`, `ply`,
`stl`, `fbx`, `usd`, `alembic`, `grease_pencil`). Each has a small public
header, an `importer/` and `exporter/` directory, and tests in
`tests/python/bl_io_*`. The operators and file-browser options live in
`source/blender/editors/io/io_<format>.cc` and are exposed in the File menu by
`scripts/startup/bl_ui/space_topbar.py`. Python add-ons (see
`scripts/addons_core/io_scene_gltf2`) are the alternative when speed is not
critical.

## Recipe 8: branding and defaults

- Name in window title: `source/blender/windowmanager/intern/wm_window.cc:694`
  (`" - Blender {}"`).
- Splash image: replace `release/datafiles/splash.png` (LFS file; 1:1 size,
  see `splash_template.xcf`). The links drawn on it are in
  `wm_splash_screen.cc` and `scripts/startup/bl_ui/space_topbar.py:600-625`.
- Version string shown everywhere: `BKE_blender_version_string()` in
  `source/blender/blenkernel/intern/blender.cc:146`. `BLENDER_VERSION_SUFFIX`
  in `BKE_blender_version.h` is the place for a fork tag (it is "LTS" today).
- Default scene/workspaces: `release/datafiles/startup.blend`. Edit it in your
  build, `File > Defaults > Save Startup File`, then copy
  `~/.config/blender/5.2/config/startup.blend` back into the tree. Programmatic
  defaults that must apply to old startup files go in `versioning_defaults.cc`.
- Default preferences: default member initializers in `DNA_userdef_types.h`,
  plus `source/blender/blenloader/intern/versioning_userdef.cc`
  (`blo_do_versions_userdef` at line 817) for existing user configs.
- Config directory name (`~/.config/blender`): `source/blender/blenkernel/intern/appdir.cc`
  lines 224-230 and the `BLENDER_USER_*` environment variables. Changing it
  means user settings no longer collide with stock Blender; changing it also
  means existing users start from scratch.
- Desktop file, icons, AppStream metadata: `release/freedesktop/`.
- Extensions repository default: the `remote_url` field in `DNA_userdef_types.h:657`
  and `scripts/startup/bl_operators/wm.py:1156` point at `extensions.blender.org`.
  A fork that ships its own extensions server changes these.

The Blender name and logo are trademarks of the Blender Foundation. A modified
build that is distributed to others should carry its own name and logo; the
GPL covers the code, not the brand. See https://www.blender.org/about/logo/.
