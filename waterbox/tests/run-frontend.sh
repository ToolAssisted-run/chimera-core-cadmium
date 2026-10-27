#!/bin/bash
# The window test: the package loaded by Chimera's window (run headless: it
# needs no display), not by its headless runner. Three things are asked:
#  - a program run for a number of frames with no key pressed leaves the
#    machine's whole memory byte for byte what the native build leaves;
#  - a setting made in Chimera's configuration reaches the machine: with
#    another random seed the memory is another, and is what the native build
#    has with that seed;
#  - the keys the package ships become Chimera's keys for the keypad.
#
# Usage: waterbox/tests/run-frontend.sh --chimera-root <chimera checkout> [--frames N]
# Needs: Chimera built with its window, the package
# installed (waterbox/build-package.sh -r <chimera>), the native build.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
frames=300
chimera_root=""
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "say where Chimera is: --chimera-root <chimera checkout>" >&2; exit 2; }
chimera_root="$(cd "$chimera_root" && pwd)"

chimera_exe="$chimera_root/build/meson-linux/source/ui/chimera"
package="$chimera_root/build/Cores/cadmium.chimeraCore"
rn="$root/build/meson-native/run-native"
# a first-generation CHIP-8 game that throws dice, from C-Octo's tests
rom="$root/extern/cadmium/external/c-octo/tests/chipwar.ch8"
[ -x "$chimera_exe" ] || { echo "Chimera's window is not built: $chimera_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "the package is not installed: $package (run waterbox/build-package.sh -r <chimera>)" >&2; exit 1; }
[ -x "$rn" ] || { echo "the native build is not there: $rn" >&2; exit 1; }
[ -f "$rom" ] || { echo "extern/cadmium is not checked out" >&2; exit 1; }

work="$(mktemp -d)"
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

# Chimera's window, run headless: the cores installed in the checkout, its
# settings, cache and recovery files the run's own, and nothing carried over
# from a Chimera installed on this machine
export ALSOFT_DRIVERS=null CHIMERA_CORES_DIR="$chimera_root/build/Cores" CHIMERA_QT_CACHE="$work/qt-cache" CHIMERA_QT_RECOVERY="$work/qt-recovery" \
	CHIMERA_QT_HASHES="$work/qt-file-hashes.txt" CHIMERA_QT_CHIMERA_SETTINGS="$work/no-chimera-config.ini"

ok=0
failed=0
report() { printf '%-22s %-6s %s\n' "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok + 1)) ;; *) failed=$((failed + 1)) ;; esac; }

run_frontend() { # <tag> <configuration> <frames>
	local tag="$1" cfg="$2" nframes="$3"
	local job="$work/job.$tag.txt"
	local out="$work/$tag.ram.bin"
	[ "${lua_script:-}" = dump-controls.lua ] && out="$work/$tag.controls.json"
	{
		echo "frames=$nframes"
		echo "out=$out"
		echo "meta=$work/$tag.meta.txt"
		echo "shot="
	} > "$job"
	local project
	project="$(game_project "$tag" "$cfg" "$package" "$rom")" || { cp "$work/projects/$tag.make.log" "$work/$tag.log"; return 1; }
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 "$chimera_exe" --headless \
		"--config=$cfg" "--core=$package" "--lua=$here/${lua_script:-frontend-ram.lua}" "--project=$project" ) > "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}
native_ram() { # <tag> <options...>
	local tag="$1"; shift
	"$rn" "$rom" "$work/native-dir.$tag" --frames "$frames" --dump-ram "$work/native.$tag.ram.bin" "$@" > "$work/native.$tag.txt" 2>&1
}
# A game runs only in a project (Chimera, 2026-10-26): each run opens one made
# of its game with chimera-run --new-project, holding the settings
# settings_config was given for the config it runs with.
declare -A settings_of
settings_config() { printf '{}' > "$1"; settings_of["$1"]="$2"; }
crun="$chimera_root/build/dll/chimera-run"
game_project() { # tag, config, package, game: prints the project's path
	local dir="$work/projects" settings="${settings_of[$2]:-}"
	local pkg="$3" game="$4"
	local fw=()
	# one copy of each game, every run's project beside it
	local copy="$dir/$(basename "$game")"
	mkdir -p "$dir"
	[ -f "$copy" ] && [ "$(stat -c %s "$copy")" = "$(stat -c %s "$game")" ] || cp "$game" "$copy"
	"$crun" --new-project "$dir/$1.chimeraProject" "$pkg" "$copy" \
		--settings "${settings:-"{}"}" "${fw[@]}" > "$dir/$1.make.log" 2>&1 && echo "$dir/$1.chimeraProject"
}

settings_config "$work/settings.base.json" '{}'
if ! native_ram base; then
	report "window:memory" FAIL "the native runner failed: $(tail -1 "$work/native.base.txt")"
elif ! run_frontend base "$work/settings.base.json" "$frames"; then
	report "window:memory" FAIL "Chimera did not finish the run: $(grep '^detail=' "$work/base.meta.txt" 2>/dev/null) $(tail -2 "$work/base.log" | tr '\n' ' ' | cut -c1-200)"
elif cmp -s "$work/native.base.ram.bin" "$work/base.ram.bin"; then
	report "window:memory" PASS "$frames frames in Chimera's window: the machine's memory ($(wc -c < "$work/base.ram.bin") bytes) is the native build's"
else
	report "window:memory" FAIL "the memory differs from the native build's"
fi

settings_config "$work/settings.seed.json" '{"random_seed": 777}'
if ! native_ram seed --seed 777; then
	report "window:setting" FAIL "the native runner failed: $(tail -1 "$work/native.seed.txt")"
elif ! run_frontend seed "$work/settings.seed.json" "$frames"; then
	report "window:setting" FAIL "Chimera did not finish the run: $(tail -2 "$work/seed.log" | tr '\n' ' ' | cut -c1-200)"
elif cmp -s "$work/native.seed.ram.bin" "$work/seed.ram.bin" && ! cmp -s "$work/seed.ram.bin" "$work/base.ram.bin"; then
	report "window:setting" PASS "with Random Seed 777 set in Chimera's configuration the memory is another, and is the native build's with that seed"
else
	report "window:setting" FAIL "seed 777: same as native $(cmp -s "$work/native.seed.ram.bin" "$work/seed.ram.bin" && echo yes || echo no); differs from seed 0 $(cmp -s "$work/seed.ram.bin" "$work/base.ram.bin" && echo no || echo yes)"
fi

# the keys the package ships are the machine's when nobody changed them (the
# window keeps only the keys a person changed; fresh settings have none)
lua_script=dump-controls.lua
cp "$work/settings.base.json" "$work/settings.keys.json"
if run_frontend keys "$work/settings.keys.json" 1; then
	lua_script=
	if python3 "$here/check-keybinds.py" "$work/keys.controls.json" "$wb/default_keybinds.json" "CHIP-8 Keypad" > "$work/keys.txt" 2>&1; then
		report "window:keys" PASS "$(cat "$work/keys.txt")"
	else
		report "window:keys" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "window:keys" FAIL "Chimera did not finish the run: $(tail -2 "$work/keys.log" | tr '\n' ' ' | cut -c1-200)"
fi

echo
echo "$ok ok, $failed failed"
[ "$failed" -eq 0 ]
