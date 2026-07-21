"""Correlate racing net_log CSVs: do corrections line up with clock events?

Reads the files written by examples/racing/scripts/net_log.gd (armed with
NETW_NETLOG=1) and answers the two questions the rubber-banding investigation
turns on:

  1. What is the period of the corrections? A tight distribution around one
     interval means a beat drives them, not accumulated noise.
  2. Do they coincide with starved/held server ticks or with physics
     frames that ran zero or two ticks? Lift well above 1.0 means the timing
     event and the correction are the same story.

Usage:
    python netlog_report.py user://... path/to/netlog_client_7.csv [more.csv]

Stdlib only, no arguments beyond the file list.
"""

import csv
import math
import re
import statistics
import sys
from collections import Counter, defaultdict

# Frames on either side of a correction that count as "coincident". At 60 Hz
# this is a 50 ms window, wide enough to cover the send/receive gap between a
# server-side timing event and the client correction it provokes.
WINDOW = 3

EVENT_COLUMNS = (
    "starved",
    "held",
    "folded",
    "missing",
    "resync",
    "skipped",
)

DRIVE_KINDS = {
    0: "NONE",
    1: "FRESH",
    2: "REPEAT",
    3: "HOLD",
    4: "STARVED",
    5: "FOLD_DRIVE",
    6: "MISSING",
}
FRESH_KINDS = {"FRESH", "FOLD_DRIVE", "MISSING"}
INSERT_KINDS = {"REPEAT", "HOLD", "STARVED"}
DIVERGENCE_STEP = 0.05


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


def drive_kind(row):
    """Returns a readable drive kind from either text or enum-valued logs."""
    raw = row.get("drive_kind", "NONE")
    try:
        return DRIVE_KINDS.get(int(raw), f"UNKNOWN({raw})")
    except ValueError:
        return raw.upper()


def drive_stream(frames, path):
    """Extracts one row per drive and enforces the FRAME ordering contract."""
    if not frames or "drive_seq" not in frames[0]:
        return []
    stream = []
    previous = 0
    for frame_index, row in enumerate(frames):
        sequence = int(row["drive_seq"])
        drives = sequence - previous
        if drives not in (0, 1):
            raise ValueError(
                f"{path}: drive_seq changed by {drives} at FRAME row "
                f"{frame_index}; FRAME requires zero or one drive per row"
            )
        if drives == 1:
            entry = dict(row)
            entry["sequence"] = sequence
            entry["label"] = int(row["drive_label"])
            entry["kind_name"] = drive_kind(row)
            stream.append(entry)
        previous = sequence
    return stream


def fresh_indices(stream):
    """Returns stream indices that advance the fresh-label boundary."""
    return [i for i, row in enumerate(stream) if row["kind_name"] in FRESH_KINDS]


def tape_events(stream, peer, replayed_tape=False):
    """Extracts insertion and deletion events with fresh-label brackets."""
    fresh = fresh_indices(stream)
    previous_fresh = {}
    next_fresh = {}
    latest = None
    for i, row in enumerate(stream):
        previous_fresh[i] = latest
        if row["kind_name"] in FRESH_KINDS:
            latest = row["label"]
    latest = None
    for i in range(len(stream) - 1, -1, -1):
        next_fresh[i] = latest
        if stream[i]["kind_name"] in FRESH_KINDS:
            latest = stream[i]["label"]

    events = []
    for i, row in enumerate(stream):
        kind = row["kind_name"]
        if kind in INSERT_KINDS:
            events.append(
                {
                    "peer": peer,
                    "type": "insert",
                    "kind": kind,
                    "label": row["label"],
                    "left": previous_fresh[i],
                    "right": next_fresh[i],
                }
            )

    for before_index, after_index in zip(fresh, fresh[1:]):
        before = stream[before_index]["label"]
        after = stream[after_index]["label"]
        if peer == "client" or replayed_tape:
            deleted = range(before + 1, after)
        else:
            count = int(stream[after_index].get("folded", 0))
            deleted = range(after - count, after)
        for label in deleted:
            events.append(
                {
                    "peer": peer,
                    "type": "delete",
                    "kind": "LABEL_GAP" if peer == "client" else "FOLD_DRIVE",
                    "label": label,
                    "left": before,
                    "right": after,
                }
            )
    return events


