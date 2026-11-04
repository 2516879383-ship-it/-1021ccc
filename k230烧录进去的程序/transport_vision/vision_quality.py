"""Pure, bounded fresh-frame quality checks; pixel stability is not grip proof."""
import math
import time
import vision_config as cfg


def hypot(x, y):
    # CanMV's MicroPython math module lacks hypot; image offsets are bounded.
    return math.sqrt(x*x + y*y)


def elapsed(now, before):
    diff = getattr(time, 'ticks_diff', None)
    return diff(now, before) if diff else now - before


def finite(value):
    return value == value and -1e12 < value < 1e12


def in_roi(item, roi):
    if roi is None:
        return True
    x, y, w, h = roi
    return x <= item[3] < x + w and y <= item[4] < y + h


def full_box(item):
    x, y, w, h = item[2]
    return x > 0 and y > 0 and x+w < cfg.AI_SIZE[0] and y+h < cfg.AI_SIZE[1]


def select(items, ident, roi=None):
    candidates = [item for item in items if item[0] == ident and in_roi(item, roi)]
    if not candidates:
        return None, 'NOT_FOUND'
    # Do not silently jump between same-colour objects or duplicate number boxes.
    if len(candidates) > 1:
        return None, 'AMBIGUOUS'
    return candidates[0], 'FOUND'


class Stability:
    def __init__(self):
        self.reset()

    def reset(self):
        self.samples = []
        self.last_id = None
        self.stable = False
        self.speed = None

    def update(self, item, frame_id, now):
        if self.last_id is not None:
            advance = (frame_id - self.last_id) & 65535
            if advance == 0:  # A repeated report is never a new sample.
                return self.stable
            if advance >= 32768:
                self.reset()
        if not item or not full_box(item):
            self.reset()
            self.last_id = frame_id
            return False
        if self.samples:
            previous = self.samples[-1]
            dt = elapsed(now, previous[0])
            if dt <= 0 or dt > cfg.STABLE_MAX_GAP_MS:
                self.reset()
            else:
                self.speed = hypot(item[3]-previous[1], item[4]-previous[2])*1000/dt
        self.last_id = frame_id
        self.samples.append((now, item[3], item[4], item[2][2], item[2][3]))
        self.samples = [s for s in self.samples if elapsed(now, s[0]) <= cfg.STABLE_WINDOW_MS]
        self.samples = self.samples[-cfg.STABLE_MAX_SAMPLES:]
        self.stable = False
        if len(self.samples) < cfg.STABLE_MIN_FRAMES:
            return False
        if elapsed(now, self.samples[0][0]) < cfg.STABLE_MIN_MS:
            return False
        xs, ys = [s[1] for s in self.samples], [s[2] for s in self.samples]
        widths, heights = [s[3] for s in self.samples], [s[4] for s in self.samples]
        self.stable = (hypot(max(xs)-min(xs), max(ys)-min(ys)) <= cfg.STABLE_CENTER_PX
                       and max(widths)/min(widths)-1 <= cfg.STABLE_SIZE_FRACTION
                       and max(heights)/min(heights)-1 <= cfg.STABLE_SIZE_FRACTION)
        return self.stable
