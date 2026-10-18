"""Generates levels 6-15 from "legs" (runs, climbs, descents, shafts, chasms): tools/levelgen.py assets/levels [N ...]

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


LEVELS = {
    6: (6, [('run', 8), ('run', 8), ('chasm', 5), ('run', 6)]),                          # long
    7: (7, [('climb', 8), ('shaft', 12), ('climb', 8), ('climb', 6, -1)]),               # tall
    8: (8, [('run', 6), ('climb', 6), ('run', 6), ('climb', 6), ('run', 5)]),            # long and tall
    9: (9, [('run', 10, 3, 2, 0.7), ('chasm', 6), ('run', 10, 3, 2, 0.7)]),              # long, busy
    10: (10, [('descend', 6), ('run', 5), ('descend', 6), ('chasm', 4), ('descend', 5)]),  # going down
    11: (11, [('climb', 7), ('run', 8), ('descend', 6), ('run', 6)]),                    # up, across, down
    12: (12, [('chasm', 8), ('run', 4), ('chasm', 8), ('run', 4), ('chasm', 6)]),        # a long chasm
    13: (13, [('shaft', 12), ('climb', 5), ('duck_step',), ('shaft', 14), ('climb', 6)]),  # tall, wall-jumps
    14: (14, [('run', 5), ('climb', 5), ('descend', 4), ('climb', 6), ('descend', 4), ('run', 5)]),  # waves
    15: (15, [('run', 6), ('climb', 6), ('shaft', 12), ('chasm', 5), ('run', 6, 3, 2, 0.6), ('duck_step',),
              ('climb', 6), ('descend', 5), ('chasm', 5), ('run', 6)]),                 # the finale
}

if __name__ == '__main__':
    out = Path(sys.argv[1])
    only = [int(a) for a in sys.argv[2:]] or LEVELS.keys()
    for n in only:
        seed, legs = LEVELS[n]
        (out / f'level{n}.txt').write_text(level(seed, legs))
