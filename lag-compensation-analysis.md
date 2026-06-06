# Lag Compensation Analysis — Netfox, Standard Techniques & Networked Plan

> Evaluating the user's rollback architecture plan for Networked against netfox's
> implementation and standard multiplayer lag compensation techniques from
> public sources (Valve Source Engine, Gaffer on Games, Unreal Engine).

---

## 1. Netfox's Approach (source-read from the codebase)

### 1.1 Architecture Pattern

Netfox uses a **global autoload** pattern. `NetworkTime` is an autoloaded singleton. `NetworkRollback` is another autoload. Servers (`NetworkHistoryServer`, `RollbackSimulationServer`, `NetworkSynchronizationServer`) are singletons registered at startup.

```
NetworkTime (autoload) → tick loop → signals → NetworkRollback → rollback loop
                                                           ↓
                                              RollbackSynchronizer (per-object, Node)
                                                           ↓
                                              _rollback_tick(delta, tick, is_fresh)
```

### 1.2 Property Classification

Netfox uses **explicit classification** on RollbackSynchronizer:

```gdscript
@export var state_properties: Array[String]    # server → clients
@export var input_properties: Array[String]    # client → server
@export var enable_prediction: bool = false
```

Properties are developer-declared, not auto-detected. The synchronizer reads these, resolves NodePaths, and registers them with `NetworkHistoryServer` and `NetworkSynchronizationServer`.

### 1.3 History & Snapshot System

Three separate history pools:

```
_rb_state_history    → PerObjectHistory (carry_forward = true)
_rb_input_history    → PerObjectHistory (carry_forward = false)
_sync_history        → PerObjectHistory (for non-rollback sync state)
```

Key design: snapshots are the unit of storage. `_Snapshot` objects store `{Object → {StringName → Variant}}` plus `is_auth` flag. HistoryBuffer stores `_Snapshot` at each tick. This enables atomic capture/restore: all properties for all objects at a given tick are captured or restored together.

### 1.4 The Rollback Loop

Connected to `NetworkTime.after_tick_loop`. Core logic in `NetworkRollback._rollback()`:

```
_rollback():
    // 1. Determine resimulation range
    _resim_from = NetworkTime.tick              // start from current, only move earlier
    before_loop.emit()                          // let nodes submit earlier ticks
    
    if _earliest_input >= 0 and _earliest_input <= _resim_from:
        _resim_from = _earliest_input
    if _earliest_state >= 0 and _earliest_state <= _resim_from:
        _resim_from = _earliest_state
    _resim_from = mini(_resim_from, NetworkTime.tick - 1)    // never resim current
    
    // 2. Clamp to history limit
    if tick - _resim_from > history_limit:
        _resim_from = tick - history_limit
    
    // 3. Loop
    for tick in range(_resim_from, NetworkTime.tick):
        // PREPARE: restore state + input for this tick
        on_prepare_tick.emit(tick)
        NetworkHistoryServer._restore_rollback_input(tick)
        NetworkHistoryServer._restore_rollback_state(tick)
        RollbackLivenessServer.restore_liveness(tick)
        
        // SIMULATE: run _rollback_tick on registered nodes
        on_process_tick.emit(tick)
        RollbackSimulationServer.simulate(delta, tick)
        
        // RECORD: capture resulting state for tick+1
        on_record_tick.emit(tick + 1)
        NetworkHistoryServer._record_rollback_state(tick + 1)
        NetworkSynchronizationServer._synchronize_state(tick + 1)
    
    // 4. RESTORE DISPLAY: apply state at display_tick
    NetworkHistoryServer._restore_rollback_state(display_tick)
    RollbackLivenessServer.restore_liveness(display_tick)
    
    // 5. CLEANUP
    _earliest_input = -1
    _earliest_state = -1
    _is_rollback = false
```

### 1.5 Simulation Dispatch

`RollbackSimulationServer.simulate()`:

1. Get input snapshot from history at this tick
2. For each registered callback (in scene-tree order via groups):
   - Check if input-dependent: if so, check if input data available
   - If no input and not prediction-enabled: skip
   - If no input but prediction-enabled: simulate anyway
   - Track `is_fresh` per-node via `_simulated_ticks[node]` (PackedInt32Array)
   - Call `node._rollback_tick(delta, tick, is_fresh)`

### 1.6 Input Handling

