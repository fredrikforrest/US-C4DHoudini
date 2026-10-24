# What the other plugins do, and what we took

Inventory of the four open-source SideFX plugins (checked out October 2026:
Unreal targets Houdini 22.0.475 / HAPI 9.0, Maya 22.0.461, Max and Unity on
the 22 line) and the mapping onto the Cinema 4D proof of concept.

Legend: **done** in this PoC, *partial*, planned (see roadmap), n/a.

## Session and library

| Feature | Unreal | Unity | Maya | Max | C4D PoC |
| --- | --- | --- | --- | --- | --- |
| Generated runtime function table | yes | yes (P/Invoke) | yes | yes | **done** (`HapiFunctions.inc`, generator shipped) |
| Houdini discovery (env, registry, defaults) | yes | yes | prefs + MEL | registry chain | **done** (`Lib::defaultSearchPaths`) |
| Named pipe / socket / shared memory | all | all | pipe/socket | all | **done** |
| In-process | hidden | yes | disabled (crashes) | no | available, not default |
| Auto-start HARS with `autoClose` | yes | yes | yes | yes | **done** |
| Connect to existing / Session Sync | yes | yes | no | yes (launches houdini.exe) | **done** (connect-first); launching Houdini: planned |
| Multiple sessions | yes | no | no | no | no |
| Exact-build version lock | registry keyed by build | compiled-in path | refuses mismatch | pinned | deliberately none; version printed |
| `HAPI_CLIENT_NAME` | unreal | unity | maya | 3dsmax | **done** (cinema4d) |
| Cook options exposed | yes | yes | some | some | **done** (`SessionOptions`) |

## Parameters

| Feature | Unreal | Unity | Maya | Max | C4D PoC |
| --- | --- | --- | --- | --- | --- |
| Int/float/string/toggle/color/button/file | yes | yes | yes | yes | **done** |
| Menus (int + string token) | yes | yes | yes | yes | **done** |
| Folders / tabs | yes | yes | yes | yes | **done** (groups) |
| Multiparm add/remove | yes | yes | yes | yes | *partial*: count shown, instances listed, no add/remove UI |
| Float / color ramps | yes | yes | yes | no | planned (preserved via preset) |
| Operator path (node) parms | yes | yes | yes | yes | **done** (link field -> input node) |
| Parm tags (units, asset_ref, file_readonly) | yes | some | some | no | planned |
| Visibility / disabled conditions | yes | yes | yes | yes | **done** (invisible skipped, disabled via `GetDEnabling`, refreshed after each edit) |
| Button strips | yes | no | no | no | no |
| Revert to defaults | yes | yes | yes | yes | planned (`HAPI_RevertParmToDefaults` is in the table) |
| Presets | no | yes | no | no | **done** (binary preset saved with object) |
| Deferred, batched cook | yes | yes | yes | no (cook per edit) | **done** |

## Inputs

| Feature | Unreal | Unity | Maya | Max | C4D PoC |
| --- | --- | --- | --- | --- | --- |
| Mesh: P, faces, N, uv(N), Cd/Alpha | yes | yes | yes | yes | *partial*: P, faces, uv, groups, material; N/Cd planned |
| Transform via `SetObjectTransform` | yes | yes | yes | yes | **done** |
| Several objects per input (merge) | yes | yes | yes | yes | **done** |
| Curves / splines | yes | yes | NURBS | linear/NURBS | **done** (CVs, closed, order) |
| Particles / points | n/a | n/a | yes | no | planned |
| Host material names on prims | `unreal_material` | yes | `maya_shading_group` | `hemax_material` | **done** (`c4d_material` from texture tags) |
| Selections as groups | yes | yes | sets | yes | **done** (polygon selection tags) |
| Incremental input update | yes | yes | partly | delete+recreate | delete+recreate (same as Max) |

## Outputs

| Feature | Unreal | Unity | Maya | Max | C4D PoC |
| --- | --- | --- | --- | --- | --- |
| Object / geo / part traversal | yes | yes | yes | yes | **done** (SOP and OBJ assets, output nodes preferred) |
| Mesh with n-gons | triangulates | triangulates | n-gons | n-gons | fan-triangulates >4 (Cinema n-gons planned) |
| N, uv, uv2, Cd, Alpha | yes | yes | yes | yes | **done** |
| Point vs vertex attribute owners | discovers owner | both | promote | point then vertex | **done** (vertex first) |
| Groups | LOD/collision splits | LOD/collision | sets | selections | **done** (selection tags); LOD semantics planned |
| `name` attribute splitting | split by attr | split by attr | no | no | planned (`nameAttr` already read) |
| Materials per face | yes | yes | yes | yes | **done** (selection tags + texture tag by name) |
| Material parameters / textures | full | full | phong + file | phong + texture | *partial*: node name/path, base color, roughness, metallic, opacity, texture path read; no material creation |
| Packed primitive instancers | yes | yes | yes | yes | **done** (multi-instance / render instance / copies) |
| Attribute (`*_instance`) instancers | yes | yes | yes | yes | *partial*: transforms + attribute read, no scene object lookup yet |
| Old-style object instancers | yes | yes | yes | yes | *partial*: transforms as nulls |
| Curves | splines | editable curves | NURBS/Bezier | linear/NURBS | **done** (linear/B-spline/Bezier approximation) |
| Volumes / heightfields | landscape, VDB | terrain | fluids | no | metadata only, placeholder null |
| Transforms (`HAPI_SRT`) | yes | yes | yes | yes | **done** |
| Time / `HAPI_SetTime` | yes | yes | yes | yes | **done** (time dependent assets only) |
| Bake / detach | yes | yes | no | yes | planned |
| PDG | yes | yes | no | yes | no |
| Cook log UI | window | console | console | log widget | **done** (status line, log tab, console) |

## Attribute conventions we honour

See README "Conventions read from HDAs". The Unreal list is far longer
(`unreal_uproperty_*`, landscape, foliage, level paths ...). Of those, the
ones that generalise and are worth adding next: `*_bake_name`/`*_bake_folder`,
`*_output_curve`, `lod<N>` / `collision_geo` group semantics, and a
`c4d_property_*` setter for arbitrary BaseContainer values (the Unreal
`uproperty` idea).

## Pitfalls the other codebases document, and what we did about them

- In-process sessions crash from library conflicts (Maya). Default is out
  of process; environment scrubbed while spawning HARS.
- Menu choice counts change between cooks (Maya). Parms are refetched after
  every edit and after every cook; description is marked dirty when
  visibility/disabled/menu sizes differ.
- Buttons fire on scene load (Maya). Buttons are commands, never values.
- Synchronous cooks freeze the UI (Max). Cooking thread + status polling +
  `HAPI_Interrupt` on `BaseThread::TestBreak`.
- No reconnect after HARS dies (Max). `Restart Session` command invalidates
  every object's node id (`InvalidateAllAssets`) and re-instantiates lazily;
  automatic detection is on the roadmap.
- Axis/winding conversions duplicated by hand (Max). One `GeoMath.h` used by
  input, output, instances and transforms, with unit tests.
- Exact Houdini build lock (Maxon's old plugin, Maya, Unity). None here;
  version is printed and missing entry points are reported.
