# US-C4DHoudini

A Houdini Engine bridge for Cinema 4D: load an HDA as a generator object, edit
its parameters in the Attribute Manager, feed it Cinema geometry, and get
polygons, splines and instances back. This is a **C++ proof of concept** that
borrows its architecture from SideFX's open-source Unreal, Unity, Maya and
3ds Max plugins. Status and limitations are listed honestly below.

## Why build it

Maxon's own Houdini Engine bridge has been unsupported since Cinema 4D S24. It
was also locked to one exact Houdini build per Cinema release. Every SideFX
plugin avoids that lock the same way: resolve the Houdini Engine C API
(HAPI) at runtime from whichever `libHAPIL` the user points at, run the engine
out of process (HARS), and never link against Houdini. This repository does
the same.

## What is here

```
tools/generate_hapi_bindings.py   parses HAPI.h -> src/hapi_core/HapiFunctions.inc (432 entry points)
src/hapi_core/                    host-neutral C++17 core, no Cinema 4D dependency
  HapiLibrary.*                   runtime loader (dlopen/LoadLibrary), Houdini discovery, version info
  HapiSession.*                   one shared session: HARS auto-start (pipe/socket/shared memory),
                                  connect-to-existing (Session Sync), cook loop with abort/progress,
                                  cook/call error text, timeline
  HapiParms.*                     HDA parameter interface model + setters, presets
  HapiAsset.*                     load .hda libraries, instantiate/delete asset nodes, input names
  HapiGeometry.*                  cooked output -> meshes/curves/instancers/volumes/materials,
                                  Cinema meshes/splines -> Houdini input nodes, merge nodes
  GeoMath.h                       Houdini <-> Cinema 4D space (unit scale, Z flip, winding)
src/hapi_probe/                   CLI that loads an HDA, prints parms, sets values, cooks, dumps OBJ
src/c4d_plugin/                   the Cinema 4D plugin (Maxon project tool layout)
  source/HoudiniAssetObject.*     ObjectData generator: dynamic description, inputs, cook, convert
  source/HoudiniGeoConvert.*      PolygonObject/SplineObject/InstanceObject building, tags
  source/HoudiniSessionManager.*  plugin-wide session + preferences
  source/main.cpp                 registration, "Session Info" / "Restart Session" commands
  res/                            description resources (ohoudiniasset.res/.h/.str)
tests/                            geomath unit tests + a mock libHAPIL that exercises the loader
docs/                             architecture, what was borrowed from each plugin, roadmap
```

## Build status

| Part | State |
| --- | --- |
| `hapi_core`, `hapi_probe`, tests | Compiles with GCC 13 (`-Wall -Wextra`, clean). `ctest`: 2/2 pass. |
| Cinema 4D plugin | Written against the 2024/2025 C++ SDK, **not compiled yet**: the Maxon SDK is not available in the environment this was authored in. Expect a short round of signature fixes (see "Known compile risks"). |
| End-to-end with Houdini | Not run yet: no Houdini install here. `hapi_probe` is the first thing to run on a machine that has one. |

### Building the core and the probe

You need the HAPI headers from a Houdini install (`$HFS/toolkit/include`) or
from a checkout of `sideeffects/HoudiniEngineForUnreal`
(`Source/HoudiniEngine/Public`). They are not vendored here.

