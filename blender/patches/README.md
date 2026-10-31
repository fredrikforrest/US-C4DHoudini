# Fork patches

Patches on top of upstream Blender `v5.2.3`, in `git format-patch` form. Apply
to a fresh checkout with:

```sh
cd blender            # upstream checkout at v5.2.3
git checkout -b fork/main v5.2.3
git am ../US-C4DHoudini/blender/patches/*.patch
```

| Patch | Purpose | Upstreamable? |
| --- | --- | --- |
| `0001-BLI-build-BLI_ustring.hh-against-OpenImageIO-2.x-as-.patch` | Lets `BLI_ustring.hh` compile against OpenImageIO 2.4 (Ubuntu 24.04) as well as 3.x. Without it the very first compile unit (`makesdna`) fails. | Yes. Small, guarded by `OIIO_VERSION_MAJOR`, no behavior change on 3.x. |
| `0002-Object-add-example-fork-operator-OBJECT_OT_fork_hell.patch` | Minimal C++ operator in a new file, registered in `object_ops.cc`. Exists to prove the edit-compile-run loop and as a template for real operators. Drop it once you have real features. | No, it is an example. |

| `0003-Movie-build-stereo3d-metadata-code-against-FFmpeg-7..patch` | Version guard so the stereo-3D video metadata code compiles against FFmpeg 6.1 (Ubuntu 24.04); upstream pins FFmpeg 8.1. | Yes. |
| `0004-WM-start-with-an-empty-scene-when-the-embedded-start.patch` | When the embedded `startup.blend` is a Git LFS pointer, create a scene/workspace/screen instead of crashing in `wm_add_default`. Lets background mode and the Python API run from a checkout without LFS objects. The UI is bare in that state. | Maybe, as a robustness fix; upstream may prefer the hard error. |

Regenerate after changing the fork branch:

```sh
git format-patch v5.2.3..fork/main -o ../US-C4DHoudini/blender/patches/
```