```
// On client: after each tick
after_tick.connect(func(_dt, tick):
    NetworkHistoryServer._record_rollback_input(tick + input_delay)
    NetworkSynchronizationServer._synchronize_input(tick + input_delay)
)

// On server: when input arrives
_handle_input(snapshot):
    _earliest_input = min(_earliest_input, snapshot.tick)
```

Input is recorded with `tick + input_delay` — timestamped into the future. This gives time for it to arrive before it's needed. For reliable transport (Phase A of the user's plan), `input_delay = 0`.

### 1.7 Prediction

Prediction is **not a mode** — it's a consequence of missing data:

```
_is_predicting(node, input_snapshot):
    if not node.is_multiplayer_authority() and has_input:
        return false   // we own the input for it
    if not node.is_multiplayer_authority():
        return true    // no ownership, pure guess
    if node has no input dependencies:
        return false   // deterministic
    if owned but missing input:
        return true    // should have data but don't
    return false       // authoritative with fresh input
```

---

## 2. Standard Lag Compensation Techniques (Public Knowledge)

### 2.1 Client-Side Prediction (Source: Valve Developer Wiki, Gaffer on Games)

The core idea: the client predicts the result of its own actions locally instead of waiting for the server. Three components:

1. **Input sampling**: Client captures input at each tick and sends to server
2. **Local simulation**: Client runs the same simulation code as the server using its own input
3. **Reconciliation**: When server state arrives, compare predicted vs authoritative. If they differ, re-simulate from the divergence point

Key design decision: simulation code must be **deterministic** — same input on same state always produces same output.

### 2.2 Server-Side Reconciliation (Source: Unreal Engine, Source Engine)

The server is authoritative. When it receives a player's input, it:

1. **Stores input** at the tick it was generated
2. **Simulates forward** from that tick to produce updated state
3. **Broadcasts authoritative state** to all clients

The server never re-simulates — it's always correct. Only clients re-simulate.

### 2.3 Rollback vs Rewind (Source: various GDC talks)

**Rollback** (netfox's approach): Start from a past tick and re-simulate forward to current. Used on clients when server state arrives and diverges from prediction.

**Rewind** (hit-scan lag compensation): Temporarily rewind the world to a past state to check a single action (e.g., "did the player's bullet hit?"), then restore. Used for hit-scan weapons.

Netfox implements rollback. The binary implements something closer to "prediction with state reset" — snapshot data overwrites predicted state when it arrives.

### 2.4 Input Buffering (Source: Source Engine, many GDC talks)

Inputs are sent with redundancy (`input_redundancy` in netfox) — sending current plus previous N ticks of input. This handles UDP packet loss without requiring reliable channels. The binary's InputSynchronizer approach (sending properties via Godot's reliable MultiplayerSynchronizer) avoids packet loss but adds latency from re-transmission.

---

## 3. Comparison: Netfox vs Standard Multiplayer Engine

| Aspect | Netfox | Standard Engine (Valve/Unreal) |
|--------|--------|-------------------------------|
| **Architecture** | Global autoloads | Service/component-based |
| **Property declaration** | Explicit arrays on synchronizer | Code-driven (GDExtension class registration or UPROPERTY macros) |
| **Transport** | Custom via NetworkSynchronizationServer, diff states, schema serializers | Binary word buffer with compressed transform data and FNV-1a property hashing |
| **Clock model** | `NetworkTime` with clock stretching | Peer-internal network time, server-calibrated via ping/pong |
| **Authority model** | Server-authoritative only | Both Shared and Client-Server modes |
| **Input delay** | Configurable `input_delay` (timestamps into future) | Implicit from transport latency |
| **Prediction** | Implicit, derived from data availability | Configurable per-object (AutonomousProxy vs SimulatedProxy in Unreal) |
| **State sync** | Diff states with full-state fallback every N ticks | Per-property word buffer, only changed properties sent |
| **Physics** | `_rollback_tick` runs physics simulation code directly | PhysicsServer RID-based snap with velocity reset, separate forecast model |
| **Interest management** | None built-in (relies on transport) | Spatial cell-based grid with send-rate decay |
| **Spawn/despawn** | `RollbackLivenessServer` — tracks spawn/despawn ticks, shows/hides nodes during rollback | Spawner + scene handler with late-join support |

---

## 4. What Networked's Plan Gets Right

### 4.1 Service-Based Architecture

The plan to use `NetwServices` registration instead of global autoloads is correct for Networked's architecture. Networked already has `NetworkClock` as a registered service — extending this pattern to `RollbackHistoryServer`, `RollbackSimulator`, `RollbackManager` maintains consistency.

