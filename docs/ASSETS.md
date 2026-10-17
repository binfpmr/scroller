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
| `G` | goal flag (not solid); touching it shows WELL DONE and restarts the level |
| `.` or space | empty |
| `P` | player start (exactly one) |

Outside the map counts as walls and floor, open above. The game starts with `levels/level1.txt`
(`kStartLevel` in `src/platform/game_loop.cpp`); `levels/test.txt` is used by tests.

Check a level with the real physics:

```sh
build/host/levelcheck assets/levels/level1.txt
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
