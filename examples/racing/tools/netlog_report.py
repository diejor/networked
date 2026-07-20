"""Correlate racing net_log CSVs: do corrections line up with clock events?

Reads the files written by examples/racing/scripts/net_log.gd (armed with
NETW_NETLOG=1) and answers the two questions the rubber-banding investigation
turns on:

  1. What is the period of the corrections? A tight distribution around one
     interval means a beat drives them, not accumulated noise.
  2. Do they coincide with starved/held/drained server ticks or with physics
     frames that ran zero or two ticks? Lift well above 1.0 means the timing
     event and the correction are the same story.

Usage:
    python netlog_report.py user://... path/to/netlog_client_7.csv [more.csv]

Stdlib only, no arguments beyond the file list.
"""

import csv
import math
import statistics
import sys
from collections import Counter, defaultdict

# Frames on either side of a correction that count as "coincident". At 60 Hz
# this is a 50 ms window, wide enough to cover the send/receive gap between a
# server-side timing event and the client correction it provokes.
WINDOW = 3

EVENT_COLUMNS = ("starved", "held", "drained", "missing", "resync", "skipped")


def load(path):
    """Splits one CSV into its FRAME, CORR, EVAL and CONTACT rows.

    The file interleaves several row shapes under their own header lines, so
    rows are dispatched on the leading kind column rather than a single
    DictReader. A header is recognised by a column unique to it, which keeps
    older captures readable as the schema grows.
    """
    out = {"FRAME": [], "CORR": [], "EVAL": [], "CONTACT": [], "FIELD": []}
    headers = {}
    for row in csv.reader(open(path, newline="")):
        if not row:
            continue
        if row[0] == "kind":
            if "ticks_this_frame" in row:
                headers["FRAME"] = row
            elif "magnitude" in row:
                headers["CORR"] = row
            elif "divergence" in row:
                headers["EVAL"] = row
            elif "error" in row:
                headers["FIELD"] = row
            else:
                headers["CONTACT"] = row
        elif row[0] in out and row[0] in headers:
            out[row[0]].append(dict(zip(headers[row[0]], row)))
    return out


def frame_index_by_tick(frames):
    """Maps a clock tick to the index of the last frame recorded at it."""
    index = {}
    for i, frame in enumerate(frames):
        index[int(frame["tick"])] = i
    return index


def lift(frames, corrections, predicate):
    """Returns (base_rate, coincident_rate, lift) for a frame predicate.

    base_rate is how often the predicate holds across all frames; coincident is
    how often it holds within WINDOW frames of a correction. Lift is their
    ratio, so 1.0 means the event says nothing about corrections and a large
    value means they travel together.
    """
    if not frames or not corrections:
        return 0.0, 0.0, 0.0
    flags = [predicate(f) for f in frames]
    base = sum(flags) / len(flags)

    index = frame_index_by_tick(frames)
    near = set()
    for corr in corrections:
        at = index.get(int(corr["tick"]))
        if at is None:
            continue
        for i in range(max(0, at - WINDOW), min(len(frames), at + WINDOW + 1)):
            near.add(i)
    if not near:
        return base, 0.0, 0.0
    coincident = sum(flags[i] for i in near) / len(near)
    return base, coincident, (coincident / base if base else float("inf"))


def divergence_summary(label, evals):
    """Prints the divergence distribution over a set of EVAL rows.

    A receive with no prior predicted state reports an infinite divergence, so
    those are counted separately rather than folded into the percentiles, which
    they would otherwise saturate.
    """
    if not evals:
        print(f"    {label:16s} (no state receives)")
        return
    values = [float(e["divergence"]) for e in evals]
    mags = sorted(v for v in values if math.isfinite(v))
    infinite = len(values) - len(mags)
    corrected = sum(1 for e in evals if e["corrected"] == "true")
    if not mags:
        print(f"    {label:16s} n={len(values):5d} all non-finite")
        return
    p = lambda q: mags[min(len(mags) - 1, int(q * len(mags)))]
    extra = f" non-finite={infinite}" if infinite else ""
    print(
        f"    {label:16s} n={len(mags):5d} median={p(0.5):6.3f}"
        f" p90={p(0.9):6.3f} p99={p(0.99):6.3f} max={mags[-1]:6.3f}"
        f" corrected={corrected}{extra}"
    )


