#!/bin/sh
# The core's tests. Each line says what was compared. The script ends with a
# count of tests that passed, failed and were skipped, and with an error if
# any failed. A skipped test is not a pass.
#
#   waterbox/run-gate.sh
#
# CHIMERA_ROOT=<a Chimera checkout> adds the tests that run the package in
# Chimera's own headless runner (see them below).
#
# Needs the native build (meson setup build/meson-native ... && ninja) and the
# sandboxed core (waterbox/setup-guest.sh && ninja -C build/meson-guest).
#
# Every program the tests run is Cadmium's or C-Octo's own test or example
# program, read from the pinned submodules: nothing has to be supplied, and
# the whole script runs on a public CI runner.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
native="$root/build/meson-native/run-native"
runwbx="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
cd="$root/extern/cadmium"
work="$(mktemp -d)"
# A game runs only in a project (Chimera, 2026-10-26): crun_game <package>
# <game> <movie> [chimera-run's options] makes one of the game and the movie,
# with any --settings and --firmware given, and runs it with the rest.
crun_quote() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }
crun_game() {
	cg_pkg="$1" cg_game="$2" cg_movie="$3" cg_made=""
	shift 3
	cg_n=$#
	while [ "$cg_n" -gt 0 ]; do
		case "$1" in
			--settings) cg_made="$cg_made --settings $(crun_quote "$2")"; shift 2; cg_n=$((cg_n - 2)); continue ;;
			--settings=*) cg_made="$cg_made --settings $(crun_quote "${1#--settings=}")"; shift; cg_n=$((cg_n - 1)); continue ;;
			--firmware) cg_made="$cg_made --firmware $(crun_quote "$2")"; set -- "$@" "$1" "$2"; shift 2; cg_n=$((cg_n - 2)); continue ;;
			--firmware=*) cg_made="$cg_made --firmware $(crun_quote "${1#--firmware=}")"; set -- "$@" "$1"; shift; cg_n=$((cg_n - 1)); continue ;;
		esac
		set -- "$@" "$1"; shift; cg_n=$((cg_n - 1))
	done
	cg_dir="$(mktemp -d "$work/project.XXXXXX")"
	eval "env -u DISPLAY \"\$engine\" --new-project \"\$cg_dir/game.chimeraProject\" \"\$cg_pkg\" \"\$cg_game\" \"\$cg_movie\" $cg_made" \
		> "$cg_dir/make.log" 2>&1 || { echo "chimera-run: no project made: $(tail -1 "$cg_dir/make.log")" >&2; return 2; }
	env -u DISPLAY "$engine" --project "$cg_dir/game.chimeraProject" "$cg_pkg" --files "$(dirname "$cg_game")" "$@"
}
trap 'rm -rf "$work"' EXIT
ok=0; failed=0; skipped=0
report() { # <test> <PASS|FAIL|SKIP> <what>
	printf '%-22s %-6s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; FAIL) failed=$((failed + 1)) ;; *) skipped=$((skipped + 1)) ;; esac
}
# what a run ended with, without the word that says how it got there
last() { tail -1 "$1" 2> /dev/null | cut -d' ' -f2-; }
field() { # <file> <name>: the value of name= in the file's last line
	tail -1 "$1" 2> /dev/null | tr ' ' '\n' | sed -n "s/^$2=//p"
}
n=0
# (a shell function's variables are the script's: these two have names of
# their own, or they would overwrite the loops' below)
nat() { # <name> <program> <options...>: a native run, its lines in $work/<name>.out
	run_name="$1"; run_prog="$2"; shift 2
	n=$((n + 1))
	"$native" "$run_prog" "$work/dir-$n" "$@" > "$work/$run_name.out" 2> "$work/$run_name.err"
}
box() { # <name> <program> <options...>: the same in the sandbox
	run_name="$1"; run_prog="$2"; shift 2
	"$runwbx" "$core" "$run_prog" "$@" > "$work/$run_name.out" 2> "$work/$run_name.err"
}

if [ ! -x "$native" ] || [ ! -x "$runwbx" ] || [ ! -f "$core" ] || [ ! -x "$root/build/meson-native/upstream-tests-fp" ]; then
	echo "not built: this needs build/meson-native/run-native, build/meson-native/run-wbx and build/meson-guest/core.wbx (docs/BUILDING.md)" >&2
	exit 2
fi
if [ ! -f "$cd/test-roms/bin/schip-test.sc8" ]; then
	echo "extern/cadmium is not checked out: git submodule update --init --recursive" >&2
	exit 2
fi

