# CHIP-8 core - plan and design log

## What this is

A Chimera core for CHIP-8, built on Cadmium (gulrak/cadmium, pinned at
`extern/cadmium`). Requested in chimera issue 253, which named two
emulators, CubeChip and Cadmium, "and/or". This is Cadmium; "Why Cadmium"
below says why, and what CubeChip would add.

Where it stands (2026-10-12). The core is built and tested and has not been
published: the repository is local, with no remote.

- It runs every test and example program that comes with Cadmium and with
  C-Octo, on eleven machines, and the sandboxed core gives the native build's
  results on all of them.
- Savestates work, in the same process and carried into another.
- As a package it runs in Chimera's headless runner (`chimera-run`), on
  Linux and on Windows, with the same pictures on both.
- Chimera's window has not run it. Nobody has played a game on it.

## The pin

Cadmium `main` at 6d9f103 (2025-10-26). The last release tag, v1.1.8, is
from 2024-01-01; `main` has a year of fixes on top of it (the emulation
library differs in 24 files). The author's work on a version 2 is on other
branches and is not used.

Cadmium's build downloads its libraries while it configures. Here each is a
submodule at the version Cadmium asks for:

| Library | Version | For |
| --- | --- | --- |
| Chiplet (gulrak/chiplet) | tag `dev-v1.x`, 3cd8f46 | the Octo compiler and decompiler |
| {fmt} | 9.1.0 | text formatting |
| ghc::filesystem | v1.5.12 | paths |
| fast_float | v6.1.0 | the Octo compiler's numbers |
| doctest | v2.4.9 | Cadmium's own tests (native only, not in the core) |

raylib, its window library, is not used: `waterbox/shims/raylib.h` stands in
for the two functions the compiled files call (a log line and one colour
conversion).

## The eight questions (chimera docs/new-core.md), answered from the source

- **Is it a new core?** Yes. No core here runs CHIP-8.
- **May it be distributed and modified?** Cadmium is MIT, and so is
  everything compiled in with it, or freer (`waterbox/package-licenses.json`
  lists fourteen components). One thing is the owner's to know: Cadmium's
  source contains, as data, the programs the emulated computers came with -
  the COSMAC VIP's monitor and RCA's CHIP-8 interpreters (1977-78), and
  CHIPOS for the DREAM 6800 (Michael J. Bauer, 1978; Cadmium's README says
  it is used with his permission). They are a few hundred bytes each, they
  are in every COSMAC VIP emulator, and they are in the core because they
  are in Cadmium. RCA's have no stated licence.
- **One thread on one clock?** Yes, as it comes. The emulation library has no
  threads; a frame is a number of instructions (or, for the whole-computer
  variants, a number of processor cycles). The host's clock is read in one
  place, "as fast as the host can" when asked for 0 instructions a frame,
  which the core never asks for.
- **Where does the picture come from?** The machine's own screen buffer.
- **Does it recompile?** No. Interpreters throughout.
- **What must the user bring?** A program. No firmware.
- **What can a test run on a public runner?** Everything. Cadmium carries
  its own test programs and examples, and C-Octo's programs and cartridges.
- **How big is it?** Small: the biggest machine has 16 MiB of memory.

## Why Cadmium, and what CubeChip would add

Both were read (2026-10-12).

- Cadmium: MIT; the emulation is a library of its own, apart from the
  window; it has tests of its interpreters; besides the usual CHIP-8 kinds
  it emulates the two original computers whole, which is the most faithful
  CHIP-8 there is and the only way to run programs with machine code in
  them; and it carries a table of known programs with the kind and speed
  each needs. Last commit to the pinned branch 2025-10-26.
- CubeChip (janitor-raus/CubeChip): MPL-2.0, under active development (last
  push 2026-10-09), built on SDL3. It also emulates BytePusher and GigaChip,
  which Cadmium does not.

One emulator was enough to answer the request, and Cadmium's shape made it
the smaller and surer job. CubeChip as a second core, or its extra machines,
is a separate piece of work nobody has started.

## How the core is put together

