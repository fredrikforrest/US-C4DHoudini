# Building the fork on Windows (PC)

Windows is the easiest platform to build Blender on, because upstream ships a
complete set of prebuilt libraries for MSVC and a `make.bat` that does the
rest. Nothing from the Linux system-library workaround in this kit is needed.
Everything below was checked against `make.bat` and `build_files/windows/*.cmd`
in Blender 5.2.3.

## 1. Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| Windows | 10 or 11, x64 | ARM64 works too but builds with clang; see `make.bat help` |
| Visual Studio | 2022 (Community is fine); 2019 16.9.16+ and 2026 also accepted | Install the **"Desktop development with C++"** workload. Build Tools edition works with `make.bat 2022b` |
| CMake | 3.21 minimum, 3.28 or newer recommended | Must be on `PATH` (`cmake.exe` is looked up there) |
| Git + Git LFS | any recent | `git lfs install` once. Git must be on `PATH` |
| Disk | 25 GB free | ~6 GB prebuilt libraries, ~10 GB build, plus the source |
| Optional: CUDA Toolkit 12.8 | for `release` target | Only needed to ship precompiled Cycles CUDA/OptiX kernels (`WITH_CYCLES_CUDA_BINARIES`). Without it GPU rendering still works on NVIDIA through runtime compilation on first use |
| Optional: AMD HIP SDK 7.1 | for `WITH_CYCLES_HIP_BINARIES` | same idea for AMD |

Do not put the source in a path with spaces; `make.bat` refuses it.
Python is not required: `make.bat` uses the one inside `lib\windows_x64`.

## 2. Get the source and the fork patches

```powershell
cd C:\dev
git clone https://projects.blender.org/blender/blender.git
cd blender
git checkout -b fork/main v5.2.3
git am C:\path\to\US-C4DHoudini\blender\patches\*.patch
```

Cloning from projects.blender.org (not the GitHub mirror) means the Git LFS
files (`startup.blend`, splash, assets) arrive automatically. If you cloned
the GitHub mirror instead, run
`git config lfs.url https://projects.blender.org/blender/blender.git/info/lfs`
and `git lfs pull` before building.

## 3. Prebuilt libraries

```powershell
make.bat update
```

This enables and checks out the `lib/windows_x64` submodule (several GB,
hosted on projects.blender.org) and pulls LFS. Re-run it after switching to a
new release branch; the library set is tied to the Blender version
(`.gitmodules` pins `blender-v5.2-release`).

## 4. Build

```powershell
make.bat full       # everything except precompiled CUDA kernels; what CI uses
make.bat release    # identical to blender.org builds; needs the CUDA toolkit
make.bat lite       # fastest compile, no Cycles/OpenVDB/USD etc.; good for UI/Python work
make.bat ninja full # same, built with ninja instead of msbuild (faster)
```

First build: 1-2 hours on 8 cores, longer for `release` with CUDA kernels.
The output is a portable Blender in
`..\build_windows_Full_x64_vc17_Release\bin\Release\` (the folder name encodes
target, architecture, VS version and build type). `blender.exe` runs from
there and the folder can be zipped and handed to other machines.

Rebuilds after a code change: run `rebuild.cmd` inside the build folder, or
`make.bat full` again; only changed files recompile.

Other useful switches (`make.bat help` lists them all): `developer` (faster
incremental builds, asserts, tests), `debug`, `with_tests` then `make.bat test`,
`builddir <dir>`, `packagename <name>`, `format` (clang-format via the
bundled LLVM).

## 5. Package

- **Portable zip**: zip `bin\Release\`. This is what `scripts\build_windows.ps1 -Package` does.
- **MSI installer**: install the WiX Toolset 3.x, then run `cpack -G WIX` in
  the build folder. Blender's installer branding lives in
  `release\windows\installer_wix\` (replace the banner/dialog images and the
  product name for a renamed fork).
- **MSIX / Microsoft Store**: `release\windows\msix\` has upstream's scripts.

## 6. Scripted

`scripts\build_windows.ps1` runs sections 2 to 5 end to end:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build_windows.ps1 -Workspace C:\dev -Target full -Package
```

It checks for Git, Git LFS, CMake and a Visual Studio with the C++ tools
(via `vswhere`), clones if needed, creates `fork/main` from `v5.2.3`, applies
the patches that are not applied yet, runs `make.bat update` and
`make.bat <target>`, and zips the result.

## 7. Continuous builds on GitHub

`ci\windows-build.yml` is a GitHub Actions workflow for the fork repository
(copy it to `.github\workflows\`). It builds `full` with ninja on a
`windows-2022` runner on manual trigger and on version tags, and uploads the
portable zip as an artifact. Two honest caveats:

- It was written here on Linux and has not been executed. Expect to adjust
  small things on the first run.
- Hosted Windows runners have roughly 14 GB free disk; the prebuilt
  libraries plus a full build are close to that. The workflow prints free
  space and uses the ninja path to keep the build directory small, but if it
  runs out of space, use a self-hosted Windows runner (any PC with the tools
  from section 1 and the GitHub runner agent) or the larger paid runners.

## 8. Fork patches on Windows

The four patches apply unchanged. Patches 0001 and 0003 are version guards
that take the upstream path with the prebuilt libraries (OpenImageIO 3.1,
FFmpeg 8.1), 0004 never triggers when the LFS `startup.blend` is present,
and 0002 is the example operator. `make.bat format` before committing keeps
them clean for the next LTS rebase.
