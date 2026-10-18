"""Generates assets/music/*.txt (all but the minuet) from compact note lists: tools/songgen.py assets/music

Each voice is a list of (note, rows) pairs; note None = hold silence ('...' after a release), '-' = release.
"""
import sys
from pathlib import Path

OUT = Path(sys.argv[1])

def expand(voice, total):
    cells = []
    for note, rows in voice:
        cells.append('---' if note == '-' else note)
        cells += ['...'] * (rows - 1)
    assert len(cells) == total, (len(cells), total)
    return cells

def drums(bars, rows_per_bar, kick_rows, hat_rows):
    k, h = [], []
    for _ in range(bars):
        for r in range(rows_per_bar):
            k.append('x' if r in kick_rows else '.')
            h.append('x' if r in hat_rows else '.')
    return k, h

def song(name, header, instruments, voices, rows_per_bar, bar_comment=True):
    total = len(voices[0][1])
    lines = [header.strip(), '']
    lines += instruments
    lines += ['', 'channels ' + ' '.join(v[0] for v in voices)]
    for i in range(total):
        if bar_comment and i % rows_per_bar == 0:
            lines.append(f'# bar {i // rows_per_bar + 1}')
        lines.append(' '.join(v[1][i] for v in voices))
    (OUT / name).write_text('\n'.join(lines) + '\n')

LEAD = 'instrument lead square duty 25 attack 3 decay 120 sustain 55 release 60 volume 55'
SOFT = 'instrument lead square duty 50 attack 4 decay 160 sustain 45 release 90 volume 45'
BASS = 'instrument bass triangle attack 3 decay 200 sustain 70 release 80 volume 80'
KICK = 'instrument kick noise attack 1 decay 70 sustain 0 volume 45 note C-3'
HAT = 'instrument hat noise attack 1 decay 25 sustain 0 volume 20 note C-7'

def q(n, r=2): return (n, r)  # quarter note when a row is an eighth

# --- Eine kleine Nachtmusik (Mozart, K. 525, 1st movement), G major 4/4, opening bars 1-4, one row per eighth.
nacht = [('G-5', 2), ('-', 1), ('D-5', 1), ('G-5', 2), ('-', 1), ('D-5', 1),
         ('G-5', 1), ('D-5', 1), ('G-5', 1), ('B-5', 1), ('D-6', 4),
         ('C-6', 2), ('-', 1), ('A-5', 1), ('C-6', 2), ('-', 1), ('A-5', 1),
         ('C-6', 1), ('A-5', 1), ('F#5', 1), ('A-5', 1), ('D-5', 4)]
nacht_bass = [('G-3', 2), ('-', 2), ('G-3', 2), ('-', 2), ('G-3', 1), ('D-3', 1), ('G-3', 1), ('B-3', 1), ('G-3', 4),
              ('D-3', 2), ('-', 2), ('D-3', 2), ('-', 2), ('D-3', 1), ('F#3', 1), ('A-3', 1), ('F#3', 1), ('D-3', 4)]
k, h = drums(8, 8, {0, 4}, {2, 6})
song('eine_kleine.txt', '''
# Eine kleine Nachtmusik: Mozart, Serenade No. 13 in G major, K. 525, 1st movement, the opening bars 1-4, played
# twice (the second time the bass doubles the theme's rhythm). 4/4, one row per eighth note.

tempo 132
rows_per_beat 2
loop yes
''', [LEAD, BASS, KICK, HAT],
     [('lead', expand(nacht + nacht, 64)), ('bass', expand(nacht_bass + nacht_bass, 64)), ('kick', k), ('hat', h)], 8)

# --- Canon in D (Pachelbel): the ground bass in quarters, the first violin's opening in halves, then in quarters.
ground = [q('D-3'), q('A-2'), q('B-2'), q('F#2'), q('G-2'), q('D-2'), q('G-2'), q('A-2')]
canon_lead = ([('-', 16)] + [('F#5', 4), ('E-5', 4), ('D-5', 4), ('C#5', 4), ('B-4', 4), ('A-4', 4), ('B-4', 4),
                             ('C#5', 4)] +
              [q('D-5'), q('C#5'), q('B-4'), q('A-4'), q('G-4'), q('F#4'), q('G-4'), q('E-4'),
               q('D-4'), q('F#4'), q('A-4'), q('G-4'), q('F#4'), q('D-4'), q('F#4'), q('E-4')])
k, h = drums(10, 8, {0}, {4})
song('canon.txt', '''
# Canon in D: Johann Pachelbel, the ground bass (8 notes over 2 bars) under the first violin's entries: alone, then
# half notes, then quarter notes. 4/4, one row per eighth note.

tempo 84
rows_per_beat 2
loop yes
''', [SOFT, BASS, KICK, HAT], [('lead', expand(canon_lead, 80)), ('bass', expand(ground * 5, 80)), ('kick', k),
                               ('hat', h)], 8)

