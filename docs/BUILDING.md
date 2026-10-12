# Building and testing

This builds `cadmium.chimeraCore` from source and runs its tests. It has been
done on Ubuntu 24.04 with GCC 14; the native build has also been made with
GCC 13, which is what a CI runner has, and runs the same.

## What is needed

- GCC and G++ (13 or 14), `meson`, `ninja`, `python3` and `git`.
- A Chimera checkout with its submodules, built far enough to have miniBox,
  the sandbox:

  ```sh
  git clone --recurse-submodules https://github.com/ToolAssisted-run/chimera
  mb=chimera/extern/chimera-common-minibox
  meson setup $mb/build/meson-linux $mb && meson compile -C $mb/build/meson-linux
  meson setup $mb/build/meson-cpp $mb -Dguest_cpp=true && meson compile -C $mb/build/meson-cpp
  ```

  The first is the sandbox's host library; the second is the toolchain a
  C++ core is compiled with (a C library and a C++ library for the sandbox).
- For the tests that run the package in Chimera's headless runner, Chimera's
  engine too: `meson setup build/meson-linux --prefix "$PWD/build" --libdir dll`
  and `meson compile -C build/meson-linux` in the Chimera checkout. Chimera's
  README lists what that needs.

Nothing is downloaded while building. Cadmium's own build fetches five
libraries when it configures; here the four the machine needs (Chiplet,
{fmt}, the ghc filesystem library, fast_float) and the one its tests need
(doctest) are git submodules at the versions Cadmium asks for.

## The sources

```sh
git clone --recurse-submodules https://github.com/ToolAssisted-run/chimera-core-cadmium
```

`extern/cadmium` is Cadmium at a fixed commit. The patches in `patches/` are
applied to it by `waterbox/apply-patches.sh`, which the build runs by itself;
a tree that already has them is left alone, and one that has only some of
them stops the build and says which files.

## The builds

```sh
# the native reference, the sandbox runner, and Cadmium's own tests
meson setup build/meson-native -Dminibox_dir=<chimera>/extern/chimera-common-minibox
ninja -C build/meson-native

# the sandboxed core and the package, installed into <chimera>/build/Cores
waterbox/build-package.sh -r <chimera>
```

- `build/meson-native/run-native` is the core compiled for the host:
  `waterbox/cinterface.cpp` and Cadmium, with nothing of the sandbox.
- `build/meson-guest/core.wbx` is the same sources compiled for the sandbox.
  `build-package.sh` configures that build the first time
  (`waterbox/setup-guest.sh -m <miniBox>`), checks the result
  (`check-wbx.sh`: nothing in it may use thread-local storage), and writes
  the package.
- `build/meson-native/run-wbx` runs `core.wbx` in the sandbox the way
  Chimera's engine does.

## The tests

```sh
CHIMERA_ROOT=<chimera> waterbox/run-gate.sh
```

Each line says what was compared. The script ends with a count of tests that
passed, failed and were skipped, and exits with an error if any failed.
Without `CHIMERA_ROOT` the three tests that need Chimera's headless runner
are skipped, and a skipped test is not a pass.

Every program the tests run comes with the submodules: Cadmium's test
programs and examples, and the programs and cartridges of C-Octo, which
Cadmium carries. Nothing has to be supplied.

- `upstream:tests` - Cadmium's own tests of its interpreters, on each of the
  four kinds of emulation this core uses.
- `machines:sandbox` - eleven machines, a program each, 600 frames with keys
  pressed: the sandboxed core prints the native build's lines (how far the
  machine ran, its memory, its sound, its picture, and every picture on the
  way), and each is the variant, frame rate and picture size it should be.
- `sound:rate` - each machine makes 48000 samples for a second of its frames.
- `state:every-frame`, `state:other-process` - a savestate saved and loaded
  before every frame changes nothing; one saved by one process and loaded
  by another ends where the unbroken run ends.
- `input:keys`, `input:wait-for-key` - keys reach the machine, and a program
  that waits for a key gets it.
- `settings:machine` - the random seed and the speed change a run, and what
  cannot be run is refused with a reason.
- `program:source` - an Octo source file is compiled, and a bad one refused.
- `frontend:picture`, `frontend:states`, `frontend:movie` - the package in
  Chimera's headless runner: a movie with keys recorded and played back,
  with and without savestates around every frame.

Chimera's own checks of the package (it loads, and its keys, settings and
file types are well formed) are run from the Chimera checkout:

```sh
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test \
  source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj -c Release \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

## Running a program by hand

```sh
build/meson-native/run-native <program> <a new folder> --frames 600 --every 60 --ppm out.ppm
build/meson-native/run-wbx build/meson-guest/core.wbx <program> --frames 600 --every 60 --ppm out.ppm
<chimera>/build/meson-linux/chimera-run <chimera>/build/Cores/cadmium.chimeraCore <program> <movie>
```

The two runners take the same options (`waterbox/gate-harness.h` lists
them): `--variant`, `--speed` and `--seed` are the package's three
settings, `--press F:N:MASK` holds keys, `--exercise` presses keys by a fixed
pattern. A line shows the frame, the instructions run, the frames in which
the program asked for the keys, the sound and memory digests, the picture's
size and digest, and a digest of every picture so far.

`chimera-run` plays a movie: to run frames with no keys, give it an empty
file as the movie and `--frames N --record <file>`. Without `--record` an
empty movie has no frames to play.

## What CI does

`.github/workflows/chimera.yml` builds everything above from a fresh clone
on a public runner, runs the whole test script and Chimera's package checks,
and publishes the package. Nothing is skipped there: the workflow fails if
the script's last line is not "0 failed, 0 skipped".

## When something goes wrong

- `extern/cadmium is partly patched`: a file under `extern/cadmium` was
  edited without updating the patches. Run `waterbox/make-patches.sh` (add
  the file to its list first if it is a new one), or start again as the
  message says.
- `the core asked for system call N, which the sandbox does not provide`:
  something in Cadmium called the operating system in a way the sandbox does
  not have. `waterbox/guest-syscalls.cpp` is where such calls are answered.
- The native run and the sandbox run print different lines: look for the
  host's clock, the C library's `rand()`, or memory nobody wrote. The first
  line that differs says the frame.