def field_breakdown(fields):
    """Prints each state field's own divergence distribution.

    This is what separates a position fork from a velocity fork. A field that
    sits high here while corrections stay rare is diverging without ever
    triggering one, which is the signature of an excluded or teleport-only
    field accumulating error nothing ever restores.
    """
    if not fields:
        print("  per-field divergence: not recorded in this capture")
        return
    by_field = defaultdict(list)
    for row in fields:
        by_field[row["field"]].append(float(row["error"]))
    print("  per-field divergence:")
    ranked = sorted(
        by_field.items(),
        key=lambda kv: statistics.median([v for v in kv[1] if math.isfinite(v)] or [0]),
        reverse=True,
    )
    for field, raw in ranked:
        vals = sorted(v for v in raw if math.isfinite(v))
        if not vals:
            print(f"    {field:24s} all non-finite ({len(raw)})")
            continue
        p = lambda q: vals[min(len(vals) - 1, int(q * len(vals)))]
        print(
            f"    {field:24s} median={p(0.5):7.3f} p90={p(0.9):7.3f}"
            f" p99={p(0.99):7.3f} max={vals[-1]:8.3f}"
        )


def timeline(frames, fields, evals, corrections, contacts, bin_s=5):
    """Prints how divergence and corrections evolve over the run.

    An aggregate distribution averages a quiet opening stretch together with a
    bad later one and hides a ramp completely. A divergence that climbs bin over
    bin and never falls back is an error nothing restores, which reads very
    differently from one that spikes and recovers.
    """
    if not frames:
        return
    t0 = int(frames[0]["wall_ms"])
    step = bin_s * 1000

    def bucket(rows):
        out = defaultdict(list)
        for r in rows:
            out[(int(r["wall_ms"]) - t0) // step].append(r)
        return out

    field_bins = bucket(fields)
    corr_bins = bucket(corrections)
    contact_bins = bucket(contacts)
    eval_bins = bucket(evals)

    # Track the fields that actually move, so the table stays readable.
    medians = defaultdict(list)
    for row in fields:
        v = float(row["error"])
        if math.isfinite(v):
            medians[row["field"]].append(v)
    top = sorted(medians, key=lambda f: statistics.median(medians[f]), reverse=True)[:3]
    if not top:
        return

    last = max(
        [max(field_bins, default=0), max(corr_bins, default=0), max(contact_bins, default=0)]
    )
    print(f"  timeline ({bin_s}s bins), median divergence per field:")
    print(
        "    t(s)  contacts  corr  evals  "
        + "  ".join(f"{f[:18]:>18s}" for f in top)
    )
    for b in range(last + 1):
        cells = []
        for f in top:
            vals = [
                float(r["error"])
                for r in field_bins.get(b, [])
                if r["field"] == f and math.isfinite(float(r["error"]))
            ]
            cells.append(f"{statistics.median(vals):18.3f}" if vals else f"{'-':>18s}")
        print(
            f"    {b * bin_s:4d}  {len(contact_bins.get(b, [])):8d}"
            f"  {len(corr_bins.get(b, [])):4d}  {len(eval_bins.get(b, [])):5d}  "
            + "  ".join(cells)
        )


def contact_split(frames, evals, contacts):
    """Compares divergence before and after the first contact.

    The question this answers is whether a collision changes the steady state
    rather than just producing one transient: if post-contact divergence sits
    higher for the rest of the run, the contact moved the system into a worse
    regime and never left it.
    """
    if not contacts:
        print("  contacts: none recorded")
        return
    times = [int(c["wall_ms"]) for c in contacts]
    t0 = int(frames[0]["wall_ms"])
    print(
        f"  contacts: {len(contacts)}, first at t={(times[0] - t0) / 1000:.1f}s,"
        f" last at t={(times[-1] - t0) / 1000:.1f}s"
    )
    first = times[0]
    print("  divergence split on first contact:")
    divergence_summary("before", [e for e in evals if int(e["wall_ms"]) < first])
    divergence_summary("after", [e for e in evals if int(e["wall_ms"]) >= first])


def report(path):
    data = load(path)
    frames = data["FRAME"]
    corrections = data["CORR"]
    evals = data["EVAL"]
    contacts = data["CONTACT"]
    print(f"\n=== {path} ===")
    if not frames:
        print("  no FRAME rows")
        return

    span_ms = int(frames[-1]["wall_ms"]) - int(frames[0]["wall_ms"])
    print(f"  {len(frames)} frames over {span_ms / 1000:.1f}s")

    ticks = Counter(int(f["ticks_this_frame"]) for f in frames)
    total = sum(ticks.values())
    detail = ", ".join(
        f"{n} tick(s): {c} ({100 * c / total:.1f}%)" for n, c in sorted(ticks.items())
    )
    print(f"  physics frames by ticks run -- {detail}")
    off_beat = sum(c for n, c in ticks.items() if n != 1)
    print(
        f"  frames not running exactly one tick: {off_beat}"
        f" ({100 * off_beat / total:.1f}%)"
    )

    # A capture predating a counter simply lacks its column, so every read
    # defaults rather than failing the whole report.
    totals = {col: sum(int(f.get(col, 0)) for f in frames) for col in EVENT_COLUMNS}
    print("  consume events -- " + ", ".join(f"{k}={v}" for k, v in totals.items()))

    if evals:
        print("  divergence over all state receives:")
        divergence_summary("all", evals)
    field_breakdown(data["FIELD"])
    timeline(frames, data["FIELD"], evals, corrections, contacts)
    contact_split(frames, evals, contacts)

    # A reconcile that fires but moves nothing emits no pose change, so the two
    # counts differ legitimately. A large gap means corrections are being
    # triggered and then applying no visible change, which is worth knowing
    # before reading anything into a low correction count.
    fired = sum(1 for e in evals if e["corrected"] == "true")
    if fired and not corrections:
        print(
            f"  NOTE: {fired} state receives reported corrected=true but no pose"
            " change was emitted -- corrections are firing and moving nothing"
        )

    if not corrections:
        print(f"  corrections emitting a pose change: 0 (of {fired} fired)")
        return

    by_field = defaultdict(list)
    for corr in corrections:
        by_field[corr["field"]].append(float(corr["magnitude"]))
    teleports = sum(1 for c in corrections if c["teleported"] == "true")
    print(f"  corrections: {len(corrections)} rows, {teleports} teleport-tier")
    for field, mags in sorted(by_field.items()):
        print(
            f"    {field}: n={len(mags)} median={statistics.median(mags):.3f}"
            f" max={max(mags):.3f}"
        )

    # Period between distinct correction events, in wall time. Several field
    # rows share one event, so collapse on the tick they were emitted at.
    event_ms = sorted({int(c["tick"]): int(c["wall_ms"]) for c in corrections}.values())
    if len(event_ms) > 2:
        gaps = [b - a for a, b in zip(event_ms, event_ms[1:])]
        print(
            f"  correction period: median={statistics.median(gaps):.0f}ms"
            f" mean={statistics.mean(gaps):.0f}ms"
            f" stdev={statistics.pstdev(gaps):.0f}ms n={len(gaps)}"
        )
        buckets = Counter(min(int(g // 250) * 250, 3000) for g in gaps)
        rows = ", ".join(f"{b}-{b + 250}ms: {c}" for b, c in sorted(buckets.items()))
        print(f"    histogram -- {rows}")

    print(f"  coincidence with corrections (+/-{WINDOW} frames):")
    checks = [(col, (lambda f, c=col: int(f.get(c, 0)) > 0)) for col in EVENT_COLUMNS]
    checks.append(("multi/zero-tick frame", lambda f: int(f["ticks_this_frame"]) != 1))
    if any("contacts" in f for f in frames[:1]):
        checks.append(("contact", lambda f: int(f.get("contacts", 0)) > 0))
        checks.append(("airborne", lambda f: f.get("grounded", "1") == "0"))
    for label, predicate in checks:
        base, near, ratio = lift(frames, corrections, predicate)
        if base == 0:
            print(f"    {label:22s} never occurred")
        else:
            print(
                f"    {label:22s} base={100 * base:5.1f}%"
                f" near-correction={100 * near:5.1f}%  lift={ratio:.2f}x"
            )


def main():
    paths = sys.argv[1:]
    if not paths:
        print(__doc__)
        return 1
    for path in paths:
        report(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
