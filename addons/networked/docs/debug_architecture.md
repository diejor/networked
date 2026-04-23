# Networked Debugger — Architecture

This document describes the design of the debug subsystem introduced in the `addons/networked/debug/` directory. It is intended as a reference for developers extending the addon or diagnosing its internals.

---

## Overview

The debugger is a three-layer system that keeps failure detection, telemetry collection, and failure reporting as separate concerns. Production components know nothing about it; the debugger attaches to them from the outside.

```
Static Validators          NetRaceDetector, TopologyValidator
      ↓  (return data)
Reporter / Coordinator     NetworkedDebugReporter
      ↓  (dispatch)
Editor / Fallback          EngineDebugger  ·  push_error
```

The reporter is the only layer that touches `EngineDebugger`. Validators and detectors are pure functions that return data and have no side effects.

---

## Layer 1 — Static Validators

Static validators answer a single question: "is this node or this configuration correct right now?" All methods are `static`. They accept nodes or resources as parameters and return structured data; they never emit signals, never send messages, and never depend on runtime state beyond what is passed to them.

**`TopologyValidator`** checks structural correctness of a player node:

- Whether the expected component set (`ClientComponent`, `SaveComponent`) is present and its synchronizers are reachable.
- Whether the synchronizer cache matches a fresh tree traversal (stale-cache detection).
- Whether the `SaveSynchronizer`'s tracked properties match what the `NetworkedDatabase` has registered for that table — *schema drift*. This matters because a save schema is registered once at instantiate time; if a developer renames or removes a property without migrating the database, the drift is otherwise silent until a load mismatch occurs in production.
- Whether virtual properties have `watch=true`, which C++ cannot resolve and will crash silently.
- Whether multiplayer authority was assigned correctly based on the node name convention.

**`NetRaceDetector`** checks timing hazards specific to Godot's `simplify_path` mechanism. When a `MultiplayerSynchronizer` or `MultiplayerSpawner` enters the scene tree, C++ immediately sends `simplify_path` packets to all connected peers. If the *parent node* (the lobby level, the player root) has not yet been spawned on those peers, the `simplify_path` resolution fails with a silent `"Node not found"` error and replication stops working for that node. This is not a Networked bug — it is a fundamental ordering constraint in Godot's multiplayer API. The detector identifies the three sites where this race can occur: lobby spawn, peer connect, and player spawn.

---

## Layer 2 — NetworkedDebugReporter

The reporter is a singleton `Node` (Autoload) that knows *when* to check and *how to report*. It owns no detection math.

### Lifecycle

The reporter only activates when `EngineDebugger.is_active()` is true and no headless/test flags are present. In exported builds it is entirely inert, costing nothing.

When a `MultiplayerTree` calls `register_tree`, the reporter connects to its signals and begins observing. All signal handlers are guarded by `_should_report()`.

### Span Tracing

Every significant network operation (lobby spawn, peer connect, player spawn) opens a [code]NetSpan[/code] or [code]NetPeerSpan[/code] via [code]Netw.dbg.span[/code] or [code]Netw.dbg.peer_span[/code]. Spans are causal identifiers: they track which operation was in flight when a failure occurred and what peers it involved. A span's ID flows into every manifest emitted during that operation as the [code]cid[/code] field, making it possible to correlate a manifest in the editor panel with the exact span step that preceded it.

Spans are no-ops when the debugger is not active — [code]Netw.dbg.span[/code] returns a dummy span — so call sites need no conditional guards.

### Telemetry Ring Buffer

`NetTelemetryBuffer` records one entry per flush cycle (one per frame, deferred). When a failure is detected, the reporter calls `_freeze_and_slice()` before sending the manifest: this freezes the ring buffer (stopping writes so subsequent events do not overwrite the pre-failure history) and returns the last N entries as the `telemetry_slice` field of the manifest. The editor panel can then display what happened in the frames leading up to the crash.

### `_send_manifest`

All crash manifest emissions go through a single method:

```gdscript
func _send_manifest(trigger: String, payload: Dictionary) -> void
```

When a debugger session is active, this calls `EngineDebugger.send_message`. When it is not (e.g., running a debug build outside the editor), it falls back to `push_error` so topology and race failures are still surfaced. Every payload is required to carry `trigger`, `active_scene`, `network_state.peer_id`, and `errors` as a minimum set. These are the fields the editor panel keys on and the fields that will become the formal base contract if a contract system is introduced later.

Trigger names are `SCREAMING_SNAKE_CASE` and self-documenting: `TOPOLOGY_VALIDATION_FAILED`, `SERVER_SIMPLIFY_PATH_RACE`, `CPP_ERROR_LOG_WATCHDOG`, `ZOMBIE_PLAYER_DETECTED`.

### Zombie Player Detection

When a peer disconnects, the reporter schedules a deferred check two seconds later. If any node in an active lobby still has its multiplayer authority set to the disconnected peer's ID, a `ZOMBIE_PLAYER_DETECTED` manifest is emitted. This catches cases where the despawn callback did not run (e.g., a signal was disconnected prematurely, or the server held a strong reference that prevented the node from exiting the tree). The two-second window is intentionally generous to avoid false positives from normal deferred cleanup.

---

## Layer 3 — Production Components

`SaveComponent`, `ClientComponent`, and `LobbySynchronizer` contain no debug assertions and no `NetwLog` calls that address concerns the debugger covers. If a new diagnostic is needed, it goes into a validator first, then the reporter calls it. This boundary ensures that exported builds have zero overhead from the debug system and that the detection logic can be tested independently of the component lifecycle.

---

## Schema Drift

`NetworkedDatabase.register_schema` is called during `SaveComponent.instantiate` with the columns derived from the `SaveSynchronizer`'s replication config. Topology validation compares these columns against what the database already has registered for that table. A mismatch — columns present in the synchronizer but absent from the database, or vice versa — indicates that a property was added, removed, or renamed without a corresponding database migration.

The database's `mismatch_policy` (`PURGE`, `LOAD_PARTIAL`, `FAIL`) determines what happens at load time; topology validation catches the drift at spawn time, before any load is attempted, making it easier to identify which scene introduced the change.

---

## Design Decisions

**Static validators, not methods on components.** Putting validation logic on `SaveComponent` or `ClientComponent` would mean importing `EngineDebugger` and debug dependencies into production code. Static classes with no `extends Node` inheritance keep the diagnostic concerns entirely outside the runtime path.

**Single manifest dispatch point.** Before `_send_manifest` was introduced, each failure site had its own inline `EngineDebugger.send_message` block. There was no fallback for non-debugger builds and no single place to enforce the payload contract. A single dispatch method makes both concerns trivially auditable.

**`freeze_and_slice` before every manifest.** A manifest without context of what preceded it is hard to act on. Freezing the ring buffer at the moment of detection — not after — guarantees the slice covers the frames immediately before the failure rather than the frames immediately after the send.

**No remediation in the reporter.** The reporter reports. It does not attempt to fix the conditions it detects, restart failed operations, or reroute players. Attempting recovery inside a diagnostic layer conflates two responsibilities and can mask the original failure. The intended home for recovery logic is a future contract system where subsystems can register named handlers keyed to trigger names.

---

## Future: Contract System

The current implementation is designed to be retro-fittable into a contract-based architecture. Each `_send_manifest` call already includes the fields that would form the base contract schema. Trigger names are stable identifiers. When a contract system is introduced, it would allow subsystems to register `(trigger, required_fields, on_fire_handler)` tuples. The `on_fire_handler` slot is the intended location for the recovery logic described above.