# Cadmium's own tests of its interpreters, 33 cases, run on each kind of
# emulation this core uses - the ordinary one ("fp"), the strict one, the
# COSMAC VIP and the DREAM 6800 - as this repository builds them, patches
# included. Native only: they are programs of their own, not the core.
bad=""; cases=0; checks=0
for kind in fp strict vip dream; do
	"$root/build/meson-native/upstream-tests-$kind" > "$work/up-$kind.out" 2>&1
	up_rc=$?
	passed="$(sed -n 's/^\[doctest\] test cases: *\([0-9]*\) | *\([0-9]*\) passed | *\([0-9]*\) failed.*/\1 \2 \3/p' "$work/up-$kind.out")"
	set -- $passed
	if [ "$up_rc" != 0 ] || [ "${1:-0}" -lt 30 ] || [ "${1:-0}" != "${2:-x}" ] || [ "${3:-1}" != 0 ]; then
		bad="$bad $kind(exit $up_rc: ${passed:-no summary})"
	fi
	cases=$((cases + ${2:-0}))
	checks=$((checks + $(sed -n 's/^\[doctest\] assertions: *\([0-9]*\) | *\([0-9]*\) passed.*/\2/p' "$work/up-$kind.out" | head -1)))
done
if [ -z "$bad" ]; then
	report "upstream:tests" PASS "Cadmium's own interpreter tests on the four kinds of emulation: $cases cases passed, $checks checks"
else
	report "upstream:tests" FAIL "$bad"
fi

# The machines, and a program for each: name | program | the variant asked for
# | the variant Cadmium must say it is | frames a second | the picture's size.
# Between them they are every kind of emulation in the core: Cadmium's
# high-level CHIP-8 (SUPER-CHIP, XO-CHIP, MegaChip), its cycle-exact one, a
# whole COSMAC VIP running RCA's interpreters, and a whole DREAM 6800.
gif="$cd/external/c-octo/carts"
octo="$cd/external/c-octo/tests"
machines="
chip8|$octo/chipwar.ch8|auto|CHIP-8|60|64x32
schipc|$octo/gradsim.ch8|auto|SUPER-CHIP-COMPATIBILITY|60|128x64
schip|$cd/test-roms/bin/schip-test.sc8|auto|SUPER-CHIP 1.1|64|128x64
xochip|$cd/examples/XO-CHIP/color-test.xo8|auto|XO-CHIP|60|128x64
cartridge|$gif/superneatboy.gif|auto|XO-CHIP|60|128x64
megachip|$cd/examples/MegaChip8/mcblendtest.8o|megachip|MEGACHIP8|50|384x192
strict|$cd/test-roms/bin/schip-test.sc8|chip-8-strict|CHIP-8-STRICT|60|64x32
vip8e|$cd/test-roms/bin/chip8e-test.c8e|auto|VIP-CHIP-8E|60|64x128
vip8x|$cd/test-roms/bin/chip8x-color-mode-test.c8x|auto|VIP-CHIP-8X|60|64x128
vip64|$cd/examples/CHIP8/vip-chip8-tpd/chip8tpd-test.ch8|auto|VIP-CHIP-8 64x64|60|64x128
dream|$cd/test-roms/bin/schip-test.sc8|dream-6800|CHIP-8-DREAM|50|64x128
"
frames=600

# The sandboxed core is the native build: for every machine, the same
# program with the same keys gives the same line at every sixtieth frame -
# how far the machine has run, its memory, its sound, its picture and every
# picture on the way. And each is the machine it was meant to be, at its own
# frame rate and picture size, and has run something.
bad=""; count=0
old_ifs="$IFS"
for line in $(printf '%s\n' "$machines" | grep '|' | tr ' ' '^'); do
	IFS='|'; set -- $(printf '%s' "$line" | tr '^' ' '); IFS="$old_ifs"
	name="$1"; prog="$2"; asked="$3"; variant="$4"; fps="$5"; size="$6"
	nat "n-$name" "$prog" --frames "$frames" --every 60 --exercise --variant "$asked"
	box "s-$name" "$prog" --frames "$frames" --every 60 --exercise --variant "$asked"
	count=$((count + 1))
	if ! [ -s "$work/n-$name.out" ] || ! cmp -s "$work/n-$name.out" "$work/s-$name.out"; then
		bad="$bad $name(native and sandbox differ: $(last "$work/s-$name.out" | cut -c1-80) $(tail -1 "$work/s-$name.err" | cut -c1-80))"
	elif ! head -1 "$work/s-$name.out" | grep -q "^machine variant=\"$variant\" framesPerSecond=$fps "; then
		bad="$bad $name(it is [$(head -1 "$work/s-$name.out")], not $variant at $fps)"
	elif [ "$(field "$work/s-$name.out" video)" != "$size" ]; then
		bad="$bad $name(picture $(field "$work/s-$name.out" video), not $size)"
	elif [ "$(field "$work/s-$name.out" cycles)" -le 0 ] 2> /dev/null; then
		bad="$bad $name(ran nothing)"
	fi
