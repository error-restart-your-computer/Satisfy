# SATISFY

A satisfying terminal toybox in a single C++17 file — no dependencies beyond a
POSIX terminal and `g++`.

Nine little worlds, true colour, mouse support, and a lot of tiny details.

| # | world         | what makes it satisfying |
|---|---------------|--------------------------|
| 1 | FALLING SAND  | 15 materials that react: lava + water = stone + steam, acid dissolves, oil floats, fire spreads through wood, plants drink water and grow, gunpowder chains explode, sparks travel, ice melts and freezes |
| 2 | GAME OF LIFE  | colourful generations with fading ghost trails, wrap-around, adjustable speed |
| 3 | WATER RIPPLE  | a real wave-equation surface — click, drag, or let it rain |
| 4 | PARTICLES     | attract with left button, repel with right, glowing motion trails |
| 5 | FIREWORKS     | rockets, ring bursts, golden willows, twinkling glitter |
| 6 | BOIDS         | a flock you can gather or scatter |
| 7 | PLASMA        | four generative colour fields, bent by the mouse |
| 8 | GLYPH RAIN    | falling code with bright heads and long trails |
| 9 | ZEN           | a breathing circle, rotating mandala, drifting bokeh, a quote |

## Build

```sh
make          # g++ -std=c++17 -O2 -Wall -Wextra -pedantic
./satisfy
```

## Keys

```
1-9 / Tab   switch world        space        pause / resume
h or ?      help overlay        c            clear the world
t           next colour theme   a            auto-demo (hands-free)
[ ] wheel   brush size          , .          world option
S           save screenshot     q / Ctrl-C   quit (terminal restored)
```

Mouse: **left** paints / attracts / splashes, **right** erases / repels,
**wheel** changes the brush size. Every world also has its own keys — they are
listed on the bottom bar.

## Options

```
--mode NAME|N   start world (sand, life, ripple, particles, fireworks,
                boids, plasma, rain, zen — or 1..9)
--theme NAME    NEON SUNSET OCEAN CANDY FOREST RAINBOW
--fps N         frame-rate cap (default 60)
--demo          start with auto-demo on
--frames N      render N frames then exit (headless testing)
--no-mouse      ignore mouse input
--help          usage
```

## Notes

* Rendering uses upper-half-block characters, so each terminal cell shows two
  true-colour pixels. Output is diffed against the previous frame, so only
  changed cells are written.
* Terminals without UTF-8 in `LANG`/`LC_CTYPE` fall back to an ASCII
  brightness ramp automatically.
* Resizing the terminal is safe: every world rebuilds itself for the new size.
* Screenshots are written as `satisfy_MMDD_HHMM.ans` — replay one with
  `cat satisfy_*.ans`.