`waterbox/cinterface.cpp` is the whole adapter. Cadmium calls the object an
emulator talks to a "host": it is asked for keys and told when the screen
changed. Cadmium has such a host with no window (`Chip8HeadlessHost`, for
its command-line mode); the core's `Machine` is the same thing with keys
that are the frontend's, a picture that is kept, and the state of "wait for
a key" inside it.

Three parts of Cadmium are compiled (the list is in `meson.build`):

- its emulation library (`src/emulation`), whole;
- from its application, the program loader (`chip8emuhostex.cpp`) and the
  table of known programs with the code that guesses a program's kind
  (`librarian.cpp`). These decide which machine a program gets, and using
  them as they are is what makes the core choose as Cadmium chooses;
- Chiplet's library, which the loader uses to compile Octo sources and
  cartridges and to look inside a `.ch8` file.

**A frame** is one call of the emulator's `tick`. Then the picture is taken
if the emulator says it changed, and the sound for that frame is asked for.

**The picture.** The size comes from the emulator every frame: 64x32,
128x64, and for the COSMAC VIP and the DREAM 6800 64x128 (their display
chip's lines; a CHIP-8 dot is four lines high). All of these are twice as
wide as high on a television, so the package says 2:1 (512x256) and the
frontend stretches. MegaChip's 256x192 is not that shape, so the core gives
it black borders left and right, 384x192. MegaChip draws into a work buffer
and shows it when the program says so, which is why the picture is taken
when the emulator says "update" and not when the frame ends: taken at the
frame's end, a MegaChip picture is half drawn.

**The sound** is Cadmium's: one channel, asked for at 48000 samples a
second. 48000 and not 44100 because the machines have three frame rates -
60, 64 (the HP-48 calculators' variants) and 50 (MegaChip and the DREAM
6800) - and 48000 divides by all three: 800, 750 and 960 samples a frame.

**The keys** are sixteen, 0 to F. "Wait for a key" (the instruction Fx0A)
is answered in Cadmium's window by statics of that window; here it is the
machine's own state, so it is in a savestate: a key that goes down is
noticed, and reported when it comes up, as long as the program is still at
that instruction.

**Memory for tools**: one domain, "RAM", the emulator's memory (4 KiB, 64
KiB for XO-CHIP, 16 MiB for MegaChip, the computer's whole memory for the
VIP and the DREAM).

## What had to change, and what was decided

**Two patches** to Cadmium (`patches/`), both behind `CHIMERA_CORE`:

1. *The loader reads no settings file.* Cadmium's loader, when made, reads
   `config.json` from the user's home folder and lists the folder it was
   last in. A core must not: what it is told comes from the frontend. The
   same patch compiles an Octo source from the bytes handed over instead of
   reading the file again by its name, as Cadmium already does for a
   cartridge. Natively the old way looked for the file in the wrong folder;
   in the sandbox it found nothing. Cost: a source that includes other
   files is not supported.
2. *The random seed is the machine's.* Cadmium has a seed option for its
   test mode that sets only one of its two generators; the patch sets both.

**Random numbers** needed a decision. Cadmium makes them three ways:

- for the first CHIP-8 and its close relations, the way the COSMAC VIP's
  interpreter did, from a counter that also advances every frame;
- for the later variants (SUPER-CHIP onwards), the C library's `rand()`;
- for its test mode, a small generator of its own ("rand-lcg").

`rand()` is another generator in another C library, so the native build
(glibc) and the sandbox (musl) would not agree, and it is seeded by nobody.
The core asks for "rand-lcg" where Cadmium would use `rand()`, and leaves
the VIP's way alone. Measured: with `rand()` put back, a game that throws
dice differs between native and sandbox from the first sixty frames. The
setting **Random Seed** starts either generator. The whole-computer variants
and the strict one make their random numbers inside the emulated machine,
from its own memory and timing, and take no seed.

**Speed.** The setting **Instructions per Frame** overrides the variant's or
the known program's speed; 0 leaves it. The core never passes 0 on to
Cadmium, where 0 means "as fast as the host can" and reads the host's clock.

