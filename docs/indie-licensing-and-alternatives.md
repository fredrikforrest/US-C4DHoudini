# Houdini Indie and this plugin: what is allowed, and the way around it

Written 2026-10-26 from SideFX's public documentation, forum threads and the
plugin sources. sidefx.com could not be fetched directly from the authoring
environment, so quotations come from search snippets; the two short
sentences that matter should be confirmed on the live pages before money is
spent. Links are at the end.

## 1. The question

The HAPI headers define `HAPI_RESULT_DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN`
(code 160). The Unreal plugin prints it as "Disallowed Houdini Engine Indie
With 3rd Party Plugin". Does that stop a self-built Cinema 4D plugin from
running on an Indie license, and is the forum claim that a plugin "only has
to be built against the Indie version" correct?

## 2. Is the forum comment correct? No.

There is no "Indie version" of Houdini, HARS or libHAPIL to build against.
Indie and commercial are the same binaries; the tier is decided by the
license that is checked out at runtime and by the file formats it may read
(`.hiplc` / `.hdalc` for Indie). A plugin compiled against the headers in a
commercial install and one compiled against an Indie install are the same
plugin. The restriction is enforced by the engine when it acquires its
license, so compiling differently changes nothing.

What the SideFX Houdini Engine licensing page says (search snippet of
`docs/hengine/_h_a_p_i__licensing.html`): Indie usage is "restricted to
authorized plugins", and the list it gives is **Unity, Unreal, Maya, Max,
and C4D**. The error code is documented there as "Indie license can only
be used with authorized plugins".

Two things follow:

1. The check is about *which plugin* talks to the engine, not about how it
   was built. The commenter who said "commercial plugins can't be built for
   Indie, they just need to be built against the Indie version" was
   speculating, and the Godot plugin author in the same thread confirmed
   their plugin never worked with Indie.
2. Cinema 4D is on SideFX's authorised list. It is there because of Maxon's
   bridge, and third-party HDA sellers advertised their Cinema 4D assets as
   working with the free Engine Indie license. So the *host* is authorised;
   what is open is whether SideFX's check recognises our plugin as "C4D".

## 3. How the engine identifies the plugin

Nothing public documents the mechanism. The evidence:

- Every SideFX plugin announces itself through the `HAPI_CLIENT_NAME`
  server variable (`HAPI_ENV_CLIENT_NAME`): Unreal calls
  `HAPI_SetServerEnvString(session, "HAPI_CLIENT_NAME", "unreal")` right
  after `HAPI_Initialize`; Unity sets the same variable in the process
  environment before the session starts; Maya sends "maya", Max "3dsmax".
- The license type the server ended up with is readable through
  `HAPI_GetSessionEnvInt(HAPI_SESSIONENVINT_LICENSE)`; the enum separates
  `HOUDINI_ENGINE_INDIE`, `HOUDINI_INDIE`, `HOUDINI_ENGINE_UNITY_UNREAL`.
- The Indie Engine license is checked out by HARS, so whatever is checked
  is visible to HARS: the client name variable and, in principle, the name
  of the process that connected.

Our plugin already sends `cinema4d` as its client name. That is a truthful
description of the host, not spoofing; what Maxon's plugin sent is unknown,
so the check may or may not match. Sending "unreal" or "unity" would make
the check pass and would be a license violation; that is not on the table.

**The empirical test is cheap**: run `hapi_probe` on the PC with the Indie
license. `HAPI_Initialize` or the first cook returns 160 with the message
above, or it succeeds and `--verbose` can print the license type. Ten
minutes, and it settles the HAPI path for this machine.

**The policy question still needs SideFX.** Even if the check passes,
"authorized plugins" plausibly means SideFX's and Maxon's own, and an
in-house plugin may be outside the spirit of it. The right move is one
email to sales@sidefx.com: Maxon's bridge is discontinued, Cinema 4D is on
the authorised list, you are an Indie-eligible studio, may an in-house
Cinema 4D integration use Houdini Engine Indie. The Godot author reported
SideFX "very open" to such requests. Get the answer in writing and keep it
with the repo.

## 4. What Houdini Indie definitely allows

From SideFX's Indie and studio FAQs:

- Houdini Indie is a limited commercial license for studios under about
  $100K USD annual revenue, node-locked, with its own file formats.
- Each Houdini Indie subscription includes **up to three free Houdini Engine
  Indie licenses**. SideFX describes Engine Indie as "a non-graphical
  version of Houdini Indie" that "will not run the Indie graphical
  interface".