### 4.2 TickAwareSynchronizer as Transport Foundation

Using `ProxySynchronizer` → `TickAwareSynchronizer` → `RollbackSynchronizer` / `InputSynchronizer` as the inheritance chain is the right call. The `__tick` property provides tick-level information without custom serialization. The `_write_property` / `_read_property` interception pattern gives full control over data flow.

### 4.3 State vs Input Classification via Authority

The plan correctly identifies that authority determines direction:
- Properties on authoritative nodes → input (local peer drives them)
- Properties on non-authoritative nodes → state (someone else drives them)

This is cleaner than netfox's explicit `state_properties`/`input_properties` arrays — it's less configuration, same result.

### 4.4 Carry-Forward Semantics

The plan correctly distinguishes:
- **State**: carry_forward = true — a velocity that didn't change should still be available
- **Input**: carry_forward = false — no input means "player wasn't pressing anything"

This matches netfox's approach exactly and is the correct design.

### 4.5 Prediction as Consequence, Not Mode

The plan's `_is_predicting()` logic is sound — prediction happens when data is missing, not because a checkbox is ticked. The `is_fresh` parameter for one-shot effects (sounds, spawns) vs re-simulation is essential for correct behavior.

### 4.6 Sync Suppression Observation

The plan notes that Godot's `poll()` (which processes MultiplayerSynchronizer sync) runs AFTER physics processing, and the rollback loop completes within `_physics_process`. This means intermediate values during rollback won't be captured by the engine. This is correct — no explicit suppression needed.

---

## 5. What the Plan Could Improve

### 5.1 History Buffer: Per-Object vs Per-Property

Netfox stores `_Snapshot` objects (per-object-per-tick) in HistoryBuffer. The plan uses a similar approach with `RollbackSnapshot`. This is good — it enables atomic capture/restore.

However, the plan's `PerObjectHistory` wraps `{Object → HistoryBuffer[RollbackSnapshot]}`. Netfox uses `HistoryBuffer[_Snapshot]` at the top level and `_Snapshot` internally maps `{Object → {Property → Value}}`. The difference is subtle but important:

- Netfox approach: One ring buffer of snapshots, each containing all objects
- Plan approach: One ring buffer per object, each containing that object's snapshots

Netfox's approach means a single snapshot read gives you the complete world state at a tick. The plan's approach requires iterating all objects. For the plan's Phase A with per-property sync, either works. For Phase C with blob encoding, netfox's approach would be more efficient.

### 5.2 Scene-Tree Order vs Callback Priority

Netfox sorts simulation by scene-tree order via `add_to_group()` + `get_nodes_in_group()`. The plan's `RollbackSimulator` mentions sorting by scene-tree order. For Networked, consider using `group` sorting (Godot's internal group system already returns nodes in scene-tree order). No custom priority system is needed.

### 5.3 Input Redundancy

Netfox sends `input_redundancy` previous ticks of input to handle UDP packet loss. The plan uses Godot's reliable `MultiplayerSynchronizer` (via `REPLICATION_MODE_ALWAYS`) which doesn't need redundancy but adds latency on retransmission. The plan could add a note: if switching to unreliable transport in a future phase, consider input redundancy.

### 5.4 Mutations API

Netfox has a `mutations` system: `notify_mutation(node, tick)` marks an object as externally modified during rollback, triggering re-recording. The plan doesn't address this. For objects that receive external interactions during rollback (e.g., a physics body being hit by another), mutation tracking prevents stale state from being recorded. Consider adding this to Phase B or later.

### 5.5 Diff States and Full-State Intervals

Netfox sends diff states (only changed properties) by default with a fallback to full states every `full_state_interval` ticks. The plan's Phase A uses Godot's built-in `REPLICATION_MODE_ON_CHANGE` which provides implicit diffing at the Godot engine level. This is actually a strength — no custom code needed. The plan could note that Phase C's blob encoding should include diff capability.

---

## 6. Comparison: The Binary Analysis Engine

Based on the analysis of a production multiplayer engine (not named), here's how its lag compensation compares:

### 6.1 What It Implements

