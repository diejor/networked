# Crossing ledgers

A family crosses when its specification moves from GdUnit to a native tier and
its GDScript suite is deleted. Deleting that suite is the migration's only
irreversible step, so it is licensed here, in the repository, in the same
change that does it.

Three kinds of file live in this directory.

`baseline.census` is the corpus counted: `func test_*` per directory on the
GDScript side, and per family tag on each native tier. It exists so a later
sweep can diff against a number rather than against a memory. A suite that
quietly stops being discovered makes the corpus read smaller and still green,
and this file is what catches that. Regenerate it with
`.agents/bin/test-census --write`.

`<family>.ledger` is one family's license, written at that family's crossing
slice rather than up front:

```
# family: table
# bridge: A(byte-parity)
# regime: bit-exact
# instrument-first: d284146
# baseline: tests/unit/table, tests/unit/schema
# baseline-count: 83
test_table_codec.gd:test_round_trip -> table_tests.cpp:"[Networked][Table][Hosted] codec round trip"
test_table_carrier.gd:test_late_join -> CARRY tests/integration/table (kit-facing)
test_table_model.gd:*                -> DROP superseded by the schema split
```

Every case in the family's baseline scopes gets exactly one row, and a row's
right-hand side is one of four licenses: a named native successor that must
exist in that tier's inventory, `CARRY` naming where the case stays, `DROP`
saying why it goes, or `MATRIX` for a family whose specification is law-shaped.
A family declaring a parity claim with no tolerance regime has not made a
claim.

`MATRIX` names a cell of the scenario x law product rather than a case, and a
family opts into it in its own header:

```
# successor-form: matrix
test_prediction_reconciliation.gd:test_perturbation_reconverges
    -> MATRIX predict_lane_laws.cpp:"L-CONV"x"perturbed-lane"
```

The cell is resolved against `reports/native/cells.txt`, which the native
runner writes under `--netw-list-cells` from the cells the run actually
executed, so a cell nobody ran licenses nothing. One spelling of a cell is
committed, `"<law>"x"<scenario>"`. Every case still gets its own row, so the
mapping is finer than 1-for-1 rather than coarser and the balanced equation is
untouched.

`baseline-count` is what those scopes held when the family crossed, frozen,
because the census is retaken and stops counting the cases that left. It is not
the ledger marking its own homework: the gate holds the two against each other,
and what the census counts now must be that number minus what the rows say
ported and dropped.

`<family>_arm.gd` is a family's instrument: it drives the GDScript arm and
compares it to a golden recorded from that arm before the native port existed,
so the native side has something to reproduce other than itself. It is run
directly rather than through GdUnit, it is not in any census scope, and it dies
at the crossing with the classes it drives.

```sh
godot --headless --path . -s res://tests/crossing/<family>_arm.gd
godot --headless --path . -s res://tests/crossing/<family>_arm.gd -- --record
```

Both bridges use one. A Bridge-A family records what its wire or its simulator
produced; a Bridge-B family has no seam and no bytes, so it records what its
scenarios observed instead — signal order, state readings, queue depths. Either
way the rule is the same and it is the only rule that matters here: **record
before the replacement exists.** A golden regenerated against a candidate
describes what the candidate produced, which is the one thing it must not do,
and no amount of later work recovers the evidence.

Trace rows are names and integers. States are written symbolically so a
renumbering fails the comparison instead of moving the golden quietly, keys are
sorted as text rather than as `StringName` (which compares by interned address,
so it is stable enough to look correct and not stable enough to be), and handles
are written as identity relations rather than as RIDs, since an RID is an
allocation address.

Goldens outlive their arms. `tests/native/goldens/transport_permutations.txt`
was recorded through the arm transport crossed with, and it is now what the
native cases reproduce.

`tests/support/netw_recorder.gd` is the GDScript twin of the native
`Recorder` in `extension/tests/support/netw_recorder.h`, and it is what a
Bridge-B arm captures signal order with. One rule carries across both: a
recorder on a signal that does not exist fails where it is constructed, because
an unconnected recorder answers zero to every question and reads exactly like a
correct one that saw nothing.

`.agents/bin/crossing-gate` is what turns that from a convention into a gate.
It refuses a missing regime, a row naming a native case no tier has, a ledger
whose rows do not account for the family's whole baseline, and — the reason the
file exists — a GDScript case that vanished since the baseline commit with no
row licensing it. Every exclusion it honours is printed, because an exclusion
nobody prints is indistinguishable from an effect.

```sh
.agents/bin/crossing-gate --inventory reports/native/results.xml \
                          --inventory tmp/module-cases.txt
.agents/bin/crossing-gate --self-test    # proves itself red, then green
```

One thing to know before the first crossing: the census records the commit its
counts were taken at, and the deletion check reads the GDScript corpus at that
commit through `git ls-tree`. This project's public history is rewritten into
weekly buckets, so a recorded baseline can stop being reachable. When that
happens the gate refuses with `cannot read <scope> at <commit>` rather than
passing quietly, and the fix is to re-take the census at a commit that still
exists.

Transport is the first family to cross, and its ledger is the worked example.
The census is the floor every later one is measured against.
