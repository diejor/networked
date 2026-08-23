# Crossing arms

A family crosses when its specification moves from GdUnit to a native tier and
its GDScript suite is deleted. What that deletion is held to lives in
`tests/suite_census.txt`, one row per suite, and
`extension/tools/check_suite_census.py` refuses a suite with no row.

`<family>_arm.gd` is a family's instrument: it drives the GDScript arm and
compares it to a golden recorded from that arm before the native port existed,
so the native side has something to reproduce other than itself. It is run
directly rather than through GdUnit, and it dies at the crossing with the
classes it drives.

```sh
godot --headless --path . -s res://tests/crossing/<family>_arm.gd
godot --headless --path . -s res://tests/crossing/<family>_arm.gd -- --record
```

A wire or simulator family records what its bytes produced. A family with no
seam and no bytes records what its scenarios observed instead: signal order,
state readings, queue depths. Either way the rule is the same and it is the
only rule that matters here: **record before the replacement exists.** A golden
regenerated against a candidate describes what the candidate produced, which is
the one thing it must not do, and no amount of later work recovers the
evidence.