- Engine licenses are what run Houdini without a GUI: SideFX's own FAQ says
  the engine license covers "the non-graphical version of Houdini,
  controlled by a script, which is either hython (Python) or hbatch
  (HScript)", and that a render node running hython needs an Engine Indie
  license. This is how Indie users batch render today.
- Engine Indie opens both `.hda` and `.hdalc`.
- Indie cannot be mixed with commercial Houdini in one pipeline.

So under Indie you may run, at the same time: one Houdini Indie GUI, and up
to three `hython` processes. Both are full Houdini with the complete Python
object model (HOM). Nothing in the Indie terms restricts what a script may
do with HOM or who connects to that script. That is the opening.

## 5. The Indie-safe design: a HOM backend

Houdini Engine is, functionally, "load an HDA, expose its parms, set them,
cook, hand the geometry over". Every one of those steps exists in HOM:

| Houdini Engine call | HOM equivalent |
| --- | --- |
| `HAPI_LoadAssetLibraryFromFile` | `hou.hda.installFile(path)`, `hou.hda.definitionsInFile(path)` |
| `HAPI_CreateNode` | `container.createNode(type_name)` |
| `HAPI_GetParameters` + values + choices | `node.parmTemplateGroup()`, `node.parmTuple(name).eval()`, `parm.isHidden()`, `parm.isDisabled()`, `template.menuItems()` |
| `HAPI_SetParm*Values` | `parmTuple.set(values)`, `parm.pressButton()` |
| `HAPI_SetTime` | `hou.setFrame()`, `hou.setFps()` |
| `HAPI_CookNode` + status | `node.cook(force=True)`, `node.errors()`, `node.warnings()`, `node.isTimeDependent()` |
| `HAPI_GetAttribute*Data` (bulk) | `geo.pointFloatAttribValuesAsString("P")` and the vertex/prim/int variants: one call returns the whole array as bytes |
| `HAPI_GetFaceCounts` / `GetVertexList` | no bulk accessor, but a two-line VEX wrangle appended after the asset writes `i@__nv` per prim and `i@__pt` per vertex, which the bulk accessors then return |
| `HAPI_GetInstancerPartTransforms` | `prim.intrinsicValue("packedfulltransform")` per packed prim, sources via `unpack` SOP grouped by `geometryid` |
| `HAPI_CreateInputNode` + `SetAttribute*` | `hou.Geometry()`, `createPoints`, `createPolygons`, `set*AttribValuesFromString`, stored in a **Stash SOP** wired into the asset's input |
| `HAPI_GetMaterialNodeIdsOnFaces` | `shop_materialpath` prim attribute |
| HARS over a pipe | `hython bridge_server.py --port N`, or the same script started inside the Houdini Indie GUI |

The transport is a socket on localhost carrying a small JSON header plus
raw binary arrays (float32/int32), the same shape of data HAPI delivers.
For large meshes the arrays can go through shared memory instead, since
both Cinema 4D's Python and hython are CPython 3.

Where this lands against the three options you have:

| | Alembic/VDB round trip (today) | HAPI plugin (needs license answer) | HOM backend (Indie safe) |
| --- | --- | --- | --- |
| Live parameter editing in Cinema | no | yes | yes |
| Cinema geometry as HDA input | export first | yes | yes |
| Latency per edit | file write + import | pipe round trip + cook | socket round trip + cook; a few ms more than HAPI for the Python layer |
| Startup | none | HARS 2 to 5 s | hython 3 to 8 s, once |
| Live link to a Houdini GUI | no | Session Sync | yes, run the server inside the Indie GUI; no extra license |
| Everything HOM can do (PDG, hwebserver, custom Python) | n/a | no | yes |
| License needed | Indie GUI | Engine (Indie if authorised) | Indie GUI or one of the 3 free Engine Indie |
| Crash isolation | n/a | yes (HARS) | yes (hython is a separate process) |
| Code from this repo reused | n/a | all | ObjectData, description builder, GeoConvert, GeoMath; the HAPI transport layer is replaced |

The important architectural point: `hapi_core` already separates "talk to
Houdini" from "neutral geometry and parameter structures" from "Cinema
objects". A HOM backend is a second implementation of the first layer that
fills the same `ParmSet` / `AssetOutput` / `InputMesh` structures. The
Cinema plugin does not change. The plugin can carry both backends and pick
by preference: HAPI when a commercial Engine or an Indie go-ahead is
available, HOM otherwise.

