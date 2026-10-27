# AGENTS.md - CHIP-8 core for Chimera

This repository builds Cadmium (https://github.com/gulrak/cadmium), a CHIP-8
emulator written in C++, as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `cadmium.chimeraCore`: Cadmium's machines
compiled to run inside Chimera's sandbox (miniBox), a thin layer that runs a
machine one frame at a time, and the declarations Chimera reads (keys,
settings, file types). Cadmium is a git submodule at a fixed commit; every
change to it is a numbered patch. The picture is the emulated machine's own.

Read [docs/BUILDING.md](docs/BUILDING.md) before building and
[docs/PLAN.md](docs/PLAN.md) before changing how the core works: it is the
design log, with what was decided, why, and what was measured.

## Layout

- `extern/cadmium` - Cadmium, a git submodule at a fixed commit. Only its
  emulation library, its program loader and its table of known programs are
  compiled; its window, editor and debugger are not.
- `extern/chiplet`, `extern/fmt`, `extern/ghc-filesystem`,
  `extern/fast_float` - the libraries Cadmium's own build downloads while it
  configures, here as submodules at the versions it asks for.
- `patches/` - the numbered patches applied to `extern/cadmium`.
- `meson.build` - the build of both flavours. The list of sources is in it.
- `waterbox/cinterface.cpp` - the core: the functions Chimera's engine
  calls. The same file is compiled for the sandbox and for the host.
- `waterbox/run-native.c`, `waterbox/run-wbx.c` - the native runner and the
  sandbox runner. They take the same options and print the same lines
  (`waterbox/gate-harness.h` is the part they share).
- `waterbox/shims/raylib.h` - stands in for Cadmium's window library, of
  which the compiled files use two functions. `waterbox/native-shim/` -
  stands in for miniBox's header in the native build.
  `waterbox/guest-syscalls.cpp` - answers, in the sandbox only, a system
  call the sandbox does not have.
- `waterbox/*.sh` - `setup-guest.sh` (configures the sandbox build),
  `build-package.sh`, `run-gate.sh` (the test script), `apply-patches.sh`
  and `make-patches.sh`.
- `waterbox/tests/` - the window test (`run-frontend.sh`, with its Lua
  scripts and its helpers) and the helpers of the test script.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json` - what the package declares. Written by hand.
- `.github/workflows/chimera.yml` - CI: build, test, publish.
- `build/` - every build output. Ignored by git.

## Build and test

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<chimera>/extern/chimera-common-minibox
ninja -C build/meson-native
waterbox/build-package.sh -r <chimera>
CHIMERA_ROOT=<chimera> waterbox/run-gate.sh
bash waterbox/tests/run-frontend.sh --chimera-root <chimera>
```

The test script must end with `0 failed, 0 skipped`: every program it runs
comes with the submodules, so nothing is missing anywhere. The last line is
the window test: the package in Chimera's own window, with nobody at it
(it needs Chimera's window built, and no display). Run both before every
commit that changes the core, and say in the commit what they printed.

## Rules

- **Never edit `patches/` by hand.** Edit the tree under `extern/cadmium`,
  add the file to the list in `waterbox/make-patches.sh` if it is new, and
  run that script. `waterbox/apply-patches.sh` must then say "already
  applied".
- **The native run is the reference.** `run-native` and `run-wbx` must print
  the same lines for the same input. A change that makes them differ is
  wrong even when both look right.
- **The same input gives the same run, every time.** Nothing may read the
  host's clock, or the C library's `rand()` (it is another generator in
  another C library), or leave an answer to a tool in memory a savestate
  carries. Cadmium runs "as fast as the host can" when it is asked for 0
  instructions a frame, by the host's clock: the core never asks for 0.
- **A new test is broken on purpose once and seen to fail.** docs/PLAN.md
  says how each one was.
- **No CHIP-8 program is committed.** The tests use the programs that come
  with the submodules. A program somebody else wrote is theirs.
- **Write in plain language.** Commit messages, documents and the
  descriptions a user reads in Chimera (a setting's, a file slot's) say what
  they mean in ordinary words.
- **Commits.** Subject: `type(scope): what changed`. Body: what changed,
  why, and how it was tested, with the numbers. Stage explicit paths. No
  amending, no force push.

## What is the owner's to decide

Publishing a repository, a place in Chimera's list of official cores,
anything a licence does not plainly allow, and reporting anything to
Cadmium's author.
