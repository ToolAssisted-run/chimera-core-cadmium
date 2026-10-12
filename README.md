# chimera-core-cadmium

A CHIP-8 core for [Chimera](https://github.com/ToolAssisted-run/chimera), the
frontend for tool-assisted speedruns. CHIP-8 is a small programming language
for games from 1977, first run on the RCA COSMAC VIP computer and since then
on calculators and on many machines that never existed. The core is built on
[Cadmium](https://github.com/gulrak/cadmium) and produces one file,
`cadmium.chimeraCore`, that goes in Chimera's `Cores` folder.

## Status

New. It runs Cadmium's and C-Octo's own test and example programs, with
picture, sound, keys and savestates, and gives the same results in the
sandbox as a plain native build does. Chimera's window has loaded it only
under a test script; nobody has played a game on it yet.

- The picture is the emulated machine's own, so it is the same on every
  computer and needs no graphics card.
- The same input gives the same run every time.
- Twenty-one kinds of CHIP-8 are offered (see Settings). Three ways of
  emulating are behind them: Cadmium's ordinary one, its "strict" one with
  the original machine's exact timing, and the whole original computer (a
  COSMAC VIP or a DREAM 6800) running its own CHIP-8 interpreter.

## What you bring

A CHIP-8 program. Its extension says which kind of CHIP-8 it is for:
`.ch8`, `.sc8` (SUPER-CHIP), `.xo8` (XO-CHIP), `.mc8` (MegaChip), `.ch10`,
`.c8x`, `.c8e`, `.hc8`, `.c8tp`, `.c8b`. An Octo cartridge (`.gif`) and an
Octo source file (`.8o`) are compiled when the project starts. A `.bin`,
`.ram` or `.vip` file is loaded into a bare COSMAC VIP. Nothing else is
needed: the machines' own programs are part of Cadmium.

Chimera picks this core by itself for the extensions that only CHIP-8 uses.
For a `.gif`, `.bin`, `.ram` or `.vip` file choose the core yourself in the
New Project window.

## Keys and settings

The sixteen keys of a CHIP-8 keypad, 0 to F. By default the keypad's four
rows, `1 2 3 C`, `4 5 6 D`, `7 8 9 E`, `A 0 B F`, are the keyboard's
`1 2 3 4`, `Q W E R`, `A S D F`, `Z X C V`.

- **Variant**: which CHIP-8 machine runs the program. `auto` lets Cadmium
  decide: it knows many programs by their contents, and otherwise goes by the
  file's extension. Choose one yourself when a program runs wrongly.
- **Instructions per Frame**: the machine's speed. 0 uses the variant's own,
  or the speed Cadmium knows for the program.
- **Random Seed**: the number the machine's random numbers start from.

All three are part of the machine: a movie made with one value does not play
back with another. The last two do not apply to the variants that emulate a
whole computer, or to `chip-8-strict`.

RAM Watch and RAM Search see the machine's memory as one domain, "RAM".

## Building and testing

[docs/BUILDING.md](docs/BUILDING.md) has every step. [docs/PLAN.md](docs/PLAN.md)
is the design log: what was decided, why, and what was measured.

## Licence

This repository's own code is MIT, and so is Cadmium. The built package
carries the licence texts of everything in it. See [LICENSE](LICENSE).
