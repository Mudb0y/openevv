#!/usr/bin/env python3
"""What a sample rate does to the sound, in numbers rather than opinions.

The engine will now synthesise at rates IBM never shipped, and the question
that decides whether that is worth anything is not whether it makes a noise.
It is whether the formants are still where they were -- the resonator tables
encode a frequency as a fraction of the sample rate, so a table built for the
wrong rate puts every formant in the wrong place while the speech stays
perfectly fluent and merely sounds like somebody else.

So this reads the same sentence spoken at several rates and puts the two
things that matter beside each other. Where the peaks of the long-term
average spectrum are, which must not move; and how much energy sits above
5,512 hertz, which is what a rate above eleven thousand and twenty five can
carry and eleven thousand cannot, and is therefore the whole of what there is
to listen for.

Pure Python and no numpy, as tools/pitch.py is and for the same reason: the
dev shell has neither.

    tools/rate-compare.py a.wav b.wav ...
"""

import cmath
import math
import struct
import sys

# One FFT to a window, hann-windowed, half-overlapped, averaged over the whole
# file. Long enough to separate two formants and short enough that a moving
# one does not smear across the whole band.
N = 2048

# What eleven thousand and twenty five can represent, and therefore the line
# above which everything is new.
OLD_NYQUIST = 11025.0 / 2.0


