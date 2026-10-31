# Blender fork kit

Everything needed to stand up and maintain our own fork of Blender, prepared
against **Blender 5.2.3 (LTS)**, the latest stable release as of 2026-10-30.

```
blender/
  README.md                      this file: status, how to build, what is on/off
  docs/01-architecture.md        tour of the source tree and its subsystems
  docs/02-adding-features.md     recipes: Python operator, app template, C++ operator, node, DNA/RNA, modifier, IO, branding
  docs/03-fork-workflow.md       repository layout, tracking upstream LTS, keeping the diff small, releases
  cmake/fork_linux_syslibs.cmake CMake preset: build against Ubuntu 24.04 packages instead of Blender's prebuilt libs
  scripts/bootstrap_ubuntu2404.sh  apt packages + Python 3.13 NumPy
  scripts/fetch_eigen.sh         Eigen snapshot Blender pins (Ubuntu's 3.4.0 is too old)
  scripts/fetch_fmt.sh           fmt 12.1 (Ubuntu's 9.1 lacks format_as support)
  scripts/fetch_ocio.sh          OpenColorIO 2.5 (Ubuntu's 2.1.3 fails Blender's >= 2.3 assert)
  scripts/build.sh               runs the fetch scripts if needed, configures, builds, prints the binary
  patches/                       our commits on top of v5.2.3, as git format-patch files
```

## Quick start (Ubuntu 24.04)

```sh
# 1. Source
GIT_LFS_SKIP_SMUDGE=1 git clone --branch v5.2.3 https://github.com/blender/blender.git ~/blender-fork/blender
cd ~/blender-fork/blender
git lfs pull                               # needs GitHub LFS access; see "Git LFS" below
git checkout -b fork/main
git am /path/to/US-C4DHoudini/blender/patches/*.patch

# 2. Dependencies and build (about 2-3 hours on 4 cores the first time)
sudo /path/to/US-C4DHoudini/blender/scripts/bootstrap_ubuntu2404.sh
/path/to/US-C4DHoudini/blender/scripts/build.sh ~/blender-fork/blender ~/blender-fork/build

# 3. Run
~/blender-fork/build/bin/blender
~/blender-fork/build/bin/blender -b --python-expr "import bpy; bpy.ops.object.fork_hello()"
```

For a build with every feature (Cycles, OpenSubdiv, OpenVDB, USD, Vulkan...)
use upstream's precompiled libraries instead: on a machine that can reach
projects.blender.org run `make update` in the source tree and then `make`.
The fork code is the same; only the dependency source differs.

## Two ways to get dependencies

| | Upstream precompiled libraries (`make update`) | Distro packages (this kit) |
| --- | --- | --- |
| Where from | Git LFS submodule `lib/linux_x64` on projects.blender.org, ~5 GB | `apt` + one Eigen tarball |
| Feature set | Everything | Everything Ubuntu 24.04 can satisfy (table below) |
| Portability of binary | Portable tarball, runs on any glibc 2.28+ distro | Linked to this machine's shared libraries |
| Use for | Releases you distribute | Development, CI, servers, headless render farms you control |

### Feature status with Ubuntu 24.04 packages

| Feature | State | Reason |
| --- | --- | --- |
| UI, modeling, sculpt, animation, Grease Pencil, VSE, compositor | ON | |
| Geometry / shader / compositor nodes | ON | |
| EEVEE and Workbench renderers (OpenGL backend) | ON | |
| Python 3.13 API, NumPy | ON | system interpreter, NumPy from pip |
| OBJ / PLY / STL / FBX / glTF (Python) IO, FFmpeg video, OpenAL/PulseAudio audio | ON | |
| Fluid (Mantaflow), ocean, remesh, Freestyle, Bullet, motion tracking (libmv) | ON | |
| Cycles | OFF | needs OpenImageIO 3.x, Embree 4.4, OSL 1.15; Ubuntu has OIIO 2.4, Embree 4.3, OSL 0.8 |
| OpenSubdiv, Manifold (exact boolean), Haru (PDF export) | OFF | no Ubuntu package |
| OpenVDB / NanoVDB volumes | OFF | Ubuntu has OpenVDB 10, Blender wants 13 |
| USD, Hydra, Alembic, MaterialX | OFF | no package / too old |
| Vulkan GPU backend | OFF | needs shaderc 2025.4, Ubuntu has 2023.8 |
| OpenXR (VR), OpenImageDenoise, Draco & meshoptimizer glTF compression | OFF | SDK too old / no package |
| SDL audio | OFF | Blender 5.2 uses SDL3, Ubuntu ships SDL2 |

Each OFF item is one line in `cmake/fork_linux_syslibs.cmake`. Turning one on
means building that library from source (upstream's recipes are in
`build_files/build_environment/`).

## Compatibility fixes that were needed

1. **GCC 14** (Blender refuses GCC < 14). Ubuntu 24.04 ships 13 by default and
   14 in the archive; the kit installs `gcc-14`/`g++-14` and passes them to CMake.
2. **OpenColorIO 2.5**: Blender's GPU shader binder has
   `static_assert(OCIO_VERSION_HEX >= 0x02030000)` and Ubuntu has 2.1.3 (whose
   CMake package is also broken, resolving its include dir to `/include`).
   `scripts/fetch_ocio.sh` clones the v2.5.0 tag from GitHub and builds it in
   about two minutes; OCIO fetches its own minizip-ng.
3. **fmt 12.1**: Blender formats `StringRef`/`UString` through `format_as()`
   overloads, a fmt 10+ feature; Ubuntu has 9.1. `scripts/fetch_fmt.sh` builds
   the pinned release as a static library.
4. **OpenImageIO 2.4** vs code written for 3.x: `patches/0001-*.patch` guards
   the three incompatible lines in `BLI_ustring.hh`. Upstreamable.
5. **Eigen**: libmv uses Eigen 5-era APIs. `scripts/fetch_eigen.sh` downloads
   the exact commit upstream pins (SHA-256 verified) and installs headers only.
6. **NumPy for Python 3.13** is not packaged by Ubuntu; it comes from pip and
   its path is passed explicitly to CMake.
7. **OpenImageIO's CMake package** references the `iconvert` tool and OpenCV
   headers; `openimageio-tools` and `libopencv-core-dev` satisfy it.

The binary links `libOpenColorIO.so.2.5` from `deps/ocio/lib`, which is not on
the system library path, so run it with `LD_LIBRARY_PATH=~/blender-fork/deps/ocio/lib`
(or install OCIO to `/usr/local`).

## Git LFS

Blender keeps binary data in Git LFS: `release/datafiles/startup.blend`,
`splash.png`, `preview.blend`, 152 brush icon `.dat` files, studio lights,
color-management LUTs, the brush/node asset libraries under `assets/`, the app
template startup files, and all of `tests/files`. The LFS objects are hosted
on projects.blender.org, not on the GitHub mirror (the mirror's own README
says to clone with `GIT_LFS_SKIP_SMUDGE=1`). After cloning the mirror, point
LFS at upstream and pull:

```sh
git config lfs.url https://projects.blender.org/blender/blender.git/info/lfs
git lfs pull
```

If the objects are missing you get `Detected incomplete startup blend` at
configure time. In the preparation session projects.blender.org was not
reachable, so the build used a placeholder `startup.blend`; see the build log
below for what that means at runtime.

## Build log from the preparation session (2026-10-30)

Recorded at the bottom of this file once the build finished:

<!-- BUILD-RESULTS -->
