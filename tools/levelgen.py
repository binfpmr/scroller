"""Generates levels 6-15: tools/levelgen.py assets/levels [N ...]

Sky levels are built from "legs" (runs, climbs, descents, shafts, chasms) along a path of floating platforms; ground,
underground, hill-climb and pyramid levels are shaped on a tile grid (Grid).

Coordinates: a platform is a row of '#' (or 'X') tiles at row y from x0 to x1; the player stands on it in row y-1.
The generator walks a cursor (the last platform) through the legs, then crops the canvas around everything placed.
"""
import random
import sys
from pathlib import Path


class Level:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.tiles = {}  # (x, y) -> char
        self.last = None  # (x0, x1, y) of the platform the player stands on last

    def plat(self, x0, x1, y, c='#'):
        for x in range(x0, x1 + 1):
            self.tiles[(x, y)] = c
        self.last = (x0, x1, y)

    def put(self, x, y, c):
        self.tiles[(x, y)] = c

    def free(self, x, y):
        return (x, y) not in self.tiles

    # --- legs ---------------------------------------------------------------------------------------------------
    def start(self, width=6):
        self.plat(0, width - 1, 0)
        self.put(1, -1, 'P')

    def checkpoint(self):
        x0, x1, y = self.last
        x = x0 + (x1 - x0) // 2
        if self.free(x, y - 1):
            self.put(x, y - 1, 'C')

    def run(self, n, max_gap=3, max_rise=2, enemies=0.4):
        """n platforms to the right with gaps and gentle height changes."""
        for _ in range(n):
            x0, x1, y = self.last
            gap = self.rng.randint(1, max_gap)
            dy = self.rng.randint(-max_rise, 2)          # negative: up
            if gap == 3 and dy < -1:
                dy = -1                                   # a long jump and a rise together are too hard
            w = self.rng.randint(4, 9)
            nx0 = x1 + 1 + gap
            self.plat(nx0, nx0 + w - 1, y + dy)
            r = self.rng.random()
            if r < enemies and w >= 6:
                self.put(nx0 + w // 2, y + dy - 1, 'w' if self.rng.random() < 0.6 else 'j')
            elif r > 1 - enemies / 2 and gap >= 2:
                self.put(x1 + 1 + gap // 2, y - 5, 'f')

    def climb(self, n, side=1):
        """A zigzag up: platforms 3 rows apart, each next to the previous one, alternating sides."""
        for i in range(n):
            x0, x1, y = self.last
            w = self.rng.randint(4, 6)
            if (i % 2 == 0) == (side > 0):
                nx0 = x1 + 1
            else:
                nx0 = x0 - w
            self.plat(nx0, nx0 + w - 1, y - 3)
            if self.rng.random() < 0.25:
                self.put(nx0 + w // 2, y - 6, 'f')
        x0, x1, y = self.last
        self.plat(x1 + 1, x1 + 6, y - 2)                 # step off to the right

    def descend(self, n):
        """Down to the right: drops of 3-5 rows, small gaps."""
        for _ in range(n):
            x0, x1, y = self.last
            gap = self.rng.randint(1, 2)
            w = self.rng.randint(4, 7)
            nx0 = x1 + 1 + gap
            self.plat(nx0, nx0 + w - 1, y + self.rng.randint(3, 5))
            if self.rng.random() < 0.3 and w >= 6:
                self.put(nx0 + w // 2, self.last[2] - 1, 'w')

    def shaft(self, height=12):
        """A wall-jump shaft: walk in at the bottom (doorway), climb between walls 3 apart, leave at the top."""
        x0, x1, y = self.last
        floor_y = y
        bx = x1 + 1                                       # shaft floor continues the platform
        self.plat(x1 + 1, x1 + 3, floor_y)               # the shaft's floor (x1+1 .. x1+3)
        left, right = x1, x1 + 4                          # walls
        for yy in range(floor_y - height, floor_y - 1):   # left wall leaves the doorway row (floor_y - 1) open
            self.put(left, yy, 'X')
        for yy in range(floor_y - height, floor_y):
            self.put(right, yy, 'X')
        self.plat(right + 1, right + 7, floor_y - height - 1)   # exit ledge right of the shaft's top
        self.last = (right + 1, right + 7, floor_y - height - 1)

    def chasm(self, n):
        """Small floating platforms (2-3 wide) over nothing, flyers between them."""
        for i in range(n):
            x0, x1, y = self.last
            gap = self.rng.randint(2, 3)
            w = self.rng.randint(2, 3)
            nx0 = x1 + 1 + gap
            self.plat(nx0, nx0 + w - 1, y + self.rng.randint(-1, 1))
            if i % 3 == 1:
                self.put(x1 + 1 + gap // 2, y - 4, 'f')

    def duck_step(self):
        """A 5-row step up, right next to the platform: needs a duck-jump."""
        x0, x1, y = self.last
        self.plat(x1 + 1, x1 + 6, y - 5)

    def goal(self):
        x0, x1, y = self.last
        self.plat(x1 + 1, x1 + 5, y)
        self.put(x1 + 3, y - 1, 'G')

    # --- output ---------------------------------------------------------------------------------------------------
    def render(self, margin_x=3, margin_top=8, margin_bottom=4):
        xs = [x for x, _ in self.tiles]
        ys = [y for _, y in self.tiles]
        min_x, max_x = min(xs) - margin_x, max(xs) + margin_x
        min_y, max_y = min(ys) - margin_top, max(ys) + margin_bottom
        w = max(max_x - min_x + 1, 22)
        h = max(max_y - min_y + 1, 16)
        rows = [['.'] * w for _ in range(h)]
        for (x, y), c in self.tiles.items():
            rows[y - min_y][x - min_x] = c
        return '\n'.join(''.join(r) for r in rows) + '\n'


def level(seed, legs):
    lv = Level(seed)
    lv.start()
    for i, leg in enumerate(legs):
        name, *args = leg
        if i > 0:
            lv.checkpoint()  # at the start of every leg but the first (that is the start)
        getattr(lv, name)(*args)
    lv.goal()
    return lv.render()


class Grid:
    """A fixed-size tile map for the shaped levels (ground, underground, hills, pyramid)."""

    def __init__(self, w, h, seed):
        self.w, self.h = w, h
        self.g = [['.'] * w for _ in range(h)]
        self.rng = random.Random(seed)

    def fill(self, x0, y0, x1, y1, c):
        for y in range(max(y0, 0), min(y1, self.h - 1) + 1):
            for x in range(max(x0, 0), min(x1, self.w - 1) + 1):
                self.g[y][x] = c

    def put(self, x, y, c):
        self.g[y][x] = c

    def get(self, x, y):
        return self.g[y][x]

    def render(self):
        return '\n'.join(''.join(r) for r in self.g) + '\n'


def ground_level(seed, w=200, h=24):
    """On the ground: hills and dips, bottomless pits, blocks, enemies; checkpoints every ~50 tiles."""
    gr = Grid(w, h, seed)
    rng = gr.rng
    top = h - 6                       # ground surface row of the first segment
    x = 0
    surfaces = []                     # (x0, x1, top) of the flat segments
    first = True
    while x < w - 10:
        seg = rng.randint(6, 14) if not first else 10
        x1 = min(x + seg - 1, w - 1)
        gr.fill(x, top, x1, h - 1, '#')
        surfaces.append((x, x1, top))
        x = x1 + 1
        first = False
        if x >= w - 10:
            break
        new_top = max(h - 12, min(h - 3, top + rng.randint(-2, 2)))
        if rng.random() < 0.45:       # a bottomless pit; narrower when the far side is higher
            x += 3 if new_top < top else rng.randint(3, 4)
        top = new_top
    gr.fill(x, top, w - 1, h - 1, '#')                 # the last stretch, to the right edge
    surfaces.append((x, w - 1, top))
    gr.put(2, surfaces[0][2] - 1, 'P')
    next_checkpoint = 50
    for i, (x0, x1, t) in enumerate(surfaces[1:-1], start=1):
        width = x1 - x0 + 1
        mid = (x0 + x1) // 2
        if x0 >= next_checkpoint and width >= 6:
            gr.put(x0 + 1, t - 1, 'C')
            next_checkpoint = x0 + 50
            continue
        r = rng.random()
        if width >= 9 and r < 0.4:
            gr.put(mid, t - 1, 'w')
        elif width >= 7 and r < 0.6:
            gr.put(mid, t - 1, 'j')
        elif width >= 6 and r < 0.8:
            gr.fill(mid, t - rng.randint(1, 2), mid, t - 1, 'X')   # a block to jump over
        if x0 > surfaces[i - 1][1] + 1 and rng.random() < 0.5:     # a pit before this segment: a bird above it
            gr.put(x0 - 2, t - 5, 'f')
    x0, x1, t = surfaces[-1]
    gr.put(w - 4, t - 1, 'G')
    return gr.render()


def underground_level(seed, w=110, tunnels=5):
    """From the surface down through tunnels to the goal at the bottom: dirt, then rock with carved tunnels joined by
    drops, alternating directions."""
    h = 8 + 12 + tunnels * 12 + 4                      # deep enough for the deepest possible tunnel
    gr = Grid(w, h, seed)
    rng = gr.rng
    surface = 8
    gr.fill(0, surface, w - 1, surface + 2, '#')       # topsoil
    gr.fill(0, surface + 3, w - 1, h - 1, 'X')         # rock
    gr.put(2, surface - 1, 'P')
    gr.put(10, surface - 1, 'w')
    hole = 24
    gr.fill(hole, surface, hole + 2, surface + 6, '.')  # the way down
    floor = surface + 12                               # floor row of the first tunnel (the tile under the player)
    x = hole
    direction = 1
    for i in range(tunnels):
        height = 5 if i % 2 == 0 else 6
        length = rng.randint(40, 70)
        x_end = min(w - 4, x + length) if direction > 0 else max(3, x - length)
        a, b = sorted((x, x_end))
        gr.fill(a, floor - height, b + 2, floor - 1, '.')    # the tunnel (open rows above its floor)
        # the drop from above lands at x..x+2
        if i == 0:
            gr.fill(hole, surface + 3, hole + 2, floor - 1, '.')
        # inside: blocks to jump, enemies, a checkpoint near the start
        gr.put(x + (3 if direction > 0 else -1), floor - 1, 'C') if i > 0 else None
        for k in range(rng.randint(2, 4)):
            bx = rng.randint(a + 8, b - 6)
            r = rng.random()
            if r < 0.35:
                gr.fill(bx, floor - rng.randint(1, 2), bx + rng.randint(0, 1), floor - 1, 'X')
            elif r < 0.65:
                gr.put(bx, floor - 1, 'w')
            elif r < 0.85:
                gr.put(bx, floor - 1, 'j')
            else:
                gr.put(bx, floor - height + 2, 'f')
        if i == tunnels - 1:
            gr.put(x_end + (-2 if direction > 0 else 2), floor - 1, 'G')
            break
        # drop to the next tunnel at the far end
        next_floor = floor + rng.randint(9, 12)
        dx = x_end if direction > 0 else x_end
        gr.fill(dx, floor, dx + 2, next_floor - 1, '.')
        floor = next_floor
        x = dx
        direction = -direction
    return gr.render()


def chimney(gr, x_wall, ground_row, top_row):
    """A wall-jump chimney in front of a cliff whose face is at x_wall + 4 (solid from top_row down): a free-standing
    pillar at x_wall from top_row to ground_row - 2, so the player walks in under it (row ground_row - 1) and climbs
    between pillar and cliff."""
    gr.fill(x_wall, top_row, x_wall, ground_row - 2, 'X')


def hill_level(seed, w=150, h=70, cliffs=4):
    """Up a mountain: steps of 1-2 rows, cliffs climbed in wall-jump chimneys, a duck-jump step, then down the other
    side to the goal."""
    gr = Grid(w, h, seed)
    rng = gr.rng
    top = h - 6
    x = 0
    gr.put(2, top - 1, 'P')
    peak_x = int(w * 0.7)
    cliff_at = sorted(rng.sample(range(20, peak_x - 15, 6), cliffs))
    duck_at = rng.choice([c + 8 for c in cliff_at[:-1]])
    checkpoints = 0
    while x < peak_x:
        if cliff_at and x >= cliff_at[0]:
            cliff_at.pop(0)
            # flat run-up of 6, the chimney (3 inside), then the cliff 9-13 rows up
            height = rng.randint(9, 13)
            gr.fill(x, top, x + 5, h - 1, '#')
            gr.fill(x + 6, top, x + 8, h - 1, '#')                  # the chimney floor
            new_top = top - height
            chimney(gr, x + 5, top, new_top)
            gr.fill(x + 9, new_top, x + 15, h - 1, '#')             # the plateau
            gr.put(x + 2, top - 1, 'C') if checkpoints < 3 else None
            checkpoints += 1
            x += 16
            top = new_top
            continue
        if x >= duck_at > 0:
            duck_at = 0
            gr.fill(x, top, x + 5, h - 1, '#')
            gr.fill(x + 6, top - 5, x + 12, h - 1, '#')             # 5 rows up, right next to it
            x += 13
            top -= 5
            continue
        seg = rng.randint(4, 8)
        gr.fill(x, top, x + seg - 1, h - 1, '#')
        if seg >= 7 and rng.random() < 0.4:
            gr.put(x + seg // 2, top - 1, 'w' if rng.random() < 0.5 else 'j')
        x += seg
        top -= rng.randint(0, 2)
        top = max(top, 8)
    # the summit and the way down: steps down to the goal
    gr.fill(x, top, x + 7, h - 1, '#')
    gr.put(x + 3, top - 4, 'f')
    x += 8
    while x < w - 8:
        seg = rng.randint(4, 7)
        top = min(h - 6, top + rng.randint(1, 3))
        gr.fill(x, top, min(x + seg - 1, w - 1), h - 1, '#')
        x += seg
    gr.fill(x, top, w - 1, h - 1, '#')
    gr.put(w - 4, top - 1, 'G')
    return gr.render()


def pyramid_level():
    """A labyrinth in a pyramid. Climb its stepped face to the entrance; the entrance corridor leads to a hole (down to
    the lower passage and the subterranean chamber, a dead end) and to the ascending passage, which ends in the Grand
    Gallery: up its ledges to the King's chamber with the goal, or right to the Queen's chamber (a dead end)."""
    w, h = 124, 58
    gr = Grid(w, h, 10)
    ground = 50                                        # top row of the desert floor
    gr.fill(0, ground, w - 1, h - 1, '#')
    apex_x, apex_y = 62, 4
    for y in range(apex_y, ground):                    # stepped stone pyramid: one tile wider per row
        half = y - apex_y
        gr.fill(apex_x - half, y, apex_x + half, y, 'X')

    def corridor(x0, x1, floor, height=3):
        """Open the rows above `floor` (which stays solid) from x0 to x1."""
        gr.fill(x0, floor - height, x1, floor - 1, '.')

    gr.put(3, ground - 1, 'P')
    gr.put(10, ground - 1, 'w')
    # The entrance corridor, 6 rows above the ground, from the face inwards.
    ent = ground - 6                                   # its floor row
    face = apex_x - (ent - 1 - apex_y)                 # the face's x in the corridor's lowest open row
    corridor(face, 50, ent)
    gr.put(face + 2, ent - 1, 'C')
    gr.put(44, ent - 1, 'w')
    # A hole in its floor (x 34..36): a shaft down to the lower passage, then a drop into the subterranean chamber.
    low = ground - 1
    gr.fill(34, ent, 36, low - 1, '.')
    corridor(34, 60, low, 4)
    gr.put(48, low - 1, 'w')
    corridor(61, 72, low + 3, 4)                       # the subterranean chamber, 3 rows lower (below the desert)
    gr.put(68, low + 2, 'w')
    # The ascending passage: from the corridor's end, one row up every 3 tiles.
    x, floor = 51, ent - 1
    while floor > ent - 7:
        corridor(x, x + 2, floor, 4)
        x += 3
        floor -= 1
    # The Grand Gallery: 8 wide, tall, with ledges every 3 rows alternating left and right.
    g0, g1, gal_floor = x, x + 7, floor
    corridor(g0, g1, gal_floor, gal_floor - 21)       # open from row 21: the stone above stays 3+ tiles thick
    ledge_rows = list(range(gal_floor - 3, 24, -3))
    for i, ly in enumerate(ledge_rows):
        lx = g0 if (len(ledge_rows) - 1 - i) % 2 == 0 else g0 + 4   # the top ledge is on the left
        gr.fill(lx, ly, lx + 3, ly, 'X')
    gr.put(g0 + 6, gal_floor - 1, 'C')
    # The Queen's chamber: right from the gallery's foot, a dead end with a jumper.
    corridor(g1 + 1, g1 + 13, gal_floor)             # its end stays under 3+ tiles of the stepped face
    gr.put(g1 + 9, gal_floor - 1, 'j')
    # The King's chamber: left from the gallery's top ledge, with the goal.
    top = ledge_rows[-1] - 1                           # one row above the top ledge
    corridor(g0 - 13, g0 - 1, top, 4)
    gr.put(g0 - 7, top - 1, 'w')
    gr.put(g0 - 11, top - 1, 'G')
    return gr.render()


# Styles alternate, never more than two of one kind in a row (levels 1-5 are hand-made: ground, ground and
# climbing, long ground, tall climb, long and tall).
LEVELS = {
    6: lambda: underground_level(6),                                                     # underground
    7: lambda: level(9, [('run', 10, 3, 2, 0.7), ('chasm', 6), ('run', 10, 3, 2, 0.7)]),  # sky, long and busy
    8: lambda: ground_level(8),                                                          # ground, pits
    9: lambda: hill_level(9),                                                            # hill climb
    10: lambda: pyramid_level(),                                                         # pyramid labyrinth
    11: lambda: level(10, [('descend', 6), ('run', 5), ('descend', 6), ('chasm', 4), ('descend', 5)]),  # sky, down
    12: lambda: underground_level(12, w=130, tunnels=6),                                 # underground
    13: lambda: ground_level(13, w=230),                                                 # ground, pits
    14: lambda: hill_level(14, w=170, cliffs=5),                                         # hill climb
    15: lambda: level(15, [('run', 6), ('climb', 6), ('shaft', 12), ('chasm', 5), ('run', 6, 3, 2, 0.6),
                           ('duck_step',), ('climb', 6), ('descend', 5), ('chasm', 5), ('run', 6)]),  # sky finale
}

if __name__ == '__main__':
    out = Path(sys.argv[1])
    only = [int(a) for a in sys.argv[2:]] or LEVELS.keys()
    for n in only:
        (out / f'level{n}.txt').write_text(LEVELS[n]())