# --- Jesu, Joy of Man's Desiring (Bach, cantata BWV 147, chorale "Jesus bleibet meine Freude"): the opening of the
# triplet ritornello, G major 9/8, one row per (triplet) eighth.
jesu = ([('G-4', 1), ('A-4', 1), ('B-4', 1), ('D-5', 1), ('C-5', 1), ('C-5', 1), ('E-5', 1), ('D-5', 1), ('D-5', 1)] +
        [('G-5', 1), ('F#5', 1), ('G-5', 1), ('D-5', 1), ('B-4', 1), ('G-4', 1), ('A-4', 1), ('B-4', 1), ('C-5', 1)] +
        [('D-5', 1), ('E-5', 1), ('D-5', 1), ('C-5', 1), ('B-4', 1), ('A-4', 1), ('B-4', 1), ('G-4', 1), ('A-4', 1)] +
        [('F#4', 1), ('G-4', 1), ('A-4', 1), ('D-4', 1), ('F#4', 1), ('A-4', 1), ('C-5', 1), ('B-4', 1), ('A-4', 1)] +
        [('G-4', 6), ('-', 3)])
jesu_bass = ([('G-2', 3), ('B-2', 3), ('C-3', 3)] + [('B-2', 3), ('G-2', 3), ('F#2', 3)] +
             [('G-2', 3), ('A-2', 3), ('G-2', 3)] + [('D-3', 3), ('D-3', 3), ('D-3', 3)] + [('G-2', 6), ('-', 3)])
k, h = drums(5, 9, {0}, {3, 6})
song('jesu_joy.txt', '''
# Jesu, Joy of Man's Desiring: Bach, cantata "Herz und Mund und Tat und Leben", BWV 147, chorale "Jesus bleibet meine
# Freude": the opening of the triplet ritornello, closing on G. Transcribed from memory. 9/8, one row per eighth.

tempo 72
rows_per_beat 3
loop yes
''', [SOFT, BASS, KICK, HAT], [('lead', expand(jesu, 45)), ('bass', expand(jesu_bass, 45)), ('kick', k), ('hat', h)], 9)

# --- Prelude in C major (Bach, Well-Tempered Clavier I, BWV 846), bars 1-8: per bar the bass note, the held second
# note and the arpeggio of the upper three, twice. 4/4, one row per sixteenth.
chords = [('C-4', 'E-4', 'G-4', 'C-5', 'E-5'), ('C-4', 'D-4', 'A-4', 'D-5', 'F-5'), ('B-3', 'D-4', 'G-4', 'D-5', 'F-5'),
          ('C-4', 'E-4', 'G-4', 'C-5', 'E-5'), ('C-4', 'E-4', 'A-4', 'E-5', 'A-5'), ('C-4', 'D-4', 'F#4', 'A-4', 'D-5'),
          ('B-3', 'D-4', 'G-4', 'D-5', 'G-5'), ('B-3', 'C-4', 'E-4', 'G-4', 'C-5')]
low, mid, high = [], [], []
for a, b, c, d, e in chords:
    for _ in range(2):
        low += [(a, 8)]
        mid += [('-', 1), (b, 7)]
        high += [('-', 2), (c, 1), (d, 1), (e, 1), (c, 1), (d, 1), (e, 1)]
none = ['.'] * 128
song('prelude_c.txt', '''
# Prelude in C major: Bach, The Well-Tempered Clavier, Book I, BWV 846, bars 1-8 (all notes as written: the bass,
# the held second note and the broken chord above). 4/4, one row per sixteenth note; no drums.

tempo 70
rows_per_beat 4
loop yes
''', [LEAD.replace('lead', 'high'), BASS.replace('bass', 'low'), SOFT.replace('lead', 'mid')],
     [('low', expand(low, 128)), ('mid', expand(mid, 128)), ('high', expand(high, 128))], 16)

# --- Toccata in D minor (Bach, BWV 565): the opening flourish (A with a mordent, a pause, the run down to C# and D),
# three octaves down, then a D minor chord. Free rhythm, one row per sixteenth.
ORGAN = 'instrument organ square duty 50 attack 2 decay 300 sustain 80 release 150 volume 50'
def flourish(octave):
    o = str(octave)
    return [('A-' + o, 1), ('G-' + o, 1), ('A-' + o, 6), ('-', 4),
            ('G-' + o, 1), ('F-' + o, 1), ('E-' + o, 1), ('D-' + o, 1), ('C#' + o, 6), ('D-' + o, 8), ('-', 4)]
organ = flourish(5) + flourish(4) + flourish(3) + [('D-3', 4), ('F-3', 4), ('A-3', 4), ('D-4', 20), ('-', 4)]
pedal = [('-', 3 * 34)] + [('D-2', 32), ('-', 4)]
total = 3 * 34 + 36
song('toccata.txt', '''
# Toccata in D minor: Bach, Toccata and Fugue BWV 565, the opening flourish (A with a mordent, a pause, the run down
# to C# and D) in three octaves, then a D minor chord over the pedal D. One row per sixteenth note; no drums.

tempo 80
rows_per_beat 4
loop yes
''', [ORGAN, BASS.replace('bass', 'pedal')], [('organ', expand(organ, total)), ('pedal', expand(pedal, total))], 16)