def event_key(event):
    """Returns the mirror identity required by the tape experiment."""
    return event["type"], event["left"], event["right"]


def match_events(client_events, server_events):
    """Mirror-matches event units between identical fresh-label brackets."""
    client_counts = Counter(event_key(event) for event in client_events)
    server_counts = Counter(event_key(event) for event in server_events)
    matched = client_counts & server_counts
    remaining = {"client": matched.copy(), "server": matched.copy()}
    unmirrored = []
    for peer, events in (("client", client_events), ("server", server_events)):
        for event in events:
            key = event_key(event)
            if remaining[peer][key] > 0:
                remaining[peer][key] -= 1
            else:
                unmirrored.append(event)
    return sum(matched.values()), unmirrored


def common_label_range(client_stream, server_stream):
    """Returns the inclusive label range both tapes can fairly compare."""
    client = [row["label"] for row in client_stream if row["kind_name"] in FRESH_KINDS]
    server = [row["label"] for row in server_stream if row["kind_name"] in FRESH_KINDS]
    if not client or not server:
        return None
    first = max(min(client), min(server))
    last = min(max(client), max(server))
    return (first, last) if first <= last else None


def solve_offsets(client_stream, server_stream, label_range):
    """Returns cumulative client-minus-server drive counts per label boundary."""
    first, last = label_range
    client_counts = Counter(
        row["label"] for row in client_stream if first <= row["label"] <= last
    )
    server_counts = Counter(
        row["label"] for row in server_stream if first <= row["label"] <= last
    )
    offset = 0
    offsets = []
    for label in range(first, last + 1):
        offset += client_counts[label] - server_counts[label]
        offsets.append((label, offset))
    return offsets


def command_alignment(client_stream, server_stream):
    """Reports entry-aligned command and fresh-payload completeness."""
    client_by_label = {
        row["label"]: row
        for row in client_stream
        if row["kind_name"] in FRESH_KINDS
    }
    server_by_label = {
        row["label"]: row
        for row in server_stream
        if row["kind_name"] in FRESH_KINDS
    }
    labels = sorted(client_by_label.keys() & server_by_label.keys())
    mismatches = []
    incomplete = []
    for label in labels:
        client = client_by_label[label]
        server = server_by_label[label]
        if server["kind_name"] == "MISSING":
            incomplete.append((client, server))
        client_command = (
            client.get("steer", ""),
            client.get("throttle", ""),
        )
        server_command = (
            server.get("steer", ""),
            server.get("throttle", ""),
        )
        if client_command != server_command:
            mismatches.append((client, server))

    print(
        f"  commands: common fresh labels={len(labels)} "
        f"mismatched={len(mismatches)} "
        f"incomplete_fresh={len(incomplete)}"
    )
    for client, server in mismatches[:20]:
        print(
            f"    entry={client['sequence'] - 1} label={client['label']} "
            f"client=({client.get('steer')},{client.get('throttle')}) "
            f"server=({server.get('steer')},{server.get('throttle')}) "
            f"server_kind={server['kind_name']}"
        )
    if len(mismatches) > 20:
        print(f"    ... {len(mismatches) - 20} more command mismatches")
    unchanged_missing = len(incomplete) - sum(
        1
        for client, server in incomplete
        if (
            client.get("steer") != server.get("steer")
            or client.get("throttle") != server.get("throttle")
        )
    )
    if incomplete:
        missing_labels = ",".join(
            str(client["label"]) for client, _ in incomplete
        )
        print(
            f"    incomplete fresh labels: {missing_labels} "
            f"({unchanged_missing} repeated an unchanged command)"
        )