done
if [ -z "$bad" ]; then
	report "machines:sandbox" PASS "$count machines, $frames frames each with keys pressed: the sandbox gives the native build's lines, and each is the variant, frame rate and picture size it should be"
else
	report "machines:sandbox" FAIL "$bad"
fi

# Sound comes at the machine's own rate: 48000 samples a second is 800 a frame
# at 60 frames a second, 750 at 64 and 960 at 50.
bad=""
for line in $(printf '%s\n' "$machines" | grep '|' | tr ' ' '^'); do
	IFS='|'; set -- $(printf '%s' "$line" | tr '^' ' '); IFS="$old_ifs"
	want=$((frames * 48000 / $5))
	[ "$(field "$work/s-$1.out" audioSamples)" = "$want" ] || bad="$bad $1($(field "$work/s-$1.out" audioSamples), not $want)"
done
if [ -z "$bad" ]; then
	report "sound:rate" PASS "every machine makes 48000 samples for each second of its own frames (800 a frame at 60, 750 at 64, 960 at 50)"
else
	report "sound:rate" FAIL "$bad"
fi

# A state is the whole machine: saved and loaded before every frame, the run
# is the run that was never stopped. For every machine.
bad=""
for line in $(printf '%s\n' "$machines" | grep '|' | tr ' ' '^'); do
	IFS='|'; set -- $(printf '%s' "$line" | tr '^' ' '); IFS="$old_ifs"
	box "r-$1" "$2" --frames "$frames" --every 60 --exercise --variant "$3" --rerecord
	cmp -s "$work/s-$1.out" "$work/r-$1.out" || bad="$bad $1($(last "$work/r-$1.out" | cut -c1-80) $(tail -1 "$work/r-$1.err" | cut -c1-80))"
done
if [ -z "$bad" ]; then
	report "state:every-frame" PASS "$count machines: a state saved and loaded before each of $frames frames changes nothing"
else
	report "state:every-frame" FAIL "$bad"
fi

# And saved by one process half way and loaded by another, it ends where the
# unbroken run ends: a game with keys held across the save, and the COSMAC VIP.
bad=""
for name in cartridge vip8x megachip; do
	line="$(printf '%s\n' "$machines" | grep "^$name|")"
	IFS='|'; set -- $line; IFS="$old_ifs"
	box "h-$1" "$2" --frames "$frames" --every 60 --exercise --variant "$3" --save-state "$work/$1.state" --at 300
	box "o-$1" "$2" --frames "$frames" --every 60 --exercise --variant "$3" --state "$work/$1.state"
	[ -s "$work/$1.state" ] && [ "$(tail -1 "$work/o-$1.out")" = "$(tail -1 "$work/s-$1.out")" ] ||
		bad="$bad $1(unbroken [$(last "$work/s-$1.out" | cut -c1-60)] carried on [$(last "$work/o-$1.out" | cut -c1-60)])"
done
if [ -z "$bad" ]; then
	report "state:other-process" PASS "saved after frame 300 and loaded by another process, a game, the COSMAC VIP and MegaChip end where the unbroken runs end ($(wc -c < "$work/megachip.state") bytes for MegaChip's 16 MiB machine)"
else
	report "state:other-process" FAIL "$bad"
fi

# The keys reach the machine: a run with keys pressed is another run than one
# without, the same one natively and in the sandbox; and the program asked
# for the keys in some frames and not in others (which is what the frontend's
# lag counter is made of).
nat n-idle "$gif/superneatboy.gif" --frames "$frames" --every 60
box s-idle "$gif/superneatboy.gif" --frames "$frames" --every 60
asked="$(field "$work/s-cartridge.out" inputFrames)"
if [ -s "$work/n-idle.out" ] && cmp -s "$work/n-idle.out" "$work/s-idle.out" && ! cmp -s "$work/s-idle.out" "$work/s-cartridge.out" &&
	[ "${asked:-0}" -gt 0 ] && [ "${asked:-0}" -lt "$frames" ]; then
	report "input:keys" PASS "keys pressed make another run of a game than no keys, the same one in the sandbox; the game asked for the keys in $asked of $frames frames"
else
	report "input:keys" FAIL "with keys [$(last "$work/s-cartridge.out" | cut -c1-90)] without [$(last "$work/s-idle.out" | cut -c1-90)] native without [$(last "$work/n-idle.out" | cut -c1-90)]"