```sh
cmake -S . -B build -DHAPI_INCLUDE_DIR="$HFS/toolkit/include"
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Regenerate the function table when moving to a new Houdini major:

```sh
python3 tools/generate_hapi_bindings.py "$HFS/toolkit/include" src/hapi_core/HapiFunctions.inc
```

### Running the probe (needs Houdini + a Houdini Engine license)

```sh
./build/hapi_probe my_asset.hda
./build/hapi_probe my_asset.hda --parm height=2.5 --parm divisions=12 --obj out.obj
./build/hapi_probe my_asset.hda --input box.obj --frame 24 --fps 25 --hip debug.hip
./build/hapi_probe my_asset.hda --session shm              # shared memory transport (Houdini 20.5+)
./build/hapi_probe my_asset.hda --connect --session socket --port 9090   # attach to houdini -hess=port:9090
```

It prints the library and engine version, how long HARS took to start, the
asset's inputs, the full parameter tree with current values, cook time, and
every object/geo/part with its attributes. `--obj` writes the result in
Cinema 4D space (cm, Z negated) so you can import it and check facing and
scale before touching the plugin. `--hip` saves the server scene for
inspection in Houdini.

Houdini is found through `HAPI_LIBRARY_PATH` (full path to libHAPIL), `HFS`,
the Windows registry, or the default install folders (see
`Lib::defaultSearchPaths`). HARS is located next to the library or via
`HOUDINI_HARS_LOCATION`.

### Building the Cinema 4D plugin

1. Install the Cinema 4D C++ SDK (2024 or 2025) and the project tool.
2. Copy or symlink `src/c4d_plugin` to `<sdk>/plugins/us_c4dhoudini`.
3. Run `tools/sync_plugin_sources.sh` (copies `src/hapi_core` into
   `source/hapi_core`; the project tool only compiles files under `source/`).
4. Set `HFS` so `project/projectdefinition.txt` can find `toolkit/include`.
5. Run the project tool and build with Visual Studio / Xcode as usual.
6. Replace the placeholder plugin ids (`1064320`-`1064322`) with ids from
   developers.maxon.net before distributing.

Known compile risks (things that could not be checked without the SDK):

- `GetDEnabling` / `GetDParameter` const-ness and the two-argument `Init`
  changed around 2023/2024; adjust to the SDK you build against.
- `InstanceObject::SetInstanceMatrices`, `VertexColorTag::SetPoint/SetPolygon`
  and `HyperFile::ReadMemory` signatures are written from memory of R21+.
- Resource files have not been run through the resource compiler.

## How it works in Cinema 4D

- **Object**: *Houdini Asset* generator (`Ohoudiniasset`). Pick an `.hda`,
  choose the operator if the library holds several. The HDA's parameters
  appear under *Parameters* as native controls: folders become groups,
  toggles, ints/floats with slider ranges, vectors, colors, menus, strings,
  file pickers, buttons, operator-path parms as link fields.
- **Inputs**: children of the object fill the HDA's geometry inputs in order
  (standard generator behaviour), link fields in the *Inputs* tab override
  them. Polygon objects go across with P, UVs, polygon selections as
  primitive groups, and texture-tag materials as a `c4d_material` prim
  attribute; splines go across as input curves. Several objects on one
  input are merged in Houdini.
- **Cooking**: parameters are diffed against Houdini's state and pushed
  together, then one asynchronous cook runs with abort support (Esc /
  interrupted redraws call `HAPI_Interrupt`). Time-dependent assets recook
  on frame change (`HAPI_SetTime`). *Auto Cook* off makes the *Cook* button
  the only trigger.
- **Output**: a Null per Houdini object with its transform, polygon objects
  per mesh part (UVW tag with V flipped, Normal tag or Phong tag, Vertex Color
  tag from `Cd`/`Alpha`, selection tags per primitive/point group), splines
  per curve part, and instancers as a multi-instance `InstanceObject` (or
  render instances / copies). Materials: a polygon selection per Houdini
  material plus a Texture Tag when a Cinema material of the same name exists
  (`c4d_material` prim/detail attribute wins over the SHOP assignment).
- **Persistence**: parameter values live in the object's container under
  stable ids mapped by parm name; a binary HAPI preset is also saved so
  ramps and multiparm instances survive even before the UI supports them.
- **Session**: one HARS per Cinema process, started on first use over a
  named pipe (socket / shared memory / in-process selectable). It first tries
  to attach to an already running server, so `houdini -hess=pipe:hapi_c4d_<pid>`
  gives you Session Sync for free. *Houdini Engine: Session Info* and
  *Restart Session* commands are in the plugins menu.

## Conventions read from HDAs

In priority order, mirroring the Unreal/Unity ones so existing assets work:

| Purpose | Attributes |
| --- | --- |
| material name | `c4d_material`, `unreal_material`, `unity_material`, `shop_materialpath` (prim or detail) |
| instance source | `c4d_instance`, `unreal_instance`, `unity_instance`, `instance` (point or detail) |
| split instances | `c4d_split_attr`, `unreal_split_attr`, `unity_split_attr` (detail, names another attribute) |
| output object name | `c4d_output_name`, `unreal_output_name` (detail or prim) |
| standard geometry | `P`, `N`, `uv`, `uv2`, `Cd`, `Alpha`, `name`, primitive/point groups |

Vertex attributes win over point attributes, point over primitive.

## Licensing reminder

Every machine that cooks needs a Houdini Engine license (Engine, Indie or
FX). Render nodes included. The roadmap has a "bake to Alembic/VDB" item for
that reason.

## Coordinate conventions (verify first on a real HDA)

Houdini is Y-up right-handed in metres; Cinema 4D is Y-up left-handed in
centimetres. The plugin negates Z (what Cinema's own FBX/Alembic importers
do) and scales by 100. Polygon vertex order is **not** reversed: Houdini
winds clockwise and Cinema's `CalcFaceNormal` is `cross(b-a, c-a)`, and the
reflection cancels the difference. The unit test in `tests/test_geomath.cpp`
checks the algebra, the Unity plugin (same handedness) does the same, but
this is the first thing to confirm with `hapi_probe --obj` on a real asset.
A *Reverse Polygon Winding* switch exists in case an HDA was authored the
other way.

## Next steps

See `docs/roadmap.md`. The short version: run `hapi_probe` against a few
real HDAs, compile the plugin against the SDK, then ramps, multiparm
editing in the UI, `name`-based splitting, volumes, and a bake command.