def divergence_steps(data, label_range, ack_labels=None):
    """Finds position-divergence jumps and maps them to reconcile ack labels."""
    evaluations = {
        (row["wall_ms"], row["tick"]): row
        for row in data["EVAL"]
    }
    positions = []
    for row in data["FIELD"]:
        if row["field"] != "sphere_position":
            continue
        evaluation = evaluations.get((row["wall_ms"], row["tick"]))
        if evaluation is None:
            continue
        ack = int(evaluation["ack"])
        label = ack_labels.get(ack, ack) if ack_labels else ack
        if label_range and not label_range[0] <= label <= label_range[1]:
            continue
        positions.append(
            (
                int(row["wall_ms"]),
                label,
                float(row["error"]),
                evaluation["corrected"] == "true",
            )
        )
    positions.sort()
    steps = []
    for previous, current in zip(positions, positions[1:]):
        delta = abs(current[2] - previous[2])
        if delta > DIVERGENCE_STEP:
            steps.append(
                {
                    "label": current[1],
                    "delta": delta,
                    "error": current[2],
                    "after_correction": previous[3],
                }
            )
    return steps


def event_distance(label, events):
    """Returns label distance to the nearest unmirrored event."""
    if not events:
        return None
    return min(abs(label - event["label"]) for event in events)


def capture_role(path):
    """Reads the recording role from a RacingNetLog filename."""
    name = str(path).replace("\\", "/").rsplit("/", 1)[-1]
    if name.startswith("netlog_client_"):
        return "client"
    if name.startswith("netlog_server_"):
        return "server"
    return None


def car_key(path):
    """Returns the entity suffix shared by its client and server CSVs."""
    match = re.search(r"_car(.+)\.csv$", str(path).replace("\\", "/"))
    return match.group(1) if match else None


