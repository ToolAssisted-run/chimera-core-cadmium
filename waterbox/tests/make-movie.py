#!/usr/bin/env python3
"""Writes the keys of a test movie, in one of two forms.

  make-movie.py <frames>               the movie as Chimera's engine reads and
                                       writes it: a row a frame, sixteen
                                       columns between two bars, a key's own
                                       letter (0-9, A-F) where it is held and
                                       a dot where it is not
  make-movie.py <frames> --as-presses  the same keys as this repository's
                                       runners take them: --press F:N:MASK ...

The engine counts frames from 0 and the runners from 1, so row r of the movie
is the runners' frame r + 1.

The keys are held in stretches, as a person plays: in an Octo game key 7 is
left, 9 is right, 5 is up, 8 is down and 6 is the action.
"""
import sys

# (first row, rows, key)
STRETCHES = [(40, 30, 9), (80, 6, 6), (100, 40, 9), (120, 8, 5), (160, 50, 7), (200, 5, 6),
             (230, 60, 9), (250, 10, 5), (300, 30, 8), (340, 25, 7), (350, 6, 6)]


def main():
    frames = int(sys.argv[1])
    stretches = [s for s in STRETCHES if s[0] < frames]
    if "--as-presses" in sys.argv:
        print(" ".join("--press %d:%d:%x" % (first + 1, min(count, frames - first), 1 << key)
                       for first, count, key in stretches))
        return
    for row in range(frames):
        held = {key for first, count, key in stretches if first <= row < first + count}
        print("|" + "".join("%X" % k if k in held else "." for k in range(16)) + "|")


if __name__ == "__main__":
    main()
