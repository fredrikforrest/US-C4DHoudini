# Fork workflow

How to own a Blender fork without drowning in merge conflicts.

## 1. Repository layout

```
<your GitHub org>/blender            your fork of github.com/blender/blender (the mirror of projects.blender.org)
  main                               tracks upstream main, never commit here
  blender-v5.2-release               tracks upstream LTS branch, never commit here
  fork/main                          your default branch: upstream LTS + your commits
  fork/<feature>                     short-lived feature branches, PR'd into fork/main
```

Upstream's canonical repository is Gitea at https://projects.blender.org/blender/blender.
GitHub is a read-only mirror. Fork the GitHub mirror (Fork button) so the
`git remote add upstream https://github.com/blender/blender.git` relationship
is cheap, and so that `git lfs pull` has a working LFS endpoint (the mirror
serves the LFS objects for anyone with a GitHub account).

### First-time setup (on a machine with normal internet access)

```sh
git clone https://github.com/<you>/blender.git
cd blender
git remote add upstream https://github.com/blender/blender.git
git fetch upstream --tags
git checkout -b fork/main v5.2.3
git lfs pull                       # startup.blend, splash.png, brush/node assets, test files
git am ../US-C4DHoudini/blender/patches/*.patch   # the patches prepared in this session
git push -u origin fork/main
```

`make update` (upstream's helper, `build_files/utils/make_update.py`) fetches
the precompiled dependency submodules under `lib/<platform>` from
projects.blender.org and pulls LFS. It also rewrites any `projects.blender.org`
remote URL to `git.blender.org`; it leaves GitHub remotes alone, so it is safe
to run in the fork.

## 2. Which upstream line to follow

| Line | Pros | Cons |
| --- | --- | --- |
| **LTS branch** (`blender-v5.2-release`, supported ~2 years) | Only bug fixes land, so rebases are trivial; stable file format | You get new upstream features only when you move to the next LTS |
| `main` | Always newest | DNA, nodes and UI code churn weekly; rebasing a sizeable fork is a part-time job |

Recommendation: base the fork on the current LTS (5.2.x) and plan one
migration per LTS cycle. Pull LTS point releases with:

```sh
git fetch upstream
git checkout fork/main
git rebase upstream/blender-v5.2-release     # or: git merge --no-ff v5.2.4
```

Rebase keeps `git format-patch` output clean (you can always regenerate the
`patches/` directory); merge keeps history if several people push to the fork.
Pick one and write it down.

## 3. Keeping the diff small

- Put new code in **new files** wherever possible (new `bl_operators` module,
  new node file, new `MOD_*.cc`) and touch upstream files only for the one-line
  registration. Upstream rarely edits the same line you added.
- Reserve a **fork namespace**: operator idnames `FORK_OT_*` / `fork.*`,
  node idnames `GeometryNodeFork*`, legacy node type numbers 9000+, modifier
  enum values from 200 up, DNA members prefixed `fork_`.
- Reserve a **subversion range**: upstream 5.2 uses `BLENDER_FILE_SUBVERSION`
  up to ~45. Use 100-199 for fork versioning blocks in `versioning_520.cc`, and
  only compare against your own numbers. When you move to the next LTS,
  re-home those blocks under the new file's version.
- Keep **branding** in a single commit (`wm_window.cc`, `splash.png`,
  `freedesktop/`, `BLENDER_VERSION_SUFFIX`) so it can be dropped or re-applied
  as a unit.
- Run `make format` before committing; a reformatted upstream file is the most
  common cause of spurious conflicts.

## 4. Builds

- Official-style build (recommended for releases you distribute): a machine
  that can reach projects.blender.org, `make update` for the precompiled
  libraries, then `make release` or `cmake -C build_files/cmake/config/blender_release.cmake`.
  This gives Cycles, OpenSubdiv, OpenVDB, USD, Vulkan, etc. with the exact
  versions upstream tests against.
- Distro-library build (what was done in this session, see `../README.md`):
  `scripts/bootstrap_ubuntu2404.sh` then `scripts/build.sh`. Good for CI and
  for developers who do not want 5 GB of prebuilt libraries. Missing features
  are listed in `cmake/fork_linux_syslibs.cmake`.
- `make lite` for the fastest compile when iterating on UI or Python.
- `ccache` and `WITH_LINKER_LLD=ON` (already in the fork config) roughly halve
  rebuild times.

## 5. Testing what you change

- `make test` runs the ctest suite. For a system-library build many render
  tests are skipped (no Cycles) and `tests/files` needs Git LFS content.
- Headless smoke test for every change:
  `bin/blender -b --factory-startup --python-expr "import bpy; print(bpy.app.version_string)"`.
- Python API tests live in `tests/python/`; add a `bl_fork_*.py` there for
  each operator or node you add and register it in `tests/python/CMakeLists.txt`.
- GUI tests need a display; `xvfb-run` works for `WITH_UI_TESTS_HEADLESS`.

## 6. Release checklist for the fork

1. Rebase/merge the latest LTS point release; rebuild; run tests.
2. Bump `BLENDER_VERSION_SUFFIX` (or a fork-specific string) so bug reports
   can be told apart from stock Blender.
3. Build with the official library set (`make update && make release`) on
   Linux, Windows and macOS. Upstream's packaging scripts are
   `build_files/utils/make_source_archive.py`, `release/freedesktop/snap/`,
   `release/windows/`, `release/darwin/`.
4. Licensing: Blender is GPL-2.0-or-later (GPL-3 as a whole). Distributing a
   binary means distributing (or offering) the complete fork source. Keep the
   fork repository public or ship a source archive with each build. Replace
   the Blender logo and name per the trademark policy.

## 7. Working on GitHub

- Open PRs into `fork/main`; require `make format` and a Linux build in CI
  (`scripts/bootstrap_ubuntu2404.sh && scripts/build.sh` runs in ~2 hours on 4
  cores, mostly in `source/blender`). Cache the build directory between runs.
- Mirror upstream's templates if you want: `.gitea/issue_template/*.yaml` can
  be copied to `.github/ISSUE_TEMPLATE/`.
- Upstream contributions: anything generic (bug fixes, the OpenImageIO 2.x
  compatibility patch in this repo) can go back to projects.blender.org as a
  pull request; that shrinks your fork diff permanently.
