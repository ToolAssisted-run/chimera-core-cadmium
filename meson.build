# chimera-core-cadmium: Cadmium's CHIP-8 machines in miniBox's sandbox, as a
# Chimera core.
#
# Native reference build (run-native + run-wbx under the build directory):
#     meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes
# (needs miniBox's C++ guest toolchain, build/meson-cpp):
#     sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest core.wbx
#
# Upstream builds with CMake and fetches raylib (its window), Chiplet, fmt,
# a filesystem library and fast_float while it configures. Nothing is fetched
# here: the four that the machine needs are pinned submodules under extern/,
# and the sources are listed by hand. They are upstream's own "emulation"
# library (src/emulation/CMakeLists.txt), the program loader and the table of
# known programs from its application, and Chiplet's library. Both flavours
# compile the SAME waterbox/cinterface.cpp; only emulibc.h differs
# (waterbox/native-shim for the reference build).
project('chimera-core-cadmium', ['c', 'cpp'],
  default_options: ['c_std=gnu11', 'cpp_std=c++17', 'optimization=2', 'debug=false', 'warning_level=0'])

cd = 'extern/cadmium'
cl = 'extern/chiplet'

# the submodule pin is upstream as it is; the patches are laid over it
# (waterbox/apply-patches.sh does nothing to a tree that has them)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/chimera-common-minibox)')
  endif
endif

defs = [
  '-DNDEBUG',
  # the emulation library's own switch (src/emulation/CMakeLists.txt)
  '-DCADMIUM_WITH_GENERIC_CPU',
  # fmt as headers only: nothing of it to build
  '-DFMT_HEADER_ONLY',
  # what the patches test for
  '-DCHIMERA_CORE',
]
quiet = [
  '-fno-strict-aliasing',
  # one answer from a floating-point expression on every machine: no
  # contraction into fused multiply-adds, which a compiler may otherwise
  # choose where the host has them
  '-ffp-contract=off',
]
add_project_arguments(defs + quiet, language: ['c', 'cpp'])

inc = include_directories(
  'waterbox',
  'waterbox/shims',
  cd / 'src',
  cd / 'external',
  cd / 'external/c-octo/src',
  cl / 'include',
  cl / 'external',
  'extern/fmt/include',
  'extern/ghc-filesystem/include',
  'extern/fast_float/include',
)

core_sources = files(
  # upstream's emulation library
  cd / 'src/emulation/time.cpp',
  cd / 'src/emulation/chip8cores.cpp',
  cd / 'src/emulation/chip8opcodedisass.cpp',
  cd / 'src/emulation/chip8emulatorbase.cpp',
  cd / 'src/emulation/chip8options.cpp',
  cd / 'src/emulation/hardware/cdp186x.cpp',
  cd / 'src/emulation/hardware/mc682x.cpp',
  cd / 'src/emulation/chip8vip.cpp',
  cd / 'src/emulation/chip8dream.cpp',
  cd / 'src/emulation/utility.cpp',
  cd / 'src/emulation/properties.cpp',
  # the loader, the table of known programs, and the settings type they share
  cd / 'src/chip8emuhostex.cpp',
  cd / 'src/librarian.cpp',
  cd / 'src/configuration.cpp',
  # Chiplet: the Octo compiler and decompiler the loader uses
  cl / 'src/octo_compiler.cpp',
  cl / 'src/chip8compiler.cpp',
  cl / 'src/chip8decompiler.cpp',
  cl / 'src/octocompiler.cpp',
  cl / 'src/octocartridge.cpp',
  'waterbox/stb_image_impl.c',
  'waterbox/cinterface.cpp',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  # miniBox's musl and libstdc++ arrive through the cross file. The -u lines
  # name the pthread functions libgcc's unwinder refers to weakly: a weak
  # reference is not enough to pull them out of a static libc, and it would
  # call address zero.
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources + files('waterbox/guest-syscalls.cpp'),
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      '-Wl,-u,pthread_mutexattr_init', '-Wl,-u,pthread_mutexattr_settype',
      '-Wl,-u,pthread_mutexattr_destroy',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  # Cadmium's own tests of its interpreters (test/, 33 cases), built for each
  # kind of emulation this core uses: the ordinary one, the strict one, the
  # COSMAC VIP and the DREAM 6800. They are run natively, by the test script.
  upstream_tests = files(
    cd / 'test/main.cpp',
    cd / 'test/basic_opcode_tests.cpp',
    cd / 'test/variant_specific_opcode_tests.cpp',
    cd / 'test/chip8adapter.cpp',
  )
  emulation = static_library('emulation-for-tests', files(
      cd / 'src/emulation/time.cpp',
      cd / 'src/emulation/chip8cores.cpp',
      cd / 'src/emulation/chip8opcodedisass.cpp',
      cd / 'src/emulation/chip8emulatorbase.cpp',
      cd / 'src/emulation/chip8options.cpp',
      cd / 'src/emulation/hardware/cdp186x.cpp',
      cd / 'src/emulation/hardware/mc682x.cpp',
      cd / 'src/emulation/chip8vip.cpp',
      cd / 'src/emulation/chip8dream.cpp',
      cd / 'src/emulation/utility.cpp',
      cd / 'src/emulation/properties.cpp',
    ),
    include_directories: inc)
  foreach kind : [['fp', '-DTEST_CHIP8EMULATOR_FP=1', 'C8FP:'], ['strict', '-DTEST_CHIP8EMULATOR_STRICT', 'C8ST:'],
                  ['vip', '-DTEST_CHIP8VIP', 'C8RV:'], ['dream', '-DTEST_CHIP8DREAM', 'C8RD:']]
    executable('upstream-tests-' + kind[0], upstream_tests,
      include_directories: [inc, include_directories('extern/doctest', cd / 'test')],
      cpp_args: [kind[1], '-DC8CORE="' + kind[2] + '"'],
      link_with: emulation,
      dependencies: [m])
  endforeach

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so'],
    build_rpath: mbhost)
endif