def tape_alignment(client_path, server_path):
    """Prints mirror alignment and divergence coincidence for one car."""
    client_data = load(client_path)
    server_data = load(server_path)
    client_stream = drive_stream(client_data["FRAME"], client_path)
    server_stream = drive_stream(server_data["FRAME"], server_path)
    if not client_stream or not server_stream:
        return

    print(f"\n=== tape alignment: {client_path} <> {server_path} ===")
    print(
        f"  drives: client={len(client_stream)} server={len(server_stream)}"
    )
    label_range = common_label_range(client_stream, server_stream)
    if label_range is None:
        print("  no common fresh-label range")
        return
    print(f"  common labels: {label_range[0]}..{label_range[1]}")

    server_depths = [
        int(row.get("tape_depth", 0)) for row in server_data["FRAME"]
    ]
    folded = sum(int(row.get("folded", 0)) for row in server_data["FRAME"])
    replayed_tape = any(server_depths) and max(server_depths) < 64 and folded == 0
    client_events = tape_events(client_stream, "client")
    server_events = tape_events(server_stream, "server", replayed_tape)
    matched, unmirrored = match_events(client_events, server_events)
    total = len(client_events) + len(server_events)
    mirrored_units = matched * 2
    fraction = mirrored_units / total if total else 1.0
    print(
        "  events: "
        f"client={len(client_events)} server={len(server_events)} "
        f"mirrored={mirrored_units}/{total} ({100 * fraction:.1f}%)"
    )
    if unmirrored:
        print("  unmirrored events:")
        for event in unmirrored:
            print(
                f"    {event['peer']:6s} {event['type']:6s} "
                f"label={event['label']} kind={event['kind']} "
                f"bracket={event['left']}..{event['right']}"
            )
    else:
        print("  unmirrored events: none")

    offsets = solve_offsets(client_stream, server_stream, label_range)
    changes = []
    previous = 0
    for label, offset in offsets:
        if offset != previous:
            changes.append(f"{label}:{offset:+d}")
        previous = offset
    print(
        f"  solve offset client-server: min={min(v for _, v in offsets):+d} "
        f"max={max(v for _, v in offsets):+d} final={offsets[-1][1]:+d}"
    )
    if changes:
        shown = ", ".join(changes[:30])
        suffix = f", ... ({len(changes)} changes)" if len(changes) > 30 else ""
        print(f"    changes by label: {shown}{suffix}")

    command_alignment(client_stream, server_stream)

    in_range_events = [
        event
        for event in unmirrored
        if label_range[0] <= event["label"] <= label_range[1]
    ]
    ack_labels = None
    if replayed_tape:
        ack_labels = {row["sequence"] - 1: row["label"] for row in client_stream}
    steps = divergence_steps(client_data, label_range, ack_labels)
    resets = sum(step["after_correction"] for step in steps)
    print(
        f"  divergence steps: {len(steps)} above {DIVERGENCE_STEP:.2f} m, "
        f"post-correction resets={resets}"
    )
    for step in steps:
        distance = event_distance(step["label"], in_range_events)
        nearest = "none" if distance is None else str(distance)
        source = "post-correction-reset" if step["after_correction"] else "free"
        print(
            f"    label={step['label']} delta={step['delta']:.4f} "
            f"error={step['error']:.4f} nearest_event={nearest} {source}"
        )

    labels = range(label_range[0], label_range[1] + 1)
    chance = sum(
        event_distance(label, in_range_events) is not None
        and event_distance(label, in_range_events) <= WINDOW
        for label in labels
    ) / len(labels)
    for label, selected in (
        ("raw", steps),
        ("free", [step for step in steps if not step["after_correction"]]),
    ):
        coincident = sum(
            event_distance(step["label"], in_range_events) is not None
            and event_distance(step["label"], in_range_events) <= WINDOW
            for step in selected
        )
        step_rate = coincident / len(selected) if selected else 0.0
        lift_ratio = (
            step_rate / chance if chance else float("inf") if step_rate else 0.0
        )
        print(
            f"  event-adjacent {label} (+/-{WINDOW} labels): "
            f"steps={coincident}/{len(selected)} ({100 * step_rate:.1f}%) "
            f"chance={100 * chance:.1f}% lift={lift_ratio:.2f}x"
        )


def tape_pairs(paths):
    """Pairs client and server streams for each shared logged car."""
    grouped = defaultdict(dict)
    for path in paths:
        role = capture_role(path)
        key = car_key(path)
        if role and key:
            grouped[key][role] = path
    return [
        (pair["client"], pair["server"])
        for pair in grouped.values()
        if "client" in pair and "server" in pair
    ]


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
    kinds = Counter(c.get("contact_kind", "legacy") for c in contacts)
    print(
        "  contact kinds: "
        + ", ".join(f"{kind}={count}" for kind, count in sorted(kinds.items()))
    )
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

    wall_contacts = [
        contact
        for contact in contacts
        if contact.get("contact_kind") == "wall_solver"
    ]
    if not wall_contacts:
        return
    first_wall = int(wall_contacts[0]["wall_ms"])
    strongest = max(
        wall_contacts,
        key=lambda contact: float(contact.get("impulse", 0.0)),
    )
    print(
        "  wall samples: "
        f"{len(wall_contacts)}, first label={wall_contacts[0].get('drive_label')}, "
        f"strongest impulse={float(strongest.get('impulse', 0.0)):.3f} "
        f"at label={strongest.get('drive_label')}"
    )
    print("  divergence split on first solver-classified wall contact:")
    divergence_summary(
        "before wall", [e for e in evals if int(e["wall_ms"]) < first_wall]
    )
    divergence_summary(
        "after wall", [e for e in evals if int(e["wall_ms"]) >= first_wall]
    )


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
    for client_path, server_path in tape_pairs(paths):
        tape_alignment(client_path, server_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