def read_wav(path):
    with open(path, "rb") as fh:
        blob = fh.read()

    if blob[0:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("%s is not a wave file" % path)

    at = 12
    rate = None
    data = None
    while at + 8 <= len(blob):
        tag = blob[at:at + 4]
        size = struct.unpack("<I", blob[at + 4:at + 8])[0]
        body = blob[at + 8:at + 8 + size]
        if tag == b"fmt ":
            fields = struct.unpack("<HHIIHH", body[:16])
            if fields[0] != 1 or fields[1] != 1 or fields[5] != 16:
                raise ValueError("%s is not sixteen bit mono pcm" % path)
            rate = fields[2]
        elif tag == b"data":
            data = body
        at += 8 + size + (size & 1)

    if rate is None or data is None:
        raise ValueError("%s has no format or no samples" % path)

    n = len(data) // 2
    return rate, list(struct.unpack("<%dh" % n, data[:n * 2]))


def fft(a):
    """Iterative radix-2, in place. len(a) must be a power of two."""
    n = len(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]

    length = 2
    while length <= n:
        step = -2j * math.pi / length
        w = cmath.exp(step)
        for i in range(0, n, length):
            cur = 1.0 + 0j
            for k in range(i, i + length // 2):
                u = a[k]
                v = a[k + length // 2] * cur
                a[k] = u + v
                a[k + length // 2] = u - v
                cur *= w
        length <<= 1
    return a


def spectrum(samples, rate):
    """The long-term average spectrum, as (hertz, power) pairs."""
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (N - 1))
              for i in range(N)]
    acc = [0.0] * (N // 2)
    frames = 0

    for start in range(0, len(samples) - N, N // 2):
        chunk = [complex(samples[start + i] * window[i], 0.0)
                 for i in range(N)]
        # Silence contributes nothing but dilutes everything else.
        if sum(abs(c.real) for c in chunk) < N * 8:
            continue
        fft(chunk)
        for i in range(N // 2):
            acc[i] += abs(chunk[i]) ** 2
        frames += 1

    if frames == 0:
        raise ValueError("nothing loud enough to measure")

    return [(i * rate / float(N), acc[i] / frames) for i in range(N // 2)]


def smooth(spec, span_hz):
    """Average over a span given in hertz, so that files at different rates
    are smoothed by the same amount of spectrum rather than the same number
    of bins.

    The span has to be wider than the spacing of the voice's own harmonics or
    what comes out is the harmonic series and not the envelope -- which is the
    difference between measuring the pitch and measuring the formants."""
    if len(spec) < 2:
        return list(spec)

    bin_hz = spec[1][0] - spec[0][0]
    span = max(1, int(round(span_hz / bin_hz / 2.0)))
    out = []
    for i in range(len(spec)):
        lo = max(0, i - span)
        hi = min(len(spec), i + span + 1)
        out.append((spec[i][0],
                    sum(p for _, p in spec[lo:hi]) / float(hi - lo)))
    return out


# Wider than any voice's harmonic spacing and narrower than the distance
# between two formants.
ENVELOPE_HZ = 400.0


def peaks(spec, below, count):
    """The strongest local maxima under a ceiling, lowest first.

    The first formant sits above three hundred hertz in every voice, and
    below that is the fundamental and its neighbours, which is not what this
    is looking at."""
    found = []
    for i in range(1, len(spec) - 1):
        hz, p = spec[i]
        if hz > below:
            break
        if hz < 300.0:
            continue
        if p >= spec[i - 1][1] and p > spec[i + 1][1]:
            found.append((p, hz))
    found.sort(reverse=True)
    return sorted(hz for _, hz in found[:count])


def db(x, ref):
    if x <= 0.0 or ref <= 0.0:
        return float("-inf")
    return 10.0 * math.log10(x / ref)


def band_energy(spec, lo, hi):
    return sum(p for hz, p in spec if lo <= hz < hi)


# The bands the report is given in. The last two are what only a rate above
# eleven thousand can carry at all.
BANDS = [(0, 500), (500, 1500), (1500, 3000), (3000, 5512),
         (5512, 11000), (11000, 24000)]


def envelope_at(spec, hz):
    """The smoothed power at a frequency, by linear interpolation."""
    if hz <= spec[0][0]:
        return spec[0][1]
    for i in range(1, len(spec)):
        if spec[i][0] >= hz:
            lo_hz, lo_p = spec[i - 1]
            hi_hz, hi_p = spec[i]
            if hi_hz == lo_hz:
                return lo_p
            f = (hz - lo_hz) / (hi_hz - lo_hz)
            return lo_p + f * (hi_p - lo_p)
    return spec[-1][1]


def mirror(spec, nyquist, top):
    """What sits above a rate's own Nyquist, held against its own reflection.

    Raising a rate can only repeat what the source already carried: a
    component at g hertz comes back at Fs minus g, so the band above Nyquist
    is the band below it reflected, and how far down says which resampler
    left it there. Reading the difference at a run of frequencies gives that
    resampler's stopband as a curve, which is a fingerprint: holding barely
    falls, a curve through four samples falls steeply, a windowed sinc puts
    the whole thing sixty decibels down.

    It is deliberately not a verdict. The reflection is loudest where the
    speech is quietest and quietest where the speech is loud, so a sloping
    difference is what a plain resampler gives and says nothing on its own.
    What the curve is for is putting one recording beside another: two files
    made the same way have the same curve, and a recording whose curve fits
    none of them was not made that way.
    """
    rows = []
    hz = nyquist * 1.05
    while hz < min(top, nyquist * 1.95):
        here = envelope_at(spec, hz)
        there = envelope_at(spec, 2.0 * nyquist - hz)
        rows.append((hz, db(here, there) if there > 0 else float("-inf")))
        hz += nyquist * 0.1
    return rows


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2

    rows = []
    for path in argv[1:]:
        rate, samples = read_wav(path)
        raw = spectrum(samples, rate)
        spec = smooth(raw, ENVELOPE_HZ)
        # Everything is stated against the energy in the band every rate has,
        # so that a file with more spectrum in it is not thereby quieter
        # everywhere.
        ref = band_energy(spec, 0.0, OLD_NYQUIST)
        rows.append({
            "path": path,
            "rate": rate,
            "seconds": len(samples) / float(rate),
            "spec": spec,
            "ref": ref,
            "peaks": peaks(spec, 4000.0, 4),
        })

    for r in rows:
        print("%s at %d hertz, %.2f seconds" % (r["path"], r["rate"],
                                                r["seconds"]))
        print("  formant peaks: %s"
              % ", ".join("%.0f" % hz for hz in r["peaks"]))
        parts = []
        for lo, hi in BANDS:
            if lo >= r["rate"] / 2.0:
                continue
            top = min(hi, r["rate"] / 2.0)
            level = db(band_energy(r["spec"], lo, top), r["ref"])
            if level == float("-inf"):
                parts.append("%d to %d: silent" % (lo, top))
            else:
                parts.append("%d to %d: %.1f dB" % (lo, top, level))
        print("  bands, against the energy below %.0f hertz" % OLD_NYQUIST)
        for part in parts:
            print("    " + part)

    # And whether the top of each is a mirror of its own speech band, which
    # is what says the energy up there came from resampling rather than from
    # something else the audio passed through.
    for r in rows:
        if r["rate"] <= 11025:
            continue
        told = mirror(r["spec"], OLD_NYQUIST, r["rate"] / 2.0)
        if not told:
            continue
        levels = [d for _, d in told if d != float("-inf")]
        if not levels:
            continue
        print("")
        print("%s above %.0f hertz, against its own reflection:"
              % (r["path"], OLD_NYQUIST))
        for hz, d in told:
            print("  %5.0f hertz: %6.1f dB" % (hz, d))
        # A reflection cannot be louder than what it reflects, so anything at
        # or above nought is content the source never had.
        louder = [hz for hz, d in told if d > -1.0]
        if louder:
            print("  louder than its own reflection at %s hertz, so something"
                  " up there did not come from the speech below it"
                  % ", ".join("%.0f" % hz for hz in louder))

    if len(rows) < 2:
        return 0

    # The point of the whole exercise. A resonator table built for the wrong
    # rate leaves the speech fluent and moves every formant, so a peak that
    # has walked is the failure this catches; and the envelope held against
    # the first file over the band they share says whether the timbre is the
    # same voice or a different one.
    base = rows[0]
    print("")
    print("against %s:" % base["path"])
    for r in rows[1:]:
        # Each peak against the nearest one in the first file, not against the
        # one in the same position. Two files need not find the same number of
        # peaks, and where one of them finds an extra the rest of the list
        # shifts along and every later peak reads as having moved when none of
        # them has.
        if r["peaks"] and base["peaks"]:
            worst = max(min(abs(hz - other) for other in base["peaks"])
                        for hz in r["peaks"])
            print("  %s: furthest a formant peak is from the nearest of %s's,"
                  " %.0f hertz" % (r["path"], base["path"], worst))
        else:
            print("  %s: no peaks to compare" % r["path"])

        worst_db = 0.0
        worst_hz = 0.0
        hz = 300.0
        while hz < OLD_NYQUIST:
            a = envelope_at(base["spec"], hz) / base["ref"]
            b = envelope_at(r["spec"], hz) / r["ref"]
            d = abs(db(b, a)) if a > 0 and b > 0 else 0.0
            if d > worst_db:
                worst_db, worst_hz = d, hz
            hz += 25.0
        print("    envelope over the shared band: no more than %.1f dB "
              "apart, worst at %.0f hertz" % (worst_db, worst_hz))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
