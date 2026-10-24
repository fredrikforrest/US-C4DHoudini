# Architecture

## Layers

```
 Cinema 4D (main thread + scene execution threads)
 ┌──────────────────────────────────────────────────────────────┐
 │ HoudiniAssetObject (ObjectData)                              │
 │   GetDDescription  -> builds UI from hapi::ParmSet           │
 │   Message          -> buttons, parm edits, reload            │
 │   GetVirtualObjects-> inputs, diff+push parms, cook, convert │
 │ HoudiniGeoConvert   <-> PolygonObject/SplineObject/Instance  │
 │ HoudiniSessionManager  prefs, lazy session start             │
 └───────────────┬──────────────────────────────────────────────┘
                 │ hapi:: (host neutral, std::* only)
 ┌───────────────▼──────────────────────────────────────────────┐
 │ HapiGeometry  HapiParms  HapiAsset                            │
 │ HapiSession   (one session, one recursive mutex, cook loop)   │
 │ HapiLibrary   (function table generated from HAPI.h)          │
 └───────────────┬──────────────────────────────────────────────┘
                 │ dlopen / LoadLibrary at runtime
          libHAPIL  ── Thrift (pipe / socket / shared memory) ──  HARS.exe (Houdini Engine)
```

Nothing above the dotted line includes a Cinema header, so the same core
drives `hapi_probe` and the unit tests, and could drive a Python binding or
a render-time baker later.

## Decisions and their sources

| Decision | Why | Borrowed from |
| --- | --- | --- |
| Resolve every `HAPI_*` symbol at runtime from a generated table | Single plugin binary across Houdini builds; plugin loads even without Houdini installed | Maya `HoudiniApi.C`, Unreal `FHoudiniApi`, Max `HEMAX_HoudiniApi` (all generated, none ship the generator, so `tools/generate_hapi_bindings.py` is ours) |
| Missing symbols stay `nullptr` and are reported, not fatal | Older `libHAPIL` than the headers we generated from still works for the subset it has | Maya/Unreal use failing stubs; we prefer explicit `has()` checks |
| Out of process (HARS over named pipe) by default | Crash isolation; Cinema's Python/Qt/OpenSSL never meet Houdini's | All four plugins; Maya even forces in-process off ("crashing due to library conflicts") |
| Try to connect before spawning HARS | Attaching to `houdini -hess=...` gives Session Sync; a leftover server is reused | Max `ConnectSession` then `CreateSession`; Unreal `SessionSyncConnect` |
| Scrub `LD_LIBRARY_PATH`/`PYTHONPATH`/`DYLD_*` while spawning HARS | Host env leaking into HARS makes it pick up the wrong Python/Qt | Maya `plugin.C:174-271` |
| `HAPI_Initialize(use_cooking_thread=true)` + poll `HAPI_GetStatus` with `HAPI_Interrupt` on abort | UI stays responsive, Esc cancels | Unreal scheduler, Maya `statusCheckLoop`; Max's synchronous cook is listed as a pitfall there |
| `READY_WITH_COOK_ERRORS` is a success with a log | Houdini still produced output; users want to see it | Unreal, Unity |
| Wrap cooks in `HAPI_PythonThreadInterpreterLock` | HDAs with Python callbacks can deadlock against the host | Maya `Util::PythonInterpreterLock` |
| `SetServerEnvString(HAPI_CLIENT_NAME, "cinema4d")` | Shows up in licensing/crash logs | All four |
| Batch string lookups (`HAPI_GetStringBatch`) | One round trip for hundreds of parm labels over a pipe | Maya `Util::getStringBatch` |
| Diff container values against cached parm values, push once, cook once | Avoids a cook per slider tick; also makes undo "just work" | Maya's dirty-plug queue; Unreal's `PreCook` state |
| Keep the host container as the source of truth after a cook | Prevents ping-pong with HDA callbacks that rewrite parms | Our fix for a Maya-documented pitfall (`Asset.C:1285`) |
| Buttons are description commands, never stored values | Avoids "button fires on scene load" | Maya's compare-to-1 trick made unnecessary |
| Stable description ids via a name->id map in the container | Parameter ids survive save/load and HDA edits; animation tracks keep pointing at the right parm | Max's CustAttrib mirror, Maya's `ParmNameCache` |
| Binary HAPI preset saved with the object | Ramps and multiparms round-trip before the UI can edit them | `HAPI_GetPreset/SetPreset` (unused by Maya/Max, used by Unity) |
| Vertex-then-point attribute lookup | Vertex data is the most specific | Maya (vertex first); Max looks point first, Unreal discovers the owner |
| `packedPrimInstancingMode = FLAT` and `GetInstancedPartIds` + `GetInstancerPartTransforms(HAPI_SRT)` | The modern instancing path; maps to a multi-instance `InstanceObject` | Max/Maya/Unreal |
| Old-style object instancers and attribute instancers also recognised | Older HDAs | Unreal's classification in `HoudiniOutputTranslator.cpp` |
| Output nodes (`HAPI_GetOutputGeoInfos`) preferred, display SOP as fallback | HDAs with several Output SOPs | Unreal (`preferOutputNodes`) |
| `c4d_*` attribute names with `unreal_*`/`unity_*` fallbacks | Existing assets work unchanged | Unreal `HAPI_UNREAL_ATTRIB_*`, Unity `HEU_Defines` |
| Z flip + x100, no winding reversal | Cinema is Y-up left-handed cm; see README "Coordinate conventions" | Unity (same handedness) reflects without reversing; Maya reverses because it is right-handed |
| UV `v' = 1 - v` | Cinema's UVW origin is top-left | Unreal does the same |
| One `Sop/merge` per input slot when several objects feed it | N host objects per input | Max `HEMAX_InputInstance` |
| Prim string `c4d_material` on inputs from texture tags | HDAs can route Cinema materials through | Max `hemax_material`, Maya `maya_shading_group` |

## Threading

Cinema evaluates generators on worker threads while the Attribute Manager
runs on the main thread. Two locks:

- `hapi::Session::mutex()` (recursive) around every HAPI call; `SessionLock`
  is the RAII helper. HAPI is not thread safe and HARS serialises anyway.
- `HoudiniAssetObject::m_mutex` (recursive) around object state (`m_parms`,
  node ids), taken by `GetVirtualObjects`, `GetDDescription` and `Message`.

Lock order is always object mutex then session mutex.

## Data flow for one parameter edit

1. User drags a slider. Cinema writes the value into the object's
   `BaseContainer` under the parm's stable id and sends
   `MSG_DESCRIPTION_POSTSETPARAMETER`.
2. `Message` pushes the single changed value to Houdini immediately,
   refetches `ParmSet` (cheap, 5 round trips), and if any
   invisible/disabled/menu state changed marks the description dirty.
3. Cinema calls `GetVirtualObjects`. Inputs are re-marshalled only if a
   dependence changed. `pushParameterChanges` finds nothing left to push,
   `m_needsCook` is set, so one cook runs, output is fetched and converted.
4. The binary preset is snapshotted for the next save.

## Errors

- Failed HAPI calls record `HAPI_GetStatusString(CALL_RESULT)` into
  `Session::lastError()`; the object shows it in *Status*.
- Cook results use `HAPI_ComposeNodeCookResult` (per node, warnings level)
  and are shown in the *Cook Log* tab and the console.
- Licensing results (`HAPI_RESULT_NO_LICENSE_FOUND` and friends) get an
  explicit explanation.