fi

# "Wait for a key" (the instruction Fx0A) is answered by the machine and not
# by a window: the test program for it waits, takes a key when it is let go,
# and is somewhere else afterwards. A key that is never let go is not taken.
wait_prog="$cd/test-roms/bin/chip8x-color-mode-test.c8x"
box k-none "$wait_prog" --frames 120
box k-tap "$wait_prog" --frames 120 --press 30:10:20
box k-held "$wait_prog" --frames 120 --press 30:200:20
nat k-ntap "$wait_prog" --frames 120 --press 30:10:20
if [ -s "$work/k-tap.out" ] && cmp -s "$work/k-tap.out" "$work/k-ntap.out" &&
	[ "$(field "$work/k-tap.out" ramHash)" != "$(field "$work/k-none.out" ramHash)" ] &&
	[ "$(field "$work/k-tap.out" cycles)" -gt "$(field "$work/k-none.out" cycles)" ]; then
	report "input:wait-for-key" PASS "a program waiting for a key goes on when key 5 is pressed and let go ($(field "$work/k-none.out" cycles) instructions without, $(field "$work/k-tap.out" cycles) with, $(field "$work/k-held.out" cycles) while it is still held), the same natively"
else
	report "input:wait-for-key" FAIL "no key [$(last "$work/k-none.out" | cut -c1-70)] tapped [$(last "$work/k-tap.out" | cut -c1-70)] native tapped [$(last "$work/k-ntap.out" | cut -c1-70)]"
fi

# The settings are the machine's. Another random seed is another run of a
# game that throws dice, the same one natively and in the sandbox - for the
# later variants (Cadmium's own generator; an XO-CHIP cartridge) and for the
# first CHIP-8's (the COSMAC VIP's way of making a random number; a .ch8
# program). Another speed is another run.
# And a variant that does not exist, or a file whose kind nothing says, is
# refused with a reason.
dice="$gif/contagious.gif"
box d0 "$dice" --frames 900 --exercise
box d1 "$dice" --frames 900 --exercise --seed 777
nat d1n "$dice" --frames 900 --exercise --seed 777
box e0 "$octo/chipwar.ch8" --frames 900 --exercise
box e1 "$octo/chipwar.ch8" --frames 900 --exercise --seed 777
nat e1n "$octo/chipwar.ch8" --frames 900 --exercise --seed 777
box f1 "$dice" --frames 900 --exercise --speed 200
cp "$cd/test-roms/bin/schip-test.sc8" "$work/unnamed.program"
box g0 "$work/unnamed.program" --frames 10
box g1 "$work/unnamed.program" --frames 10 --variant schip-1.1
box g2 "$cd/test-roms/bin/schip-test.sc8" --frames 10 --variant chip-9
why=""
cmp -s "$work/d1.out" "$work/d1n.out" && cmp -s "$work/e1.out" "$work/e1n.out" || why="$why a seeded run differs between native and sandbox;"
grep -q "^end frame=900 " "$work/d0.out" && grep -q "^end frame=900 " "$work/d1.out" && ! cmp -s "$work/d0.out" "$work/d1.out" || why="$why seed 777 changes nothing in XO-CHIP;"
grep -q "^end frame=900 " "$work/e0.out" && grep -q "^end frame=900 " "$work/e1.out" && ! cmp -s "$work/e0.out" "$work/e1.out" || why="$why seed 777 changes nothing in CHIP-8;"
grep -q 'instructionsPerFrame=200$' "$work/f1.out" && ! cmp -s "$work/d0.out" "$work/f1.out" || why="$why speed 200 is not taken;"
grep -q "error.which_variant" "$work/g0.err" || why="$why a file of no known kind is not refused [$(tail -1 "$work/g0.err" | cut -c1-60)];"
head -1 "$work/g1.out" | grep -q 'variant="SUPER-CHIP 1.1"' || why="$why a file of no known kind does not run with a variant chosen;"
grep -q "error.bad_variant.chip-9" "$work/g2.err" || why="$why an unknown variant is not refused;"
if [ -z "$why" ]; then
	report "settings:machine" PASS "another random seed is another run (an XO-CHIP game and a CHIP-8 one), the same natively; another speed is another run; an unknown variant and a file of no known kind are refused with a reason, and that file runs once a variant is chosen"
else
	report "settings:machine" FAIL "$why"
fi