**Which machine.** The setting **Variant** is `auto` or one of twenty-one
names. With `auto` the loader decides, in Cadmium's order: a program its
table knows (by SHA-1) gets what the table says; otherwise the extension
decides; a `.ch8` file is looked into to see which instructions it uses. A
file with an extension the loader does not know is refused under `auto`,
with a message that says to choose a variant; with a variant chosen it is
loaded as plain bytes. A source file that does not compile is refused -
Cadmium's window "loads" it, to show the error in its editor, and the core
used to start a machine with nothing in it.

**A system call the sandbox lacks.** Cadmium's loader asks whether the
folder a program came from exists. In the sandbox that is `lstat`, which
miniBox does not have, and the core died in `Init`.
`waterbox/guest-syscalls.cpp` answers it for the sandbox only: a path is
there when it can be opened.

**An answer to a tool is in no savestate.** The scratch buffer the picture
is converted in, and the text `DebugVariant` returns, are in memory a state
does not carry.

## What was measured (2026-10-12)

The test script, `waterbox/run-gate.sh`, with `CHIMERA_ROOT` set: 12 tests,
12 passed, none skipped. `docs/BUILDING.md` says what each compares.

- Cadmium's own interpreter tests pass on the four kinds of emulation the
  core uses: 132 cases, 10,614 checks.
- Eleven machines (CHIP-8, SUPER-CHIP 1.1, SUPER-CHIP compatibility,
  XO-CHIP twice - a program and an Octo cartridge -, MegaChip from a source
  file, the strict CHIP-8, the COSMAC VIP with CHIP-8E, with CHIP-8X and in
  its 64x64 mode, and the DREAM 6800): 600 frames each with keys pressed,
  the same lines natively and in the sandbox.
- The same eleven with a savestate saved and loaded before every frame: no
  difference. A state carried into another process: no difference (a state
  of MegaChip's 16 MiB machine is 945,343 bytes).
- Chimera's package checks: 17 of 17.
- Windows: `chimera-run.exe` with the package gives the pictures Linux
  gives, file for file, for a game played from a movie with keys (also with
  a savestate around every frame), for the COSMAC VIP, and for MegaChip
  from a source file.
- A native build with GCC 13, the compiler a CI runner has, gives the runs
  of the GCC 14 build on five machines, sound included.

Each test was seen to fail once:

- a stale savestate (`HARNESS_BREAK_STALE_STATE`: half way, the state loaded
  is the one from the frame before): six of the eleven lines differ;
- the C library's `rand()` instead of Cadmium's own generator, by hand:
  native and sandbox part at the first line;
- the frontend's picture against the runner's picture one frame away, by
  hand: 64 of 8,192 pixels differ;
- the picture's size: the first table said 64x32 for the DREAM 6800, which
  is 64x128, and the test said so;
- the test script's first version failed six tests of its own making (a
  shell function's variables are the script's, and two functions overwrote
  a loop's), and `frontend:` tests in an earlier core taught that
  `chimera-run` without `--record` plays an empty movie's zero frames.

## What is not done

- Chimera's window has not run the core: no project made in the New Project
  window, no TAStudio, no key pressed by a person.
- Cadmium's "quirks" (eighteen switches, such as whether a shift uses one
  register or two) are not settings. A variant sets them all, and the table
  of known programs sets them for the programs it knows; a program that
  needs an unusual mix has no way to ask for it. They could be added as
  settings.
- A source file that includes other files does not compile.
- The standard CHIP-8 test suite (Timendus's) was not run: it is not in the
  submodules, and its licence was not looked at.
- CubeChip, and the machines only it has (BytePusher, GigaChip).

## What is the owner's

- Whether to publish: a public repository
  `ToolAssisted-run/chimera-core-cadmium`, the first CI run, a row in
  Chimera's list of official cores and README, the comment on issue 253.
- The note above about the computers' own programs inside Cadmium.
- Whether anything should be reported to Cadmium's author (the loader
  reading a source file again by name; `rand()` unseeded).