### Two deployment modes, one protocol

1. **Headless**: the plugin spawns `hython` with the server script on first
   use (like HARS). Uses one of the three Engine Indie licenses. This is the
   day-to-day mode.
2. **Attached**: the artist starts the same server from a shelf tool inside
   their open Houdini Indie session. The HDA is then live in both apps: edit
   the network in Houdini, see the result in Cinema at once. Uses the GUI
   seat only. This is better than Session Sync for an in-house pipeline,
   because the Cinema side can also ask the GUI session for anything else
   (render a flipbook, save a `.hiplc`, kick off PDG).

### What you give up

- About 5 to 15 ms per cook for the Python layer and the wrangle pass; the
  cook itself dominates for anything non-trivial.
- Houdini's `hou.Geometry` builders for inputs are Python-side and slower
  than HAPI's for very large input meshes (hundreds of thousands of
  polygons). Mitigation: write inputs as `.bgeo.sc` to a RAM disk and load
  them with a File SOP, which is one fast C++ call on each side.
- HAPI's incremental "has this part changed" flags; the HOM backend
  re-sends whole geometries. Fine at the sizes a Cinema scene uses.

## 6. Recommendation

1. On the PC, run `hapi_probe` with the Indie license first. It answers the
   technical half in minutes.
2. Email SideFX the policy question in parallel. Note the answer in this
   file.
3. Regardless of both, build the HOM backend. It is Indie-safe today, it
   adds the attached-GUI mode that HAPI cannot offer under Indie, and it
   is a week of work because the Cinema side and the neutral structures
   exist. The prototype server and client are in `src/hom_bridge/`, with
   `tools/hom_probe.py` as the counterpart of `hapi_probe`.
4. Keep the HAPI backend. If SideFX says yes, or a commercial Engine
   license arrives later, it is the faster transport and already written.

## 7. Sources

- SideFX, Houdini Engine licensing documentation (`_h_a_p_i__licensing.html`): Indie restricted to authorised plugins (Unity, Unreal, Maya, Max, C4D); error 160 meaning. https://www.sidefx.com/docs/hengine/_h_a_p_i__licensing.html
- SideFX, Indie FAQ: revenue cap, node-locked, pipeline mixing, file formats. https://www.sidefx.com/faq/indie-new/
- SideFX, Production Facility FAQ: "up to 3 FREE Houdini Engine Indie". https://www.sidefx.com/faq/studio/
- SideFX, "Why are Houdini Engine licenses used to run Houdini in Batch mode?": the engine license covers hython and hbatch. https://www.sidefx.com/faq/question/why-are-the-houdini-engine-and-houdini-batch-license-being-merged/
- SideFX, "How can I license Houdini Engine Indie?" https://www.sidefx.com/faq/question/how-do-i-license-houdini-engine-indie/
- SideFX, Indie licensing page: Engine Indie is non-graphical; do not mix with the Unity/Unreal commercial license. https://www.sidefx.com/Support/licensing/indie/
- SideFX, "How can I use the Cinema 4D plugin with Houdini?": Maxon produced the plugin; R23.110 tied to Houdini 18.0.460. https://www.sidefx.com/faq/question/how-can-i-use-the-cinema-4d-plugin-with-houdini/
- SideFX forum, Houdini Engine for Godot: Indie not supported by a third-party plugin; SideFX "very open" to discussing it; the "built against Indie" comment. https://www.sidefx.com/forum/topic/101121/?page=1
- SideFX forum, Houdini Engine Indie license problems (Unity/Unreal vs Indie license confusion). https://www.sidefx.com/forum/topic/59875/
- SideFX forum, Indie license + Engine license. https://www.sidefx.com/forum/topic/61948/?page=1
- SideFX forum, Hython and Houdini Engine. https://www.sidefx.com/forum/topic/58886/
- SideFX forum, newer Cinema 4D Houdini Engine support (discontinued since S24). https://www.sidefx.com/forum/topic/52925/
- SideFX, hrpyc remote Python docs. https://www.sidefx.com/docs/houdini/hom/rpc.html
- SideFX, hwebserver / hou.webServer docs. https://www.sidefx.com/docs/houdini/hwebserver/index.html
- SideFX plugin sources: `HAPI_ENV_CLIENT_NAME` usage in `HoudiniEngineForUnreal/Source/HoudiniEngine/Private/HoudiniEngine.cpp` and `HoudiniEngineForUnity/.../HEU_Platform.cs`; license enum in `HAPI_Common.h`.
