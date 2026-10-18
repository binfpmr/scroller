# Assets

Everything under `assets/` is plain text, packed at build time by `tools/pack_assets.py` into `\scroller\assets.pak` on the
ESP (format: `src/lib/pack.h`) and parsed by the game at boot. Host tests parse the same files, so a broken asset fails
`ctest --preset host` before it ever reaches a stick; on the target a broken asset is shown on screen with file and line.

## Levels (`assets/levels/*.txt`)

One line per tile row, one character per 16x16 tile; shorter lines are padded with empty tiles.

| Char | Tile |
|---|---|
| `#` | ground (solid), drawn with grass when the tile above is open |
| `X` | stone (solid) |
| `G` | goal flag (not solid); touching it shows WELL DONE and starts the next level |
| `C` | checkpoint (not solid, stands on the ground like `P`); touching it makes it the respawn point (turns green) |
| `.` or space | empty |
| `P` | player start (exactly one) |

Outside the map counts as walls left and right, open above, and a bottomless pit below: falling below the map
respawns the player at the last checkpoint reached (or the start). The game plays `levels/level1.txt`, `levels/level2.txt`, ...
in order (up to 8, as many as exist), then starts over with level 1; `levels/test.txt` is used by tests only.
Level 1 is the beginner level: walking and plain jumps must be enough (rises of at most 2 tiles, gaps of at most
3), which the host tests check with `levelcheck --basic`.

Check a level with the real physics:

```sh
build/host/levelcheck assets/levels/level2.txt
build/host/levelcheck --basic assets/levels/level1.txt   # only walking and plain jumps
```

It prints the map with `o` on every spot the player can stand on, `x` on dead ends (reachable, but the goal is not
reachable from there), `g` for a reachable goal, and exits non-zero if the goal is unreachable or a dead end exists.
It knows walking, jumps with different run-ups and hold times, duck-jumps, single and chained wall-jumps; human players
may manage tricks it does not (it never claims something is reachable that is not).

## Sprite sheets (`assets/sprites/*.txt`)

```
# comment (not allowed inside a frame)
palette
  K 2a1e3c          <char> <rrggbb>; '.' is always transparent
  O f0962a
size 16 16          size of the frames that follow (may change between frames)
frame idle          then exactly <height> rows of exactly <width> palette characters
................
....KKKK........
...
```

- `sprites/player.txt`: frames `idle`, `blink`, `run1`..`run4`, `jump`, `fall`, `duck`; drawn facing right and mirrored
  for left. The frame is centred on the 12x14 hitbox with its bottom on the hitbox bottom.
- `sprites/tiles.txt`: `ground_top`, `ground`, `stone`, `flag`.

All listed frames must exist; a missing one is reported at boot (and by the tests).

## Music and sound effects (`assets/music/*.txt`, `assets/sounds/*.txt`)

A small tracker format, one row per line, one column per channel. The game plays `music/minuet.txt` in a loop and
`sounds/jump.txt`, `land.txt`, `walljump.txt`, `goal.txt` as effects (on two extra voices above the music).

```
# Minuet in G, BWV Anh. 114
tempo 100                 # beats per minute
rows_per_beat 2           # one row per eighth note
loop yes                  # music loops; sound effects use 'loop no'

instrument lead square duty 25 attack 3 decay 120 sustain 55 release 60 volume 55
instrument bass triangle attack 3 decay 200 sustain 70 release 80 volume 80
instrument kick noise attack 1 decay 70 sustain 0 volume 45 note C-3
instrument hat  noise attack 1 decay 25 sustain 0 volume 20 note C-7

channels lead bass kick hat
# bar 1
D-5 G-3 x .
... ... . .
G-4 ... . x
```

| Item | Meaning |
|---|---|
| waves | `square` (with `duty` 1..99 %), `triangle`, `saw`, `noise` (pitch = brightness) |
| instrument keys | `attack`, `decay`, `release` (ms), `sustain` and `volume` (%), `note` (what `x` plays) |
| `C-4`, `F#5`, `Bb3` | start a note (C-4 = middle C, A-4 = 440 Hz) |
| `x` | hit: the instrument's own `note` (percussion) |
| `.` / `...` | keep the current note going |
| `-` / `---` | release the note |
| `\|` | ignored (bar lines inside a row), `#` starts a comment |

Up to 8 instruments and 6 channels. A percussion instrument usually has `sustain 0`, so a hit dies after its decay.

Listen on the development machine (48 kHz WAV via the game's own synthesizer):

```sh
build/host/render_audio assets/music/minuet.txt /tmp/minuet.wav && aplay /tmp/minuet.wav
```