# A source file is compiled from what was handed over, and one that does not
# compile is refused with a reason (it used to be "loaded" as nothing).
box c0 "$gif/test_badcode.8o" --frames 10
box c1 "$gif/test_tiny.8o" --frames 10
if grep -q "error.not_compiled" "$work/c0.err" && grep -q "^end frame=10 " "$work/c1.out"; then
	report "program:source" PASS "an Octo source file is compiled and run in the sandbox, and one with an error in it is refused with a reason"
else
	report "program:source" FAIL "bad source [$(tail -1 "$work/c0.err" | cut -c1-80)] good source [$(last "$work/c1.out" | cut -c1-80)]"
fi

# The package in the frontend's own engine (chimera-run, from a Chimera
# checkout named by CHIMERA_ROOT that has the package installed by
# waterbox/build-package.sh):
#  - a movie recorded there and played back by another process shows, at a
#    frame, the picture this repository's own runner has at that frame;
#  - the same movie with the engine's own save and load before every frame
#    shows the same picture;
#  - a movie's row has sixteen columns, one for each key.
chimera_root="${CHIMERA_ROOT:-}"
engine="$chimera_root/build/meson-linux/chimera-run"
package="$chimera_root/build/Cores/cadmium.chimeraCore"
same="$here/tests/same-picture.py"
if [ -z "$chimera_root" ] || [ ! -x "$engine" ] || [ ! -f "$package" ]; then
	why="set CHIMERA_ROOT to a Chimera checkout with build/meson-linux/chimera-run and the package (waterbox/build-package.sh -r <it>)"
	report "frontend:picture" SKIP "$why"
	report "frontend:states" SKIP "$why"
	report "frontend:movie" SKIP "$why"
else
	game="$gif/superneatboy.gif"
	fe_frames=400
	shot=380
	# a movie with keys in it, written as the engine writes one: a row a
	# frame, sixteen columns, a key's own letter where it is held
	python3 "$here/tests/make-movie.py" "$fe_frames" > "$work/keys.movie"
	crun_game "$package" "$game" "$work/keys.movie" --record "$work/fe.movie" --screenshot "$shot=$work/fe-a.tga" > "$work/fe-record.out" 2>&1
	crun_game "$package" "$game" "$work/fe.movie" --screenshot "$shot=$work/fe.tga" > "$work/fe-play.out" 2>&1
	played=$?
	# the engine counts frames from 0, the runner from 1; the same keys
	box fe "$game" --frames $((shot + 1)) --ppm "$work/fe.ppm" $(python3 "$here/tests/make-movie.py" "$fe_frames" --as-presses)
	verdict="$(python3 "$same" "$work/fe.tga" "$work/fe.ppm" 2>&1)"; same_rc=$?
	if [ "$played" = 0 ] && grep -q "^frames=$fe_frames" "$work/fe-play.out" && [ "$same_rc" = 0 ] && cmp -s "$work/fe-a.tga" "$work/fe.tga"; then
		report "frontend:picture" PASS "a movie of $fe_frames frames with keys, recorded in chimera-run and played back by another process: its picture at frame $shot is this repository's runner's ($verdict)"
	else
		report "frontend:picture" FAIL "chimera-run exit $played; pictures: $verdict; $(tail -1 "$work/fe-play.out")"
	fi
	crun_game "$package" "$game" "$work/fe.movie" --rerecord --screenshot "$shot=$work/fe-states.tga" > "$work/fe-states.out" 2>&1
	stated=$?
	verdict="$(python3 "$same" "$work/fe-states.tga" "$work/fe.ppm" 2>&1)"; same_rc=$?
	if [ "$stated" = 0 ] && [ "$same_rc" = 0 ]; then
		report "frontend:states" PASS "the same movie with the engine's save and load before every frame: the same picture at frame $shot ($verdict)"
	else
		report "frontend:states" FAIL "chimera-run --rerecord exit $stated; pictures: $verdict; $(tail -1 "$work/fe-states.out")"
	fi
	bars="$(grep -m1 '^|' "$work/fe.movie" | tr -cd '|' | wc -c)"
	cols="$(grep -m1 '^|' "$work/fe.movie" | tr -d '|\r\n' | wc -c)"
	held="$(grep -c '[0-9A-F]' "$work/fe.movie")"
	if [ "$bars" = 2 ] && [ "$cols" = 16 ] && [ "$held" -gt 0 ] && cmp -s "$work/keys.movie" "$work/fe.movie"; then
		report "frontend:movie" PASS "a recorded movie's row has sixteen columns, one a key, and the movie recorded is the movie that was played ($held rows with a key held)"
	else
		report "frontend:movie" FAIL "a row has $bars bars and $cols columns; $held rows with a key; recorded $(wc -l < "$work/fe.movie") rows of $(wc -l < "$work/keys.movie")"
	fi
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