| Feature | Production Engine | Netfox | Networked Plan |
|---------|------------------|--------|---------------|
| Client-Server with prediction | ✓ (Configurable) | ✓ (Via `enable_prediction`) | ✓ (Via `_is_predicting()`) |
| Shared authority (P2P) | ✓ | ✗ | ✗ (out of scope for Phase A) |
| State reset on divergence | ✓ (`state_reset` with reason codes) | ✓ (implicit via rollback loop) | ✓ (implicit via rollback loop) |
| Input authority management | ✓ (`set_input_authority(player_id)`) | ✗ (implicit via authority) | ✗ (not yet designed) |
| Per-property interpolation | ✓ (shadow state → exponential decay) | ✗ (snap-based from history) | ✗ (uses existing TickInterpolator) |
| Physics body snap | ✓ (PhysicsServer RID) | ✗ (property-level only) | ✗ (would use TickInterpolator approach) |
| Interest management | ✓ (spatial cell grid) | ✗ | ✗ |

### 6.2 What It Does NOT Implement

| Feature | Production Engine | Notes |
|---------|------------------|-------|
| Full rollback re-simulation | Partial | Appears to use "state reset" rather than re-simulating from past ticks. When authoritative state arrives that diverges from prediction, past state is overwritten and prediction restarts from the corrected state. Netfox's approach of re-running _rollback_tick for every tick from divergence is more thorough but more expensive. |
| `is_fresh` distinction for effects | No | The engine doesn't appear to distinguish first-time vs re-simulation. One-shot effects (sounds, spawns) would need a separate mechanism. |
| Explicit rollback loop | No | The main frame loop processes replicator ticks, inputs, and spawns in a single pass. There's no separate "go back to tick N and re-simulate to tick M" loop. Divergence is handled by state overwrite. |

### 6.3 Key Insight

The production engine takes a **lower-cost approach**: instead of re-simulating from divergence, it applies authoritative state directly (overwriting prediction) and continues forward. This means:
- Less CPU usage (no re-simulation loops)
- No need for deterministic simulation code
- Visual interpolation handles the transition from predicted to authoritative position
- State resets are infrequent (only when significant divergence occurs)

Netfox's approach is **higher quality but higher cost**: it guarantees that every frame of simulation is correct, but at the cost of potentially re-running many ticks of simulation.

The user's plan follows netfox's higher-quality approach. For Networked (GDScript), this is appropriate — GDScript simulation is cheap enough that re-running ticks is acceptable, and the correctness guarantee is valuable.

---

## 7. What the Production Engine's Architecture Would Mean for Networked

If the production engine's approach were adapted to Networked, it would look like:

```
Instead of:  resimulate ticks 47→50
Do this:     when server state arrives at tick 47 with divergence:
             1. Apply server state directly (position, velocity, etc.)
             2. Set state_reset flag with RESIM_PREDICTION reason
             3. Clear prediction history from tick 47 forward
             4. Continue predicting from corrected state at tick 50
```

This reduces CPU but requires the `state_reset` signal and a separate interpolation pass to smooth the transition from predicted to authoritative state. Networked already has `TickInterpolator` for this — but the plan's rollback loop would need to work with it.

### Recommendation
The plan's netfox-style approach (full re-simulation) is the right call for Phase A. It's simpler to implement correctly, yields higher quality results, and GDScript can handle it for typical entity counts. If performance becomes an issue, the production engine's "state reset + interpolation" approach could be added as an optimization path later, with `state_reset(REASON_PREDICTION_RESET)` and a `FusionStateResetInfo`-like signal.

---

## 8. Overall Assessment of the Plan

### Strengths
- The `_write_property` → history-only design is the correct approach — prevents one-frame flicker
- The `_earliest_input` / `_earliest_state` per-loop accumulator pattern matches netfox's proven design
- Record tick = `tick + 1` is correct given `state(t+1) = simulate(state(t), input(t))`
- Display restore at `tick - display_offset`, not current tick — gives authoritative state a window to arrive
- History trimming AFTER display restore — never before
- `is_fresh` deduplication prevents double-effects without preventing double-simulation

### Areas to Watch
1. **Memory**: `PerObjectHistory` with per-object ring buffers could grow. Consider per-tick snapshots (like netfox) in later phases
2. **Group sorting**: The plan mentions groups for scene-tree ordering. Ensure this doesn't conflict with existing Networked group usage
3. **Integration with TickInterpolator**: When the rollback loop restores display state, it overwrites interpolated values. TickInterpolator needs to recognize rollback frames and skip interpolation
4. **Spawn/despawn during rollback**: The plan doesn't address what happens when `_rollback_tick` spawns or despawns objects during re-simulation. Netfox has `RollbackLivenessServer` for this — the plan should add a similar component

### The plan is sound. It ports netfox's battle-tested design into Networked's service-based architecture. The Phase A line budget (~940 lines) is realistic for a working prototype.
