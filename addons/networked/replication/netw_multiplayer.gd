## The one [MultiplayerAPIExtension] a [MultiplayerTree] ever installs.
##
## A tree whose installed API is not [NetwMultiplayer] is unrepresentable.
## [MultiplayerTree] constructs exactly one per session and nothing else
## installs an API on the tree's branch. It wraps an
## [member inner] [SceneMultiplayer] rather than reimplementing peer transport,
## and plays three roles on top of it.
## [codeblock]
## wrapper   inner keeps the MultiplayerPeer, the connection
##           lifecycle, and the auth handshake
## seam      every node-facing call (spawner and synchronizer
##           registration, native @rpc dispatch) passes through
##           here on its way to inner
## carrier   every Networked frame leaves through send_packet over
##           SceneMultiplayer.send_bytes and arrives back through
##           inner's raw packet signal, where a NetwFrameEnvelope
##           magic byte selects Networked framing versus
##           peer_packet pass-through for application bytes
## [/codeblock]
## Every intake verdict and engine stage is an overridable method with a working
## default. Install a subclass through [method make],
## [constant MULTIPLAYER_SCRIPT_SETTING], or [member MultiplayerTree.api_script],
## override the decisions a game owns, and call [code]super()[/code] for the
## stock behavior. Each is independently overridable, so replacing one stage
## never obliges you to replace another.
## [codeblock]
## gate    a verdict on remote bytes, judged before the wire route
##         resolves to an entity, so it keys on route and never on RID
## stage   the work itself, keyed by entity: RID, already resolved
## [/codeblock]
## Composing a rule on top of the stock engine is layer work rather than
## override work. [method layer_create] with a [enum LayerPolicy], a
## [enum LeavePolicy], and a driver callback expresses "hide this from them"
## without replacing the interest matrix at all. Reach for the stages below
## when you are replacing an engine, not when you are adding a rule.
## [br][br][b]The seam[/b][br]
## [br]- Gates: [method _sync_admit_frame], [method _spawn_admit_frame],
## [method _predict_admit_frame]
## [br]- Sync: [method _entity_add_property_set], [method _entity_remove_property_set],
## [method _sync_encode], [method _sync_decode], [method _note_ack],
## [method _note_sent]
## [br]- The sync lane's property boundary: [method _gather_set],
## [method _apply_set]
## [br]- Spawn: [method _spawn_declare], [method _spawn_undeclare],
## [method _spawn_reconcile], [method _spawn_construct]
## [br]- Prediction: [method _predict_drive], [method _predict_consume],
## [method _predict_evaluate], [method _predict_recover]
## [br]- Interest: [method _layer_declare], [method _layer_undeclare],
## [method _interest_recompute], [method _interest_commit],
## [method _interest_row_of], [method _interest_admits],
## [method _interest_explain]
## [br]- Display: [method _display_declare], [method _display_undeclare],
## [method _display_record], [method _display_pump_entity],
## [method _display_write]
## [br][br]
## Interest's seven stages share one committed matrix and display's five share
## per-entity sample history, so replacing part of either family leaves the rest
## reading state your override never fills. Nothing enforces this. Override the
## family together, or forward the members you do not care about.
## [codeblock]
## extends NetwMultiplayer
##
## func _interest_recompute() -> Error:
##     return _my_own_fold()
##
## func _interest_admits(entity: RID, peer_bit: int) -> bool:
##     return _my_matrix.test(entity, peer_bit)
## [/codeblock]
class_name NetwMultiplayer
extends MultiplayerAPIExtension

const PersistenceCore := preload("res://addons/networked/sync/database/persistence_core.gd")

const RpcCore := preload("res://addons/networked/replication/rpc_core.gd")

const SessionRoster := preload("res://addons/networked/session/session_roster.gd")

const AuthProtocol := preload("res://addons/networked/session/auth/auth_protocol.gd")

const AreaReparentGuard := preload("res://addons/networked/utils/area_reparent_guard.gd")

const Async := preload("res://addons/networked/utils/async.gd")

## Project setting naming the default [NetwMultiplayer] implementation script.
const MULTIPLAYER_SCRIPT_SETTING := "networked/multiplayer_script"
## Environment variable naming the extension certified by a law-suite run.
const LAW_EXTENSION_ENV := "NETW_LAW_EXTENSION"

# The pumps per second a session runs at while no clock is configured, which is
# the engine's own frame rate, because every idle frame carries one poll.
const _POLL_PUMP_RATE := 60.0

# The tickrate a when_live wait ages against while no clock is configured, so a
# default timeout is still roughly one second on a rig that never registered one.
const _CLOCKLESS_TICKRATE := 30

const _LIVENESS_CLEAR_KEY := &"liveness-clear-session"

# Resolved once by law_extension_script, because the environment is read once
# per run and never per call.
static var _law_extension_script: Script
static var _law_extension_resolved := false
## The wrapped [SceneMultiplayer]. Owns the [MultiplayerPeer], connection
## lifecycle, [method SceneMultiplayer.send_bytes], and the auth protocol.
var inner: SceneMultiplayer

var _native_core: NetwMultiplayerCore

# The steady-state half of the replication core: the sender tick pump, per-peer
# aggregation buffers, receive-side dispatch, and on-demand property and signal
# sync. Never null.
var _replication: ReplicationCore

# The RPC half of the replication core: Netw.rpc and Netw.request framing,
# transactions, and deferred-call parking. Never null.
var _rpc_core: RefCounted

# The tick engine. Never null, inert until a MultiplayerClock configurator
# registers through object_configuration_add.

# The read view handed out by the clock property. Built with the engine, so it
# is never null and never rebound.
var _clock_handle: NetwClockHandle

# The bootstrap and teardown machine the installing embedding drives. Never
# null, and constructed DECLARING.
var _embedding: EmbeddingCore

# The view handed out by the embedding property. Built with the machine, so it
# is never null and never rebound.
var _embedding_handle: NetwEmbeddingHandle

## Fires at the start of each simulation tick, before game logic.
signal before_tick(delta: float, tick: int)
## Fires during each simulation tick.
signal on_tick(delta: float, tick: int)
## Fires at the end of each simulation tick, after game logic.
signal after_tick(delta: float, tick: int)
## Fires once before the tick loop runs each physics frame.
signal before_tick_loop()
## Fires once after the tick loop finishes each physics frame.
signal after_tick_loop()
## Fires when the client calibrates its clock with the server.
signal clock_synchronized()
## Fires when [member NetwClockHandle.display_offset] is below
## [member NetwClockHandle.recommended_display_offset].
signal display_offset_insufficient(recommended: int)
## Fires when network clock stability changes.
signal stability_changed(is_stable: bool)

## Fires at the top of every [method poll], before the transport is read.
##
## Anything that services a carrier has to run ahead of the transport read or
## the packets it queues arrive a frame late, so this is the one ordering
## guarantee a subscriber outside this API can stand on. [param delta] is the
## wall-clock gap since the previous poll, which is what a countdown or a
## signaling pump advances on.
## [codeblock]
## api.poll_started.connect(func(delta: float) -> void:
##     _view.poll(delta)          # queues packets ...
## )                              # ... read by the transport later this frame
## [/codeblock]
signal poll_started(delta: float)

## This session's clock, the one place the tick and its metrics are read.
##
## Never [code]null[/code]. The clock is inert until a [MultiplayerClock]
## registers its [NetwClockConfig], so
## [member NetwClockHandle.is_configured] is the absence story rather than a
## null check. A game reads the tick here and subscribes to [signal on_tick],
## because a tick is a session event rather than a handle's own.
## [codeblock]
## var api := NetwMultiplayer.of(self)
## api.on_tick.connect(func(delta: float, tick: int) -> void:
##     _simulate(tick)
## )
## var lag := api.clock.tick - api.clock.display_tick
## [/codeblock]
var clock: NetwClockHandle:
	get:
		return _clock_handle

## The install contract between this session and whatever embedded it.
##
## Never [code]null[/code]. An embedding decides when authoring is complete and
## when the session is released, and both are one call here, so a
## [MultiplayerTree] subtree, a root autoload install, and a tree-less pump run
## the identical bring-up and teardown sequence. Nothing a game writes touches
## this: the embedding it installed through already drives it.
## [codeblock]
## var api := NetwMultiplayer.of(self)
## if api.embedding.phase == NetwEmbeddingHandle.Phase.LIVE:
##     pass   # authoring resolved, bring-up may proceed
## [/codeblock]
var embedding: NetwEmbeddingHandle:
	get:
		return _embedding_handle


# Physics space RID -> the stepper that re-steps it for a STEPPED replay.
var _steppers: Dictionary = { }

# The persistence core. Never null. Registers nothing on clients and no-ops
# without a Netw.configure_persistence archetype.
var _persistence: PersistenceCore

# The visibility and interest core for this tree.

# The view handed out by the session property. It reads this api rather than a
# machine, so replacing the machine can never leave the session property
# reading a retired one.
var _session_handle := NetwSessionHandle.new(self)

# The session configuration, a default until a MultiplayerTree registers its
# NetwSessionConfig. Facts read through it so a bare API answers with defaults
# instead of a special inert mode.
var _session_config: NetwSessionConfig = NetwSessionConfig.new()

# Authentication is session wire state, so it binds straight to the wrapped
# SceneMultiplayer. The tree only supplies a typed config and optional
# admission policy.
var _auth: AuthCoordinator

# A successfully prepared join waiting for the next client ONLINE edge. It is
# consumed before submission so repeated connection signals cannot resend it.
var _prepared_join: JoinPayload

# A submitted client join held for one resend, in case the first submit dropped
# at the carrier because the server peer had not yet landed in get_peers.
var _resubmit_join: JoinPayload

# The built-in join handler, bound lazily to this api. Resolved when no project
# registration or per-session override is present.
var _default_join: NetwDefaultJoin

# Per-session join handler override and its wire-arg quantizers, taking
# precedence over the project-wide Netw.configure_join registration. For tests,
# the debugger, or a session that genuinely differs.
var _join_override: Callable
var _join_override_quantizers: Array = []

# The auth flow constructed once from the project-wide Netw.configure_auth
# factory, cached for this session's lifetime. A per-session override takes
# precedence, for tests, the debugger, or a service binding a runtime flow.
var _bound_auth_flow: NetwAuthFlow
var _auth_flow_override: NetwAuthFlow

## The verbs that move this session between offline and online.
##
## Never [code]null[/code], and it outlives any one session: the same handle
## hosts, leaves, and hosts again. What the session currently [i]is[/i] stays on
## this API, because [member role] and [member state] have derived readers here
## ([member is_host], [member is_local_client], [member is_online],
## [method is_server]) and splitting a mirror from its composites is what this
## grouping exists to avoid.
## [codeblock]
## var api := NetwMultiplayer.of(self)
## await NetwConnector.of(api).host(payload)
## if api.is_host:
##     api.session.pause("intermission")
## [/codeblock]
var session: NetwSessionHandle:
	get:
		return _session_handle

# The engine state behind every scene: construction, the native-capture policy,
# the request protocol, and the admission rows. Never null, because the session
## The timeline role of a compiled property set.
##
## The role decides one thing: whether the server keeps history for the stream,
## which it does exactly when it must be able to second-guess the author.
enum RecordKind {
	## The server's own truth, authored by it and kept in a rewind timeline.
	## A body pose is the usual example.
	STATE = NetwPropertySet.Record.RECORD_STATE,
	## A client's claim, sent to the server only and verified by it, also kept
	## for rewind. Movement keys are the usual example.
	INPUT = NetwPropertySet.Record.RECORD_INPUT,
	## Trusted display sent to everyone, checked by nobody and kept nowhere.
	## An aim arrow is the usual example.
	BROADCAST = NetwPropertySet.Record.RECORD_BROADCAST,
}

## The current connection lifecycle state.
enum SessionState {
	## No peer is connected and none is being sought.
	OFFLINE = 0,
	## A host or join is in flight and may still fail.
	CONNECTING = 1,
	## The session carries traffic.
	ONLINE = 2,
	## Teardown is in flight; no new traffic is admitted.
	DISCONNECTING = 3,
}

## The local peer's role in the current session.
##
## Server authority means [constant DEDICATED_SERVER] or
## [constant LISTEN_SERVER]. Very little should test for [constant CLIENT],
## because a listen-server host is also a player.
enum Role {
	## No session, so no role yet.
	NONE = 0,
	## A remote peer holding no authority.
	CLIENT = 1,
	## Server authority with no local player.
	DEDICATED_SERVER = 2,
	## Server authority held by a peer that is also playing.
	LISTEN_SERVER = 3,
}

## Existence state of an entity handle or wire route.
##
## The states are ordered and a route only ever moves forward through them, so
## a tombstone can never come back to life under the same route.
enum EntityState {
	## The route was never issued, or was issued by a peer this one has not
	## heard from.
	UNKNOWN = NetwLivenessCore.STATE_UNKNOWN,
	## The entity exists and accepts traffic.
	LIVE = NetwLivenessCore.STATE_LIVE,
	## Despawn has begun. Frames still in flight are dropped rather than
	## treated as addressing nothing.
	LINGERING = NetwLivenessCore.STATE_LINGERING,
	## A tombstone. The route is retired and will not be reissued.
	DEAD = NetwLivenessCore.STATE_DEAD,
}

## How a calibrated clock corrects local tick drift.
##
## Correction is never optional: the local tick is always driven back toward the
## server's, and the mode only decides whether that happens in one step or is
## spread across frames.
enum SyncMode {
	## Hard-jump the local tick to the calibrated target on every sample.
	SYNC_MODE_SNAP = NetwClockHandle.SyncMode.SYNC_SNAP,
	## Nudge the tick accumulator toward the target, snapping only once the
	## divergence exceeds the configured panic threshold.
	SYNC_MODE_STRETCH = NetwClockHandle.SyncMode.SYNC_STRETCH,
}

## Mutable properties of an interest layer.
##
## Passed to [method layer_set_param] and [method layer_get_param].
enum LayerParam {
	## The layer's [enum LayerPolicy], which composes its viewer set.
	LAYER_PARAM_POLICY,
	## The layer's [enum LeavePolicy], which decides what happens on the wire
	## when the layer stops admitting an entity.
	LAYER_PARAM_LEAVE_POLICY,
	## The layer's [enum PerceptionPolicy], which decides what the local player
	## sees when the layer stops admitting an entity.
	LAYER_PARAM_PERCEPTION_POLICY,
}

## One declared fact about a scene, read and written through
## [method scene_set_param] and [method scene_get_param].
enum SceneParam {
	## [StringName] stem naming the scene's archetype. Stems are NOT unique,
	## because identity is the entity RID. See
	## [member NetwEntity.scene_label].
	SCENE_PARAM_LABEL,
	## The scene's [enum SceneIsolation]. WRITE-ONCE before the entity arms,
	## because it chooses the container every peer builds.
	SCENE_PARAM_ISOLATION,
	## [bool] gating whether the scene's subtree processes. Freeze and resume.
	SCENE_PARAM_PROCESSING,
}

## One edge a scene reports through [method scene_observe].
##
## Every callback takes [code](present: bool, subject: Variant)[/code], so one
## registration covers both directions of an edge and a caller that only wants
## arrivals discards the other half.
enum SceneEvent {
	## Admission changed for a participant. [code]subject[/code] is its peer id.
	SCENE_EVENT_PARTICIPANT,
	## A player entity entered or left. [code]subject[/code] is its entity RID.
	SCENE_EVENT_PLAYER,
	## Any tracked entity entered or left. [code]subject[/code] is its entity
	## RID. Players report here too, so a caller wanting only them reads
	## [constant SCENE_EVENT_PLAYER] instead.
	SCENE_EVENT_ENTITY,
}

## How far an admitted scene request reaches.
##
## A request names one participant, so reaching past it is a policy server
## authority states rather than something a requester may ask for. This is the
## other half of [method scene_set_request_handler], which answers whether a
## request is admitted at all.
enum SceneReach {
	## Only the requesting participant moves.
	SCENE_REACH_PARTICIPANT,
	## Every participant follows and the source scene retires, which is a
	## session-wide change applied to every request.
	SCENE_REACH_SESSION,
}

## Whether a scene hosts its own world or shares the session's.
##
## This is a per-scene fact carried by the spawn recipe rather than a session
## setting, so one session can hold a shared lobby and several isolated match
## worlds at once. It must be settled before the entity arms: the recipe runs on
## every peer to build the container, and a peer that built the wrong kind
## cannot anchor the scene's children.
enum SceneIsolation {
	## The scene shares the session's world. Its container is a plain [Node].
	SCENE_ISOLATION_NONE,
	## A hosting peer builds the scene an offscreen [SubViewport] with its own
	## world, so two live scenes never share a physics space. A peer that only
	## views the scene still builds a plain [Node], because only the host
	## simulates.
	SCENE_ISOLATION_OWN_WORLD,
}

## Interest composition policy for a layer's viewer set.
##
## A layer names a set of viewers; the policy says which side of that set the
## layer's entities are visible to.
enum LayerPolicy {
	## Only members of the layer see its entities. The usual choice for a team
	## or a private room.
	HIDE_FROM_OUTSIDERS = NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS,
	## Only non-members see its entities. The usual choice for something a
	## player should not see their own copy of.
	HIDE_FROM_INSIDERS = NetwInterestLayer.Policy.HIDE_FROM_INSIDERS,
}

## Wire behavior when an interest layer stops admitting an entity.
##
## Leaving interest is not the same as ceasing to exist, which is why this is a
## choice rather than a rule.
enum LeavePolicy {
	## Tell the peer to destroy its copy. Frees memory, and the entity is
	## rebuilt from scratch if it comes back.
	DESPAWN = 0,
	## Keep the copy and stop updating it. It resumes from a stale pose when
	## interest returns, with no spawn cost.
	RETAIN = 1,
	## Neither. The game decides, through the layer's own handler.
	CUSTOM = 2,
}

## Local presentation behavior when an interest layer stops admitting.
##
## Orthogonal to [enum LeavePolicy]: that decides what the wire does, this
## decides what the player sees. A [constant LeavePolicy.RETAIN] entity is
## still present, so something must say whether it is drawn.
enum PerceptionPolicy {
	## Hide the retained copy. It stops being drawn where it was last seen.
	HIDE = 0,
	## Keep drawing the retained copy at its last known pose.
	SHOW = 1,
	## Neither. The game decides, through the layer's own handler.
	CUSTOM = 2,
}

## Mutable parameters of an entity's display runtime.
##
## Passed to [method display_set_param] and [method display_get_param]. Each
## one names a setting the display pump reads, the same setting the like-named
## member on [NetwDisplayHandle] is a view of, and the note after each name is
## the [Variant] type the setter expects.
enum DisplayParam {
	## [enum DisplayRole]. Which display path the entity uses. The only knob
	## most games ever set, and usually left at
	## [constant DisplayRole.AUTO].
	DISPLAY_PARAM_ROLE,
	## [enum PredictedMode]. How a predicted entity turns its body into a pose.
	DISPLAY_PARAM_PREDICTED_MODE,
	## [float] seconds. How long a predicted visual takes to absorb a
	## correction.
	DISPLAY_PARAM_PREDICTED_SMOOTH_TIME,
	## [float] seconds. How long
	## [constant PredictedMode.CHASE] takes to close the gap to the live body.
	DISPLAY_PARAM_CHASE_GLIDE_TIME,
	## [enum TimelineMode]. Where on the timeline a remote entity renders.
	DISPLAY_PARAM_TIMELINE_MODE,
	## [int] ticks. How far
	## [constant TimelineMode.FORECAST] may project past the newest sample
	## before it gives up and holds.
	DISPLAY_PARAM_MAX_FORECAST_TICKS,
	## [bool]. Whether the buffer may stretch playback to ride out jitter
	## instead of starving.
	DISPLAY_PARAM_SMART_DILATION,
	## [float] as a fraction of normal speed. The ceiling on that stretch.
	DISPLAY_PARAM_MAX_EXTRA_DILATION,
	## [float]. How quickly the buffer re-targets when the measured lag moves.
	## Higher follows the network faster and jitters more.
	DISPLAY_PARAM_LAG_ADAPT_RATE,
	## [float]. How much the buffer grows after a starvation, buying margin at
	## the cost of latency.
	DISPLAY_PARAM_STARVATION_GROWTH,
	## [float]. Smoothing applied to the buffer floor so it settles rather than
	## chases every sample.
	DISPLAY_PARAM_FLOOR_SMOOTHING,
	## [int] frames. How many starved frames pass before the runtime treats the
	## stream as genuinely stalled.
	DISPLAY_PARAM_STARVATION_GRACE_FRAMES,
	## [int] ticks. How often display tracing samples, or [code]0[/code] to
	## trace nothing.
	DISPLAY_PARAM_TRACE_INTERVAL,
	## [int] component id, or the [NodePath] itself. Names the node the smoothed
	## pose is written to. A component id resolves through
	## [method display_set_target_node]. A [NodePath] is stored as authored,
	## which is what lets a visual be named before the node exists.
	DISPLAY_PARAM_VISUAL_ROOT,
}

## Display strategy for an entity.
##
## Mirrors [enum NetwDisplayHandle.DisplayRole].
enum DisplayRole {
	## Resolve from [NetwEntity] control and
	## [member Node.multiplayer_authority]. The default, and correct for
	## almost every entity.
	AUTO = NetwDisplayHandle.DisplayRole.AUTO,
	## Interpolate replicated snapshots. What a peer uses for somebody else's
	## entity.
	REMOTE = NetwDisplayHandle.DisplayRole.REMOTE,
	## Follow the locally predicted body. What a player uses for their own
	## entity.
	PREDICTED = NetwDisplayHandle.DisplayRole.PREDICTED,
	## No smoothing. The node is left alone for the game to drive.
	DISABLED = NetwDisplayHandle.DisplayRole.DISABLED,
	## Sample the locally authored simulation each tick and play it back, so
	## the server sees its own world smoothed the same way.
	AUTHORITY = NetwDisplayHandle.DisplayRole.AUTHORITY,
}

## Local predicted-display filter.
##
## Mirrors [enum NetwDisplayHandle.PredictedMode].
enum PredictedMode {
	## Ease the visual toward the live predicted body every frame. Most
	## responsive, and shows correction ripple.
	CHASE = NetwDisplayHandle.PredictedMode.CHASE,
	## Interpolate between the previous and current tick samples. Steadier, and
	## one tick behind.
	BRACKETED = NetwDisplayHandle.PredictedMode.BRACKETED,
}

## Remote display timeline strategy.
##
## Mirrors [enum NetwDisplayHandle.TimelineMode].
enum TimelineMode {
	## Render behind the newest sample by the jitter buffer, always a delayed
	## truth. The default, and the only mode the server ever resolves.
	BUFFERED = NetwDisplayHandle.TimelineMode.BUFFERED,
	## Project along the last replicated velocity, trading buffer delay for
	## extrapolation error. Collision blind, so a body projected toward a wall
	## penetrates it until the bounce sample arrives. Prefer
	## [constant BUFFERED] for anything that meets world geometry.
	FORECAST = NetwDisplayHandle.TimelineMode.FORECAST,
}

## Mutable parameters of an entity's prediction declaration.
##
## Passed to [method predict_set_param] and [method predict_get_param]. Each
## writes the like-named knob on the entity's [NetwPredictionHandle], and the
## note after each name is the [Variant] type the setter expects. An archetype
## presets most of the rest, so a game usually sets one or two of these.
enum PredictParam {
	## [enum NetwPredict.Archetype]. The preset the entity starts from, such as
	## a character or a vehicle. Set this first, then adjust.
	PREDICT_PARAM_ARCHETYPE,
	## [enum NetwPredict.Schedule]. When in the tick the entity simulates.
	PREDICT_PARAM_SCHEDULE,
	## [enum NetwPredict.MissingInput]. What authority does for a tick whose
	## input never arrived.
	PREDICT_PARAM_MISSING_POLICY,
	## [enum NetwPredict.RecoveryPolicy]. What the owner does about a
	## divergence, from replaying it to merely reporting it.
	PREDICT_PARAM_RECOVERY_POLICY,
	## [enum NetwPredict.RestoreMode]. Whether a snap restores to the
	## acknowledged state or extrapolates it forward to now.
	PREDICT_PARAM_SNAP_RESTORE,
	## [enum NetwPredict.CorrectionMode]. The mechanism a recovery policy is
	## carried out by.
	PREDICT_PARAM_CORRECTION_MODE,
	## [float] in the property's own units. The fallback distance past which a
	## body is judged to hold nothing worth keeping, used for any property that
	## declares no threshold of its own.
	PREDICT_PARAM_TELEPORT_THRESHOLD,
	## [float]. The tolerance a divergence is judged against wherever the
	## transition was not declared exactly reproducible.
	PREDICT_PARAM_DIVERGENCE_EPSILON,
	## [enum NetwPredict.BreachResponse]. What happens when a client's claimed
	## state cannot be reconciled at all.
	PREDICT_PARAM_BREACH_RESPONSE,
	## [int] ticks. The cap on how far an extrapolated restore may project, so
	## a backlogged acknowledgement cannot launch the body down a long straight
	## line off a curved path.
	PREDICT_PARAM_MAX_RESTORE_TICKS,
	## [int] ticks. How long after a contact the entity stays out of partial
	## recovery, since a body still being disturbed cannot say which properties
	## are safe to leave behind.
	PREDICT_PARAM_COLLISION_COOLDOWN_TICKS,
	## [int]. The ceiling on transitions authority replays in one tick, which
	## bounds catch-up cost after a stall.
	PREDICT_PARAM_MAX_CONSUME_PER_TICK,
	## [int] ticks. How far behind authority may fall before it stops trying to
	## catch up transition by transition.
	PREDICT_PARAM_MAX_CONSUME_LAG_TICKS,
	## [int] ticks. The queue depth authority holds before replaying, the
	## [code]buffer[/code] of [method _predict_consume]. Zero by default, so the
	## ordinary verdict is simply whether anything is queued.
	PREDICT_PARAM_CONSUME_BUFFER_TICKS,
	## [int]. How many transitions the owner keeps for replay, which bounds how
	## far a correction can rewind.
	PREDICT_PARAM_REPLAY_BUFFER_DEPTH,
}

## Mutable parameters of a prediction island.
##
## An island is the set of entities that must be resimulated together because
## they interact. Passed to [method predict_island_set_param]. Membership
## itself is [method predict_island_add] and [method predict_island_remove].
enum IslandParam {
	## [bool]. Whether the island admits approximate agreement rather than
	## demanding reproducible transitions.
	ISLAND_PARAM_APPROXIMATE,
	## [bool]. Whether the island claims its transitions are exactly
	## reproducible, which is what entitles them to be judged without
	## tolerance.
	ISLAND_PARAM_EXACT_CLAIM,
	## [enum NetwPredict.Reconcile]. How the island resimulates after a
	## divergence.
	ISLAND_PARAM_RECONCILE,
	## [enum NetwPredict.Promotion]. What causes a nearby entity to be drawn
	## into the island.
	ISLAND_PARAM_PROMOTION,
	## [int]. The number of nearest members promoted. Clamped at zero.
	ISLAND_PARAM_PROMOTION_COUNT,
	## [float] meters. The distance that triggers promotion. Clamped at zero.
	ISLAND_PARAM_PROMOTION_METERS,
	## [enum NetwPredict.Pacing]. When the island's members open transitions.
	ISLAND_PARAM_PACING,
	## [int] ticks. The input delay a
	## [constant NetwPredict.Pacing.DELAY_CLOSED] group holds. Clamped at zero.
	ISLAND_PARAM_INPUT_DELAY,
}

## Mutable per-member parameters of a prediction island. Each is addressed as
## [code](entity, member, param)[/code] through
## [method predict_island_set_member_param] and
## [method predict_island_get_member_param].
enum MemberParam {
	## [enum NetwPredict.Fidelity]. How this member is simulated.
	MEMBER_PARAM_FIDELITY,
	## [Callable]. The substituted command producer for this member, or an
	## empty callable for none.
	MEMBER_PARAM_PREDICTOR,
}

## Sender policy for flat sync author gates.
##
## Decides which peer's writes to a property set are honored. Mirrors
## [enum NetwScriptModel.Policy].
enum WritePolicy {
	## Only the server. The default, and the safe choice.
	AUTHORITY = NetwScriptModel.Policy.AUTHORITY,
	## Only the peer holding [member NetwEntity.controller], which is how a
	## player drives their own entity.
	CONTROLLER = NetwScriptModel.Policy.CONTROLLER,
	## Any peer. Trusts every client, so use it only where a forged write
	## cannot matter.
	ANY_PEER = NetwScriptModel.Policy.ANY_PEER,
}

## Emitted when [param entity] becomes live on [param route].
signal entity_live(route: int, entity: NetwEntity)
## Emitted when [param entity] begins lingering on [param route].
signal entity_lingering(route: int, entity: NetwEntity)
## Emitted when [param route] becomes a tombstone.
signal entity_dead(route: int)

## Emitted once per table per applied wave, with the tick that wave carried.
##
## Coalesced at the end of carrier intake, so a commit that needed twenty two
## frames still emits once. Reading [method table_get_tick] in your own loop
## instead is equally first-class. On the authority, including a listen-server
## host, this fires locally at the tick boundary, so client code and host code
## are the same code.
signal table_received(table: RID, tick: int)

## Mutable properties of one field before its set is sealed.
##
## Passed to [method property_set_set_column_param]. Writable only until
## [method property_set_seal], because declaration order and field shape are wire
## order and wire shape. The note after each name is the [Variant] type the
## setter expects.
enum ColumnParam {
	## [enum NetwPropertySet.PropertyClass]. Whether the property causes the
	## simulation, is derived from it, or is only cosmetic. Everything else
	## here refines what this implies.
	COLUMN_PARAM_CLASS,
	## [float]. This property's own divergence tolerance, overriding the
	## entity's [constant PredictParam.PREDICT_PARAM_DIVERGENCE_EPSILON].
	COLUMN_PARAM_EPSILON,
	## [float]. This property's own teleport distance, overriding the entity's
	## [constant PredictParam.PREDICT_PARAM_TELEPORT_THRESHOLD]. Per property
	## because the errors have different units.
	COLUMN_PARAM_TELEPORT_AT,
	## [StringName]. Names the property whose value carries this one forward
	## when a restore extrapolates, usually a velocity for a position.
	COLUMN_PARAM_CARRY_CHANNEL,
	## [bool]. Restore this property only on a full teleport, leaving it on the
	## predicted body otherwise. For a value that re-converges on its own and
	## is worse off rewound.
	COLUMN_PARAM_TELEPORT_ONLY,
	## [bool]. Send this property only for reconciliation, never as ordinary
	## display state.
	COLUMN_PARAM_RECONCILE_ONLY,
	## [enum NetwPropertySet.Lane]. The delivery lane.
	## [constant NetwPropertySet.Lane.RETAINED] also marks the field watched, so a
	## change is noticed rather than sampled.
	COLUMN_PARAM_LANE,
	## [float]. How hard a partial recovery pulls this property toward
	## authority, the middle answer between withholding it and writing it
	## outright.
	COLUMN_PARAM_CONVERGE_STIFFNESS,
}

## Mutable axes of a property set before it is sealed.
##
## Passed to [method property_set_set_param]. Writable only until
## [method property_set_seal], which fixes the schema hash that peers agree on.
## The note after each name is the [Variant] type the setter expects.
enum PropertySetParam {
	## [bool]. Whether the set sends per-recipient masked deltas rather than
	## one shared frame.
	SET_PARAM_MASKED,
	## [int] ticks. How many past ticks travel in one frame, so a dropped
	## packet is covered by the next. Defaults to [code]2[/code] for
	## [constant RecordKind.INPUT] and [code]0[/code] elsewhere.
	SET_PARAM_WINDOW,
	## [enum NetwPropertySet.Audience]. Who receives the set.
	SET_PARAM_AUDIENCE,
	## [enum WritePolicy]. Whose writes to the set are honored.
	SET_PARAM_POLICY,
	## [enum NetwPropertySet.Trigger]. What causes the set to be sent.
	SET_PARAM_TRIGGER,
	## [enum NetwPropertySet.Cadence]. How often it is sent once triggered.
	SET_PARAM_CADENCE,
	## [enum NetwPropertySet.Stamp]. What tick information rides along, which is
	## what lets a late frame be placed rather than merely applied.
	SET_PARAM_STAMP,
	## [enum NetwPropertySet.Profile]. The bandwidth profile the set is compiled
	## against.
	SET_PARAM_PROFILE,
	## [enum NetwFrameEnvelope.Channel]. The carrier channel its frames travel
	## on.
	SET_PARAM_CHANNEL,
	## [bool]. Whether delivery is reliable. Reliable costs latency under loss,
	## so prefer [code]false[/code] for anything sent every tick.
	SET_PARAM_RELIABLE,
}

## The value shapes a schema column may hold.
##
## Passed to [method schema_add_column]. Mirrors
## [enum SchemaCore.ColumnType], which records what each value stores as and
## how wide it travels. Every value but [constant COLUMN_VARIANT] is fixed
## width, which is the premise of a table rather than a limit: it is what makes
## a column's bytes a memcpy and rows-per-frame computable. Send
## variable-length payloads through [method Netw.channel] keyed by the same
## route.
enum ColumnType {
	## 32-bit float.
	COLUMN_F32 = SchemaCore.ColumnType.F32,
	## 64-bit float, for a value whose precision is the caller's to decide.
	COLUMN_F64 = SchemaCore.ColumnType.F64,
	## 8-bit signed integer.
	COLUMN_I8 = SchemaCore.ColumnType.I8,
	## 8-bit unsigned integer.
	COLUMN_U8 = SchemaCore.ColumnType.U8,
	## 16-bit signed integer.
	COLUMN_I16 = SchemaCore.ColumnType.I16,
	## 16-bit unsigned integer.
	COLUMN_U16 = SchemaCore.ColumnType.U16,
	## 32-bit integer.
	COLUMN_I32 = SchemaCore.ColumnType.I32,
	## 64-bit integer.
	COLUMN_I64 = SchemaCore.ColumnType.I64,
	## One bit on the wire.
	COLUMN_BOOL = SchemaCore.ColumnType.BOOL,
	## [Vector2].
	COLUMN_VECTOR2 = SchemaCore.ColumnType.VECTOR2,
	## [Vector3].
	COLUMN_VECTOR3 = SchemaCore.ColumnType.VECTOR3,
	## [Vector4].
	COLUMN_VECTOR4 = SchemaCore.ColumnType.VECTOR4,
	## [Color].
	COLUMN_COLOR = SchemaCore.ColumnType.COLOR,
	## [Quaternion], distinct from [constant COLUMN_VECTOR4] because
	## smallest-three packing is a semantic rather than a width.
	COLUMN_QUATERNION = SchemaCore.ColumnType.QUATERNION,
	## A route, the link one table draws to another. Resolve one back to a
	## handle with [method entity_from_route].
	COLUMN_ENTITY = SchemaCore.ColumnType.ENTITY,
	## The self-describing tier, what a [String] or a [Dictionary] compiles to.
	## Legal in a schema and in a property set, refused by
	## [method table_create], because variable width has no memcpy and no
	## rows-per-frame budget.
	COLUMN_VARIANT = SchemaCore.ColumnType.VARIANT,
}

## Local axes of a table.
##
## Passed to [method table_set_param]. Local configuration rather than wire
## surface, so a param never enters [method table_get_wire_hash] and a later
## value is not a wire event. The note after the name is the [Variant] type the
## setter expects.
enum TableParam {
	## [bool]. Whether commits ride the reliable lane. A table that changes
	## rarely wants this, because it has no next commit to heal a lost
	## datagram with. A table published every tick does not.
	TABLE_PARAM_RELIABLE,
}

## Stable identifiers accepted by [method get_stat].
##
## [method stats_snapshot] exposes the same values under their lowercase names.
##
## Two shapes share this enum. A counter only ever rises and answers "how often
## has this happened", so what matters is its rate. A gauge is a census of
## right now and moves both ways. Every [code]DROPS[/code] and
## [code]SKIPS[/code] name counts something the pipeline deliberately refused,
## so a rising one is a diagnosis rather than a fault in itself.
enum Stat {
	## Counter. Frames addressed to a route this peer never saw issued.
	STAT_DROPS_UNKNOWN_ROUTE,
	## Counter. Frames for a route that is lingering or a tombstone. Ordinary
	## during a despawn, since frames already in flight have to land somewhere.
	STAT_DROPS_NOT_LIVE,
	## Counter. Frames for a live route whose node is gone.
	STAT_DROPS_NO_NODE,
	## Counter. Frames refused because the receive backlog was already full.
	STAT_DROPS_BACKLOG_LIMIT,
	## Counter. Frames whose node path could not be walked.
	STAT_DROPS_TRAVERSAL,
	## Counter. Frames naming a component id the entity does not have.
	STAT_DROPS_COMP_UNRESOLVED,
	## Counter. Outbound frames with no route to send them on.
	STAT_SENDS_DROPPED_UNROUTABLE,
	## Counter. Outbound frames for an entity that stopped being live before
	## the send.
	STAT_SENDS_DROPPED_NOT_LIVE,
	## Counter. Sync frames older than what has already been applied. Expected
	## under reordering, and the reason stamps travel.
	STAT_SYNC_DROPS_STALE,
	## Gauge. Derived property sets currently registered.
	STAT_DERIVED_SETS_ACTIVE,
	## Counter. Derived frames received.
	STAT_DERIVED_FRAMES_IN,
	## Counter. Derived frames naming a set this peer has not compiled.
	STAT_DROPS_DERIVED_NO_SET,
	## Counter. Derived frames from a peer not entitled to author them.
	STAT_DROPS_DERIVED_BAD_SENDER,
	## Counter. Derived frames whose schema hash disagrees with the local set.
	## A rising count means the two builds compiled different declarations.
	STAT_DROPS_DERIVED_SCHEMA,
	## Counter. Per-recipient declared-set row frames sent.
	STAT_ROW_FRAMES_OUT,
	## Counter. Row frames that carried every column rather than a diff, which
	## is what a peer gets before its baseline is established.
	STAT_ROW_FRAMES_FULL,
	## Counter. Row frames the installed encode stage refused. A pass that sent
	## nothing because its stage refused everything otherwise reads exactly like
	## a caught-up one.
	STAT_ROW_FRAMES_STAGE_REFUSED,
	## Counter. Rows whose gather refused, so no recipient was offered one.
	STAT_ROW_FRAMES_UNGATHERED,
	## Counter. Windowed row frames sent, one per recipient per pass.
	STAT_WINDOW_FRAMES_OUT,
	## Counter. Ticks those frames carried in total. It exceeds
	## [constant STAT_WINDOW_FRAMES_OUT] by the redundancy the window buys, so
	## the two together read as the cost of healing without a retransmit.
	STAT_WINDOW_SAMPLES_OUT,
	## Counter. Per-recipient retained row frames sent on the reliable lane.
	## Rises only when a retained column changed for that recipient, so a flat
	## count beside a rising [constant STAT_ROW_FRAMES_OUT] is the healthy
	## reading rather than a stalled lane.
	STAT_RETAINED_FRAMES_OUT,
	## Gauge. Sync property sets currently registered.
	STAT_SYNC_SETS_ACTIVE,
	## Counter. Full sync frames sent.
	STAT_SYNC_FRAMES_OUT,
	## Counter. Full sync frames received.
	STAT_SYNC_FRAMES_IN,
	## Counter. Delta sync frames sent.
	STAT_DELTA_FRAMES_OUT,
	## Counter. Delta sync frames received.
	STAT_DELTA_FRAMES_IN,
	## Counter. Sync frames naming a set this peer has not compiled.
	STAT_DROPS_SYNC_NO_SET,
	## Counter. Sync frames from a peer the set's [enum WritePolicy] does not
	## admit.
	STAT_DROPS_SYNC_BAD_SENDER,
	## Counter. Sync frames refused because the stream's baseline is no longer
	## trustworthy, so a delta cannot be applied against it.
	STAT_DROPS_SYNC_POISONED,
	## Counter. Sync frames carrying a flag bit this build does not know.
	STAT_DROPS_SYNC_UNKNOWN_FLAG,
	## Gauge. Entities armed for spawn but not yet sent.
	STAT_SPAWN_BOOK_ARMED,
	## Counter. Entities spawned out to peers.
	STAT_SPAWN_BOOK_SPAWNED,
	## Counter. Spawns received and materialized locally.
	STAT_SPAWN_BOOK_RECV,
	## Counter. Packets handed to the carrier.
	STAT_SENT_PACKETS,
	## Counter. Bytes handed to the carrier, framing included.
	STAT_SENT_BYTES,
	## Counter. Packets taken from the carrier.
	STAT_RECEIVED_PACKETS,
	## Counter. Bytes taken from the carrier, framing included.
	STAT_RECEIVED_BYTES,
	## Counter. State acknowledgements sent, which is how a peer's delta
	## baseline advances.
	STAT_STATE_ACKS_OUT,
	## Counter. State acknowledgements received.
	STAT_STATE_ACKS_IN,
	## Counter. Acknowledgements sent on their own because no state frame was
	## going out to carry them.
	STAT_STANDALONE_ACKS_OUT,
	## Gauge. Entities live but not yet fully brought up.
	STAT_PENDING_LIVE,
	## Gauge. Interest layers declared.
	STAT_INTEREST_LAYERS,
	## Gauge. Entities some layer is currently hiding from someone.
	STAT_INTEREST_ENTITIES_FILTERED,
	## Gauge. Admitted entity-to-viewer pairs in the committed matrix. The
	## closest thing to a cost of the current interest configuration.
	STAT_INTEREST_VISIBLE_EDGES,
	## Gauge. Entities awaiting the next recompute.
	STAT_INTEREST_DIRTY_ENTITIES,
	## Gauge. Interest changes queued for relay.
	STAT_INTEREST_RELAY_BACKLOG,
	## Counter. Entity visibility changes committed. Rising fast means entities
	## are flapping across a boundary, which costs a spawn or despawn each way.
	STAT_INTEREST_TRANSITIONS_TOTAL,
	## Gauge. Entities declared for prediction on this peer.
	STAT_PREDICT_ENTITIES,
	## Gauge. Entities keeping a rewind timeline.
	STAT_PREDICT_TIMELINES,
	## Counter. Corrections applied. The headline prediction health number.
	STAT_PREDICT_CORRECTIONS,
	## Gauge. The deepest replay queue seen, which bounds how far a correction
	## had to rewind.
	STAT_PREDICT_MAX_REPLAY_DEPTH,
	## Counter. Transitions authority replayed.
	STAT_PREDICT_CONSUMED,
	## Counter. Ticks whose input never arrived, resolved by the entity's
	## [constant PredictParam.PREDICT_PARAM_MISSING_POLICY].
	STAT_PREDICT_MISSING,
	## Gauge. Actions awaiting their acknowledgement.
	STAT_PREDICT_PENDING_ACTIONS,
	## Gauge. Predicted effects armed but not yet confirmed.
	STAT_PREDICT_EFFECTS_ARMED,
	## Counter. Times a prediction gate fell back to its default because the
	## declared path could not answer.
	STAT_PREDICT_GATE_FALLBACKS,
	## Gauge. Entities holding a display runtime.
	STAT_DISPLAY_RUNTIMES,
	## Gauge. Entities with nothing left to interpolate toward. A persistent
	## non-zero value means the buffer is too shallow for the current jitter.
	STAT_DISPLAY_STARVING,
	## Gauge. Entities whose display is idle because nothing is moving.
	STAT_DISPLAY_SLEEPING,
	## Gauge. Entities currently extrapolating under
	## [constant TimelineMode.FORECAST].
	STAT_DISPLAY_PROJECTING,
	## Counter. Times a display jumped rather than smoothed.
	STAT_DISPLAY_SNAPS,
	## Gauge. The largest gap seen between a display and the newest sample it
	## has, in ticks.
	STAT_DISPLAY_MAX_DISPLAY_LAG,
	## Gauge. The furthest any forecast has been projected past its newest
	## sample, in ticks. Compare against
	## [constant DisplayParam.DISPLAY_PARAM_MAX_FORECAST_TICKS].
	STAT_DISPLAY_MAX_FORECAST_AGE,
	## Counter. Gate verdicts of [constant ERR_DOES_NOT_EXIST].
	STAT_VERDICT_DOES_NOT_EXIST,
	## Counter. Gate verdicts of [constant ERR_SKIP].
	STAT_VERDICT_SKIP,
	## Counter. Gate verdicts of [constant ERR_UNAVAILABLE].
	STAT_VERDICT_UNAVAILABLE,
	## Counter. Gate verdicts of [constant ERR_UNAUTHORIZED]. The one to watch:
	## it counts peers asking for what they are not entitled to.
	STAT_VERDICT_UNAUTHORIZED,
	## Counter. Gate verdicts of [constant ERR_INVALID_DATA].
	STAT_VERDICT_INVALID_DATA,
	## Counter. Gate verdicts of [constant ERR_BUSY].
	STAT_VERDICT_BUSY,
	## Counter. Table frames naming a wire id this peer has no table for.
	STAT_DROPS_TABLE_UNKNOWN,
	## Counter. Table frames whose schema hash disagrees with the local
	## declaration. A rising count means the two builds sealed different
	## schemas, which a mid-session script reload is enough to cause.
	STAT_DROPS_TABLE_SCHEMA,
	## Counter. Table frames carrying a flag bit this build does not implement.
	STAT_DROPS_TABLE_UNKNOWN_FLAG,
	## Counter. Table frames whose routes or columns ran out of bytes.
	STAT_DROPS_TABLE_TRUNCATED,
	## Counter. Table frames from a peer that is not the authority.
	STAT_DROPS_TABLE_BAD_SENDER,
	## Counter. Table frames older than what the table has already applied,
	## beyond the decoder's reorder window. Expected under reordering, and the
	## reason the tick rides every frame.
	STAT_TABLE_DROPS_STALE,
	## Counter. Rows naming a route this peer has tombstoned. The count a
	## resurrection attempt would show up as.
	STAT_TABLE_DROPS_TOMBSTONE,
	## Counter. Rows older than the row they would overwrite, or older than the
	## removal that already took that route away.
	STAT_TABLE_DROPS_STALE_ROW,
	## Counter. Send-pump skips for a component whose node is no longer valid.
	STAT_SYNC_PUMP_SKIPS_INVALID_NODE,
	## Counter. Send-pump skips for a node belonging to no entity.
	STAT_SYNC_PUMP_SKIPS_NO_ENTITY,
	## Counter. Send-pump skips for an entity holding no wire route.
	STAT_SYNC_PUMP_SKIPS_NO_ROUTE,
	## Counter. Send-pump skips where this peer is not the author. The ordinary
	## reason a client sends nothing for somebody else's entity.
	STAT_SYNC_PUMP_SKIPS_NOT_AUTHOR,
	## Counter. Send-pump skips where interest admitted the entity to nobody.
	STAT_SYNC_PUMP_SKIPS_NO_RECIPIENTS,
	## Counter. Dirty-set entries skipped because the entity was gone by the
	## time the recompute reached it.
	STAT_INTEREST_VANISHED_DIRTY_SKIPS,
	## Counter. Replays a declared group ran together, one per tick whose floor
	## moved rather than one per authoritative row that arrived.
	STAT_JOINT_PASSES,
	## Gauge. Members the last joint pass stepped, lingering ones included.
	STAT_JOINT_MEMBERS,
	## Counter. Cells a joint pass drove from a relayed command.
	STAT_JOINT_CELLS_RELAYED,
	## Counter. Cells a joint pass drove from a local predictor standing in for
	## an author whose command never arrived.
	STAT_JOINT_CELLS_SUBSTITUTED,
	## Counter. Passes that found a basis older than the history that could
	## replay it and snapped to the newest recorded state instead.
	STAT_JOINT_HEAL_SNAPS,
	## Gauge. Departed members still stepped because the group's replay horizon
	## has not passed their departure.
	STAT_JOINT_LINGER_HELD,
}

const _STAT_NAMES: Array[StringName] = [
	&"drops_unknown_route",
	&"drops_not_live",
	&"drops_no_node",
	&"drops_backlog_limit",
	&"drops_traversal",
	&"drops_comp_unresolved",
	&"sends_dropped_unroutable",
	&"sends_dropped_not_live",
	&"sync_drops_stale",
	&"derived_sets_active",
	&"derived_frames_in",
	&"drops_derived_no_set",
	&"drops_derived_bad_sender",
	&"drops_derived_schema",
	&"row_frames_out",
	&"row_frames_full",
	&"row_frames_stage_refused",
	&"row_frames_ungathered",
	&"retained_frames_out",
	&"window_frames_out",
	&"window_samples_out",
	&"sync_sets_active",
	&"sync_frames_out",
	&"sync_frames_in",
	&"delta_frames_out",
	&"delta_frames_in",
	&"drops_sync_no_set",
	&"drops_sync_bad_sender",
	&"drops_sync_poisoned",
	&"drops_sync_unknown_flag",
	&"spawn_book_armed",
	&"spawn_book_spawned",
	&"spawn_book_recv",
	&"sent_packets",
	&"sent_bytes",
	&"received_packets",
	&"received_bytes",
	&"state_acks_out",
	&"state_acks_in",
	&"standalone_acks_out",
	&"pending_live",
	&"interest_layers",
	&"interest_entities_filtered",
	&"interest_visible_edges",
	&"interest_dirty_entities",
	&"interest_relay_backlog",
	&"interest_transitions_total",
	&"predict_entities",
	&"predict_timelines",
	&"predict_corrections",
	&"predict_max_replay_depth",
	&"predict_consumed",
	&"predict_missing",
	&"predict_pending_actions",
	&"predict_effects_armed",
	&"predict_gate_fallbacks",
	&"display_runtimes",
	&"display_starving",
	&"display_sleeping",
	&"display_projecting",
	&"display_snaps",
	&"display_max_display_lag",
	&"display_max_forecast_age",
	&"verdict_does_not_exist",
	&"verdict_skip",
	&"verdict_unavailable",
	&"verdict_unauthorized",
	&"verdict_invalid_data",
	&"verdict_busy",
	&"drops_table_unknown",
	&"drops_table_schema",
	&"drops_table_unknown_flag",
	&"drops_table_truncated",
	&"drops_table_bad_sender",
	&"table_drops_stale",
	&"table_drops_tombstone",
	&"table_drops_stale_row",
	&"sync_pump_skips_invalid_node",
	&"sync_pump_skips_no_entity",
	&"sync_pump_skips_no_route",
	&"sync_pump_skips_not_author",
	&"sync_pump_skips_no_recipients",
	&"interest_vanished_dirty_skips",
	&"joint_passes",
	&"joint_members",
	&"joint_cells_relayed",
	&"joint_cells_substituted",
	&"joint_heal_snaps",
	&"joint_linger_held",
]

var _schema_core: SchemaCore

var _table_core: TableCore

var _property_sets := NetwHandleLedger.new()
var _property_set_records: Dictionary[RID, NetwPropertySet] = { }
var _property_set_schema: Dictionary[RID, RID] = { }
var _property_set_cache: Dictionary[Script, Dictionary] = { }
var _entity_property_sets: Dictionary[RID, Dictionary] = { }

## Handles application-defined authentication packets after Networked
## classifies its reserved protocol frames.
##
## Networked always owns [member SceneMultiplayer.auth_callback] on
## [member inner] so same-port probes remain isolated from session peers. Set
## this callback instead of reaching through [member inner]. Probe packets are
## consumed internally and every other packet is forwarded unchanged. When
## set, this callback takes precedence over the configured [NetwAuthFlow].
var auth_callback: Callable = Callable():
	set(value):
		auth_callback = value
		if _auth:
			_auth.set_application_auth_callback(value)

## Seconds an authenticating peer may remain pending before Godot disconnects
## it. Mirrors [member SceneMultiplayer.auth_timeout].
var auth_timeout: float:
	get:
		return inner.auth_timeout
	set(value):
		inner.auth_timeout = value

## Root path used by the wrapped replicator for relative node addressing.
## Mirrors [member SceneMultiplayer.root_path].
var root_path: NodePath:
	get:
		return inner.root_path
	set(value):
		inner.root_path = value

## Whether RPC payloads may decode serialized objects.
## Mirrors [member SceneMultiplayer.allow_object_decoding].
var allow_object_decoding: bool:
	get:
		return inner.allow_object_decoding
	set(value):
		inner.allow_object_decoding = value

## Whether new peer connections are rejected.
## Mirrors [member SceneMultiplayer.refuse_new_connections].
var refuse_new_connections: bool:
	get:
		return inner.refuse_new_connections
	set(value):
		inner.refuse_new_connections = value

## Whether the server relays peer packets between clients.
## Mirrors [member SceneMultiplayer.server_relay].
var server_relay: bool:
	get:
		return inner.server_relay
	set(value):
		inner.server_relay = value

## Maximum reliable replication packet size in bytes.
## Mirrors [member SceneMultiplayer.max_sync_packet_size].
var max_sync_packet_size: int:
	get:
		return inner.max_sync_packet_size
	set(value):
		inner.max_sync_packet_size = value

## Maximum unreliable replication packet size in bytes.
## Mirrors [member SceneMultiplayer.max_delta_packet_size].
var max_delta_packet_size: int:
	get:
		return inner.max_delta_packet_size
	set(value):
		inner.max_delta_packet_size = value

## Emitted when Godot begins authenticating [param peer_id]. Application auth
## code can answer through [method peer_send_auth] and [method peer_complete_auth].
signal peer_authenticating(peer_id: int)

## Emitted when Godot rejects or times out [param peer_id] during
## authentication.
signal peer_authentication_failed(peer_id: int)

## Emitted when a [Node] registers as a session service through
## [method register_service], so the connect kit and game code can react to a
## late-added service instead of scanning the tree for it.
signal service_registered(service: Node)

## Emitted when a service leaves through [method unregister_service].
signal service_unregistered(service: Node)

# Per-session service registry. Lives on the API so any node reaches it through
# node.multiplayer with per-branch scoping for free, and a bare API with no tree
# still answers get_service. See NetwService for the sealed registration base.
var _services: NetwServiceRegistry

# Which config class installs which service, written by each service's wiring
# at construction so no one dispatcher knows every service.
var _install_book := NetwServiceInstallBook.new()

var _layer_drivers: Dictionary[RID, Callable] = { }
var _layer_monitor_hooks: Dictionary[RID, Array] = { }

# Scene facets scene_declare wrote for entities with no record yet, spent by
# the node bind that builds one.
var _pending_scene_facets: Dictionary[RID, bool] = { }

# Display writer bindings that do not belong to the compatibility handle.
var _display_declarations: Dictionary[RID, Dictionary] = { }
var _display_target_items: Dictionary[RID, RID] = { }
var _display_callbacks: Dictionary[RID, Callable] = { }
var _display_pump_timing: NetwDisplayTiming

# Current shell-owned stage inputs exposed to independent virtual decisions.
var _sync_encoder: Callable
var _sync_encode_meta: Dictionary = { }
var _sync_decoder: Callable
var _set_gatherer: Callable
var _set_applier: Callable
var _spawn_reconcile_rows: Array[Dictionary] = []
var _spawn_reconcile_peers := PackedInt32Array()
# The staged plan, or null when the seam did not answer with one.
var _spawn_reconcile_plan: Variant = null
var _spawn_constructor: Callable

# The connected-peer roster and per-peer participant handles. They live on the
# API so a bare session with no tree still answers get_participant and
# get_peer_context. Participants are keyed by peer id and read their accepted
# join, identity, and context back through this same API.
var _roster: SessionRoster

# Weak registry of every constructed extension, backing live_sessions().
# Weakrefs because a strong static list would keep disposed sessions alive.
static var _session_refs: Array[WeakRef] = []

# Cache backing for [member root], and the path it was resolved for so a
# root_path change (mount, an inner swap) self-invalidates without a reset hook.
var _root: Node
var _root_path: NodePath

## The node the replicator roots relative addressing at, resolved from
## [member inner]'s [member SceneMultiplayer.root_path] the same way the native
## replicator resolves its own. In every shipped configuration this is the
## owning [MultiplayerTree], but it stays valid when no tree owns the API. The
## resolved node is cached and re-resolves only if it was freed or
## [member SceneMultiplayer.root_path] changed.
var root: Node:
	get:
		if is_instance_valid(_root) and inner.root_path == _root_path:
			return _root
		_root_path = inner.root_path
		var scene_tree := Engine.get_main_loop() as SceneTree
		_root = scene_tree.root.get_node_or_null(_root_path) if scene_tree else null
		return _root


# The reading behind NetwMultiplayerCore.session_root, which takes the tree walk
# as a Callable because the session itself never touches a SceneTree.
func _session_root() -> Node:
	return root


# The wait behind NetwMultiplayerCore.scene_request_open, taken as a Callable
# because a deadline is the SceneTree's wall clock and the session itself never
# touches a SceneTree. A tree-less run arms nothing, and the request it opened
# then waits for authority with no deadline behind it.
func _arm_request_deadline(request_id: int, deadline: float) -> void:
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree == null:
		return
	_connect_once(
		scene_tree.create_timer(deadline).timeout,
		_native_core.scene_request_expire.bind(request_id),
	)


# The reading behind NetwMultiplayerCore.authored_desired_role. It answers the
# config plane LIVE, which the session machine's own desired_role does not:
# that one is a snapshot pushed at the edges that resolve a role, and a game
# re-authors the config between two of them.
func _authored_desired_role() -> int:
	# The config authors this in the public enum, which mirrors Role exactly.
	return int(_session_config.desired_role)


func _init(inner_api: SceneMultiplayer = null) -> void:
	inner = inner_api if inner_api else SceneMultiplayer.new()
	_native_core = NetwMultiplayerCore.new()
	_native_core.set_inner(inner)
	_native_core.set_session_root(_session_root)
	_native_core.set_request_deadline_arm(_arm_request_deadline)
	_roster = SessionRoster.new(_native_core)
	_native_core.set_identity_reader(_read_peer_identity)
	_schema_core = _native_core.schema_core
	_table_core = _native_core.table_core
	_native_core.set_multiplayer_peer(inner.multiplayer_peer)
	var adopted_auth_callback := inner.auth_callback
	_replication = ReplicationCore.new(self)
	# Installed the moment the plane exists, because everything constructed
	# below can already dispatch a frame or register a constructor, and the
	# native core refuses both while it has nobody to reach.
	_native_core.set_replication_plane(_replication)
	_native_core.set_spawn_pipeline(_replication._spawn_pipeline)
	_native_core.set_sync_pipeline(_replication._sync_pipeline)
	# Which marked properties ride a spawn is a declaration-registry question,
	# so the session is told how to answer it once rather than reaching into
	# the registry per instantiation.
	_native_core.set_spawn_state_gather(_replication.spawn_state_of)
	_rpc_core = RpcCore.new(self, _replication)
	_clock_handle = _native_core.get_clock_handle()
	# The calibration protocol answers on every peer, including a server that
	# never mounts a MultiplayerClock, so the session registers it rather than
	# the configurator node.
	_replication.register_protocol(
		NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE,
		_native_core.clock_receive_handshake,
	)
	_replication.register_protocol(
		NetwFrameEnvelope.Channel.CLOCK_HANDSHAKE_REPLY,
		_native_core.clock_receive_handshake_reply,
	)
	_replication.register_protocol(
		NetwFrameEnvelope.Channel.CLOCK_PING,
		_native_core.clock_receive_ping,
	)
	_replication.register_protocol(
		NetwFrameEnvelope.Channel.CLOCK_PONG,
		_native_core.clock_receive_pong,
	)
	_install_book.register_service(
		&"NetwClockConfig",
		_install_clock_service,
		_uninstall_clock_service,
	)
	_embedding = EmbeddingCore.new(self)
	_embedding_handle = NetwEmbeddingHandle.new(_embedding)
	_registry = _native_core.lagcomp_core
	_prediction_pool = _native_core.prediction_engine
	_runner._service = self
	_replication.register_protocol(
		NetwFrameEnvelope.Channel.LAGCOMP_DENY,
		_handle_deny,
	)
	_install_book.register_service(
		&"NetwLagCompensationConfig",
		_install_lagcomp_service,
		_uninstall_lagcomp_service,
	)
	_persistence = PersistenceCore.new(self)
	_native_core.set_display_spec_reader(_display_specs)
	_native_core.set_display_lane(_display_lane)
	_native_core.set_display_sync_intervals(_display_sync_intervals)
	_native_core.set_display_authors_streams(_display_authors_streams)
	_native_core.set_display_role_facts(_display_role_facts)
	_native_core.set_display_chase_hook(_display_chase_hook)
	_native_core.set_display_chase_clamp(_display_chase_clamp)
	_native_core.display_bind_session()
	_native_core.set_interest_flush(_flush_interest_visibility)
	_native_core.set_interest_compat_refresh(_refresh_interest_intents)
	_native_core.set_interest_awareness_send(_send_interest_awareness)
	_replication.register_protocol(
		NetwFrameEnvelope.Channel.INTEREST_AWARENESS,
		_native_core.interest_receive_awareness,
	)
	_connect_once(peer_connected, _native_core.interest_peer_connected)
	_connect_once(peer_disconnected, _native_core.interest_peer_disconnected)
	_connect_once(session_ended, _native_core.interest_session_ended)
	_native_core.set_interest_visibility_sweep(
		_replication._spawn_pipeline.schedule_visibility_sweep
	)
	_session_install()
	_native_core.set_desired_role_reader(_authored_desired_role)
	_install_book.register_service(
		&"NetwSessionConfig",
		_install_session_service,
		_uninstall_session_service,
	)
	_scene_install()
	_native_core.set_scene_refresh(_scene_refresh_current)
	_native_core.set_scene_path_reader(_scene_core.declared_scene_path)
	_install_book.register_service(
		&"NetwSceneConfig",
		_install_scene_service,
		_uninstall_scene_service,
	)
	_services = NetwServiceRegistry.new(self)
	_connect_once(_native_core.local_scene_changed, local_scene_changed.emit)
	_connect_once(_native_core.scene_live, scene_live.emit)
	_connect_once(_native_core.state_changed, state_changed.emit)
	_connect_once(_native_core.session_entered, session_entered.emit)
	_connect_once(_native_core.session_ended, session_ended.emit)
	_connect_once(_native_core.session_ended, _on_session_ended)
	# The tree write is the first subscriber rather than part of the decision, so
	# the session announces a pause once and everything that reacts to it,
	# including this addon, reads the same announcement.
	_connect_once(_native_core.tree_paused, _on_tree_paused)
	_connect_once(_native_core.tree_unpaused, _on_tree_unpaused)
	_connect_once(_native_core.tree_paused, tree_paused.emit)
	_connect_once(_native_core.tree_unpaused, tree_unpaused.emit)
	_connect_once(_native_core.service_registered, service_registered.emit)
	_connect_once(
		_native_core.service_unregistered,
		service_unregistered.emit,
	)
	auth_callback = adopted_auth_callback
	# Dead routes drop their unreliable-property sequence records so they never
	# outlive the entity they track.
	_connect_once(_native_core.entity_dead, _replication.clear_route)
	_connect_once(
		_native_core.local_player_changed,
		local_player_changed.emit,
	)
	_connect_once(_native_core.participant_joined, participant_joined.emit)
	_connect_once(
		_native_core.local_participant_joined,
		local_participant_joined.emit,
	)
	# local_player follows the liveness bus so a root-installed session with no
	# owning MultiplayerTree still tracks the represented entity.
	_connect_once(session_ended, _on_liveness_session_ended)
	_connect_once(_native_core.entity_live, _on_liveness_entity_live)
	_connect_once(_native_core.entity_lingering, _on_liveness_entity_lingering)
	_connect_once(_native_core.entity_dead, _on_liveness_entity_dead)
	# The tick pump binds once at construction. The signal lives on the session,
	# so an inert clock simply never fires it, and the pump binds ahead of the
	# public after_tick so a subscriber reads a tick whose frames have flushed.
	_connect_once(_native_core.after_tick, _on_clock_tick)
	_connect_once(_native_core.before_tick, before_tick.emit)
	_connect_once(_native_core.on_tick, on_tick.emit)
	_connect_once(_native_core.after_tick, after_tick.emit)
	_connect_once(_native_core.before_tick_loop, before_tick_loop.emit)
	_connect_once(_native_core.after_tick_loop, after_tick_loop.emit)
	_connect_once(_native_core.clock_synchronized, clock_synchronized.emit)
	_connect_once(
		_native_core.display_offset_insufficient,
		display_offset_insufficient.emit,
	)
	_connect_once(_native_core.stability_changed, stability_changed.emit)
	_connect_once(_native_core.poll_started, poll_started.emit)
	_connect_once(_native_core.table_received, table_received.emit)
	_connect_once(_native_core.peer_packet, peer_packet.emit)
	_connect_once(_native_core.kicked, kicked.emit)
	_connect_once(_native_core.server_disconnecting, server_disconnecting.emit)
	_connect_once(_native_core.kick_requested, kick_requested.emit)
	_connect_once(
		_native_core.disconnect_requested,
		disconnect_requested.emit,
	)
	_bind_inner_signals()
	# A roster row exists for every connected peer, joined or not, so the roster
	# is native peer truth the join frame only enriches. Retiring the row rides
	# the same relayed peer_disconnected the freshness books clear on.
	_connect_once(peer_connected, _ensure_participant_row)
	# Per-peer teardown rides this extension's own relayed peer_disconnected, so a
	# bare API with no owning tree still clears freshness books and RPC state.
	_connect_once(peer_disconnected, _clear_disconnected_peer)
	# A peer that connects after a table already published is owed the same
	# reliable heal the spawn book replays, for the same reason.
	_connect_once(peer_connected, _replication.replay_tables)
	_adopt_schema_declarations()
	_adopt_table_declarations()
	_session_refs.append(weakref(self))


## True while this extension is the installed [MultiplayerAPI].
##
## A tree-scoped session is active while its owning [MultiplayerTree] holds it as
## multiplayer. A root-installed session (no tree, see
## [method install_as_default]) is active while it is the [SceneTree] default.
func is_active() -> bool:
	var t := root as MultiplayerTree
	if t != null and is_instance_valid(t):
		return t.multiplayer == self
	var loop := Engine.get_main_loop() as SceneTree
	return loop != null and loop.get_multiplayer() == self


## Creates the stock api or the configured implementation.
##
## [param implementation] overrides [constant MULTIPLAYER_SCRIPT_SETTING]. An
## assigned script must extend [NetwMultiplayer]. An empty setting keeps the
## stock path, which constructs [NetwMultiplayer] itself and pays no dispatch
## for the overridable stages.
static func make(
		inner_api: SceneMultiplayer = null,
		implementation: Script = null,
) -> NetwMultiplayer:
	var selected := implementation
	if selected == null:
		var configured: Variant = ProjectSettings.get_setting(
			MULTIPLAYER_SCRIPT_SETTING,
			null,
		)
		if configured is Script:
			selected = configured
		elif configured is String and not configured.is_empty():
			selected = load(configured) as Script
	if selected == null:
		selected = law_extension_script()
	if selected == null:
		return NetwMultiplayer.new(inner_api)
	assert(
		is_extension_script(selected),
		"NetwMultiplayer.make requires a script extending NetwMultiplayer",
	)
	if not is_extension_script(selected):
		return null
	var made: Variant = selected.new(inner_api)
	assert(made is NetwMultiplayer)
	return made as NetwMultiplayer


## The implementation this whole run certifies, named by
## [constant LAW_EXTENSION_ENV], or [code]null[/code] when the run certifies the
## stock one.
##
## The one place the environment is read. Conformance is a property of a run
## rather than of a call, so the answer is resolved once and reused: a run that
## could change implementations halfway through and still read green would be
## certifying nothing. A path naming something unloadable, or a script that does
## not extend [NetwMultiplayer], pushes an error rather than falling back
## quietly, because a certification run that silently certified only the stock
## engine reports exactly what a passing run reports.
## [codeblock]
## NETW_LAW_EXTENSION=res://my_game/my_policy.gd godot --path . -s \
##     res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless \
##     --ignoreHeadlessMode -c -a res://tests
## [/codeblock]
## Whether this run names a law extension at all.
##
## Separate from [method law_extension_script] because "no extension named" and
## "an extension named that could not be loaded" are different runs. The second
## is still a certification run and must not be mistaken for a stock one.
static func law_extension_named() -> bool:
	return not OS.get_environment(LAW_EXTENSION_ENV).is_empty()


static func law_extension_script() -> Script:
	if _law_extension_resolved:
		return _law_extension_script
	_law_extension_resolved = true
	var path := OS.get_environment(LAW_EXTENSION_ENV)
	if path.is_empty():
		return null
	var script := load(path) as Script
	if script == null:
		push_error("%s names '%s', which is not a script" % [LAW_EXTENSION_ENV, path])
		return null
	if not is_extension_script(script):
		push_error(
			"%s names '%s', which does not extend NetwMultiplayer"
			% [LAW_EXTENSION_ENV, path],
		)
		return null
	_law_extension_script = script
	return _law_extension_script


## Returns whether [param script] extends [NetwMultiplayer].
##
## [method make] refuses anything else, so a script named by
## [constant MULTIPLAYER_SCRIPT_SETTING] or [member MultiplayerTree.api_script]
## always inherits the stages it is replacing.
static func is_extension_script(script: Script) -> bool:
	var current := script
	while current:
		if current.get_global_name() == &"NetwMultiplayer":
			return true
		current = current.get_base_script()
	return false


## Whether this session replaces the decision [param seam] names, rather than
## inheriting the one this class ships.
##
## A prediction decision is answered natively wherever nobody has replaced it,
## so this is what a native answer defers to. It asks about the SEAM and not
## about the run: a certification arm and a game that overrode
## [method _predict_recover] are the same fact here, and a run that installed
## an extension which overrode something else is not.
##
## The answer cannot change for a live session, because a script's method
## table does not, so it is resolved once per seam.
func overrides_seam(seam: StringName) -> bool:
	return _native_core.overrides_seam(
		get_script() as Script,
		&"NetwMultiplayer",
		seam,
	)


## Installs a fresh [NetwMultiplayer] as [param scene_tree]'s default
## [MultiplayerAPI] and returns it, so every node's [member Node.multiplayer]
## resolves to the session and it survives a native scene change.
##
## The session has no [MultiplayerTree]. Spawns anchor at [code]/root[/code], a
## sibling of [member SceneTree.current_scene], so a
## [method SceneTree.change_scene_to_file] that frees the scene leaves the
## session and its replicated content standing. This is the one-session shipping
## mode. Multiplexed processes (the harness, the tiling rig) keep the stock
## default and scope each session to a [MultiplayerTree] subtree instead.
## [codeblock]
## # once at startup (the networked/install_as_default setting does this):
## NetwMultiplayer.install_as_default(get_tree())
## multiplayer.multiplayer_peer = peer   # multiplayer is now the session
## [/codeblock]
static func install_as_default(
		scene_tree: SceneTree,
		implementation: Script = null,
) -> NetwMultiplayer:
	var api := make(null, implementation)
	if api == null:
		return null
	api.inner.root_path = ^"/root"
	scene_tree.set_multiplayer(api)
	return api


## Restores a stock [SceneMultiplayer] as [param scene_tree]'s default, undoing
## [method install_as_default]. The installed session is closed and disposed,
## so the provider releases its complete owned graph in one teardown call.
static func uninstall_default(scene_tree: SceneTree) -> void:
	var installed := scene_tree.get_multiplayer() as NetwMultiplayer
	scene_tree.set_multiplayer(SceneMultiplayer.new())
	if installed == null:
		return
	if installed.has_multiplayer_peer():
		installed.multiplayer_peer.close()
		installed.multiplayer_peer = null
	installed.embedding.dispose()


## Returns every [NetwMultiplayer] currently passing [method is_active], in
## construction order. [method Netw.replicate] resolves its session through
## this list when exactly one session is active. Multi-session hosts (the
## test harness, the debugger's tiled participants) address a session
## directly through [method Netw.of] instead.
static func live_sessions() -> Array[NetwMultiplayer]:
	var out: Array[NetwMultiplayer] = []
	var kept: Array[WeakRef] = []
	for ref in _session_refs:
		var api := ref.get_ref() as NetwMultiplayer
		if api == null:
			continue
		kept.append(ref)
		if api.is_active():
			out.append(api)
	_session_refs = kept
	return out


## Returns the [NetwMultiplayer] installed on [param node]'s branch, or
## [code]null[/code] off-tree, outside a [MultiplayerTree] branch, or before
## the tree has installed its API.
static func of(node: Node) -> NetwMultiplayer:
	if node == null or not node.is_inside_tree():
		return null
	return node.multiplayer as NetwMultiplayer


## Returns the [NetwMultiplayerCore] answering for [param node]'s session, or
## [code]null[/code] when there is none. Quiet by design, so a detached node and
## an offline rig degrade to unroutable rather than raising.
##
## Falls back to the enclosing [MultiplayerTree] when a node's own
## [member Node.multiplayer] is not yet this api. A root install has no tree, so
## the api-first resolve is what keeps such a session answering.
static func core_of(node: Node) -> NetwMultiplayerCore:
	var api := of(node)
	if api == null:
		var mt := MultiplayerTree.resolve(node)
		api = mt.api if mt else null
	return api._native_core if api else null


func _bind_inner_signals() -> void:
	_connect_once(inner.peer_packet, _on_inner_peer_packet)
	_connect_once(inner.peer_connected, _on_inner_peer_connected)
	_connect_once(inner.peer_disconnected, _on_inner_peer_disconnected)
	_connect_once(inner.connected_to_server, _on_inner_connected_to_server)
	_connect_once(inner.connection_failed, _on_inner_connection_failed)
	_connect_once(inner.server_disconnected, _on_inner_server_disconnected)
	_connect_once(_native_core.peer_authenticating, peer_authenticating.emit)
	_connect_once(
		_native_core.peer_authentication_failed,
		peer_authentication_failed.emit,
	)


# Connects a signal once and makes a failed local wiring invariant visible.
func _connect_once(
		source: Signal,
		callback: Callable,
		flags: int = 0,
) -> void:
	var wired := _native_core.connect_once(source, callback, flags)
	assert(wired, "NetwMultiplayer signal connection failed")


func _unbind_inner_signals() -> void:
	if inner.peer_packet.is_connected(_on_inner_peer_packet):
		inner.peer_packet.disconnect(_on_inner_peer_packet)
	if inner.peer_connected.is_connected(_on_inner_peer_connected):
		inner.peer_connected.disconnect(_on_inner_peer_connected)
	if inner.peer_disconnected.is_connected(_on_inner_peer_disconnected):
		inner.peer_disconnected.disconnect(_on_inner_peer_disconnected)
	if inner.connected_to_server.is_connected(_on_inner_connected_to_server):
		inner.connected_to_server.disconnect(_on_inner_connected_to_server)
	if inner.connection_failed.is_connected(_on_inner_connection_failed):
		inner.connection_failed.disconnect(_on_inner_connection_failed)
	if inner.server_disconnected.is_connected(_on_inner_server_disconnected):
		inner.server_disconnected.disconnect(_on_inner_server_disconnected)
	if _native_core.peer_authenticating.is_connected(peer_authenticating.emit):
		_native_core.peer_authenticating.disconnect(peer_authenticating.emit)
	if _native_core.peer_authentication_failed.is_connected(
		peer_authentication_failed.emit,
	):
		_native_core.peer_authentication_failed.disconnect(
			peer_authentication_failed.emit,
		)


# Re-emits [member inner]'s connection-lifecycle signals on this extension, the
# standard [MultiplayerAPIExtension] wrapper pattern, so consumers that bind to
# [code]tree.api.peer_connected[/code] and friends never need to reach into
# [member inner] directly.
func _on_inner_peer_connected(id: int) -> void:
	_report_peer(NetwMultiplayerCore.PEER_JOINED, id)
	peer_connected.emit(id)


func _on_inner_peer_disconnected(id: int) -> void:
	_report_peer(NetwMultiplayerCore.PEER_LEFT, id)
	peer_disconnected.emit(id)


func _on_inner_connected_to_server() -> void:
	connected_to_server.emit()


func _on_inner_connection_failed() -> void:
	connection_failed.emit()


func _on_inner_server_disconnected() -> void:
	server_disconnected.emit()


# Reports one peer edge to the event plane, which is the observation surface
# the signal above is a convenience projection of.
func _report_peer(event: int, peer_id: int) -> void:
	report_event(event, 0, { peer = peer_id }, peer_id)


## Watches for events and returns the row id, or [code]-1[/code] when the row
## names anything outside the closed vocabulary.
##
## [param events] are [NetwMultiplayerCore] taxonomy values. [param target]
## narrows by [code]route[/code], [code]entity_id[/code] and [code]peer[/code],
## each absent or zero meaning any, and they narrow together. [param predicate]
## narrows by the event's own fields. [param sink] is called with one
## [NetwEvent] per match and its return value is discarded, because observing
## cannot alter what a session does. [param opts] carries
## [code]phase[/code], [code]enabled[/code], [code]once[/code],
## [code]dedupe[/code] and [code]note[/code].
##
## An unknown event value or an unknown [param predicate], [param target] or
## [param opts] key REFUSES the install, so a typo is an error rather than a row
## that never fires.
## [codeblock]
## var id := multiplayer.event_watch(
##     [NetwMultiplayerCore.SPAWNED, NetwMultiplayerCore.DESPAWNED],
##     { entity_id = &"Racer" },
##     { },
##     _on_racer_event,
## )
## [/codeblock]
func event_watch(
		events: PackedInt64Array,
		target: Dictionary = { },
		predicate: Dictionary = { },
		sink: Callable = Callable(),
		opts: Dictionary = { },
) -> int:
	return _native_core.event_watch(events, target, predicate, sink, opts)


## Withdraws the row [method event_watch] returned, answering whether one was
## installed under [param id].
func event_unwatch(id: int) -> bool:
	return _native_core.event_unwatch(id)


## Returns every installed row, each carrying the [code]hit_count[/code] the
## session has written to it.
func event_watches() -> Array[Dictionary]:
	var listed: Array[Dictionary] = []
	listed.assign(_native_core.event_watches())
	return listed


## Returns [param route]'s recent events, oldest first, and empties the ring.
##
## A ring is history a caller consumes rather than a stream it subscribes to.
## Route [code]0[/code] holds the events with no entity subject, which is the
## session's own history. A route's ring is dropped when its subject dies,
## after the terminal event is delivered, because nothing can follow it.
func event_ring(route: int) -> Array[NetwEvent]:
	var rows: Array[NetwEvent] = []
	rows.assign(_native_core.event_ring(route))
	return rows


## Forgets [param route]'s recent events without reading them.
func event_ring_clear(route: int) -> void:
	_native_core.event_ring_clear(route)


## Records events into the per-route rings, independent of any row.
##
## This is the switch an attached observer throws. A session nobody watches
## records nothing and pays one branch per emit site.
func event_arm(enabled: bool) -> void:
	_native_core.event_arm(enabled)


## Returns whether anything would look at [param event] on [param route].
##
## The cheap pre-check, for a site whose detail costs something to build. The
## answer is deliberately conservative: it does not consult [param route],
## because a false positive costs one [Dictionary] nobody reads and a false
## negative loses an event.
func event_wants(event: int, route: int = 0) -> bool:
	return _native_core.event_wants(event, route)


## Reports one [param event] of the closed taxonomy to the session's observers.
##
## A fact this session owns but the native core does not reach yet is reported
## here, from the site that owns it. The pre-check is the point: a session
## nobody watches builds no [param detail] and touches no observer.
##
## [param verdict] is how a stage that judged something says so without counting
## it. A verdict over remote input belongs in the session's own book and goes
## through [method NetwMultiplayerCore.stage_verdict] instead, which records the
## same row and then counts and reports it.
##
## [br][br][b]Server Only.[/b] is not implied. Both roles report what they see.
func report_event(
		event: int,
		route: int = 0,
		detail: Dictionary = { },
		peer: int = 0,
		entity_id: StringName = &"",
		model: Dictionary = { },
		verdict: Error = OK,
) -> void:
	if not _native_core.event_wants(event, route):
		return
	_native_core.event_emit(
		event,
		route,
		detail,
		entity_id,
		peer,
		verdict,
		model,
	)


# Set true the moment the teardown begins, so the session machine can tell a
# local teardown from a server crash. Read through
# NetwEmbeddingHandle.is_disposing().
var _disposing: bool = false


# Breaks the signal-connection cycles between this extension and the RefCounted
# objects it owns (inner, clock) so the whole group can be released, since a
# signal connection strong-references its target. The embedding's dispose() is
# the entry point; only this extension can reach the connections it made.
func _release_owned_graph() -> void:
	if _disposing:
		return
	_disposing = true
	# Teardown gets the drain the pump cannot promise. A session that ends has
	# no next pump, so anything already scheduled runs here, while the graph it
	# was scheduled against is still whole.
	_settle()
	auth_callback = Callable()
	if _native_core.local_scene_changed.is_connected(local_scene_changed.emit):
		_native_core.local_scene_changed.disconnect(local_scene_changed.emit)
	_scene_dispose()
	if _native_core.session_entered.is_connected(session_entered.emit):
		_native_core.session_entered.disconnect(session_entered.emit)
	if _native_core.session_ended.is_connected(session_ended.emit):
		_native_core.session_ended.disconnect(session_ended.emit)
	if _native_core.session_ended.is_connected(_on_session_ended):
		_native_core.session_ended.disconnect(_on_session_ended)
	_session_dispose()
	_unbind_inner_signals()
	if _native_core.after_tick.is_connected(_on_clock_tick):
		_native_core.after_tick.disconnect(_on_clock_tick)
	if _native_core.entity_dead.is_connected(_replication.clear_route):
		_native_core.entity_dead.disconnect(_replication.clear_route)
	if session_ended.is_connected(_on_liveness_session_ended):
		session_ended.disconnect(_on_liveness_session_ended)
	if _native_core.entity_live.is_connected(_on_liveness_entity_live):
		_native_core.entity_live.disconnect(_on_liveness_entity_live)
	if _native_core.entity_lingering.is_connected(_on_liveness_entity_lingering):
		_native_core.entity_lingering.disconnect(_on_liveness_entity_lingering)
	if _native_core.entity_dead.is_connected(_on_liveness_entity_dead):
		_native_core.entity_dead.disconnect(_on_liveness_entity_dead)
	if peer_connected.is_connected(_ensure_participant_row):
		peer_connected.disconnect(_ensure_participant_row)
	if peer_disconnected.is_connected(_clear_disconnected_peer):
		peer_disconnected.disconnect(_clear_disconnected_peer)
	_native_core.display_clear_runtimes()
	_replication.dispose()
	_rpc_core.dispose()
	_property_set_records.clear()
	_property_set_cache.clear()
	_entity_property_sets.clear()
	_property_sets.clear()
	_clear_flat_family_state()
	_services.clear()
	clear_roster()
	_root = null


## Sends application authentication [param data] to [param peer_id].
##
## Forwards to [method SceneMultiplayer.send_auth]. Networked reserves its own
## framed packets, so application payloads must not use an [AuthProtocol]
## header.
func peer_send_auth(peer_id: int, data: PackedByteArray) -> Error:
	return inner.send_auth(peer_id, data)


## Completes local authentication for [param peer_id].
func peer_complete_auth(peer_id: int) -> Error:
	return inner.complete_auth(peer_id)


## Returns peer ids currently waiting in Godot's authentication phase.
func get_authenticating_peers() -> PackedInt32Array:
	return inner.get_authenticating_peers()


## Disconnects [param peer_id] from the session.
func peer_disconnect(peer_id: int) -> void:
	inner.disconnect_peer(peer_id)


## Clears the wrapped [SceneMultiplayer] replication state.
func clear() -> void:
	inner.clear()


## Sends application bytes through the wrapped [SceneMultiplayer].
func send_bytes(
		bytes: PackedByteArray,
		peer_id: int = 0,
		mode: MultiplayerPeer.TransferMode = MultiplayerPeer.TRANSFER_MODE_RELIABLE,
		channel: int = 0,
) -> Error:
	return inner.send_bytes(bytes, peer_id, mode, channel)

#region Entity and liveness

## Mints an unbound entity handle.
##
## For a row with no node of its own. A node that carries a [NetwEntity] already
## has a handle, and [method entity_of] answers it rather than minting a second.
func entity_create() -> RID:
	return _native_core.liveness_core.entity_create()


## Admits [param entity] to the session and returns the wire route it is now
## named by, or [code]0[/code] when it could not be admitted.
##
## Admission is the server reserving a route and binding it in one act, which is
## why there is no separate reserve verb on this surface: a route with nothing
## bound to it is the pipelines' business and never a caller's. An entity
## already
## admitted answers the route it already has.
## [codeblock]
## var route := api.entity_admit(api.entity_of(body))
## [/codeblock]
## [br][br][b]Server Only.[/b]
func entity_admit(entity: RID) -> int:
	assert(is_server(), "NetwMultiplayer.entity_admit is server-only")
	if not _native_core.liveness_core.entity_is_valid(entity):
		return 0
	var existing := entity_get_route(entity)
	if existing > 0:
		return existing
	var route := _native_core.liveness_reserve_route()
	return route if entity_bind_route(entity, route) == OK else 0


## Binds [param entity] to [param route] and moves it to
## [constant EntityState.LIVE].
##
## A route names one entity for the whole session, tombstone included, so a
## route another entity already holds answers [constant ERR_ALREADY_IN_USE]
## rather than renaming. Binding [param entity] onto its own tombstoned route is
## the re-admission, and it answers [constant OK] one
## [method entity_get_epoch] higher. Resolve the standing entity with
## [method entity_from_route] rather than minting a second handle for a route
## that already stands.
func entity_bind_route(entity: RID, route: int) -> Error:
	if not _native_core.liveness_core.entity_is_valid(entity):
		return ERR_DOES_NOT_EXIST
	if route <= 0:
		return ERR_INVALID_DATA
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		if not _native_core.liveness_bind_route(route, wrapper):
			return ERR_ALREADY_IN_USE
	elif not _native_core.liveness_core.bind_route(entity, route):
		return ERR_ALREADY_IN_USE
	return OK


## Attaches [param node] to [param entity] as its owner, moving a record-plane
## entity onto the wrapper plane without changing its RID or its route.
##
## A row and a wrapper are two planes over one identity, and until a facet needs
## a node only the row exists. The facets that do need one refuse without it:
## [method entity_add_property_set] and [method scene_declare] answer
## [constant @GlobalScope.ERR_UNAVAILABLE] and
## [constant @GlobalScope.ERR_DOES_NOT_EXIST], and
## [method entity_grant_control] does nothing at all. This is how an entity
## declared as a row earns them.
## [codeblock]
## var entity := api.entity_create()
## api.entity_bind_route(entity, api.entity_admit(entity))
## api.entity_bind_node(entity, body)   # now the facets below are reachable
## api.entity_add_property_set(entity, set, 0)
## [/codeblock]
## [codeblock]
## Error
## ┠╴OK                  bound, and the entity is LIVE on its route
## ┠╴ERR_DOES_NOT_EXIST  the entity RID is not valid
## ┠╴ERR_INVALID_DATA    the node is not a live instance
## ┖╴ERR_ALREADY_EXISTS  a different node already owns this entity
## [/codeblock]
##
## A client may bind only after [method entity_bind_route] has admitted the
## mirror's route. The binding stamps this session onto the owner before the
## entity becomes live.
func entity_bind_node(entity: RID, node: Node) -> Error:
	if not _native_core.liveness_core.entity_is_valid(entity):
		return ERR_DOES_NOT_EXIST
	if not is_instance_valid(node):
		return ERR_INVALID_DATA
	var held := _entity_wrapper(entity)
	if held:
		return OK if held.owner == node else ERR_ALREADY_EXISTS
	var route := entity_get_route(entity)
	if route <= 0:
		return ERR_INVALID_DATA
	var wrapper := NetwEntity.ensure(node)
	_apply_pending_scene_facet(entity, wrapper)
	if wrapper.multiplayer == null:
		wrapper.arm(self)
	_native_core.liveness_bind_route(route, wrapper)
	return OK


## Returns [param entity]'s wire route, or [code]0[/code].
func entity_get_route(entity: RID) -> int:
	var route := _native_core.liveness_core.route_of(entity)
	if route > 0:
		return route
	var wrapper := _entity_wrapper(entity)
	return wrapper.route if wrapper else 0


## Returns [param entity]'s [enum EntityState].
func entity_get_state(entity: RID) -> EntityState:
	return _native_core.liveness_core.state_of(entity) as EntityState


## Returns which life of [param entity] is current, counting from
## [code]0[/code], or [code]-1[/code] when this session knows no such entity.
##
## The handle names the entity for the whole session and the epoch names the
## life inside it, so the pair is what tells a frame authored before a
## re-admission from one authored after. A revival through
## [method entity_bind_route] is the only thing that raises it.
func entity_get_epoch(entity: RID) -> int:
	return _native_core.liveness_core.epoch_of(entity)


## Returns the node owned by [param entity], or [code]null[/code].
func entity_get_node(entity: RID) -> Node:
	var wrapper := _entity_wrapper(entity)
	return wrapper.owner if wrapper and is_instance_valid(wrapper.owner) else null


## Returns the nearest ancestor entity of [param entity], or an invalid RID
## when it is a root.
##
## Nesting is what the interest engine narrows through: a child's committed row
## is intersected with its parent's, so a child can only ever see less than its
## parent. Walking this chain is how any caller asks the same ancestry question
## the engine answers internally.
## [codeblock]
## var parent := api.entity_get_parent(entity)
## while parent.is_valid():
##     if some_predicate(parent):
##         break
##     parent = api.entity_get_parent(parent)
## [/codeblock]
func entity_get_parent(entity: RID) -> RID:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return RID()
	var parent := wrapper.parent_entity()
	if parent == null or not is_instance_valid(parent.owner):
		return RID()
	# entity_of, not parent.rid: an ancestor this session has not been asked
	# about is unknown to the record plane, and a walk must not stop on that.
	return entity_of(parent.owner)


## Returns the peer [param entity] represents, or [code]0[/code] when it is
## server-owned.
##
## A non-zero result is the definition of a player entity, so this is the verb
## every peer-filtering caller reaches for rather than re-deriving the fact from
## a node name.
func entity_get_peer(entity: RID) -> int:
	var wrapper := _entity_wrapper(entity)
	return wrapper.peer_id if wrapper else 0


## Returns [param node]'s entity handle, or an invalid RID when no
## [NetwEntity] covers it.
##
## The handle is the wrapper's own, held since it was constructed, so this
## mints nothing. What it does do is introduce the wrapper to this session the
## first time it is asked for: the record plane adopts the handle, so every
## flat verb below can resolve it. A node with no wrapper is not an entity and
## this says so rather than making one.
func entity_of(node: Node) -> RID:
	return _native_core.entity_of(node)


## Returns the entity handle bound to [param route], or an invalid RID.
##
## The one read that walks the bridge backwards. A tombstoned route still
## answers, because the record it names outlives the life that earned it.
func entity_from_route(route: int) -> RID:
	return _native_core.liveness_core.rid_from_route(route)


## Returns every live route in stable order.
func live_routes() -> PackedInt32Array:
	return _native_core.liveness_core.live_routes()


## Runs [param callback] when [param route] becomes live.
func when_live(
		route: int,
		callback: Callable,
		timeout_ticks: int = 0,
		on_timeout: Callable = Callable(),
) -> void:
	var clock := _native_core.clock_handle
	var clocked := clock.is_configured
	var timeout := timeout_ticks
	if timeout == 0:
		timeout = clock.tickrate if clocked else _CLOCKLESS_TICKRATE
	var origin := clock.tick if clocked \
			else _native_core.liveness_core.frame()
	_native_core.liveness_when_live(
		route,
		callback,
		origin + timeout,
		clocked,
		on_timeout,
	)


# Advances the record plane's frame counter and expires whatever aged out.
# Which counter a deadline was measured against is settled where the clock is
# reachable, which is here, and carried into the record plane as an integer.
func _liveness_poll() -> void:
	var clock := _native_core.clock_handle
	_native_core.liveness_poll(clock.tick if clock.is_configured else 0)


## Mints [param count] fresh routes and binds each as a live entity with no
## node, the identity a replicated table row is.
##
## Routes are session-global, so the returned array is yours to store as an
## ordinary column and to write into any number of tables. Claiming is
## optional: a route you already hold, from another table or from
## [method entity_get_route], is equally writable. That independence is what
## lets a component table key on the same routes as the table it refines.
## [codeblock]
## var fresh := api.claim_routes(200)
## routes.append_array(fresh)          # my storage, my order
## api.table_write_routes(mobs, routes)
## [/codeblock]
## No per-row signal fires. A wave of rows is delivered as the cohort arrays
## [method table_read_births] and [method table_read_deaths] rather than as
## [signal entity_live] two thousand times. Release them with
## [method release_routes].
## [br][br][b]Server Only.[/b]
func claim_routes(count: int) -> PackedInt64Array:
	var out := PackedInt64Array()
	if not is_server():
		Netw.dbg.error("NetwMultiplayer.claim_routes is server-only")
		return out
	if count <= 0:
		return out
	out.resize(count)
	for i in count:
		out[i] = _native_core.liveness_reserve_route()
	_native_core.liveness_bind_routes_data(out)
	return out


## Tombstones [param routes] locally and tells every peer to do the same.
##
## The death edge of [method claim_routes]. A tombstoned route is retired for
## the rest of the session, so every peer drops its rows from every table and a
## row for it can never be resurrected by an upsert that arrived late.
##
## A route is one identity everywhere, so releasing one a spawned entity also
## holds retires that entity's record too. Release the routes you minted, not
## the routes you read off a table you do not own.
## [br][br][b]Server Only.[/b]
func release_routes(routes: PackedInt64Array) -> Error:
	if not is_server():
		return ERR_UNCONFIGURED
	if routes.is_empty():
		return OK
	_native_core.liveness_tombstone_routes_data(routes)
	_table_core.retire_routes(routes)
	_table_core.queue_lifecycle_removals(routes)
	return OK


# Returns the cached wrapper for an entity handle.
func _entity_wrapper(entity: RID) -> NetwEntity:
	return _native_core.wrapper_of(entity) as NetwEntity

#endregion

#region Schemas

## Declares the schema named [param name] and returns its handle.
##
## A schema is an ordered list of named, typed columns and nothing else. It
## exists once so that the three things that read a column list read the same
## one: the row-major property binding, the column-major table, and
## [NetwDatabase].
## [codeblock]
## var mob := api.schema_create(&"Mob")
## var pos := api.schema_add_column(mob, &"pos", ColumnType.COLUMN_VECTOR3)
## api.schema_seal(mob)
## var table := api.table_create(mob)     # the wire binding
## [/codeblock]
## Declaring a name that already exists returns the existing handle and
## restarts the re-declaration cursor, so a script reload replays its own
## columns rather than minting a second schema.
## [method Netw.configure_schema] is the same declaration written where the
## code that uses it lives.
func schema_create(name: StringName) -> RID:
	return _native_core.schema_create(name)


## Appends one column in address order and returns its index, or
## [code]-1[/code].
##
## Declaration order is address order, so a column's index is what every
## binding addresses it by and, for a table, the position its bytes occupy in a
## frame. [param stride] is how a fixed-capacity array is expressed: eight
## ability cooldowns is one [constant ColumnType.COLUMN_F32] column of stride
## eight, not a second type family.
func schema_add_column(
		schema: RID,
		key: StringName,
		type: ColumnType,
		stride: int = 1,
) -> int:
	return _native_core.schema_add_column(schema, key, type, stride)


## Assigns [param quantizer] to one column before sealing.
##
## A column with no quantizer crosses the wire as a raw little-endian copy of
## its buffer, which is the fastest path in both directions and the widest.
## A quantizer buys bandwidth by paying per element, and never applies at rest:
## saves are unquantized.
func schema_set_column_quantizer(
		schema: RID,
		column: int,
		quantizer: NetwQuantize,
) -> void:
	_native_core.schema_set_column_quantizer(schema, column, quantizer)


## Seals [param schema], fixing its shape hash and rejecting later mutation.
##
## Sealing is what makes declaration order address order, so a schema that could
## still gain a column has no stable address to hand out.
## [codeblock]
## Error
## ┠╴OK                  sealed, or re-sealed against a matching redeclaration
## ┠╴ERR_DOES_NOT_EXIST  the handle names no schema
## ┖╴ERR_UNCONFIGURED    a redeclaration replayed a different shape
## [/codeblock]
func schema_seal(schema: RID) -> Error:
	return _native_core.schema_seal(schema)


## Returns the schema declared under [param name], or an invalid RID.
##
## A declaration this session has not compiled yet is compiled here, because a
## class's [code]static var[/code] initializers run on first access rather than
## at load and may therefore have missed the session's own adoption sweep.
func schema_find(name: StringName) -> RID:
	var found := _native_core.schema_find(name)
	if found.is_valid() or NetwSchemaModel.find(name) == null:
		return found
	_adopt_schema_declarations()
	return _native_core.schema_find(name)


## Returns [param schema]'s sealed shape hash, or [code]0[/code] when it is not
## sealed.
##
## The shape is the name and, per column, key, type, stride, and quantizer. It
## is the value a TABLE frame carries and the value
## [method property_set_get_wire_hash] folds membership and lanes into, so two
## peers that declared a different type or a different bit width disagree here
## rather than misreading every later frame.
func schema_get_hash(schema: RID) -> int:
	return _native_core.schema_get_hash(schema)


## Returns how many columns [param schema] declares.
func schema_get_column_count(schema: RID) -> int:
	return _native_core.schema_get_column_count(schema)


## Returns one column's key, or an empty name when the address is invalid.
func schema_get_column_key(schema: RID, column: int) -> StringName:
	return _native_core.schema_get_column_key(schema, column)


## Returns one column's [enum ColumnType].
func schema_get_column_type(schema: RID, column: int) -> ColumnType:
	return _native_core.schema_get_column_type(schema, column) as ColumnType


## Returns how many elements one row occupies in [param column], or
## [code]0[/code] when the address is invalid.
func schema_get_column_stride(schema: RID, column: int) -> int:
	return _native_core.schema_get_column_stride(schema, column)


# Compiles every NetwSchemaModel declaration into this session's own schemas.
# A schema has no node to be discovered through and a static declaration may
# have been written before any session existed.
func _adopt_schema_declarations() -> void:
	for declaration in NetwSchemaModel.declarations():
		var schema := schema_create(declaration.name)
		if not schema.is_valid():
			continue
		for column in declaration.columns:
			var index := schema_add_column(
				schema,
				column.key,
				column.type as ColumnType,
				column.stride,
			)
			if index >= 0:
				schema_set_column_quantizer(schema, index, column.quantizer)
		if schema_seal(schema) != OK:
			Netw.dbg.error(
				"NetwMultiplayer: schema '%s' was redeclared with a different "
				% declaration.name
				+ "shape; peers built from the two declarations cannot "
				+ "read each other.",
			)

#endregion

#region Tables

## Binds a table to the sealed [param schema] and returns its handle.
##
## A table is a view over routes rather than a container of them, and it
## declares nothing of its own: the schema already fixed the column order, the
## types, and the shape hash, so binding is what gives that declaration a store
## and a wire id.
## [codeblock]
## var burning := api.schema_create(&"Burning")
## var dps := api.schema_add_column(burning, &"dps", ColumnType.COLUMN_F32)
## api.schema_seal(burning)
## var table := api.table_create(burning)
## api.table_write_column(table, dps, my_dps)
## [/codeblock]
## Returns an invalid RID when [param schema] was never sealed or declares a
## [constant ColumnType.COLUMN_VARIANT] column, because variable width has no
## memcpy and no rows-per-frame budget. Binding a schema whose name already has
## a table returns the table it already has, so a script reload finds its own
## handle rather than minting a second one.
## [method Netw.configure_schema] is the same declaration written where the code
## that publishes it lives.
func table_create(schema: RID) -> RID:
	return _native_core.table_create(schema)


## Returns the schema [param table] binds, or an invalid RID.
##
## Reflection goes through the schema family, because a column's key, type, and
## stride belong to the declaration rather than to any one binding of it.
func table_get_schema(table: RID) -> RID:
	return _native_core.table_get_schema(table)


## Writes one [enum TableParam].
##
## Local configuration rather than shape, so a param never enters the wire hash
## and a later value is not a wire event.
func table_set_param(table: RID, param: TableParam, value: Variant) -> void:
	_native_core.table_set_param(table, param, value)


## Returns the table bound to the schema named [param name], or an invalid RID.
##
## Names are the user vocabulary and the handle is the machine key, so fetch
## the handle once at setup and keep it. This is also the per-session door for
## a schema declared statically through [method Netw.configure_schema].
##
## A declaration this session has not compiled yet is compiled here, because a
## class's [code]static var[/code] initializers run on first access rather than
## at load and may therefore have missed the session's own adoption sweep.
## Declare every schema before the session goes online: a wire id is the
## name-sorted position among bound tables, so two peers that bound different
## sets number them differently and the wire hash is what catches it.
func table_find(name: StringName) -> RID:
	var found := _native_core.table_find(name)
	if found.is_valid() or NetwSchemaModel.find(name) == null:
		return found
	_adopt_schema_declarations()
	_adopt_table_declarations()
	return _native_core.table_find(name)


## Returns [param table]'s wire hash, the shape hash of the schema it binds.
##
## It is [method schema_get_hash] verbatim, because a table adds nothing to the
## shape: params are local and the store is not declaration. The value rides
## every frame, so a mid-session script reload that changed a schema is caught
## rather than decoded as garbage.
func table_get_wire_hash(table: RID) -> int:
	return _native_core.table_get_wire_hash(table)


## Records the row order [method table_commit] will publish.
##
## Row [code]i[/code] of every column belongs to [code]routes[i][/code], so
## this and the columns are one statement that only becomes true at the commit.
## The array is held by reference until then.
## [br][br][b]Server Only.[/b]
func table_write_routes(table: RID, routes: PackedInt64Array) -> Error:
	return _native_core.table_write_routes(table, routes)


## Records one column's buffer for the next commit.
##
## [param data] must be the storage array [enum ColumnType] names, and is held
## by reference until [method table_commit] copies it, so writing is a slot
## store rather than a copy.
## [br][br][b]Server Only.[/b]
func table_write_column(table: RID, column: int, data: Variant) -> Error:
	return _native_core.table_write_column(table, column, data)


## Publishes the written routes and columns as [param table]'s state.
##
## Not committing is the cadence. There is no interval to declare and no change
## detection to configure, because the caller already knows when their data
## changed. A table nothing commits sends nothing.
## [codeblock]
## Error
## ┠╴OK                  snapshotted, and queued for the next tick boundary
## ┠╴ERR_DOES_NOT_EXIST  the handle names no table
## ┖╴ERR_INVALID_DATA    a column was never written, or its length disagrees
##                       with routes.size() * stride
## [/codeblock]
## The commit copies every buffer, so the caller's arrays are theirs again the
## moment it returns and mutating them cannot tear the published state. The
## wire happens at the tick boundary, so several commits in one tick collapse
## to the last, and a session with no configured clock carries no table wire at
## all.
## [br][br][b]Server Only.[/b]
func table_commit(table: RID) -> Error:
	return _native_core.table_commit(table)


## Returns the applied row order by reference. Treat it as read-only.
func table_read_routes(table: RID) -> PackedInt64Array:
	return _native_core.table_read_routes(table)


## Returns one applied column by reference.
##
## The array is the store itself, which is what makes reading a two thousand
## row column free. Treat it as read-only and [code]duplicate()[/code] it to
## keep history, which is also the render-side interpolation pattern: keep the
## last wave's columns and blend toward the new ones on your own clock.
## [codeblock]
## func _on_table_received(table: RID, _tick: int) -> void:
##     prev_pos = next_pos                                    # rebind, free
##     next_pos = api.table_read_column(mobs, Mobs.pos).duplicate()
## [/codeblock]
func table_read_column(table: RID, column: int) -> Variant:
	return _native_core.table_read_column(table, column)


## Returns the routes the most recent applied wave added to [param table].
##
## Cohorts are derived here rather than read off the wire, which is what makes
## them exact under loss: a dropped datagram delays a row's data, and can never
## lose a birth or a death. They are stable until the next wave.
func table_read_births(table: RID) -> PackedInt64Array:
	return _native_core.table_read_births(table)


## Returns the routes the most recent applied wave removed from [param table].
## The twin of [method table_read_births].
func table_read_deaths(table: RID) -> PackedInt64Array:
	return _native_core.table_read_deaths(table)


## Returns [param route]'s row index in [param table], or [code]-1[/code] when
## it has no row.
func table_get_row(table: RID, route: int) -> int:
	return _native_core.table_get_row(table, route)


## Returns one row index per entry of [param routes], [code]-1[/code] where the
## route has no row.
##
## The join primitive. One call answers a whole join across two tables, where
## a per-row [method table_get_row] loop would be a per-row crossing in
## everything but name.
## [codeblock]
## var rows := api.table_get_rows(mobs, burning_routes)
## for i in burning_routes.size():
##     if rows[i] >= 0:
##         hp[rows[i]] -= burning_dps[i] * dt
## [/codeblock]
func table_get_rows(
		table: RID,
		routes: PackedInt64Array,
) -> PackedInt32Array:
	return _native_core.table_get_rows(table, routes)


## Returns the tick of [param table]'s freshest applied row set, or
## [code]-1[/code] before anything has been applied.
##
## Poll this and compare it in your own loop when you would rather not connect
## [signal table_received]. Both styles are first-class.
func table_get_tick(table: RID) -> int:
	return _native_core.table_get_tick(table)


# Binds a table to every replicated schema this session compiled.
# Runs once at construction, after _adopt_schema_declarations, because a table
# has no node to be discovered through and a static declaration may have been
# written before any session existed.
func _adopt_table_declarations() -> void:
	for declaration in NetwSchemaModel.declarations():
		if not declaration.replicated:
			continue
		var schema := _schema_core.find(declaration.name)
		if not schema.is_valid():
			continue
		var table := table_create(schema)
		if not table.is_valid():
			Netw.dbg.error(
				"NetwMultiplayer: schema '%s' asks for a table but declares a "
				% declaration.name
				+ "VARIANT column; variable width has no row budget. Send it "
				+ "through a channel or drop replicated() from the schema.",
			)
			continue
		table_set_param(
			table,
			TableParam.TABLE_PARAM_RELIABLE,
			declaration.reliable,
		)

#endregion

#region Interest

## Creates or returns the layer named [param name].
func layer_create(name: StringName) -> RID:
	var known := _native_core.layer_named(name)
	if known.is_valid():
		return known
	var opened := _native_core.layer_open(name)
	if not opened.is_valid():
		return opened
	if _layer_declare(opened) != OK:
		_native_core.layer_close(opened)
		return RID()
	return opened


## Returns the layer named [param name], or an invalid RID.
func layer_find(name: StringName) -> RID:
	return _native_core.layer_named(name)


## Frees [param layer] and removes its current memberships.
func layer_free(layer: RID) -> void:
	var record := _layer_record(layer)
	if record == null:
		return
	_layer_undeclare(layer)
	_disconnect_layer_monitor(layer)
	_layer_drivers.erase(layer)
	_native_core.layer_close(layer)


## Adds [param peer] to [param layer].
func layer_add_viewer(layer: RID, peer: int) -> Error:
	var record := _layer_record(layer)
	if record == null:
		return ERR_DOES_NOT_EXIST
	if peer == 0:
		return ERR_INVALID_DATA
	record.add_viewer(peer)
	return OK


## Removes [param peer] from [param layer].
func layer_remove_viewer(layer: RID, peer: int) -> void:
	var record := _layer_record(layer)
	if record:
		record.remove_viewer(peer)


## Adds [param entity] to [param layer].
## [br][br][b]Server Only.[/b]
func layer_add_entity(layer: RID, entity: RID) -> Error:
	var record := _layer_record(layer)
	var wrapper := _entity_wrapper(entity)
	if record == null or wrapper == null:
		return ERR_DOES_NOT_EXIST
	if not is_server():
		return ERR_UNAUTHORIZED
	return OK if record.add_entity(wrapper) or record.has_entity(wrapper) \
	else ERR_UNAVAILABLE


## Removes [param entity] from [param layer].
## [br][br][b]Server Only.[/b]
func layer_remove_entity(layer: RID, entity: RID) -> void:
	if not is_server():
		return
	var record := _layer_record(layer)
	var wrapper := _entity_wrapper(entity)
	if record and wrapper:
		record.remove_entity(wrapper)


## Replaces one [enum LayerParam] on [param layer].
func layer_set_param(layer: RID, param: LayerParam, value: Variant) -> void:
	var record := _layer_record(layer)
	if record == null:
		return
	match param:
		LayerParam.LAYER_PARAM_POLICY:
			record.set_policy(int(value) as NetwInterestLayer.Policy)
		LayerParam.LAYER_PARAM_LEAVE_POLICY:
			record.default_leave_policy = int(value)
		LayerParam.LAYER_PARAM_PERCEPTION_POLICY:
			record.default_perception_policy = int(value)


## Sets the layer transition callback.
##
## [param callback] receives [code](visible, entity, peer)[/code], with
## [param entity] as an RID. An invalid callable clears the callback.
func layer_set_monitor_callback(layer: RID, callback: Callable) -> void:
	_disconnect_layer_monitor(layer)
	var record := _layer_record(layer)
	if record == null or not callback.is_valid():
		return
	var hooks: Array = []
	var entered := func(entity: NetwEntity, peer: int) -> void:
		callback.call(true, entity_of(entity.owner), peer)
	var exited := func(entity: NetwEntity, peer: int) -> void:
		callback.call(false, entity_of(entity.owner), peer)
	var visible := func(entity: NetwEntity) -> void:
		callback.call(true, entity_of(entity.owner), get_unique_id())
	var hidden := func(entity: NetwEntity) -> void:
		callback.call(false, entity_of(entity.owner), get_unique_id())
	for pair: Array in [
		[record.interest_enter, entered],
		[record.interest_exit, exited],
		[record.entity_visible, visible],
		[record.entity_hidden, hidden],
	]:
		_connect_once(pair[0], pair[1])
		hooks.append(pair)
	_layer_monitor_hooks[layer] = hooks


## Sets the callback that supplies the layer's complete entity RID set.
##
## The callback receives [param layer] and returns an [Array] of entity RIDs.
## It runs before each explicit or tick-driven [method interest_flush].
func layer_set_driver_callback(layer: RID, callback: Callable) -> void:
	if _layer_record(layer) == null:
		return
	if callback.is_valid():
		_layer_drivers[layer] = callback
	else:
		_layer_drivers.erase(layer)


## Returns whether [param peer] is in [param entity]'s committed row.
func interest_admits(entity: RID, peer: int) -> bool:
	var bit := _native_core.interest_peer_bit(peer)
	if bit < 0:
		return false
	return _interest_admits(entity, bit)


## Returns [param entity]'s committed peer-bit words.
func interest_get_row(entity: RID) -> PackedInt64Array:
	return _interest_row_of(entity)


## Returns the resolved layer RIDs for [param entity].
func interest_get_membership(entity: RID) -> Array[RID]:
	var out: Array[RID] = []
	for name: StringName in _native_core.interest_membership_ids(entity):
		var layer := layer_find(name)
		if not layer.is_valid():
			layer = layer_create(name)
		out.append(layer)
	return out


## Returns whether [param entity] has a visibility filter.
func interest_is_filtered(entity: RID) -> bool:
	return _native_core.interest_has_filter(entity)


## Explains the committed interest verdict for [param peer].
func interest_explain(entity: RID, peer: int) -> String:
	var bit := _native_core.interest_peer_bit(peer)
	if bit < 0:
		return "peer is not registered"
	return _interest_explain(entity, bit)


# Drives one interest flush from the settle queue, sinking its verdict the way
# every other scheduled stage does.
func _flush_interest_visibility() -> void:
	_sink_verdict(interest_flush(), 0)


# Rebuilds the compat adapter's per-peer intent after a live-peer change.
func _refresh_interest_intents() -> void:
	_replication._sync_compat.refresh_interest_intents()


# Sends one awareness batch to peer_id as a reliable route-0 carrier frame.
func _send_interest_awareness(peer_id: int, wire: Array) -> void:
	_replication.send_to(
		peer_id,
		0,
		NetwFrameEnvelope.Channel.INTEREST_AWARENESS,
		var_to_bytes(wire),
		true,
	)


## Flushes driver mutations and the committed interest matrix.
func interest_flush() -> Error:
	var driver_verdict := _run_layer_drivers()
	if driver_verdict != OK:
		return driver_verdict
	_settle_cancel(NetwMultiplayerCore.interest_flush_key())
	var verdict := _interest_recompute()
	if verdict != OK:
		return verdict
	_interest_commit()
	return _native_core.interest_flush_tail()


## Admits one newly declared interest [param layer] into the matrix.
##
## The seven interest stages share one committed matrix. Replacing some of them
## leaves the rest reading a matrix your override never fills, which shows up as
## custom logic that runs and changes nothing rather than as an error. Nothing
## checks this, so replace the family together or forward what you do not need.
## [codeblock]
## Error
## ┠╴OK                  the layer is known and now participates
## ┖╴ERR_DOES_NOT_EXIST  the RID names no layer this peer owns
## [/codeblock]
## [param layer] is minted by [method layer_create]. Override together with
## [method _layer_undeclare], [method _interest_recompute],
## [method _interest_commit], [method _interest_row_of],
## [method _interest_admits], and [method _interest_explain] to hold the
## viewer matrix in your own structure.
func _layer_declare(layer: RID) -> Error:
	return OK if _layer_record(layer) else ERR_DOES_NOT_EXIST


## Retires one interest [param layer] and everything it admitted.
##
## Removal is total: every entity and every viewer the layer held leaves with
## it, so no row can outlive the layer that justified it. An unknown
## [param layer] is a no-op rather than an error, which makes teardown safe to
## repeat. Shares the committed matrix described on [method _layer_declare].
func _layer_undeclare(layer: RID) -> void:
	var record := _layer_record(layer)
	if record == null:
		return
	for entity: NetwEntity in record.entities.keys():
		record.remove_entity(entity)
	for peer: int in record.viewer_ids():
		record.remove_viewer(peer)
	_native_core.interest_forget_layer_row(record.layer_id)


## Folds every layer into one pending viewer delta.
##
## Recompute never publishes. It leaves a delta staged so that readers keep
## seeing the previous matrix until [method _interest_commit] swaps it in, and
## one frame therefore never observes a half-folded matrix.
## [codeblock]
## Error
## ┠╴OK                the delta is staged and ready to commit
## ┖╴ERR_INVALID_DATA  a layer driver returned something other than an Array
##                     of entity RIDs
## [/codeblock]
## Called by [method interest_flush] after the layer drivers run. Part of the
## committed matrix described on [method _layer_declare].
func _interest_recompute() -> Error:
	return _native_core.interest_recompute()


## Publishes the delta that [method _interest_recompute] staged.
##
## Commit is the only point at which [method _interest_row_of] and
## [method _interest_admits] begin reporting the new matrix, which is what
## makes the fold atomic to everything downstream. Shares the committed matrix
## described on [method _layer_declare].
func _interest_commit() -> void:
	_native_core.interest_commit()


## Returns [param entity]'s committed viewer row as packed peer-bit words.
##
## The row reads the committed matrix, never the pending delta, so it agrees
## with [method _interest_admits] for every bit. An entity this peer does not
## know returns an empty array rather than a zero-filled row, so "no viewers"
## and "no entity" stay distinguishable.
##
## Each element holds 64 peer bits, least significant bit first. Part of the
## committed matrix described on [method _layer_declare].
func _interest_row_of(entity: RID) -> PackedInt64Array:
	var wrapper := _entity_wrapper(entity)
	return _native_core.interest_committed_row(wrapper) if wrapper \
	else PackedInt64Array()


## Returns whether [param entity] is visible to the viewer at [param peer_bit].
##
## This is the single question the sender asks per entity and per recipient, so
## it must stay cheap and must agree with [method _interest_row_of]. An unknown
## [param entity] is not admitted, which fails closed.
##
## [param peer_bit] is a dense viewer index, not a peer id. Part of the
## committed matrix described on [method _layer_declare].
func _interest_admits(entity: RID, peer_bit: int) -> bool:
	var wrapper := _entity_wrapper(entity)
	return _native_core.interest_bit_admits(wrapper, peer_bit) if wrapper \
	else false


## Returns human-readable reasoning for one [method _interest_admits] verdict.
##
## Diagnostics are part of the seam rather than a debug aside, because an
## interest matrix that cannot say why it hid something is untestable. Nothing
## on the send path calls this, so it may cost whatever clarity costs.
##
## The stock text names the deciding layer and policy, and reports
## [code]"entity is not registered"[/code] for an unknown [param entity]. Part
## of the committed matrix described on [method _layer_declare].
func _interest_explain(entity: RID, peer_bit: int) -> String:
	var wrapper := _entity_wrapper(entity)
	return _native_core.interest_explain_bit(wrapper, peer_bit) \
	if wrapper else "entity is not registered"


# Resolves one owned layer record.
func _layer_record(layer: RID) -> NetwInterestLayer:
	return _native_core.layer_view(layer) as NetwInterestLayer


# Disconnects one layer's monitor signal adapters.
func _disconnect_layer_monitor(layer: RID) -> void:
	var hooks: Array = _layer_monitor_hooks.get(layer, [])
	for pair: Array in hooks:
		var source := pair[0] as Signal
		var callback := pair[1] as Callable
		if source.is_connected(callback):
			source.disconnect(callback)
	_layer_monitor_hooks.erase(layer)


# Reconciles callback-produced membership before the interest fold.
func _run_layer_drivers() -> Error:
	for layer: RID in _layer_drivers:
		var callback: Callable = _layer_drivers[layer]
		var desired_value: Variant = callback.call(layer)
		if not (desired_value is Array):
			return ERR_INVALID_DATA
		var desired: Dictionary[RID, bool] = { }
		for value: Variant in desired_value:
			if not (value is RID) or _entity_wrapper(value) == null:
				return ERR_DOES_NOT_EXIST
			desired[value] = true
		var record := _layer_record(layer)
		if record == null:
			continue
		for wrapper: NetwEntity in record.entities.keys():
			if not desired.has(wrapper.rid):
				record.remove_entity(wrapper)
		for entity: RID in desired:
			record.add_entity(_entity_wrapper(entity))
	return OK

#endregion

#region Scenes

## Declares [param entity] a scene, so it owns an admission boundary every
## descendant entity inherits.
##
## Legal only while the record is [constant EntityState.UNBOUND]. The facet is
## consumed once at arm and rides the SPAWN packet, so declaring after the
## packet flushed would leave every client holding an ordinary entity. That is
## a programmer error rather than a runtime condition, which is why it answers
## [constant @GlobalScope.ERR_UNCONFIGURED] instead of failing quietly.
## [codeblock]
## var arena := api.entity_create()
## api.scene_declare(arena)          # before the route binds
## var route := api.entity_admit(arena)
## [/codeblock]
## The everyday door is [method Netw.configure_multiplayer_scene], which writes
## the same field from the scene root's own [code]_init[/code].
## [br][br][b]Server Only.[/b]
func scene_declare(entity: RID) -> Error:
	return _write_scene_facet(entity, true)


## Reverses [method scene_declare], subject to the same pre-arm rule.
## [br][br][b]Server Only.[/b]
func scene_undeclare(entity: RID) -> Error:
	return _write_scene_facet(entity, false)


## Returns whether [param entity] carries the scene facet.
##
## True on every peer once the SPAWN packet lands, not only on the server that
## declared it.
func scene_is_declared(entity: RID) -> bool:
	return _native_core.scene_declared(entity)


## Replaces one [enum SceneParam] on [param scene].
##
## [constant SceneParam.SCENE_PARAM_ISOLATION] is write-once before the entity
## arms and answers [constant @GlobalScope.ERR_UNCONFIGURED] afterwards, because
## it selects the container every peer builds. The other params are live.
## [br][br][b]Server Only.[/b]
func scene_set_param(scene: RID, param: SceneParam, value: Variant) -> Error:
	var wrapper := _entity_wrapper(scene)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	match param:
		SceneParam.SCENE_PARAM_LABEL:
			wrapper.scene_label = StringName(value)
		SceneParam.SCENE_PARAM_ISOLATION:
			if wrapper.stage != NetwEntity.STAGE_UNBOUND:
				return ERR_UNCONFIGURED
			wrapper.scene_isolation = int(value)
		SceneParam.SCENE_PARAM_PROCESSING:
			var node := entity_get_node(scene)
			if node == null:
				return ERR_DOES_NOT_EXIST
			node.process_mode = Node.PROCESS_MODE_INHERIT if bool(value) \
			else Node.PROCESS_MODE_DISABLED
	return OK


## Reads one [enum SceneParam] off [param scene].
func scene_get_param(scene: RID, param: SceneParam) -> Variant:
	var wrapper := _entity_wrapper(scene)
	if wrapper == null:
		return null
	match param:
		SceneParam.SCENE_PARAM_LABEL:
			return _native_core.scene_stem(scene)
		SceneParam.SCENE_PARAM_ISOLATION:
			return wrapper.scene_isolation
		SceneParam.SCENE_PARAM_PROCESSING:
			var node := entity_get_node(scene)
			return node != null and node.process_mode != Node.PROCESS_MODE_DISABLED
	return null


## Returns a live scene whose stem is [param stem], or an invalid RID.
##
## Stems are not unique, so this answers "an instance of this stem". Use
## [method scene_find_all] when the difference matters.
func scene_find(stem: StringName) -> RID:
	var found: RID = _native_core.scene_named(stem)
	return found if entity_get_node(found) != null else RID()


## Returns every live scene whose stem is [param stem].
func scene_find_all(stem: StringName) -> Array[RID]:
	var out: Array[RID] = []
	for scene: RID in _native_core.scenes_named(stem):
		if entity_get_node(scene) != null:
			out.append(scene)
	return out


## Returns the scene [param entity] belongs to, or an invalid RID.
##
## Self-inclusive: a declared scene answers with itself. Otherwise this walks
## the parent-entity chain, so membership follows the tree and self-heals on
## reparent without anything having to re-enroll the entity.
func scene_of(entity: RID) -> RID:
	return _native_core.entity_scene_of(entity)


## Returns [param scene]'s interest layer, or an invalid RID.
##
## A scene's boundary is opened by [method scene_admit] rather than by
## [method layer_create], so this mints the handle for one that already exists
## instead of only answering handles a caller asked for. It stays a read: a
## scene whose boundary nothing has opened gets no handle.
func scene_get_layer(scene: RID) -> RID:
	var layer_id := _scene_layer_id(scene)
	var found := layer_find(layer_id)
	if found.is_valid() \
			or not _native_core.interest_has_layer(layer_id):
		return found
	return layer_create(layer_id)


# The framework-derived layer id naming [param scene]'s admission boundary, or
# empty. Every caller asks in terms of the scene RID, so
# this shell names no scene class.
func _scene_layer_id(scene: RID) -> StringName:
	return _native_core.scene_layer_id(scene)


## Returns the scene this peer currently presents, or an invalid RID.
##
## A dedicated server presents nothing, so it always answers invalid.
func scene_get_current() -> RID:
	var scene: RID = _native_core.current_scene
	return scene if entity_get_node(scene) != null else RID()


## Returns the wrapper-tier handle for [param scene], or [code]null[/code].
##
## The one bridge back from the machine tier, so a lookup that answers in RIDs
## can still be read as a scene without anyone naming a container type.
## [codeblock]
## var arena := api.scene_handle(api.scene_find(&"Arena"))
## [/codeblock]
func scene_handle(scene: RID) -> NetwSceneHandle:
	var wrapper := _entity_wrapper(scene)
	return wrapper.scene if wrapper != null else null

## Emitted when a scene comes online on this peer.
##
## A scene is an ordinary entity, so this is [signal entity_live] narrowed to
## the ones carrying [member NetwEntity.declares_scene]. A listener that also
## wants the scenes already up iterates [method scene_instances] once.
signal scene_live(scene: NetwSceneHandle)


## Returns a live scene labelled [param label], or [code]null[/code].
##
## Labels are not unique, so this answers "an instance of this label", never
## "the arena". Ask [method scene_instances] when the difference matters.
## [codeblock]
## var arena := api.scene(&"Arena")
## if arena:
##     arena.admit(participant)
## [/codeblock]
func scene(label: StringName) -> NetwSceneHandle:
	return scene_handle(scene_find(label))


## Returns every live scene, or every instance labelled [param label].
##
## Named for what it answers: two live copies of one arena are two instances of
## one label, and this is the verb that admits that.
func scene_instances(label: StringName = &"") -> Array[NetwSceneHandle]:
	var out: Array[NetwSceneHandle] = []
	var found := scene_list() if label.is_empty() else scene_find_all(label)
	for entity: RID in found:
		var handle := scene_handle(entity)
		if handle != null:
			out.append(handle)
	return out


## Spawns a scene and returns its handle.
##
## The wrapper-tier twin of [method scene_create], for a caller that wants to
## act on the scene rather than address it.
## [br][br][b]Server Only.[/b]
func scene_spawn(
		recipe: Variant,
		isolation: SceneIsolation = SceneIsolation.SCENE_ISOLATION_NONE,
) -> NetwSceneHandle:
	return scene_handle(scene_create(recipe, isolation))


## Returns every live scene in the session.
func scene_list() -> Array[RID]:
	var out: Array[RID] = []
	for scene: RID in _native_core.live_scenes():
		if entity_get_node(scene) != null:
			out.append(scene)
	return out


## Admits [param peer] to [param scene].
##
## Admission is an interest-layer edge, so every descendant entity inherits it
## through the parent clamp rather than being enrolled one by one. The result is
## the admission itself rather than the attempt: an answer of [constant OK]
## means [method scene_admits] now holds, so a caller that ignores it cannot
## mistake a dropped admission for a completed one.
##
## [method NetwMultiplayerCore.scene_admit_peer] writes the boundary and reports
## the participant edge in one act, so a peer that was already admitted changes
## nothing and reports nothing.
## [br][br][b]Server Only.[/b]
func scene_admit(scene: RID, peer: int) -> Error:
	var verdict: Error = _native_core.scene_admit(scene, peer)
	if verdict == OK:
		interest_flush()
	return verdict


## Removes [param peer] from [param scene].
##
## A released peer is told so on its own side by
## [method NetwMultiplayerCore.scene_notify_released], because a client learns
## membership from awareness of the scene entity and would otherwise keep
## presenting a scene it no longer belongs to. The telling goes first, while the
## peer still holds the seat that names what it is being released from.
## [br][br][b]Server Only.[/b]
func scene_release(scene: RID, peer: int) -> void:
	if _native_core.scene_release(scene, peer):
		interest_flush()


## Returns whether [param scene] admits [param peer].
func scene_admits(scene: RID, peer: int) -> bool:
	return _native_core.scene_admits(scene, peer)


## Returns every peer [param scene] admits.
##
## [method NetwMultiplayerCore.scene_peers] reads the same admission boundary
## [method scene_admit] writes, so what is listed here is what admission means
## rather than a roster kept beside it.
func scene_get_peers(scene: RID) -> PackedInt32Array:
	return _native_core.scene_peers(scene)


## Returns every entity [param scene] encloses.
##
## Read off the container's subtree rather than off an enrollment book, so it
## answers alike on every peer and self-heals on reparent. A nested scene owns
## its own descendants, so the walk stops there and they report against it.
func scene_get_entities(scene: RID) -> Array[RID]:
	var out: Array[RID] = []
	out.assign(_native_core.scene_entities_under(scene))
	return out


## Registers [param callback] for one [enum SceneEvent] on [param scene].
##
## Plural where [method layer_set_monitor_callback] is singular, because several
## unrelated observers legitimately watch one scene, while a layer's monitor is
## the session's own. A per-object lifecycle edge stays a callback rather than a
## signal so the registration names the scene RID and survives whichever node
## currently stands in for it.
## [codeblock]
## api.scene_observe(arena, api.SceneEvent.SCENE_EVENT_PLAYER,
##     func(present: bool, player: RID) -> void:
##         if present:
##             score_board.add(player))
## [/codeblock]
func scene_observe(scene: RID, event: SceneEvent, callback: Callable) -> void:
	_native_core.scene_observe(scene, event, callback)


## Reverses [method scene_observe] for one [param callback].
func scene_unobserve(scene: RID, event: SceneEvent, callback: Callable) -> void:
	_native_core.scene_unobserve(scene, event, callback)


## Spawns a scene and declares it, returning its entity RID.
##
## Documented sugar, not a separate mechanism: this is
## [method spawn_registered] on the scene constructor followed by
## [method scene_declare]. A [code]null[/code] recipe builds a content-less
## scene, which is a pure admission boundary with no world of its own, useful
## for a lobby, a spectator scope, or a team channel.
## [codeblock]
## var arena := api.scene_create("res://levels/arena.tscn")
## var team := api.scene_create(null)          # boundary only, no content
## [/codeblock]
## [br][br][b]Server Only.[/b]
func scene_create(
		recipe: Variant,
		isolation: SceneIsolation = SceneIsolation.SCENE_ISOLATION_NONE,
) -> RID:
	assert(is_server(), "NetwMultiplayer.scene_create is server-only")
	var node := _native_core.scene_spawn(recipe, isolation)
	if not is_instance_valid(node):
		return RID()
	var entity := entity_of(node)
	scene_declare(entity)
	return entity


## Despawns [param scene], freeing its container after [param linger_seconds].
##
## A zero linger frees on the spot. A positive one drops the scene from the
## registry immediately and frees the container later, which is what gives
## in-flight frames addressed at its subtree somewhere to land. Retiring and
## destroying a scene were two verbs for this one operation at two linger
## values.
## [br][br][b]Server Only.[/b]
func scene_despawn(scene: RID, linger_seconds: float = 0.0) -> Error:
	assert(is_server(), "NetwMultiplayer.scene_despawn is server-only")
	return _native_core.scene_despawn(
		scene,
		0 if linger_seconds <= 0.0 else _linger_pumps(linger_seconds),
	)


# The pumps a linger in seconds is worth, at the rate this session pumps: its
# tick rate once a clock is configured, and the engine's own frame rate while
# the session pumps on polls alone.
func _linger_pumps(seconds: float) -> int:
	var clock := _native_core.clock_handle
	var rate := float(clock.tickrate) if clock.is_configured \
			else _POLL_PUMP_RATE
	return NetwClockHandle.pumps_for(seconds, rate)


## Moves [param entity] into [param destination].
##
## The returned [NetwPromise] settles once reparenting, membership, and
## persistence have all landed. [method NetwMultiplayerCore.scene_move_entity]
## mints it and rejects with [constant @GlobalScope.ERR_UNAVAILABLE] when either
## handle names no live node, so a move that cannot start still answers.
## [br][br][b]Server Only.[/b]
func scene_move(
		entity: RID,
		destination: RID,
		opts: NetwReparentOpts = null,
) -> NetwPromise:
	assert(is_server(), "NetwMultiplayer.scene_move is server-only")
	return _native_core.scene_move_entity(entity, destination, opts)


## Asks server authority to move the local player to [param destination].
##
## [param destination] accepts a declared stem or a scene file path. The
## returned promise rejects with [constant @GlobalScope.ERR_UNAUTHORIZED] when
## policy refuses, [constant @GlobalScope.ERR_SKIP] when a newer request
## supersedes it, and [constant @GlobalScope.ERR_TIMEOUT] when authority never
## answers.
## [br][br][b]Player request.[/b]
func scene_request(
		destination: Variant,
		args: Array = [],
) -> NetwPromise:
	var is_path: bool = destination is String
	return _native_core.scene_request_open(
		is_path,
		destination if is_path else StringName(destination),
		args,
		false,
		SCENE_REQUEST_DEADLINE,
	)


## Installs the single handler deciding player scene requests.
##
## [param handler] receives
## [code](participant: NetwParticipant, destination: Variant, args: Array)[/code]
## and returns an [enum @GlobalScope.Error]: [constant @GlobalScope.OK] admits
## the request, anything else refuses it and becomes the promise's settle code.
## A single-owner handler replaces a veto signal because several listeners
## vetoing one request has no defined resolution.
## [br][br][b]Server Only.[/b]
func scene_set_request_handler(handler: Callable) -> void:
	assert(is_server(), "NetwMultiplayer.scene_set_request_handler is server-only")
	_native_core.scene_set_request_handler(handler)


## Whether a request [param destination] names the scene declared [param label].
##
## A destination arrives in whichever form the requester used, so
## [method scene_request] with [code]&"Arena"[/code] and the same request by
## path both mean the same scene while comparing unequal. A handler that tests
## the raw value against one form silently admits or refuses the other, which is
## why the comparison lives here rather than in every handler.
## [codeblock]
## api.scene_set_request_handler(
##     func(_who, destination, _args) -> Error:
##         if api.scene_request_targets(destination, &"Arena"):
##             return OK
##         return ERR_UNAUTHORIZED,
## )
## [/codeblock]
func scene_request_targets(destination: Variant, label: StringName) -> bool:
	return _native_core.scene_request_targets(destination, label)


## Sets how far an admitted scene request reaches.
##
## Defaults to [constant SceneReach.SCENE_REACH_PARTICIPANT], because a request
## names one participant and moving the rest of the session is a larger claim
## than the requester made.
## [codeblock]
## # A lobby-and-match game where any request takes the whole session along.
## api.scene_set_request_reach(api.SceneReach.SCENE_REACH_SESSION)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func scene_set_request_reach(reach: SceneReach) -> void:
	assert(is_server(), "NetwMultiplayer.scene_set_request_reach is server-only")
	_native_core.request_reach = reach


## Returns how far an admitted scene request reaches.
func scene_get_request_reach() -> SceneReach:
	return _native_core.request_reach as SceneReach


# Enrolls [param entity] in whatever scene now encloses it, after a move that
# kept the route alive. Visibility is ancestry-derived and needs no help, but
# the scene's own player and tracked-node books have no edge to observe on a
# route-stable reparent, so this is the one call that refreshes them. It lives
# here rather than in the spawn pipeline so core never names a scene class.
func _scene_adopt_entity(entity: RID) -> void:
	var node := entity_get_node(entity)
	var wrapper := _entity_wrapper(entity)
	if node == null or wrapper == null:
		return
	var destination := scene_of(entity)
	if not destination.is_valid() or destination == entity:
		return
	# Only authority moves an admission edge. Every peer still re-anchors the
	# node itself, which parenting already did before this ran.
	if wrapper.peer_id != 0 and is_server():
		scene_admit(destination, wrapper.peer_id)


# The one writer behind scene_declare and scene_undeclare. Both share the
# pre-arm rule, so both share the check that enforces it.
#
# An entity with no record yet parks its answer instead of refusing. Only
# binding a node builds a record and binding a node arms it, so a caller
# composing an entity by hand has no moment between the two to write the facet
# in, and _apply_pending_scene_facet spends the parked answer at that moment.
func _write_scene_facet(entity: RID, declared: bool) -> Error:
	assert(is_server(), "NetwMultiplayer.scene_declare is server-only")
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		if not _native_core.liveness_core.entity_is_valid(entity):
			return ERR_DOES_NOT_EXIST
		_pending_scene_facets[entity] = declared
		return OK
	if wrapper.stage != NetwEntity.STAGE_UNBOUND:
		return ERR_UNCONFIGURED
	wrapper.declares_scene = declared
	if not declared:
		wrapper.scene_label = &""
	return OK


# Writes what scene_declare parked for [param entity] onto its fresh
# [param wrapper], before the bind that arms it.
func _apply_pending_scene_facet(entity: RID, wrapper: NetwEntity) -> void:
	if not _pending_scene_facets.has(entity):
		return
	var declared: bool = _pending_scene_facets[entity]
	_pending_scene_facets.erase(entity)
	wrapper.declares_scene = declared
	if not declared:
		wrapper.scene_label = &""

#endregion

#region Scene machine

# Emitted when a constructed scene container enters the tree on this peer.
signal _scene_spawned(scene: Node)

# Emitted when an active scene begins processing.
signal _scene_activated(scene: Node)

# Emitted when an active scene leaves the session.
signal _scene_despawned(scene: Node)

# Emitted after an entity moves between replicated scenes.
signal _scene_entity_moved(entity: NetwEntity, from: Node, to: Node)

# Emitted after the server's startup scenes have all spawned.
signal _startup_scenes_spawned()

# The record plane behind the flat scene family: the observer routing table,
# the published declaration, and the in-flight request. Untyped because the
# record plane's class registers internally, so no GDScript may name it.
var _scene_core

# The resource the published declaration was read from, held for the session
# lifetime rather than keyed to the registrar node, so a freed registrar
# neither drops it nor leaks. The declaration itself lives in the record plane,
# so an edit here republishes rather than being read back later.
var _scene_declared_config: NetwSceneConfig
var _scene_constructor_registered := false
var _scene_local_participant: NetwParticipant
# The tree-less host presentation view the session parents under its root,
# freed on session end. Stays null when an owning MultiplayerTree already
# created its own on its branch.
var _scene_host_view: HostSceneView
# The client-side awareness mirror each live scene's admission reads from, or
# null on a server, which reads its own admission edges instead. A key is what
# says the scene's admission is wired, so wiring it twice is refused. The peers
# admitted before their roster row landed are the record plane's, on the live
# row itself.
var _scene_admission_layers: Dictionary[RID, NetwInterestLayer] = { }

## Seconds a [method scene_request] waits for a server answer before it
## resolves [constant @GlobalScope.ERR_TIMEOUT].
##
## The fallback a request takes when the target scene declared no deadline of
## its own through [method NetwScriptModel.SceneMarkConfig.deadline].
const SCENE_REQUEST_DEADLINE := 10.0

# The registry id the session registers its scene constructor under, so a
# session with no MultiplayerSceneManager still reconstructs scene wrappers.
const _SCENE_CONSTRUCTOR_ID := &"__netw_scene__"

const _SCENE_REFRESH_KEY := &"scene-refresh-current"

# The reparent reason a move records when the caller named none.
# NetwReparentOpts defaults to empty, which reads in a journal as a reparent
# nobody attributed.
const _SCENE_MOVE_REASON := &"scene_move"
const _SCENE_SYNC_LOCAL_KEY := &"scene-sync-local"


# Wires the scene machine onto the session plane, once, from the constructor.
func _scene_install() -> void:
	_scene_core = _native_core.scene_core
	_scene_register_constructor()
	_native_core.connect_once(
		_native_core.local_participant_joined,
		_scene_bind_local_participant,
	)
	_native_core.connect_once(
		_native_core.local_scene_changed,
		_scene_on_local_changed,
	)
	_native_core.connect_once(
		_native_core.session_entered,
		_scene_on_session_entered,
	)
	_native_core.connect_once(
		_native_core.session_reclaimed,
		_scene_on_session_reclaimed,
	)
	_native_core.connect_once(_native_core.entity_live, _scene_on_entity_live)
	_native_core.set_scene_mark_reader(_scene_read_mark)
	_native_core.set_scene_participant_edge(_scene_report_participant)
	_native_core.set_scene_carry_move(_scene_carry_entity_move)
	_native_core.channel_book.register_protocol(
		NetwFrameEnvelope.Channel.SESSION_SCENE_REQUEST,
		_scene_handle_request_frame,
	)
	_native_core.channel_book.register_protocol(
		NetwFrameEnvelope.Channel.SESSION_SCENE_RESULT,
		_native_core.scene_receive_result_frame,
	)
	_native_core.channel_book.register_protocol(
		NetwFrameEnvelope.Channel.SESSION_SCENE_RELEASED,
		_scene_handle_released_frame,
	)
	if not Netw.is_test_env():
		var scene_tree := Engine.get_main_loop() as SceneTree
		if scene_tree:
			_native_core.connect_once(
				scene_tree.scene_changed,
				_scene_on_native_changed,
			)


# Registers the session's scene declaration. The session owns [param config]
# for its lifetime, so a registrar node need not deregister it on the way out.
func _scene_configure(config: NetwSceneConfig) -> void:
	if _scene_declared_config != config:
		_scene_release_declaration()
		_scene_declared_config = config
		if _scene_declared_config:
			_native_core.connect_once(
				_scene_declared_config.declaration_changed,
				_scene_publish_declaration,
			)
	_scene_publish_declaration()
	_scene_register_constructor()
	_scene_settle_refresh()


# Drops the session's scene declaration.
func _scene_deconfigure() -> void:
	_scene_release_declaration()
	_scene_settle_refresh()


# Copies the authored rows into the record plane, which is the only place a
# session reads them from. Every row crosses on every publish, so a declaration
# is replaced whole rather than amended.
func _scene_publish_declaration() -> void:
	if _scene_declared_config == null:
		_scene_core.set_scene_anchor(null)
		_scene_core.declaration_drop()
		return
	_scene_core.set_scene_anchor(_scene_declared_config.anchor)
	_scene_core.declaration_open(
		_scene_declared_config.isolation,
		_scene_declared_config.level_spawn_function,
	)
	for packed: PackedScene in _scene_declared_config.initial_scenes:
		if packed:
			_scene_core.declaration_row(
				NetwMultiplayerCore.scene_packed_stem(packed),
				packed.resource_path,
				null,
				true,
			)
	for label: StringName in _scene_declared_config.declared_scene_names():
		var packed := _scene_declared_config.declared_scene(label)
		_scene_core.declaration_row(
			label,
			packed.resource_path if packed else "",
			_scene_declared_config.declared_spawn_data(label),
			false,
		)
	_scene_core.declaration_publish()


# Lets go of the authoring resource, the edge that republished it, and the
# declaration it published.
func _scene_release_declaration() -> void:
	if _scene_declared_config \
			and _scene_declared_config.declaration_changed.is_connected(
				_scene_publish_declaration,
			):
		_scene_declared_config.declaration_changed.disconnect(
			_scene_publish_declaration,
		)
	_scene_declared_config = null
	_scene_core.set_scene_anchor(null)
	_scene_core.declaration_drop()


# Binds the accepted local [param participant] to the presented scene.
func _scene_bind_local_participant(participant: NetwParticipant) -> void:
	if _scene_local_participant == participant:
		_scene_settle_sync_local()
		return
	_scene_local_participant = participant
	_scene_settle_sync_local()
	_scene_settle_refresh()


# Infers the local participant's scene from wrapper awareness.
func _scene_sync_local_participant() -> void:
	if _scene_local_participant == null:
		return
	_native_core.scene_seat_sync(_scene_local_participant.peer_id)


# Recomputes what this peer presents from its local presentation state.
func _scene_refresh_current() -> void:
	_scene_core.current_scene = _scene_resolve_current()


# Recomputes local presentation at the next settle. Keyed, so a cascade of
# scene edges recomputes once, after all of them have landed.
func _scene_settle_refresh() -> void:
	_native_core.settle_schedule(_scene_refresh_current, _SCENE_REFRESH_KEY)


# Re-reads the local participant's scene from awareness at the next settle,
# which is the pump that also applies the awareness it reads.
func _scene_settle_sync_local() -> void:
	_native_core.settle_schedule(
		_scene_sync_local_participant,
		_SCENE_SYNC_LOCAL_KEY,
	)


# Releases SceneTree and participant signal connections.
func _scene_dispose() -> void:
	_scene_local_participant = null
	_scene_core.request_abandon(ERR_UNAVAILABLE)
	if _native_core.local_participant_joined.is_connected(
		_scene_bind_local_participant,
	):
		_native_core.local_participant_joined.disconnect(
			_scene_bind_local_participant,
		)
	if _native_core.local_scene_changed.is_connected(_scene_on_local_changed):
		_native_core.local_scene_changed.disconnect(_scene_on_local_changed)
	if _native_core.session_entered.is_connected(_scene_on_session_entered):
		_native_core.session_entered.disconnect(_scene_on_session_entered)
	if _native_core.session_reclaimed.is_connected(
		_scene_on_session_reclaimed,
	):
		_native_core.session_reclaimed.disconnect(_scene_on_session_reclaimed)
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree and scene_tree.scene_changed.is_connected(
		_scene_on_native_changed,
	):
		scene_tree.scene_changed.disconnect(_scene_on_native_changed)
	_scene_release_declaration()
	# A session that ends has no next pump, so a window still open here would
	# hold its wrapper forever.
	for scene: RID in _scene_core.retiring_scenes():
		var retired := _scene_node_of(scene)
		if retired:
			retired.queue_free()
	_scene_core.clear()


# Active scene containers keyed by their label, one entry per stem. The stem is
# not unique: this holds the most recent, where scene_instances answers with
# all of them.
func _scene_nodes_by_label() -> Dictionary[StringName, Node]:
	var out: Dictionary[StringName, Node] = { }
	for scene: RID in _scene_core.live_scenes():
		var stem: StringName = _scene_core.stem_of(scene)
		if _scene_core.scene_named(stem) != scene:
			continue
		var node := _scene_node_of(scene)
		if node:
			out[stem] = node
	return out


# The container node standing in for [param label], for the paths that mount,
# free, and reparent it.
func _scene_container_named(label: StringName) -> Node:
	return _scene_node_of(_scene_core.scene_named(label))


# The container node one live scene identity stands for, or null.
func _scene_node_of(scene: RID) -> Node:
	return _native_core.wrapper_owner(scene) as Node


# Every live scene container, in registration order.
func _scene_live_nodes() -> Array[Node]:
	var out: Array[Node] = []
	for scene: RID in _scene_core.live_scenes():
		var node := _scene_node_of(scene)
		if node:
			out.append(node)
	return out


# The container node this peer currently presents, or null.
func _scene_current_node() -> Node:
	return _scene_node_of(_scene_core.current_scene)


# Activates a declared label or a file-backed PackedScene, on authority.
func _scene_activate_ref(scene_ref: Variant) -> Node:
	assert(_native_core.is_server(), "Scene activation is server-only.")
	var active := _native_core.scene_activate(scene_ref)
	if active:
		_scene_activated.emit(active)
	return active


# Ensures the declared scene named [param label] is active and forces its level
# to process, on authority.
func _scene_activate_named(label: StringName) -> Node:
	assert(_native_core.is_server(), "Scene activation is server-only.")
	var active := _native_core.scene_activate_named(label)
	if active:
		_scene_activated.emit(active)
	return active


# Constructs and replicates the declared scene named [param label].
func _scene_spawn_declared(label: StringName) -> void:
	_native_core.scene_spawn_declared(label)


# Disables processing for the active scene named [param label], on authority.
func _scene_freeze_named(label: StringName) -> void:
	assert(_native_core.is_server(), "Scene freezing is server-only.")
	_native_core.scene_freeze(label)


# Removes the active scene named [param label] from the session, on authority.
func _scene_destroy_named(label: StringName) -> void:
	assert(_native_core.is_server(), "Scene destruction is server-only.")
	_native_core.scene_destroy(label)


# Removes [param label] from the active registry now, then frees the wrapper
# after [param drain_pumps] pumps, which is how long a path stays resolvable for
# frames still in flight.
func _scene_retire_named(label: StringName, drain_pumps: int = 8) -> void:
	assert(_native_core.is_server(), "Scene retirement is server-only.")
	_native_core.scene_retire_named(label, drain_pumps)


## Answers a [NetwPromise] resolving with the [Node] a freshly instantiated
## [param player] enters, honoring a stored teleport scene over
## [param fallback].
##
## The answer waits on spawn hydration, so persisted spawn state rides the
## SPAWN frame. It is a promise rather than the node itself because the wait is
## a database read: the caller subscribes instead of awaiting, and an archetype
## that wants no hydration resolves before this returns.
## [codeblock]
##     api.scene_resolve_hydrated_spawn(player, container).then(
##         func(entered: Node) -> void:
##             NetwEntity.of(entered).scene.add_player(NetwEntity.of(player)),
##     )
## [/codeblock]
## [br][br][b]Server Only.[/b]
func scene_resolve_hydrated_spawn(
		player: Node,
		fallback: Node,
) -> NetwPromise:
	var answer := NetwPromise.new()
	var entity := NetwEntity.of(player)
	var engine: NetwPersistenceEngine = null
	if entity:
		engine = entity.persistence as NetwPersistenceEngine
	if engine == null or not engine.wants_spawn_hydration():
		answer.resolve(_scene_spawn_scene_of(player, fallback))
		return answer
	engine.hydrate().when_settled(
		func() -> void: answer.resolve(
			_scene_spawn_scene_of(player, fallback),
		),
	)
	return answer


# The scene a hydrated player belongs in: the stem its TPComponent restored,
# activated if it is not already live, and [param fallback] when it stored none.
#
# Persistence stores the stem, not an identity, so restoring means "put this
# player into an instance of this stem". With several instances live any of them
# satisfies the save, which is the semantic a non-unique stem carries.
func _scene_spawn_scene_of(player: Node, fallback: Node) -> Node:
	var tp: TPComponent = player.get_node_or_null("%TPComponent")
	if not tp or tp.current_scene_name.is_empty():
		return fallback
	var label := StringName(tp.current_scene_name)
	if _scene_container_named(label) == null:
		_scene_activate_named(label)
	var active := _scene_container_named(label)
	return active if active else fallback


# Moves [param entity] into [param destination], named as a Node rather than as
# an entity handle. The promise resolves after both physics flushes, replicated
# reparenting, participant membership, and persistence have settled.
func _scene_move_entity_to(
		entity: NetwEntity,
		destination: Variant,
		opts: NetwReparentOpts = null,
) -> NetwPromise:
	assert(_native_core.is_server(), "Scene movement is server-only.")
	var promise := NetwPromise.new()
	_scene_carry_entity_move(entity, destination, opts, promise)
	return promise


# Replaces the active SINGLE scene and moves every participant into it.
func _scene_change_session(destination: Variant) -> NetwPromise:
	assert(_native_core.is_server(), "Scene changes are server-only.")
	var promise := NetwPromise.new()
	_scene_change_single(destination, promise)
	return promise


## The muscle-memory front door mirroring
## [method SceneTree.change_scene_to_file].
##
## [param requester] carries the session and the local mover, so server
## authority runs the change directly while a client turns it into a
## [method scene_request] the server decides. Both resolve to the same verbs, so
## the front door and the captured native change never diverge.
## [codeblock]
## var promise := Netw.change_scene_to_file(self, "res://arena.tscn")
## promise.when_settled(
##     func() -> void:
##         if promise.code != OK:
##             status.text = "Could not change scene.",
## )
## [/codeblock]
func scene_change_to_file(requester: Node, path: String) -> NetwPromise:
	return _scene_front_door_change(requester, ResourceUID.ensure_path(path))


## The front door for a file-backed [PackedScene], mirroring
## [method SceneTree.change_scene_to_packed]. See
## [method scene_change_to_file].
func scene_change_to_packed(
		requester: Node,
		packed: PackedScene,
) -> NetwPromise:
	if NetwMultiplayerCore.scene_destination_kind(packed) \
			!= NetwMultiplayerCore.SCENE_DESTINATION_PACKED:
		push_error(
			"scene_change_to_packed needs a file-backed PackedScene so a "
			+ "client can request it by path.",
		)
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"scene_change_to_packed: the PackedScene has no resource path",
		)
	return _scene_front_door_change(requester, packed.resource_path)


## Re-enters the scene this peer currently presents, mirroring
## [method SceneTree.reload_current_scene]. See
## [method scene_change_to_file].
func scene_reload_current(requester: Node) -> NetwPromise:
	var here := _scene_current_node()
	if here == null or _scene_level_of(here) == null:
		push_error("scene_reload_current: this peer presents no scene.")
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"scene_reload_current: this peer presents no scene",
		)
	var path := _scene_level_of(here).scene_file_path
	if path.is_empty():
		push_error("scene_reload_current: the current scene has no file path.")
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"scene_reload_current: the current scene has no file path",
		)
	return _scene_front_door_change(requester, ResourceUID.ensure_path(path))


# Routes a front-door change to authority's verb or a client's request. Server
# authority applies the change directly at the configured reach; a client
# asks through the same request path the server policy decides.
func _scene_front_door_change(requester: Node, path: String) -> NetwPromise:
	if path.is_empty():
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"a scene change needs a live session and a scene path",
		)
	if not _native_core.is_server():
		return _native_core.scene_request_open(
			true,
			path,
			[],
			false,
			SCENE_REQUEST_DEADLINE,
		)
	var packed := NetwMultiplayerCore.scene_packed_at(path)
	if packed == null:
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"no scene loads from " + path,
		)
	return _scene_apply_player_change(
		_scene_requester_participant(requester),
		packed,
		false,
	)


# The participant a front-door call moves: the one owning the requester node, or
# the local participant when the requester is not itself a player entity.
func _scene_requester_participant(requester: Node) -> NetwParticipant:
	if is_instance_valid(requester):
		var entity := NetwEntity.of(requester)
		if entity and entity.participant:
			return entity.participant
	return _native_core.participant_admitted_local() as NetwParticipant


# Converts a marked scene's native tree entry into the role's replicated verb.
# Called by the detach hook only for a live session and a non-framework entry.
func _scene_handle_native_entry(node: Node) -> void:
	var path := node.scene_file_path
	match _native_core.scene_capture_verdict(path):
		NetwMultiplayerCore.SCENE_CAPTURE_REFUSED:
			# An in-memory instance has no path to request, mirroring activate's
			# rejection of a pathless PackedScene.
			push_error(
				"A marked scene entered natively has no resource_path, so it "
				+ "cannot become a server request. Instantiate it from a saved "
				+ "scene file.",
			)
		NetwMultiplayerCore.SCENE_CAPTURE_REQUEST:
			_scene_detach_and_request(node, path)
		var verdict:
			_scene_discard_and_respawn(node, path, verdict)


# Server: the native instance already ran _ready outside the wrapper, gate, and
# hydration, so it is discarded and re-spawned authoritatively through the
# pipeline, the same double-instantiation bare-level adoption already pays. A
# listen host under CONCURRENT reads the bare call as "move me", the same
# meaning a client's bare call carries, so the host relocates rather than
# spawning a world it does not enter.
func _scene_discard_and_respawn(node: Node, path: String, verdict: int) -> void:
	_scene_hide_and_free(node)
	var packed := NetwMultiplayerCore.scene_packed_at(path)
	if packed == null:
		return
	match verdict:
		NetwMultiplayerCore.SCENE_CAPTURE_CHANGE_SESSION:
			_scene_change_session(packed)
		NetwMultiplayerCore.SCENE_CAPTURE_MOVE_ME:
			_scene_apply_player_change(
				_native_core.participant_admitted_local() as NetwParticipant,
				packed,
				false,
			)
		_:
			_scene_activate_ref(packed)


# Client: detach the local instance and wait for the authoritative scene to
# arrive as spawn frames, the byte-identical state a freshly admitted client is
# already in.
func _scene_detach_and_request(node: Node, path: String) -> void:
	var mark := _native_core.scene_mark_of(node.get_script())
	_scene_invoke_pending_hook(node, mark.pending_method)
	_scene_hide_and_free(node)
	_native_core.scene_request_open(
		true,
		path,
		[],
		true,
		mark.deadline_or(SCENE_REQUEST_DEADLINE),
	)


# Runs the marked scene's on_pending hook, a side-effect callback for the game
# to present its own loading UI. Called on the native instance before it is
# freed.
# The game tears its UI down when the captured change settles, since a denied
# or timed-out request never reaches the scene that would clear it.
func _scene_invoke_pending_hook(node: Node, pending_method: StringName) -> void:
	if pending_method.is_empty():
		return
	if node.has_method(pending_method):
		node.call(pending_method)


# Hides and frees a detached native instance. The free defers so the engine
# finishes assigning current_scene before the node leaves the tree.
func _scene_hide_and_free(node: Node) -> void:
	node.process_mode = Node.PROCESS_MODE_DISABLED
	if node is CanvasItem or node is Node3D:
		node.set(&"visible", false)
	node.queue_free()


# Registers the session's host-less scene constructor once per session. The
# constructor is the session's own native method, so its argument schema is
# declared here under the same id the wire addresses the recipe by rather than
# reflected off a script the native session does not have. The two lifecycle
# edges are handed over in the same breath, because the constructor runs on
# every peer and is the only place that sees a wrapper before it enters a tree.
func _scene_register_constructor() -> void:
	if _scene_constructor_registered:
		return
	_scene_core.set_container_lifecycle(
		_scene_on_container_entered,
		_scene_on_container_exited,
	)
	_native_core.spawn_register_constructor(
		_SCENE_CONSTRUCTOR_ID,
		_native_core.scene_spawn_node,
		[TYPE_NIL, TYPE_INT],
	)
	_scene_constructor_registered = true


# Server startup spawns the declared initial scenes, deferred one idle frame so
# the session has settled, then announces completion.
func _scene_on_session_entered() -> void:
	if _native_core.is_server():
		_scene_spawn_initial.call_deferred()
	_scene_ensure_host_view.call_deferred()


# Turns one direct packed level a scoped embedding offered into the default
# SINGLE scene declaration, run once from the settle step so the adopted
# declaration is registered before the host view and startup spawns read it.
# Skipped when an explicit declaration already won or a manager already authored
# the session, and a no-op under a root install, which offers no bare level.
func _scene_adopt_bare_level(level: Node) -> void:
	if not is_instance_valid(level):
		return
	if _scene_core.declaration_is_published():
		return
	var root := _native_core.session_root()
	if root == null:
		return
	for child in root.get_children():
		if child is MultiplayerSceneManager:
			return
	var path := ResourceUID.ensure_path(level.scene_file_path)
	var parent := level.get_parent()
	if parent:
		parent.remove_child(level)
	level.free()
	var manager := MultiplayerSceneManager.new()
	manager.name = &"MultiplayerSceneManager"
	manager._configure_default(path)
	root.add_child(manager)


# Builds the one [HostSceneView] a listen-server host presenting an offscreen
# world is missing. Whether this peer presents as such a host is
# [method NetwMultiplayerCore.presents_as_listen_host], which is asked again on
# each of the two edges that can turn it over, the settle step and the session
# entering online, rather than cached at the first one. The view parents under
# [method NetwMultiplayerCore.session_root] so it survives a native scene change
# alongside the session content. Idempotent, and skipped when a view already
# owns the display.
func _scene_ensure_host_view() -> void:
	if not _native_core.presents_as_listen_host():
		return
	var root := _native_core.session_root()
	if root == null or not _native_core.scene_hosts_isolated_world():
		return
	for child in root.get_children():
		if child is HostSceneView:
			return
	var view := HostSceneView.new()
	view.name = &"HostSceneView"
	_scene_host_view = view
	root.add_child(view)


func _scene_spawn_initial() -> void:
	_native_core.scene_spawn_initial()
	_startup_scenes_spawned.emit()


func _scene_on_container_entered(scene_node: Node) -> void:
	var content := _scene_level_of(scene_node)
	if content == null:
		return
	var label := StringName(content.name)
	# The stem is a non-unique label: N instances of one level are N scenes,
	# each owning its own route-keyed admission boundary. The book answers "an
	# instance of this stem" by stem and "every instance" in registration order.
	var seat := _native_core.entity_of(scene_node)
	_scene_core.scene_enter(
		seat,
		label,
		_native_core.scene_owns_its_world(seat),
	)
	_scene_open_admission(scene_node)
	_scene_ensure_host_view()
	_scene_spawned.emit(scene_node)
	var record := NetwEntity.of(scene_node)
	if record:
		# The session records and announces as one act, and resolves the facet
		# from the container's own record. The container names itself, because
		# a mounted scene declares its own facet.
		_native_core.scene_publish_live(
			record.route,
			seat,
			String(scene_node.name),
		)
	_scene_settle_sync_local()
	_scene_settle_refresh()


func _scene_on_container_exited(scene_node: Node) -> void:
	_scene_close_admission(scene_node)
	_native_core.scene_forget(scene_node)
	_scene_despawned.emit(scene_node)


# Opens one scene's admission row. A server reads its own admission edges. A
# client has none to read, so it learns membership from awareness of the scene
# entity itself, which is the same fact arriving the only way a client can see
# it.
func _scene_open_admission(scene_node: Node) -> void:
	var scene := _native_core.entity_of(scene_node)
	if not scene.is_valid() or _scene_admission_layers.has(scene):
		return
	_scene_admission_layers[scene] = null
	if _native_core.is_server():
		for peer: int in _native_core.scene_peers(scene):
			_scene_report_participant(scene, peer, true)
		return
	_native_core.connect_once(
		_native_core.participant_joined,
		_scene_on_participant_joined,
	)
	var boundary := _native_core.scene_layer_view(scene) as NetwInterestLayer
	if boundary == null:
		return
	_scene_admission_layers[scene] = boundary
	_native_core.connect_once(
		boundary.entity_visible,
		_scene_on_visible.bind(scene),
	)
	_native_core.connect_once(
		boundary.entity_hidden,
		_scene_on_hidden.bind(scene),
	)
	var record := NetwEntity.of(scene_node)
	if record and boundary.has_entity(record):
		_scene_report_local_participant(scene, true)


# Closes one scene's admission row, clearing the membership of everyone still
# recorded as being in it.
func _scene_close_admission(scene_node: Node) -> void:
	var scene := _native_core.entity_of(scene_node)
	if not _scene_admission_layers.has(scene):
		return
	var layer: NetwInterestLayer = _scene_admission_layers[scene]
	if layer:
		var shown := _scene_on_visible.bind(scene)
		if layer.entity_visible.is_connected(shown):
			layer.entity_visible.disconnect(shown)
		var hidden := _scene_on_hidden.bind(scene)
		if layer.entity_hidden.is_connected(hidden):
			layer.entity_hidden.disconnect(hidden)
	_scene_admission_layers.erase(scene)
	if _native_core.participant_admitted_local():
		_native_core.participant_seat_clear(_native_core.get_unique_id(), scene)
	for peer: int in _native_core.scene_peers(scene):
		if _native_core.participant_admitted_of(peer):
			_native_core.participant_seat_clear(peer, scene)


# Records one participant's arrival or departure and reports it to observers,
# installed on the record plane so every boundary write reports its own edge. A
# peer admitted before its roster row lands is parked and retried on join.
func _scene_report_participant(scene: RID, peer: int, present: bool) -> void:
	if _native_core.participant_admitted_of(peer) == null:
		if present:
			_scene_core.admission_park(scene, peer)
		return
	_scene_core.admission_unpark(scene, peer)
	if present:
		_native_core.participant_seat_move(peer, scene)
	else:
		_native_core.scene_seat_clear_deferred(peer, scene)
	_scene_core.dispatch(
		scene,
		SceneEvent.SCENE_EVENT_PARTICIPANT,
		present,
		peer,
	)


func _scene_on_visible(entity: NetwEntity, scene: RID) -> void:
	if entity == _native_core.wrapper_of(scene):
		_scene_report_local_participant(scene, true)


func _scene_on_hidden(entity: NetwEntity, scene: RID) -> void:
	if entity == _native_core.wrapper_of(scene):
		_scene_report_local_participant(scene, false)


func _scene_report_local_participant(scene: RID, present: bool) -> void:
	if _native_core.participant_admitted_local():
		_scene_report_participant(scene, _native_core.get_unique_id(), present)


# Retries the admission fact for a peer whose roster row landed after it was
# already admitted.
func _scene_on_participant_joined(participant: NetwParticipant) -> void:
	var local := _native_core.participant_admitted_local()
	for scene: RID in _scene_core.live_scenes():
		if _scene_core.admission_is_parked(scene, participant.peer_id):
			_scene_report_participant(scene, participant.peer_id, true)
			continue
		var layer := _scene_admission_layers.get(scene) as NetwInterestLayer
		if layer == null or participant != local:
			continue
		var record := _native_core.wrapper_of(scene) as NetwEntity
		if record and layer.has_entity(record):
			_scene_report_participant(scene, participant.peer_id, true)


# Watches one entity's tree edges for as long as it is live. The scene is
# resolved at the moment of each edge rather than remembered, so a reparent
# across scenes reports a leave against the old scene, which tree_exiting still
# sees, and an enter against the new one, with nothing handing the entity over.
func _scene_on_entity_live(_route: int, entity: NetwEntity) -> void:
	_scene_watch_entity(entity)


# Reports [param entity]'s scene edges for as long as it lives. The liveness bus
# covers every routed entity, and this is also the door for one that never
# routes, such as a player seated directly into a scene, whose admission would
# otherwise outlive it.
func _scene_watch_entity(entity: NetwEntity) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	var node := entity.owner
	# The id is resolved once, here, while the entity is alive. Re-resolving per
	# edge would mint a fresh one for an entity on its way out, because a despawn
	# clears the record's id before the node leaves the tree.
	var subject := _native_core.entity_of(node)
	if not subject.is_valid():
		return
	var entered := _scene_report_entity_edge.bind(entity, subject, true)
	var exited := _scene_report_entity_edge.bind(entity, subject, false)
	if not node.tree_entered.is_connected(entered):
		node.tree_entered.connect(entered)
	if not node.tree_exiting.is_connected(exited):
		node.tree_exiting.connect(exited)
	if node.is_inside_tree():
		_scene_report_entity_edge(entity, subject, true)
	# A player's admission is released against the scene it was seated into.
	# Teardown cannot resolve that scene, because the node is already leaving the
	# tree the walk would follow, so the seat is remembered here instead.
	if entity.peer_id != 0:
		var seat := _native_core.entity_scene_of(subject)
		if seat.is_valid() and seat != subject:
			var leaving := _scene_on_player_exiting.bind(
				seat,
				subject,
				entity,
				entity.peer_id,
			)
			if not node.tree_exiting.is_connected(leaving):
				node.tree_exiting.connect(leaving)


# Settles the release so it can tell a free from a reparent, which the exit
# signal itself cannot.
func _scene_on_player_exiting(
		seat: RID,
		subject: RID,
		entity: NetwEntity,
		peer: int,
) -> void:
	_native_core.settle_schedule(
		_scene_release_departed_player.bind(seat, subject, entity, peer),
		NetwMultiplayerCore.scene_seat_release_key(peer, seat),
	)


# Reports one entity crossing a scene boundary. The node still standing is the
# one fact the record plane cannot answer, because subject was resolved while
# the entity was alive and stays answerable after it is not.
func _scene_report_entity_edge(
		entity: NetwEntity,
		subject: RID,
		present: bool,
) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	_native_core.scene_report_entity_edge(
		subject,
		present,
		entity.peer_id != 0,
	)


# Releases a player's admission once it is clear the player left for good. The
# strong reference this holds is the only thing that can still answer whether
# the mover survived the window, because the record it was known by is cleared
# before its node leaves the tree.
func _scene_release_departed_player(
		scene: RID,
		subject: RID,
		entity: NetwEntity,
		peer: int,
) -> void:
	_native_core.scene_release_departed(
		scene,
		subject,
		is_instance_valid(entity) and is_instance_valid(entity.owner),
		peer,
	)


# Runs one guarded replicated entity move, installed on the record plane as the
# session's carry.
func _scene_carry_entity_move(
		entity: NetwEntity,
		destination: Variant,
		opts: NetwReparentOpts,
		promise: NetwPromise,
) -> void:
	var mover_live := entity != null and is_instance_valid(entity.owner)
	var target := _scene_resolve_destination(destination) if mover_live else null
	var source := _native_core.scene_containing(entity.owner) if mover_live \
			else null
	match NetwMultiplayerCore.scene_move_verdict(
		mover_live,
		target != null,
		source == target,
	):
		NetwMultiplayerCore.SCENE_MOVE_REFUSED:
			promise.reject(ERR_UNAVAILABLE)
		NetwMultiplayerCore.SCENE_MOVE_ALREADY_THERE:
			promise.resolve(OK)
		_:
			_scene_carry_move(
				entity,
				target,
				opts,
				_scene_arrive.bind(entity, source, target, promise),
			)


# The one branch of a move that spends physics frames. It hands the move on
# through [param arrived] rather than returning, because the frames are
# subscribed to rather than awaited and the caller has to keep going without a
# coroutine of its own.
func _scene_carry_move(
		entity: NetwEntity,
		target: Node,
		opts: NetwReparentOpts,
		arrived: Callable,
) -> void:
	if opts == null:
		opts = NetwReparentOpts.new()
		opts.reason = _SCENE_MOVE_REASON
	var guard := AreaReparentGuard.new(entity.owner)
	guard.flush_then(
		_scene_swap_move_parent.bind(guard, entity, target, opts, arrived),
	)


# Reparents the guarded body once the physics server has dropped it from the
# source areas, then opens the second window so the destination overlaps are
# settled before the suppression ends.
func _scene_swap_move_parent(
		guard: AreaReparentGuard,
		entity: NetwEntity,
		target: Node,
		opts: NetwReparentOpts,
		arrived: Callable,
) -> void:
	entity.reparent_to(_scene_level_of(target), opts)
	guard.flush_then(_scene_end_move_guard.bind(guard, arrived))


# Ends the suppression window and lets the move answer.
func _scene_end_move_guard(guard: AreaReparentGuard, arrived: Callable) -> void:
	guard.release()
	arrived.call()


# What a completed move owes everyone watching, once the body is in place.
func _scene_arrive(
		entity: NetwEntity,
		source: Node,
		target: Node,
		promise: NetwPromise,
) -> void:
	var participant := entity.participant
	if participant:
		participant.current_scene = _scene_handle_for(target)
	var persistence := entity.persistence
	if persistence:
		persistence.flush()
	_scene_entity_moved.emit(entity, source, target)
	promise.resolve(OK)


# Replaces what the session presents. Every participant ends up in the target
# wherever it started, and every other live scene retires, so the answer does
# not depend on which scene happened to be first. A second transition entered
# while one is still moving peers resolves UNAVAILABLE, so concurrent approvals
# never interleave moves against a half-transitioned roster.
func _scene_change_single(
		destination: Variant,
		promise: NetwPromise,
) -> void:
	if not _scene_core.transition_open():
		promise.reject(ERR_UNAVAILABLE)
		return
	var target := _scene_existing_destination(destination)
	if target == null:
		_scene_core.replacing = true
		target = _scene_activate_ref(destination)
		_scene_core.replacing = false
	if target == null:
		_scene_core.transition_close()
		promise.reject(ERR_UNAVAILABLE)
		return
	var sources: Array[Node] = []
	for active: Node in _scene_live_nodes():
		if is_instance_valid(active) and active != target:
			sources.append(active)
	_scene_core.transition_arm(target, sources, promise)
	_scene_carry_transition()


# Moves the participants the transition still owes, each in turn. A move that
# settled on the spot continues the walk here and a pending one continues it on
# its own edge, which is the same order a loop-carried await reached them in.
func _scene_carry_transition() -> void:
	var target: Node = _scene_core.transition_target()
	var mover := _scene_next_mover()
	while mover != null:
		var moving := _scene_move_entity_to(mover, target)
		if not moving.is_settled:
			moving.settled.connect(
				_scene_resume_transition.bind(mover, moving),
				CONNECT_ONE_SHOT,
			)
			return
		if not _scene_core.transition_accept(moving.code, mover.peer_id):
			return
		mover = _scene_next_mover()
	_scene_land_transition()


# Continues the walk on a pending move's own settle edge.
func _scene_resume_transition(
		mover: NetwEntity,
		moving: NetwPromise,
) -> void:
	if _scene_core.transition_accept(moving.code, mover.peer_id):
		_scene_carry_transition()


# The next participant the transition still owes a move, or null when every
# source is walked.
func _scene_next_mover() -> NetwEntity:
	return _scene_core.transition_next_mover(_scene_players_in) as NetwEntity


# Admits everyone the moves did not carry, retires the sources, and answers.
func _scene_land_transition() -> void:
	var target: Node = _scene_core.transition_target()
	var arrived := _scene_handle_for(target)
	for participant: NetwParticipant in _native_core.participant_admitted_all():
		if _scene_core.transition_moved(participant.peer_id):
			continue
		participant.current_scene = arrived
		var admitted := arrived.admit(participant)
		if admitted != OK:
			_scene_core.transition_fail(admitted)
			return
	var sources: Array = _scene_core.transition_sources()
	for source: Node in sources:
		var content := _scene_level_of(source)
		if content != null:
			_scene_destroy_named(StringName(content.name))
	_scene_settle_refresh()
	_scene_core.transition_land()


# Resolves and activates a destination reference.
func _scene_resolve_destination(destination: Variant) -> Node:
	var existing := _scene_existing_destination(destination)
	return existing if existing else _scene_activate_ref(destination)


# Resolves an already active destination reference.
func _scene_existing_destination(destination: Variant) -> Node:
	return _native_core.scene_existing_destination(destination)


# Server receive for a player scene request off the carrier. The wall clock is
# read here and handed down, so the window the frame is admitted under is a
# function of its arguments.
func _scene_handle_request_frame(
		payload: PackedByteArray,
		sender: int,
) -> void:
	var row := _native_core.scene_request_frame_row(
		payload,
		sender,
		Time.get_ticks_msec(),
	)
	if row.is_empty():
		return
	var request_id: int = row[0]
	var is_path: bool = row[1]
	var scene_ref: Variant = row[2]
	var args: Array = row[3]
	_scene_receive_request(sender, request_id, is_path, scene_ref, args)


# Clears the local participant's scene membership from a server release notice.
func _scene_handle_released_frame(
		payload: PackedByteArray,
		sender: int,
) -> void:
	var seat := _native_core.scene_released_seat(payload, sender)
	if not seat.is_valid():
		return
	if _native_core.participant_admitted_local():
		_native_core.participant_seat_clear(_native_core.get_unique_id(), seat)


# Applies server policy and answers one player request.
func _scene_receive_request(
		peer_id: int,
		request_id: int,
		is_path: bool,
		scene_ref: Variant,
		args: Array,
) -> void:
	var participant := _native_core.participant_admitted_of(peer_id) \
			as NetwParticipant
	if participant == null:
		_native_core.scene_send_result(peer_id, request_id, ERR_UNAUTHORIZED)
		return
	if is_path:
		_scene_receive_path_request(
			peer_id,
			request_id,
			participant,
			String(scene_ref),
			args,
		)
	else:
		_scene_receive_named_request(
			peer_id,
			request_id,
			participant,
			StringName(scene_ref),
			args,
		)


# A declared-name request builds the wire context and authorizes it.
func _scene_receive_named_request(
		peer_id: int,
		request_id: int,
		participant: NetwParticipant,
		label: StringName,
		args: Array,
) -> void:
	var path: String = _scene_core.declared_scene_path(label)
	var normalized := ResourceUID.ensure_path(path) if not path.is_empty() else ""
	var mark := _native_core.scene_mark_of(
		NetwMultiplayerCore.scene_root_script_at(path),
	)
	if not _scene_admits_request(
		participant,
		label,
		normalized,
		args,
		mark.marked,
		mark.is_deny_default(),
	):
		_native_core.scene_send_result(peer_id, request_id, ERR_UNAUTHORIZED)
		return
	_native_core.scene_answer_when_settled(
		_scene_apply_player_change(participant, label, mark.session_wide),
		peer_id,
		request_id,
	)


# A path request is bounded to a real scene file, then authorized against the
# target scene's mark. The mark is the consent line, so a marked non-gated
# scene admits by default and an installed request handler may still refuse it.
func _scene_receive_path_request(
		peer_id: int,
		request_id: int,
		participant: NetwParticipant,
		scene_path: String,
		args: Array,
) -> void:
	var resolved := NetwMultiplayerCore.scene_resolve_requested_path(scene_path)
	if resolved.is_empty():
		_native_core.scene_send_result(peer_id, request_id, ERR_UNAVAILABLE)
		return
	var packed := NetwMultiplayerCore.scene_packed_at(resolved)
	if packed == null:
		_native_core.scene_send_result(peer_id, request_id, ERR_UNAVAILABLE)
		return
	var mark := _native_core.scene_mark_of(
		NetwMultiplayerCore.scene_packed_root_script(packed),
	)
	if not _scene_admits_request(
		participant,
		&"",
		resolved,
		args,
		mark.marked,
		mark.is_deny_default(),
	):
		_native_core.scene_send_result(peer_id, request_id, ERR_UNAUTHORIZED)
		return
	_native_core.scene_answer_when_settled(
		_scene_apply_player_change(participant, packed, mark.session_wide),
		peer_id,
		request_id,
	)


# The reading behind NetwMultiplayerCore.scene_mark_of, taken as a Callable
# because a mark is authored against a Script by Netw.mark_multiplayer_scene and
# Netw.configure_multiplayer_scene, and only the script tier can key by one. It
# reads and never decides: which of these fields deny a request by default, and
# what an undeclared deadline falls back to, are NetwSceneMark's.
func _scene_read_mark(target_script: Script) -> NetwSceneMark:
	var mark := NetwSceneMark.new()
	mark.marked = Netw.is_multiplayer_scene(target_script)
	var config := NetwScriptModel.get_scene_config(target_script)
	if config == null:
		return mark
	mark.gated = config.is_gated
	mark.session_wide = config.is_session_wide
	mark.captured = config.is_captured
	mark.pending_method = config.pending_method
	mark.deadline = config.deadline
	return mark


# Decides one player request. The mark readings are the authoring tier's, so
# they are reduced at the door and handed down, and the verdict itself is the
# record plane's.
func _scene_admits_request(
		participant: NetwParticipant,
		label: StringName,
		scene_path: String,
		args: Array,
		marked: bool,
		gated: bool,
) -> bool:
	var named := not label.is_empty()
	return _scene_core.decide_request(
		participant,
		label if named else scene_path,
		args,
		named,
		marked,
		gated,
		scene_path,
	)


# Applies an allowed request at the reach the record plane answers.
func _scene_apply_player_change(
		participant: NetwParticipant,
		destination: Variant,
		session_wide: bool,
) -> NetwPromise:
	if _scene_core.change_replaces_session(session_wide, participant != null):
		return _scene_change_session(destination)
	var target := _scene_resolve_destination(destination)
	if target == null:
		var unavailable := NetwPromise.new()
		unavailable.reject(ERR_UNAVAILABLE)
		return unavailable
	for active: Node in _scene_nodes_by_label().values():
		for entity: NetwEntity in _scene_players_in(active):
			if entity.peer_id == participant.peer_id:
				return _scene_move_entity_to(entity, target)
	participant.move_to(_scene_handle_for(target))
	var completed := NetwPromise.new()
	completed.resolve(OK)
	return completed


# The canonical handle for one scene container, which is how a participant's
# membership is recorded now that the scalar is a handle rather than a node.
func _scene_handle_for(scene_node: Node) -> NetwSceneHandle:
	var record := NetwEntity.of(scene_node)
	return record.scene if record else null


# The content root of [param scene_node], which is its only child. A scene with
# no content is a pure admission boundary and answers null.
func _scene_level_of(scene_node: Node) -> Node:
	return NetwMultiplayerCore.scene_level_of(scene_node)


# The player entities inside [param scene_node].
func _scene_players_in(scene_node: Node) -> Array[NetwEntity]:
	var record := NetwEntity.of(scene_node)
	return record.scene.players if record else [] as Array[NetwEntity]


# Resolves the scene this peer presents. A dedicated server presents nothing,
# and the readings the record plane cannot take for itself are the local role
# and whichever seat the session holds for this peer.
func _scene_resolve_current() -> RID:
	var presents := _native_core.role != Role.DEDICATED_SERVER
	var seat := _native_core.participant_seat(
		_scene_local_participant.peer_id,
	) if _scene_local_participant else RID()
	return _scene_core.resolve_current(presents, seat)


# The session publishes the edge; this refreshes what it means locally.
func _scene_on_local_changed(
		_from: NetwSceneHandle,
		_to: NetwSceneHandle,
) -> void:
	_scene_refresh_current()


# Frees every active scene so a re-host rebuilds from empty, then clears local
# presentation. Freeing wrappers clears their layer memberships through normal
# entity lifecycle teardown, so the next session starts cleanly. The free is
# synchronous, which is why it rides the reclaim phase rather than the
# announcement: every listener that holds scene nodes has dropped them by the
# time this runs.
func _scene_on_session_reclaimed() -> void:
	for scene_node: Node in _scene_live_nodes():
		if scene_node.get_parent():
			scene_node.get_parent().remove_child(scene_node)
		scene_node.free()
	_scene_core.clear()
	if is_instance_valid(_scene_host_view):
		if _scene_host_view.get_parent():
			_scene_host_view.get_parent().remove_child(_scene_host_view)
		_scene_host_view.free()
	_scene_host_view = null
	if _scene_local_participant:
		_scene_local_participant.current_scene = null
	_scene_local_participant = null
	_scene_core.request_abandon(ERR_UNAVAILABLE)
	_scene_core.current_scene = RID()


# Diagnoses native scene changes to a scene with no on-ramp during a live
# session. A scene marked [method NetwScriptModel.SceneMarkConfig.captured]
# carries its own detach hook that converts the change into a server request, so
# it is exempt. Declaring a scene without that knob declares it and nothing
# more, so the change still strands this peer.
func _scene_on_native_changed(scene_root: Node) -> void:
	var on_ramped := is_instance_valid(scene_root) \
			and _native_core.scene_mark_of(scene_root.get_script()).captured
	if not NetwMultiplayerCore.scene_native_change_strands(
		_native_core.state == SessionState.ONLINE,
		on_ramped,
	):
		return
	push_error(
		"Native change_scene_to_* to a scene with no on-ramp during an online "
		+ "session. The replicated session is intact, but this client left the "
		+ "presented game locally. Add "
		+ "Netw.configure_multiplayer_scene(self).captured() to the scene root "
		+ "to make the change a server request, or change scenes with "
		+ "Netw.change_scene_to_file(), which applies on authority and asks "
		+ "from a client.",
	)

#endregion

#region Display

## Declares one interpolated [param track] on an entity component.
func display_declare(
		entity: RID,
		comp: int,
		track: StringName,
		spec: NetwInterpolate,
) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	if track.is_empty() or spec == null:
		return ERR_INVALID_DATA
	var node := _comp_node(wrapper, comp)
	if not is_instance_valid(node):
		return ERR_UNAVAILABLE
	var verdict := _display_declare(entity, comp, track, spec)
	if verdict != OK:
		return verdict
	var declarations: Dictionary = _display_declarations.get(entity, { })
	declarations[track] = [comp, spec]
	_display_declarations[entity] = declarations
	return OK


## Removes every display declaration for [param entity].
func display_undeclare(entity: RID) -> void:
	_display_undeclare(entity)
	_display_declarations.erase(entity)
	_display_target_items.erase(entity)
	_display_callbacks.erase(entity)


## Records one authority sample for [param track].
func display_record(
		entity: RID,
		track: StringName,
		value: Variant,
		tick: int,
) -> Error:
	if not _display_declarations.get(entity, { }).has(track):
		return ERR_DOES_NOT_EXIST
	return _display_record(entity, track, value, tick)


## Writes one [enum DisplayParam] on [param entity]'s display runtime.
func display_set_param(
		entity: RID,
		param: DisplayParam,
		value: Variant,
) -> void:
	if param == DisplayParam.DISPLAY_PARAM_VISUAL_ROOT \
			and not (value is NodePath or value is String):
		var wrapper := _entity_wrapper(entity)
		var target := _comp_node(wrapper, int(value))
		display_set_target_node(entity, target)
		return
	_write_display_param(_entity_wrapper(entity), param, value)


# Writes one display param onto the entity's declaration, which the book
# publishes and which repairs whatever the write invalidated. The declaration
# is the entity handle's own, so a write that lands before the entity is live
# is the same record the book answers for it afterwards.
func _write_display_param(
		wrapper: NetwEntity,
		param: DisplayParam,
		value: Variant,
) -> void:
	var handle := wrapper.interpolation if wrapper else null
	if handle == null:
		return
	_native_core.display_book.write_param(
		wrapper.rid,
		handle._declaration(),
		param,
		value,
	)


# The display door with its event reported, installed on every channel as the
# lane it tries before its own port. A door that answers ERR_DOES_NOT_EXIST has
# no lane for this entity, which is the only verdict the channel falls through.
func _display_lane(entity: RID, track: StringName, value: Variant) -> Error:
	var verdict := _display_write(entity, track, value)
	report_event(
		NetwMultiplayerCore.DISPLAY_WRITE,
		_native_core.liveness_core.route_of(entity),
		{ track = track },
		0,
		&"",
		{ },
		verdict,
	)
	return verdict


# The display facts only the prediction handle and entity control can answer.
# The stream half is decided natively and arrives as authors_streams.
func _display_role_facts(
		runtime: NetwDisplayRuntime,
		authors_streams: bool,
) -> NetwDisplayRoleFacts:
	var entity := runtime.entity()
	var facts := NetwDisplayRoleFacts.new()
	if entity == null:
		return facts
	facts.authors_streams = authors_streams
	facts.controlled_locally = entity.is_controlled_locally
	facts.predicted_input = entity.prediction.input_source \
			== NetwPredict.InputSource.PREDICTED
	facts.prediction_registered = entity.prediction.is_registered()
	facts.simulates_locally = _display_simulates_locally(runtime)
	return facts


# True when this peer runs the entity's own simulation forward, which is the
# body a PREDICTED display chases. Registration alone does not answer it: an
# entity whose recovery policy closes the delay is registered and simulates
# nothing here, so it has an authoritative stream to play back instead.
func _display_simulates_locally(runtime: NetwDisplayRuntime) -> bool:
	var entity := runtime.entity()
	if entity and entity.prediction.is_registered():
		return entity.prediction.sim_mode != NetwPredict.SimMode.DISPLAY
	var owner := runtime.owner()
	if owner:
		return owner.get_node_or_null("%PredictionComponent") != null
	return false


# The largest render offset a chase absorption may hold, the entity's own
# teleport tier: an offset past it would show a pose a teleport was entitled
# to snap through.
func _display_chase_clamp(runtime: NetwDisplayRuntime) -> float:
	var entity := runtime.entity()
	if entity and entity.prediction:
		return maxf(entity.prediction.teleport_threshold, 0.0)
	return INF


# Subscribes a chasing runtime to its entity's reconciliation writes, and drops
# the subscription for every other role. A source transition owns any offset
# left behind after leaving the chase.
func _display_chase_hook(runtime: NetwDisplayRuntime, bind: bool) -> void:
	var entity := runtime.entity()
	var hooks := runtime.chase_hooks
	for hook: Callable in hooks:
		if entity and entity.prediction \
				and entity.prediction.recovered.is_connected(hook):
			entity.prediction.recovered.disconnect(hook)
	hooks.clear()
	if not bind or not entity or not entity.prediction:
		runtime.chase_hooks = hooks
		return
	var hook := _display_on_recovered.bind(runtime)
	entity.prediction.recovered.connect(hook)
	hooks.append(hook)
	runtime.chase_hooks = hooks


func _display_on_recovered(
		_entry: int,
		deltas: Dictionary,
		teleported: bool,
		_attribution: int,
		runtime: NetwDisplayRuntime,
) -> void:
	_native_core.display_absorb_recovery(runtime, deltas, teleported)


# Walks owner and every descendant, collecting one NetwDisplaySpecRow per
# tracked property and per interpolated RPC or signal argument.
func _display_specs(owner: Node) -> Array:
	var rows: Array = []
	if not owner:
		return rows
	var nodes: Array[Node] = [owner]
	for child in owner.find_children("*", "", true, false):
		nodes.append(child)
	for node in nodes:
		var configs := NetwScriptModel.get_node_property_configs(node)
		for property: StringName in configs:
			var opt: NetwScriptModel.SyncConfig = configs[property]
			if opt.interpolators.is_empty():
				continue
			var spec: NetwInterpolate = opt.interpolators[0]
			rows.append(NetwDisplaySpecRow.of_property(node, property, spec))
		var script := node.get_script() as Script
		if not script:
			continue
		for method in NetwScriptModel.get_rpc_configs(script):
			var rpc_opt: NetwScriptModel.SyncConfig = (
					NetwScriptModel.get_rpc_configs(script)[method]
			)
			for raw_spec in rpc_opt.interpolators:
				var arg_spec := raw_spec as NetwInterpolate
				if not arg_spec or arg_spec.mode == NetwInterpolate.MODE_NONE:
					continue
				if arg_spec.target.is_empty():
					continue
				rows.append(NetwDisplaySpecRow.of_argument(node, arg_spec))
		for signal_name in NetwScriptModel.get_signal_configs(script):
			var sig_opt: NetwScriptModel.SyncConfig = (
					NetwScriptModel.get_signal_configs(script)[signal_name]
			)
			for raw_spec in sig_opt.interpolators:
				var arg_spec := raw_spec as NetwInterpolate
				if not arg_spec or arg_spec.mode == NetwInterpolate.MODE_NONE:
					continue
				if arg_spec.target.is_empty():
					continue
				rows.append(NetwDisplaySpecRow.of_argument(node, arg_spec))
	return rows


func _display_sync_intervals(runtime: NetwDisplayRuntime) -> void:
	var max_interval := 0.0
	var entity := runtime.entity()
	if not entity:
		return
	runtime.authoring_binding = null
	for sync in entity.synchronizers():
		if not sync.public_visibility:
			continue
		if not _sync_replicates_tracked_property(runtime, sync):
			continue
		max_interval = maxf(
			max_interval,
			maxf(sync.replication_interval, sync.delta_interval),
		)
	var state_binding := entity.state_binding
	if state_binding and _set_replicates_tracked_property(runtime, state_binding.set):
		runtime.authoring_binding = state_binding
	if max_interval <= 0.0:
		return
	if _native_core.clock_handle.is_configured:
		runtime.playhead.expected_interval_ticks = maxi(
			1,
			ceili(max_interval * _native_core.clock_handle.tickrate),
		)


func _sync_replicates_tracked_property(
		runtime: NetwDisplayRuntime,
		sync: MultiplayerSynchronizer,
) -> bool:
	if not sync.replication_config:
		return false
	for path in sync.replication_config.get_properties():
		if path.get_subname_count() == 0:
			continue
		var clean_name := path.get_subname(path.get_subname_count() - 1)
		for state in runtime.states:
			if state.source_prop == clean_name or state.name == clean_name:
				return true
	return false


# True for a plain display synchronizer whose receive path is the consumed
# apply feed. Read by role resolution to tell an authored stream from a
# received one.
func _sync_feeds_consumed(sync: MultiplayerSynchronizer) -> bool:
	if not sync.replication_config:
		return false
	if not sync.public_visibility:
		return false
	return true


func _display_authors_streams(runtime: NetwDisplayRuntime) -> bool:
	var entity := runtime.entity()
	if not entity:
		return false
	var found := false
	for sync in entity.synchronizers():
		if not sync.is_inside_tree():
			continue
		if not _sync_feeds_consumed(sync):
			continue
		if not _sync_replicates_tracked_property(runtime, sync):
			continue
		if not sync.is_multiplayer_authority():
			return false
		found = true
	for binding in _replication.derived_group(runtime.route):
		var node := binding.node()
		if not is_instance_valid(node) or not node.is_inside_tree():
			continue
		if binding.set.audience != NetwPropertySet.Audience.AUDIENCE_PUBLIC:
			continue
		if not _set_replicates_tracked_property(runtime, binding.set):
			continue
		if not _authors_derived_stream(binding, entity):
			return false
		found = true
	return found


# The send-side author predicate for a derived stream, mirrored from the
# pump's gate: the server for a state set, the node authority for an
# authority-policed set, the local controller for a controller-policed set,
# any peer for an open set.
func _authors_derived_stream(
		binding: NetwPropertySetBinding,
		entity: NetwEntity,
) -> bool:
	var node := binding.node()
	if not is_instance_valid(node):
		return false
	if binding.set.record == NetwPropertySet.Record.RECORD_STATE:
		return get_unique_id() == 1
	return NetwEntityControl.policy_admits(
		binding.set.policy,
		get_unique_id(),
		node.get_multiplayer_authority(),
		entity.controller,
	)


# The derived counterpart of _sync_replicates_tracked_property: a set feeds
# the runtime when any field key names a tracked value's source or name.
func _set_replicates_tracked_property(
		runtime: NetwDisplayRuntime,
		set: NetwPropertySet,
) -> bool:
	for field in set.columns:
		for state in runtime.states:
			if state.source_prop == field.key or state.name == field.key:
				return true
	return false


## Returns one [enum DisplayParam], or [code]null[/code] when invalid.
func display_get_param(entity: RID, param: DisplayParam) -> Variant:
	var decl := _native_core.display_book.decl_of(entity)
	return decl.get_param(param) if decl else null


## Snaps one [param track] and clears its sample history.
func display_snap(entity: RID, track: StringName, value: Variant) -> void:
	var runtime := _native_core.display_book.runtime_of(entity)
	var channel := runtime.channel_named(track) if runtime else null
	if channel:
		channel.snap(value)


## Returns the most recently displayed value for [param track].
func display_get_value(entity: RID, track: StringName) -> Variant:
	var runtime := _native_core.display_book.runtime_of(entity)
	var channel := runtime.channel_named(track) if runtime else null
	return channel.last_written if channel else null


## Returns [param entity]'s displayed authoring tick, or [code]-1[/code].
func display_get_tick(entity: RID) -> int:
	var runtime := _native_core.display_book.runtime_of(entity)
	return runtime.authoring_tick() if runtime else -1


## Returns one track or runtime diagnostic selected by [param stat].
func display_get_track_stat(
		entity: RID,
		track: StringName,
		stat: StringName,
) -> Variant:
	var runtime := _native_core.display_book.runtime_of(entity)
	return runtime.track_stat(track, stat) if runtime else null


## Binds display output to [param node]. Pass [code]null[/code] to clear it.
func display_set_target_node(entity: RID, node: Node) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return
	if node == null:
		_write_display_param(
			wrapper,
			DisplayParam.DISPLAY_PARAM_VISUAL_ROOT,
			NodePath(""),
		)
		return
	if node != wrapper.owner and not wrapper.owner.is_ancestor_of(node):
		return
	_write_display_param(
		wrapper,
		DisplayParam.DISPLAY_PARAM_VISUAL_ROOT,
		wrapper.owner.get_path_to(node),
	)
	_display_target_items.erase(entity)
	_display_callbacks.erase(entity)


## Binds display output to a RenderingServer [param item].
func display_set_target_item(entity: RID, item: RID) -> void:
	if _entity_wrapper(entity) == null:
		return
	if item.is_valid():
		_display_target_items[entity] = item
	else:
		_display_target_items.erase(entity)
	_display_callbacks.erase(entity)
	_native_core.display_book.mark_dirty(entity)


## Binds display output to [param callback].
##
## The callback receives [code](entity, track, value)[/code].
func display_set_callback(entity: RID, callback: Callable) -> void:
	if _entity_wrapper(entity) == null:
		return
	if callback.is_valid():
		_display_callbacks[entity] = callback
	else:
		_display_callbacks.erase(entity)
	_display_target_items.erase(entity)
	_native_core.display_book.mark_dirty(entity)


## Declares that [param track] on [param entity] is displayed by interpolation.
##
## The four declaration and pump stages share one per-entity sample history.
## Replacing some of them leaves the rest reading history your override never
## fills, so replace them together or forward what you do not need.
## [method _display_write] touches no shared state and stands alone.
## [codeblock]
## Error
## ┠╴OK                the track is declared and will accept samples
## ┖╴ERR_INVALID_DATA  comp resolves to no node, or spec is not a
##                     NetwInterpolate
## [/codeblock]
## [param comp] is the registration-time component id, [code]0[/code] for the
## entity root. [param track] is the displayed property name, commonly
## [code]&"position"[/code], [code]&"rotation"[/code], or
## [code]&"transform"[/code]. [param spec] is the [NetwInterpolate] resource
## naming the curve and easing.
##
## Override together with [method _display_undeclare],
## [method _display_record], and [method _display_pump_entity] to hold sample
## history in your own structure.
func _display_declare(
		entity: RID,
		comp: int,
		track: StringName,
		spec: Variant,
) -> Error:
	var wrapper := _entity_wrapper(entity)
	var node := _comp_node(wrapper, comp)
	if not is_instance_valid(node) or not (spec is NetwInterpolate):
		return ERR_INVALID_DATA
	NetwScriptModel.configure_node_property(node, track).interpolate(spec)
	_native_core.display_book.mark_dirty(entity, NetwDisplayDecl.DIRT_RUNTIME)
	return OK


## Drops [param entity]'s display runtime and every track it held.
##
## Display state is per-entity, so removal takes all of an entity's tracks at
## once rather than one at a time. An entity with no runtime is a no-op, which
## makes teardown safe to repeat. Shares the sample history described on
## [method _display_declare].
func _display_undeclare(entity: RID) -> void:
	var route := _native_core.display_book.route_of(entity)
	if route > 0:
		_native_core.display_on_entity_dead(route)


## Feeds one authored sample of [param track] into the display history.
##
## Samples arrive keyed by the authoring [param tick] rather than by arrival
## order, so a late or reordered frame lands at the instant it describes.
## Recording never writes the scene: [method _display_pump_entity] decides when
## a sample becomes a visible pose.
## [codeblock]
## Error
## ┠╴OK                  the sample was filed against the declared track
## ┠╴ERR_DOES_NOT_EXIST  the track was never declared for this entity
## ┖╴ERR_UNAVAILABLE     the declaring component's node is gone
## [/codeblock]
## [param value] must match the declared track's type. [param tick] is the
## authoring tick the value was true at. Shares the sample history described on
## [method _display_declare].
func _display_record(
		entity: RID,
		track: StringName,
		value: Variant,
		tick: int,
) -> Error:
	var declaration: Array = _display_declarations.get(entity, { }).get(
		track,
		[],
	)
	if declaration.is_empty():
		return ERR_DOES_NOT_EXIST
	var wrapper := _entity_wrapper(entity)
	var node := _comp_node(wrapper, int(declaration[0]))
	if not is_instance_valid(node):
		return ERR_UNAVAILABLE
	_native_core.display_record(
		node,
		track,
		value,
		tick,
		declaration[1] as NetwInterpolate,
		false,
	)
	return OK


## Advances one entity's display by one rendered frame.
##
## The pump runs per rendered frame, not per simulation tick, which is what
## decouples smooth presentation from the tick rate. It resolves each declared
## track to a pose and hands it to [method _display_write] rather than touching
## the scene itself.
## [codeblock]
## Error
## ┠╴OK                  the entity's tracks advanced
## ┖╴ERR_DOES_NOT_EXIST  the entity has no display runtime
## [/codeblock]
## [param display_tick] is the presentation tick being resolved, which trails
## [member tick] by the buffer depth. [param alpha] is the fraction between
## that tick and the next, in [code]0.0[/code] to [code]1.0[/code].
## [param delta] is the rendered frame time in seconds. Shares the sample
## history described on [method _display_declare].
@warning_ignore("unused_parameter")
func _display_pump_entity(
		entity: RID,
		display_tick: int,
		alpha: float,
		delta: float,
) -> Error:
	return _native_core.display_pump_entity(entity, _display_pump_timing)


## Applies one resolved pose of [param track] to what the player sees.
##
## This is the only stage that touches presentation, which is why it is the one
## display virtual that may be overridden alone. Everything above it decides
## [param value]; this decides where [param value] lands.
## [codeblock]
## Error
## ┠╴OK                  written through the callback, canvas item, or
##                       instance bound for this entity
## ┠╴ERR_DOES_NOT_EXIST  the entity has no display target bound at all
## ┖╴ERR_INVALID_DATA    an RID target was given a value that is neither
##                       Transform2D nor Transform3D
## [/codeblock]
## Targets are consulted in order: the [Callable] from
## [method display_set_callback], then the [RID] from
## [method display_set_target_item], which is written straight through
## [RenderingServer] with no node in the path.
##
## Override to drive something the stock writer does not know about, such as a
## shader uniform or a [MultiMesh] instance.
## [codeblock]
## func _display_write(entity, track, value):
##     if track == &"tint":
##         _material.set_shader_parameter(&"tint", value)
##         return OK
##     return super(entity, track, value)
## [/codeblock]
func _display_write(
		entity: RID,
		track: StringName,
		value: Variant,
) -> Error:
	var callback: Callable = _display_callbacks.get(entity, Callable())
	if callback.is_valid():
		callback.call(entity, track, value)
		return OK
	var item: RID = _display_target_items.get(entity, RID())
	if not item.is_valid():
		return ERR_DOES_NOT_EXIST
	match typeof(value):
		TYPE_TRANSFORM2D:
			RenderingServer.canvas_item_set_transform(item, value)
		TYPE_TRANSFORM3D:
			RenderingServer.instance_set_transform(item, value)
		_:
			return ERR_INVALID_DATA
	return OK

#endregion

#region Prediction and lag compensation

## Declares [param entity] for prediction on this peer.
func predict_declare(entity: RID) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	if not is_configured():
		return ERR_UNCONFIGURED
	register_prediction(wrapper)
	return OK


## Removes [param entity] from prediction on this peer.
func predict_undeclare(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		unregister_prediction(wrapper)


## Writes one [enum PredictParam] on [param entity].
func predict_set_param(
		entity: RID,
		param: PredictParam,
		value: Variant,
) -> void:
	var handle := _prediction_handle(entity)
	if handle == null:
		return
	match param:
		PredictParam.PREDICT_PARAM_ARCHETYPE:
			handle.archetype = int(value) as NetwPredict.Archetype
		PredictParam.PREDICT_PARAM_SCHEDULE:
			handle.schedule = int(value) as NetwPredict.Schedule
		PredictParam.PREDICT_PARAM_MISSING_POLICY:
			handle.missing_policy = int(value)
		PredictParam.PREDICT_PARAM_RECOVERY_POLICY:
			handle.recovery_policy = int(value)
		PredictParam.PREDICT_PARAM_SNAP_RESTORE:
			handle.snap_restore = int(value)
		PredictParam.PREDICT_PARAM_CORRECTION_MODE:
			handle.correction_mode = int(value)
		PredictParam.PREDICT_PARAM_TELEPORT_THRESHOLD:
			handle.teleport_threshold = float(value)
		PredictParam.PREDICT_PARAM_DIVERGENCE_EPSILON:
			handle.divergence_epsilon = float(value)
		PredictParam.PREDICT_PARAM_BREACH_RESPONSE:
			handle.breach_response = int(value) as NetwPredict.BreachResponse
		PredictParam.PREDICT_PARAM_MAX_RESTORE_TICKS:
			handle.max_restore_ticks = int(value)
		PredictParam.PREDICT_PARAM_COLLISION_COOLDOWN_TICKS:
			handle.collision_cooldown_ticks = int(value)
		PredictParam.PREDICT_PARAM_MAX_CONSUME_PER_TICK:
			handle.max_consume_per_tick = int(value)
		PredictParam.PREDICT_PARAM_MAX_CONSUME_LAG_TICKS:
			handle.max_consume_lag_ticks = int(value)
		PredictParam.PREDICT_PARAM_CONSUME_BUFFER_TICKS:
			handle.consume_buffer_ticks = int(value)
		PredictParam.PREDICT_PARAM_REPLAY_BUFFER_DEPTH:
			handle.replay_buffer_depth = int(value)


## Returns one [enum PredictParam], or [code]null[/code] when unknown.
func predict_get_param(entity: RID, param: PredictParam) -> Variant:
	var handle := _prediction_handle(entity)
	if handle == null:
		return null
	match param:
		PredictParam.PREDICT_PARAM_ARCHETYPE:
			return handle.archetype
		PredictParam.PREDICT_PARAM_SCHEDULE:
			return handle.schedule
		PredictParam.PREDICT_PARAM_MISSING_POLICY:
			return handle.missing_policy
		PredictParam.PREDICT_PARAM_RECOVERY_POLICY:
			return handle.recovery_policy
		PredictParam.PREDICT_PARAM_SNAP_RESTORE:
			return handle.snap_restore
		PredictParam.PREDICT_PARAM_CORRECTION_MODE:
			return handle.correction_mode
		PredictParam.PREDICT_PARAM_TELEPORT_THRESHOLD:
			return handle.teleport_threshold
		PredictParam.PREDICT_PARAM_DIVERGENCE_EPSILON:
			return handle.divergence_epsilon
		PredictParam.PREDICT_PARAM_BREACH_RESPONSE:
			return handle.breach_response
		PredictParam.PREDICT_PARAM_MAX_RESTORE_TICKS:
			return handle.max_restore_ticks
		PredictParam.PREDICT_PARAM_COLLISION_COOLDOWN_TICKS:
			return handle.collision_cooldown_ticks
		PredictParam.PREDICT_PARAM_MAX_CONSUME_PER_TICK:
			return handle.max_consume_per_tick
		PredictParam.PREDICT_PARAM_MAX_CONSUME_LAG_TICKS:
			return handle.max_consume_lag_ticks
		PredictParam.PREDICT_PARAM_CONSUME_BUFFER_TICKS:
			return handle.consume_buffer_ticks
		PredictParam.PREDICT_PARAM_REPLAY_BUFFER_DEPTH:
			return handle.replay_buffer_depth
	return null


## Sets one named prediction sensor callback.
func predict_set_sensor_callback(
		entity: RID,
		name: StringName,
		callback: Callable,
) -> void:
	var handle := _prediction_handle(entity)
	if handle:
		if callback.is_valid():
			handle.sensors[name] = callback
		else:
			handle.sensors.erase(name)


## Sets the contact-witness callback for [param entity].
func predict_set_witness_callback(entity: RID, callback: Callable) -> void:
	var handle := _prediction_handle(entity)
	if handle:
		handle.witness_contacts = callback


## Sets the transport-corridor callback for [param entity].
func predict_set_corridor_callback(entity: RID, callback: Callable) -> void:
	var handle := _prediction_handle(entity)
	if handle:
		handle.transport_corridor = callback


## Sets the simulation callback for [param entity].
##
## Overrides the [code]_network_tick[/code] adopted by
## [method predict_bind_owner], and keeps overriding it across a re-bind.
func predict_set_simulate_callback(entity: RID, callback: Callable) -> void:
	var handle := _prediction_handle(entity)
	if handle:
		handle.simulate = callback


## Binds [param owner] as the object the simulation plane reads and writes for
## [param entity].
##
## Declared input properties are applied to [param owner] before each step and
## declared state properties are captured from it after, so the object that
## holds the values is the object the plane touches. When [param owner] has a
## [code]_network_tick[/code] method and no simulate callback is set, that
## method is adopted as the step. The session binds a declared entity's owner
## node automatically, so calling this yourself is only needed for a custom
## target.
## [codeblock]
## Error
## ┠╴OK                   bound
## ┠╴ERR_DOES_NOT_EXIST   the entity has no prediction slot
## ┖╴ERR_INVALID_DATA     the owner is not a live instance
## [/codeblock]
func predict_bind_owner(entity: RID, owner: Object) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	if not is_instance_valid(owner):
		return ERR_INVALID_DATA
	if not _native_core.prediction_engine.slot_bind_owner(wrapper, owner):
		return ERR_DOES_NOT_EXIST
	var handle := wrapper.prediction
	if handle and not handle.simulate.is_valid() \
			and owner.has_method(&"_network_tick"):
		handle.simulate = Callable(owner, &"_network_tick")
	return OK


## Releases [param entity]'s bound owner. The slot keeps deciding, it stops
## touching properties.
func predict_unbind_owner(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		_native_core.prediction_engine.slot_unbind_owner(wrapper)


## Adds [param other] to [param entity]'s prediction island.
func predict_island_add(entity: RID, other: RID) -> Error:
	var handle := _prediction_handle(entity)
	var member := _entity_wrapper(other)
	if handle == null or member == null:
		return ERR_DOES_NOT_EXIST
	handle.island.add(member)
	return OK


## Removes [param other] from [param entity]'s prediction island.
func predict_island_remove(entity: RID, other: RID) -> void:
	var handle := _prediction_handle(entity)
	var member := _entity_wrapper(other)
	if handle and member:
		handle.island.remove(member)


## Writes one [enum IslandParam] on [param entity]'s prediction island.
func predict_island_set_param(
		entity: RID,
		param: IslandParam,
		value: Variant,
) -> Error:
	var handle := _prediction_handle(entity)
	if handle == null:
		return ERR_DOES_NOT_EXIST
	var island := handle.island
	match param:
		IslandParam.ISLAND_PARAM_APPROXIMATE:
			island.approximate = bool(value)
		IslandParam.ISLAND_PARAM_EXACT_CLAIM:
			island.exact_claim = bool(value)
		IslandParam.ISLAND_PARAM_RECONCILE:
			island.reconcile = int(value) as NetwPredict.Reconcile
		IslandParam.ISLAND_PARAM_PROMOTION:
			island.promotion = int(value) as NetwPredict.Promotion
		IslandParam.ISLAND_PARAM_PROMOTION_COUNT:
			island.promotion_count = maxi(0, int(value))
		IslandParam.ISLAND_PARAM_PROMOTION_METERS:
			island.promotion_meters = maxf(0.0, float(value))
		IslandParam.ISLAND_PARAM_PACING:
			island.pacing = int(value) as NetwPredict.Pacing
		IslandParam.ISLAND_PARAM_INPUT_DELAY:
			island.input_delay_ticks = maxi(0, int(value))
		_:
			return ERR_INVALID_PARAMETER
	return OK


## Writes one [enum MemberParam] on [param member] of [param entity]'s
## prediction island.
func predict_island_set_member_param(
		entity: RID,
		member: RID,
		param: MemberParam,
		value: Variant,
) -> Error:
	var handle := _prediction_handle(entity)
	var other := _entity_wrapper(member)
	if handle == null or other == null:
		return ERR_DOES_NOT_EXIST
	var island := handle.island
	if other not in island.participants:
		return ERR_INVALID_PARAMETER
	match param:
		MemberParam.MEMBER_PARAM_FIDELITY:
			island._set_fidelity(other, int(value) as NetwPredict.Fidelity)
		MemberParam.MEMBER_PARAM_PREDICTOR:
			island.predict_commands(other, value as Callable)
		_:
			return ERR_INVALID_PARAMETER
	return OK


## Reads one [enum MemberParam] on [param member] of [param entity]'s
## prediction island, or [code]null[/code] when [param member] is not a member.
func predict_island_get_member_param(
		entity: RID,
		member: RID,
		param: MemberParam,
) -> Variant:
	var handle := _prediction_handle(entity)
	var other := _entity_wrapper(member)
	if handle == null or other == null:
		return null
	var island := handle.island
	if other not in island.participants:
		return null
	match param:
		MemberParam.MEMBER_PARAM_FIDELITY:
			return island.fidelity.get(other, NetwPredict.Fidelity.PROXY)
		MemberParam.MEMBER_PARAM_PREDICTOR:
			return island.command_predictors.get(other, Callable())
	return null


## Installs [param stepper] as the re-stepping driver for [param space], or
## uninstalls it when [param stepper] is invalid. A member declaring
## [constant NetwPredict.Schedule.STEPPED] needs its space's stepper to admit
## replay. Without one it runs as [constant NetwPredict.Schedule.FRAME].
func predict_stepper_install(
		space: RID,
		stepper: NetwPhysicsStepper = null,
) -> void:
	if stepper == null or not stepper._can_step():
		_steppers.erase(space)
		return
	_steppers[space] = stepper


# The re-stepping driver installed for a physics space, or null when the space
# has none and a STEPPED member must fall back to FRAME.
func _stepper_for(space: RID) -> NetwPhysicsStepper:
	return _steppers.get(space) as NetwPhysicsStepper


## Asks the server to relay [param entity]'s authored commands to this peer, or
## to stop when [param subscribed] is false.
##
## The island machinery calls this for the members of a
## [constant NetwPredict.Reconcile.JOINT] group and drops it on demotion, so a
## game declares a group rather than a subscription. The server answers against
## [method interest_admits], so this can never widen what a peer may see.
##
## [br][br][b]Player request.[/b]
func predict_relay_subscribe(entity: RID, subscribed: bool = true) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return
	if is_server():
		relay_subscribe(wrapper, get_unique_id(), subscribed)
		return
	var route := _native_core.liveness_route_of(wrapper)
	if route <= 0:
		return
	_native_core.send_to(
		MultiplayerPeer.TARGET_PEER_SERVER,
		route,
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST,
		NetwPredictRelayBook.request_bytes(subscribed),
		true,
	)


## Marks a local collision or other undeclared prediction contact.
func predict_notify_contact(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	var engine := engine_for(wrapper) if wrapper else null
	if engine:
		engine.notify_contact()


## Samples one declared sensor on [param entity].
func predict_sensor_sample(
		entity: RID,
		name: StringName,
		default: Variant = null,
) -> Variant:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return default
	var pool := _native_core.prediction_engine
	var slot := pool.slot_of(wrapper)
	if slot < 0:
		return default
	return pool.sensor_samples(slot).get(name, default)


## Declares an authoritative history timeline for [param entity].
## [br][br][b]Server Only.[/b]
func timeline_declare(entity: RID) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	_native_core.lagcomp_core.timeline_register(
		wrapper,
		NetwTimeline.DEFAULT_LIMIT,
	)
	return OK


## Removes [param entity]'s authoritative history timeline.
func timeline_undeclare(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		_native_core.lagcomp_core.timeline_unregister(wrapper)


## Returns [param entity]'s state at or before [param tick].
## [br][br][b]Server Only.[/b]
func timeline_sample(entity: RID, tick: int) -> NetwSnapshot:
	return lagcomp_sample(entity, tick)


## Returns [param entity]'s state at or before [param tick].
## [br][br][b]Server Only.[/b]
func lagcomp_sample(entity: RID, tick: int) -> NetwSnapshot:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return NetwSnapshot.new()
	return NetwSnapshot.from_dictionary(
		_native_core.lagcomp_core.timeline_sample_entity(wrapper, tick),
	)


## Rewinds [param entities] while [param body] runs, then restores them.
## [br][br][b]Server Only.[/b]
func lagcomp_rewind(entities: Array[RID], tick: int, body: Callable) -> void:
	var slots := PackedInt64Array()
	for entity: RID in entities:
		var wrapper := _entity_wrapper(entity)
		if wrapper == null:
			continue
		var slot := _arm_rewind_timeline(wrapper)
		if slot >= 0:
			slots.append(slot)
	_native_core.lagcomp_core.rewind(slots, tick, body)


# Points the slot at the node its state set declares and names that set's
# fields, so a rewind writes exactly what the set owns and puts back exactly
# what it overwrote. Answers -1 for an entity the pool cannot rewind, which is
# one with no slot, no state set, or no live node.
func _arm_rewind_timeline(entity: NetwEntity) -> int:
	var core := _native_core.lagcomp_core
	var slot := core.timeline_slot_of(entity)
	if slot < 0:
		return -1
	var state: NetwPropertySetBinding = entity.state_binding
	if state == null or state.set == null:
		return -1
	var node := state.node()
	if not is_instance_valid(node):
		return -1
	core.timeline_bind_owner(slot, node)
	var keys: Array[StringName] = []
	for column: NetwPropertySet.Column in state.set.columns:
		keys.append(column.key)
	core.timeline_declare(slot, keys)
	return slot


## Returns a predicted action bound to [param authority].
func lagcomp_action(authority: Callable) -> NetwAction:
	return action(authority)


## Returns the ledger key for an optimistic act by [param entity] at
## [param tick].
##
## The key is a [StringName] rather than a handle because it has to survive a
## round trip through a spawned node's name: the server binds the key onto the
## authoritative result and the requesting peer adopts its own act by reading
## it back. With an invalid [param entity] the key drops the entity segment,
## which is how a session-scoped act is keyed.
## [codeblock]
## var key := api.effect_key(entity, view_tick)
## api.effect_arm(key, func() -> void: ghost.queue_free())
## # Later, the authoritative reply resolves the same key.
## api.effect_adopt(key)
## [/codeblock]
func effect_key(entity: RID, tick: int, slot: int = 0) -> StringName:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null or wrapper.entity_id.is_empty():
		return StringName("act__%d__%d" % [tick, slot])
	return StringName("act__%s__%d__%d" % [wrapper.entity_id, tick, slot])


## Registers [param key] with [param revert] until it is resolved or times out.
##
## A timeout is a denial rather than a third outcome: [param revert] runs and
## any denied watcher fires, so an act nobody ever answered for leaves nothing
## behind. [param timeout_ticks] of [code]0[/code] takes the session default.
## Re-arming an armed key replaces it silently.
func effect_arm(
		key: StringName,
		revert: Callable,
		timeout_ticks: int = 0,
) -> void:
	_native_core.effect_arm(key, revert, timeout_ticks)


## Observes [param key]'s resolution, [param confirmed] on adopt and
## [param denied] on discard or timeout.
##
## Refused when [param key] is not armed, so a watcher can never outlive the
## act it observes.
func effect_watch(
		key: StringName,
		confirmed: Callable,
		denied: Callable,
) -> void:
	if _native_core.effect_watch(key, confirmed, denied):
		return
	Netw.dbg.warn(
		"NetwMultiplayer: effect_watch refused, '%s' is not armed",
		[String(key)],
	)


## Resolves [param key] as kept. The pending revert is dropped unrun.
func effect_adopt(key: StringName) -> void:
	_native_core.effect_adopt(key)


## Resolves [param key] as reverted. The pending revert runs immediately.
func effect_discard(key: StringName) -> void:
	_native_core.effect_discard(key)


## Returns whether [param key] is armed and unresolved.
func effect_pending(key: StringName) -> bool:
	return _native_core.effect_pending(key)


func _sweep_effects(_delta: float, tick: int) -> void:
	_native_core.effect_sweep(tick)


## Returns the session's lag-compensation census.
##
## The cumulative counters sum since spawn and the occupancy keys report what
## the session holds right now, so a reader that wants a rate takes two
## censuses and differences them. [LagCompensationMonitor] is that reader.
## [codeblock]
## ┠╴entities: int          engine records stepped this tick
## ┠╴timelines: int         rewindable entities recorded this tick
## ┠╴corrections: int       summed reconciliation snaps since spawn
## ┠╴max_replay_depth: int  worst replay window walked
## ┠╴consumed: int          summed inputs the server consumed
## ┠╴missing: int           summed input ticks stepped over as lost
## ┠╴pending_actions: int   actions queued awaiting readiness
## ┠╴effects_armed: int     optimistic effects awaiting confirm or deny
## ┖╴gate_fallbacks: int    state-ready actions resolved best-effort
## [/codeblock]
func lagcomp_metrics() -> Dictionary:
	var result := metrics()
	result[&"effects_armed"] = _native_core.effect_count()
	return result


# Returns the compatibility prediction handle for one entity.
func _prediction_handle(entity: RID) -> NetwPredictionHandle:
	var wrapper := _entity_wrapper(entity)
	return wrapper.prediction if wrapper else null

#endregion

#region Property sets

## Creates an unsealed property set binding [param schema] with the defaults
## for [param record].
##
## The set owns no shape. Every column's key, type, stride, and quantizer comes
## from the schema, which is the same declaration the table binding and
## [NetwDatabase] read, so the three cannot drift. Membership is what the set
## adds ([method property_set_add_column]).
##
## Returns an invalid RID when [param schema] declares a column of stride
## greater than one, because a stride has no meaning for a node property: a
## property holds one value, not a fixed-capacity row.
func property_set_create(schema: RID, record: RecordKind) -> RID:
	if record not in [
		RecordKind.STATE,
		RecordKind.INPUT,
		RecordKind.BROADCAST,
	]:
		return RID()
	var declaration := _schema_core.record_of(schema)
	if declaration == null or _schema_core.has_stride(schema):
		return RID()
	var rid := _property_sets.rid_create()
	var set := NetwPropertySet.for_record(record as NetwPropertySet.Record)
	set.schema = declaration
	set.rid = rid
	_property_set_records[rid] = set
	_property_set_schema[rid] = schema
	return rid


## Binds the schema's [param column] as the next member in wire order and
## returns its position in the set, or [code]-1[/code].
##
## Membership is explicit because a script's schema holds every configured
## property, including the ones that only persist. A column no set binds rides
## no lane, which is what keeps a persisted-only value off the wire by
## construction rather than by a negation mark.
func property_set_add_column(set: RID, column: int) -> int:
	var record := _mutable_property_set(set)
	if record == null:
		return -1
	var shape := SchemaCore.column_at(record.schema, column)
	if shape == null or record.member(column):
		return -1
	var member := NetwPropertySet.Column.new()
	member.shape = shape
	member.schema_column = column
	record.columns.append(member)
	return record.columns.size() - 1


## Writes one [enum ColumnParam] before sealing.
func property_set_set_column_param(
		set: RID,
		field: int,
		param: ColumnParam,
		value: Variant,
) -> void:
	var record := _mutable_property_set(set)
	if record == null or field < 0 or field >= record.columns.size():
		return
	var target: NetwPropertySet.Column = record.columns[field]
	match param:
		ColumnParam.COLUMN_PARAM_CLASS:
			target.property_class = value as NetwPropertySet.PropertyClass
		ColumnParam.COLUMN_PARAM_EPSILON:
			target.epsilon_override = float(value)
		ColumnParam.COLUMN_PARAM_TELEPORT_AT:
			target.teleport_at_override = float(value)
		ColumnParam.COLUMN_PARAM_CARRY_CHANNEL:
			target.carry_channel = StringName(value)
		ColumnParam.COLUMN_PARAM_TELEPORT_ONLY:
			target.explicit_teleport_only = bool(value)
		ColumnParam.COLUMN_PARAM_RECONCILE_ONLY:
			target.explicit_reconcile_only = bool(value)
		ColumnParam.COLUMN_PARAM_LANE:
			target.lane = value as NetwPropertySet.Lane
			target.watch = target.lane == NetwPropertySet.Lane.RETAINED
			record.reproject_lanes()
		ColumnParam.COLUMN_PARAM_CONVERGE_STIFFNESS:
			target.converge_stiffness = float(value)


## Writes one [enum PropertySetParam] before sealing.
func property_set_set_param(
		set: RID,
		param: PropertySetParam,
		value: Variant,
) -> void:
	var record := _mutable_property_set(set)
	if record == null:
		return
	match param:
		PropertySetParam.SET_PARAM_MASKED:
			record.masked = bool(value)
		PropertySetParam.SET_PARAM_WINDOW:
			record.window = int(value)
		PropertySetParam.SET_PARAM_AUDIENCE:
			record.audience = value as NetwPropertySet.Audience
		PropertySetParam.SET_PARAM_POLICY:
			record.policy = value as NetwScriptModel.Policy
		PropertySetParam.SET_PARAM_TRIGGER:
			record.trigger = value as NetwPropertySet.Trigger
		PropertySetParam.SET_PARAM_CADENCE:
			record.cadence = value as NetwPropertySet.Cadence
		PropertySetParam.SET_PARAM_STAMP:
			record.stamp = value as NetwPropertySet.Stamp
		PropertySetParam.SET_PARAM_PROFILE:
			record.profile = value as NetwPropertySet.Profile
		PropertySetParam.SET_PARAM_CHANNEL:
			record.channel = value as NetwFrameEnvelope.Channel
		PropertySetParam.SET_PARAM_RELIABLE:
			record.reliable = bool(value)


## Seals [param set], fixing its wire hash and rejecting later mutation.
func property_set_seal(set: RID) -> Error:
	var record := _property_set_records.get(set) as NetwPropertySet
	if record == null:
		return ERR_DOES_NOT_EXIST
	record._seal()
	return OK


## Returns [param set]'s fixed wire hash, or [code]0[/code] when invalid.
##
## The shape hash of the schema it binds, folded with the ordered membership and
## each member's lane. Two peers that disagree on a type, a quantizer,
## membership, order, or a lane disagree here, so the binding is poisoned at
## spawn rather than misread on every later frame.
func property_set_get_wire_hash(set: RID) -> int:
	var record := _property_set_records.get(set) as NetwPropertySet
	return record.wire_hash() if record else 0


## Returns the schema [param set] binds, or an invalid RID.
##
## Reflection goes through the schema family: [method schema_get_column_count],
## [method schema_get_column_key], [method schema_get_column_type].
func property_set_get_schema(set: RID) -> RID:
	return _property_set_schema.get(set, RID())


## Attaches a sealed [param set] to [param entity]'s component address.
func entity_add_property_set(entity: RID, set: RID, comp: int) -> Error:
	return _entity_add_property_set(entity, set, comp)


## Attaches a sealed property [param set] to [param entity]'s [param comp]
## address.
##
## Only a sealed set may be attached, because sealing is what fixes membership
## order, and membership order is wire order. A set that could still gain a
## column could not be decoded by a peer that attached it earlier.
## [codeblock]
## Error
## ┠╴OK                  attached and registered with the sync pipeline
## ┠╴ERR_DOES_NOT_EXIST  the entity or the set RID is not valid
## ┠╴ERR_INVALID_DATA    the set exists but was never sealed
## ┖╴ERR_UNAVAILABLE     comp resolves to no live node on this entity
## [/codeblock]
## [param set] is minted by [method property_set_create] and sealed by
## [method property_set_seal]. [param comp] is the registration-time component id,
## [code]0[/code] for the entity root. One set may be attached at each
## [param comp].
##
## Override to keep your own registry of what is
## replicated where, then call [code]super()[/code] so the pipeline still learns
## about the set.
func _entity_add_property_set(entity: RID, set: RID, comp: int) -> Error:
	if not _native_core.liveness_core.entity_is_valid(entity) \
			or not _property_sets.rid_is_valid(set):
		return ERR_DOES_NOT_EXIST
	var record := _property_set_records.get(set) as NetwPropertySet
	if record == null or not record.sealed:
		return ERR_INVALID_DATA
	var wrapper := _native_core.wrapper_of(entity) as NetwEntity
	if wrapper == null:
		return ERR_UNAVAILABLE
	var node := _comp_node(wrapper, comp)
	if not is_instance_valid(node):
		return ERR_UNAVAILABLE
	var verdict := _replication._sync_pipeline.register_property_set(node, record)
	if verdict != OK:
		return verdict
	var attached: Dictionary = _entity_property_sets.get(entity, { })
	attached[comp] = set
	_entity_property_sets[entity] = attached
	return OK


## Removes the property set attached at [param comp].
func entity_remove_property_set(entity: RID, comp: int) -> void:
	_entity_remove_property_set(entity, comp)


## Detaches whatever property set sits at [param entity]'s [param comp] address.
##
## The set itself outlives the detachment: sets are compiled once per script
## and shared across every entity that attaches one, so removal unbinds this
## address and frees nothing. An address with nothing attached is a no-op.
##
## The twin of [method _entity_add_property_set].
func _entity_remove_property_set(entity: RID, comp: int) -> void:
	var attached: Dictionary = _entity_property_sets.get(entity, { })
	if not attached.has(comp):
		return
	var wrapper := _native_core.wrapper_of(entity) as NetwEntity
	var node := _comp_node(wrapper, comp)
	if is_instance_valid(node):
		_replication._sync_pipeline.unregister_derived(node)
	attached.erase(comp)
	if attached.is_empty():
		_entity_property_sets.erase(entity)
	else:
		_entity_property_sets[entity] = attached


## Returns one member column's current value on the node, or [code]null[/code].
##
## [param column] is the set's membership position, the same index
## [method property_set_add_column] returned.
func entity_get_property(entity: RID, comp: int, column: int) -> Variant:
	var attached: Dictionary = _entity_property_sets.get(entity, { })
	var set: RID = attached.get(comp, RID())
	var record := _property_set_records.get(set) as NetwPropertySet
	if record == null or column < 0 or column >= record.columns.size():
		return null
	var wrapper := _native_core.wrapper_of(entity) as NetwEntity
	var node := _comp_node(wrapper, comp)
	if not is_instance_valid(node):
		return null
	return node.get(record.columns[column].key)


# Returns the record while it is valid and mutable.
func _mutable_property_set(set: RID) -> NetwPropertySet:
	var record := _property_set_records.get(set) as NetwPropertySet
	if record == null:
		Netw.dbg.error("NetwMultiplayer: invalid property set RID")
		return null
	if record.sealed:
		Netw.dbg.error("NetwMultiplayer: sealed property set cannot mutate")
		return null
	return record


# Compiles one script's declarations into this session's schema and the
# property set that binds it, the seam where "one schema, three consumers"
# becomes literal.
#
# The schema is named by the script's resource path, which cannot collide with
# a user-declared name in practice. It is declared once and re-declared
# idempotently by the second and third record kind, so a script's state, input,
# and broadcast sets are three bindings of one declaration rather than three
# declarations.
func _adopt_property_set(
		script: Script,
		record_kind: int,
		source: NetwPropertySet,
		node: Node = null,
) -> RID:
	var cached: Dictionary = _property_set_cache.get(script, { })
	if cached.has(record_kind):
		return cached[record_kind]
	var schema := _script_schema(script, node)
	var rid := property_set_create(schema, record_kind as RecordKind)
	if not rid.is_valid():
		return rid
	property_set_set_param(rid, PropertySetParam.SET_PARAM_MASKED, source.masked)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_WINDOW, source.window)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_AUDIENCE, source.audience)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_POLICY, source.policy)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_TRIGGER, source.trigger)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_CADENCE, source.cadence)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_STAMP, source.stamp)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_PROFILE, source.profile)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_CHANNEL, source.channel)
	property_set_set_param(rid, PropertySetParam.SET_PARAM_RELIABLE, source.reliable)
	for member: NetwPropertySet.Column in source.columns:
		var column := _schema_core.find_column(schema, member.key)
		var at := property_set_add_column(rid, column)
		if at < 0:
			continue
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_CLASS,
			member.property_class,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_EPSILON,
			member.epsilon_override,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_TELEPORT_AT,
			member.teleport_at_override,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_CARRY_CHANNEL,
			member.carry_channel,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_TELEPORT_ONLY,
			member.explicit_teleport_only,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_RECONCILE_ONLY,
			member.explicit_reconcile_only,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_LANE,
			member.lane,
		)
		property_set_set_column_param(
			rid,
			at,
			ColumnParam.COLUMN_PARAM_CONVERGE_STIFFNESS,
			member.converge_stiffness,
		)
	property_set_seal(rid)
	cached[record_kind] = rid
	_property_set_cache[script] = cached
	return rid


# Declares (or re-declares) the schema one script's configured properties
# compile to, and returns its handle.
#
# Every configured property enters it, not only the ones a set binds, because
# the schema is what the database and the persistence engine read too. Every
# record kind of one script reaches this same declaration, so a column has one
# address across all three sets and the re-declaration cursor sees the same
# shape each time.
func _script_schema(script: Script, node: Node) -> RID:
	# A script with no resource path is an inner class or a runtime script, so
	# there is no name two peers could agree on. Its instance id keeps the
	# registry key unique locally, and the wire hash folds no name at all.
	var name := &"@anonymous_script"
	if script:
		name = StringName(
			script.resource_path if not script.resource_path.is_empty() else "@script:%d" % script.get_instance_id(),
		)
	var schema := schema_create(name)
	var configs := NetwScriptModel.get_property_configs(script)
	for property: StringName in configs:
		var config = configs[property]
		if not (config is NetwScriptModel.PropertyConfig):
			continue
		var column := schema_add_column(
			schema,
			property,
			NetwPropertySet.column_type_for(script, node, property) as ColumnType,
		)
		if column >= 0 and not config.quantizers.is_empty():
			schema_set_column_quantizer(schema, column, config.quantizers[0])
	if schema_seal(schema) != OK:
		Netw.dbg.error(
			"NetwMultiplayer: script '%s' recompiled its properties with a "
			% name
			+ "different shape; peers built from the two cannot read each "
			+ "other.",
		)
	return schema


# Resolves the GDScript record behind one property set handle.
func _property_set_record(set: RID) -> NetwPropertySet:
	return _property_set_records.get(set) as NetwPropertySet

#endregion

#region Sync

## Sends one configured property from an entity component.
func sync_send_property(
		entity: RID,
		comp: int,
		property: StringName,
) -> Error:
	return _native_core.sync_send_property(entity, comp, property)


## Sends one configured signal from an entity component.
func sync_send_signal(
		entity: RID,
		comp: int,
		signal_name: StringName,
		args: Array,
) -> Error:
	return _native_core.sync_send_signal(entity, comp, signal_name, args)


## Registers one application channel handler.
##
## The handler receives [code](entity, payload, sender)[/code], where entity is
## an RID. Channels [code]100[/code] through [code]254[/code] are user-owned.
func channel_register(
		channel: int,
		handler: Callable,
		defer_when_unknown: bool = false,
) -> void:
	if channel < 100 or channel > 254 \
			or not handler.is_valid():
		return
	var adapter := func(
			wrapper: NetwEntity,
			payload: PackedByteArray,
			sender: int,
	) -> void:
		handler.call(wrapper.rid, payload, sender)
	_native_core.channel_book.register_channel(
		channel,
		adapter,
		defer_when_unknown,
	)


## Sends application [param payload] on one registered user channel.
func channel_send(
		peer: int,
		entity: RID,
		channel: int,
		payload: PackedByteArray,
		reliable: bool,
) -> Error:
	if channel < 100 or channel > 254:
		return ERR_INVALID_DATA
	var route := entity_get_route(entity)
	if route <= 0:
		return ERR_DOES_NOT_EXIST
	_native_core.send_to(peer, route, channel, payload, reliable)
	return OK


## Installs one typed service declaration into this session.
##
## A service configuration is session-owned. Kit nodes such as
## [MultiplayerClock] compile their exports into the same [NetwObjectConfig]
## resources accepted here, while code-first sessions may install those
## resources directly.
## [codeblock]
## var clock_config := NetwClockConfig.new()
## clock_config.tickrate = 60
## api.service_install(clock_config)
## [/codeblock]
##
## Returns [constant @GlobalScope.ERR_INVALID_PARAMETER] for an unknown service
## configuration.
func service_install(config: NetwObjectConfig) -> Error:
	if config == null:
		return ERR_INVALID_PARAMETER
	return _install_book.install(config, null)


func _install_clock_service(
		config: NetwObjectConfig,
		object: Object,
) -> Error:
	_apply_clock_config(config as NetwClockConfig)
	_native_core.clock_handle.is_configured = true
	_connect_once(_native_core.on_tick, _sweep_effects)
	_wire_lagcomp_service()
	if object is MultiplayerClock:
		_native_core.clock_handle.node_pumped = true
	return OK


# Writes a declared clock configuration onto the engine that runs the schedule.
# The config authors sync_mode in the public enum, which mirrors the engine's
# SyncMode exactly.
func _apply_clock_config(config: NetwClockConfig) -> void:
	var clock := _native_core.clock_handle
	clock.tickrate = config.tickrate
	clock.max_ticks_per_frame = config.max_ticks_per_frame
	clock.stall_threshold = config.stall_threshold
	clock.use_physics_interpolation = config.use_physics_interpolation
	clock.sync_mode = int(config.sync_mode)
	clock.panic_snap_threshold = config.panic_snap_threshold
	clock.stretch_nudge_factor = config.stretch_nudge_factor
	clock.ping_interval = config.ping_interval
	clock.display_offset = config.display_offset
	clock.jitter_multiplier = config.jitter_multiplier
	clock.jitter_window = config.jitter_window
	clock.jitter_stability_threshold = config.jitter_stability_threshold
	clock.enable_drift_logging = config.enable_drift_logging


func _uninstall_clock_service(
		_config: NetwObjectConfig,
		_object: Object,
) -> Error:
	_native_core.clock_handle.is_configured = false
	return OK


func _install_lagcomp_service(
		config: NetwObjectConfig,
		_object: Object,
) -> Error:
	configure(null, config as NetwLagCompensationConfig)
	_configured = true
	_wire_lagcomp_service()
	return OK


func _uninstall_lagcomp_service(
		_config: NetwObjectConfig,
		_object: Object,
) -> Error:
	_configured = false
	_close_tap()
	return OK


func _install_session_service(
		config: NetwObjectConfig,
		_object: Object,
) -> Error:
	_session_configure(config as NetwSessionConfig)
	return OK


func _uninstall_session_service(
		_config: NetwObjectConfig,
		_object: Object,
) -> Error:
	_session_deconfigure()
	return OK


func _install_scene_service(
		config: NetwObjectConfig,
		_object: Object,
) -> Error:
	if _embedding.phase == NetwEmbeddingHandle.Phase.LIVE:
		Netw.dbg.warn(
			"NetwMultiplayer: scene declaration registered after settle is "
			+ "off-contract. Authoring is only in-contract while %s.",
			[
				NetwEmbeddingHandle.Phase.keys()[
					NetwEmbeddingHandle.Phase.DECLARING
				],
			],
		)
	_scene_configure(config as NetwSceneConfig)
	return OK


func _uninstall_scene_service(
		_config: NetwObjectConfig,
		_object: Object,
) -> Error:
	_scene_deconfigure()
	return OK


func _wire_lagcomp_service() -> void:
	if not _native_core.clock_handle.is_configured or not is_configured():
		return
	_connect_once(_native_core.before_tick_loop, before_frame_step)
	_connect_once(_native_core.on_tick, tick_step)
	_connect_once(_native_core.after_tick_loop, frame_step)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.ACTION,
		_handle_action_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_COMMAND,
		_handle_predict_command_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_ACK,
		_handle_predict_ack_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_RELAY,
		_handle_predict_relay_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST,
		_handle_predict_relay_request_carrier,
	)


## Returns whether [param sender] may write one entity component.
func sync_policy_admits(
		policy: WritePolicy,
		sender: int,
		entity: RID,
		comp: int,
) -> bool:
	var wrapper := _entity_wrapper(entity)
	var node := _entity_component_node(entity, comp)
	if wrapper == null or not is_instance_valid(node):
		return false
	return NetwEntityControl.policy_admits(
		int(policy),
		sender,
		node.get_multiplayer_authority(),
		wrapper.controller,
	)


## Sends the local player's control request to server authority.
## [br][br][b]Player request.[/b]
func entity_request_control(entity: RID) -> void:
	_native_core.entity_control_request(entity)


## Grants [param peer] control and broadcasts the change.
## [br][br][b]Server Only.[/b]
func entity_grant_control(entity: RID, peer: int) -> void:
	if not is_server():
		return
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		wrapper.grant_control(peer)


## Produces the sync body one [param peer] receives for one [param tick].
##
## Encoding is per recipient because interest already decided that peers see
## different worlds. The bytes returned here are the frame body only, and the
## carrier adds the [NetwFrameEnvelope] header around them.
##
## An empty [PackedByteArray] means "nothing to send this peer this tick",
## which is the ordinary result for a peer whose visible entities are all
## unchanged. It is not an error.
##
## The exact inverse of [method _sync_decode]. Whatever this writes, that must read, so the two are
## replaced together in practice even though the group check does not require
## it.
func _sync_encode(peer: int, tick: int) -> PackedByteArray:
	if not _sync_encoder.is_valid():
		return PackedByteArray()
	return _sync_encoder.call(peer, tick)


## Applies one admitted sync body to [param entity]'s [param comp] address.
##
## Decoding runs only after [method _sync_admit_frame] returned
## [constant OK], so the route is live and the payload is non-empty by the time
## this sees it. It may still refuse bytes that pass the gate but do not match
## the attached property set.
## [codeblock]
## Error
## ┠╴OK                 the payload was applied to the component
## ┖╴ERR_UNCONFIGURED   no decoder is installed for this address
## [/codeblock]
## [param flags] carries the frame's sync bits and [param tick] the authoring
## tick, which is what lets a late frame be recognized as stale.
## [param payload] is the body [method _sync_encode] produced on the sender.
##
## The exact inverse of [method _sync_encode].
@warning_ignore("unused_parameter")
func _sync_decode(
		entity: RID,
		comp: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
) -> Error:
	return _sync_decoder.call() if _sync_decoder.is_valid() \
	else ERR_UNCONFIGURED


## Records that [param peer] acknowledged everything through [param sequence].
##
## Delta encoding is only sound against a baseline the recipient is known to
## hold, and this is how that baseline advances. A peer whose acks stop moving
## keeps its old baseline, so its deltas grow rather than going wrong.
##
## [param sequence] is monotonic per peer, and an out-of-order or repeated
## acknowledgement is ignored rather than rewinding the baseline.
##
## Override to observe delta progress, and call
## [code]super()[/code] so the baseline still advances.
func _note_ack(peer: int, sequence: int) -> void:
	_replication._sync_pipeline.note_peer_ack(peer, sequence)


## Records that [param sequence] was sent to [param peer].
##
## The send side must commit its pending masked state at the moment it goes out
## rather than when it is acknowledged, so that the delta this frame produced
## is the delta the next one builds on.
##
## The send-side twin of [method _note_ack].
func _note_sent(peer: int, sequence: int) -> void:
	_replication._sync_pipeline.commit_pending_masked(peer, sequence)


## Reads the current values of one property set from the scene.
##
## This and [method _apply_set] are the sync lane's boundary onto node
## properties: every read a replicated property set performs crosses here, and
## everything between them speaks values rather than nodes, which is what lets
## the sync core be tested with no scene at all.
##
## The lane is what this pair covers, not the session. Prediction and
## [method lagcomp_rewind] write the scene through their own slot-bound port
## instead, because a rewind moves an object the sync lane never declared a set
## for and must put it back whatever the body did. An override installed here
## therefore sees replication traffic and does not see a rewind.
##
## The returned [Array] is positional: one entry per field, in the sealed
## declaration order of the set attached at [param comp]. That order is the
## wire order, so an override must not reorder, pad, or omit entries. An
## address with no set attached gathers an empty array.
##
## Override to synthesize a value the scene does not
## store as a plain property.
@warning_ignore("unused_parameter")
func _gather_set(entity: RID, comp: int) -> Array:
	return _set_gatherer.call() if _set_gatherer.is_valid() else []


## Writes one decoded property set's [param values] back into the scene.
##
## Writes are staged and applied as one batch per address, so a component never
## observes half of an update. This is the exact inverse of
## [method _gather_set], reads the same positional order, and covers the same
## lane: a rewind or a prediction restore does not pass through here.
## [codeblock]
## Error
## ┠╴OK                 every field was written
## ┖╴ERR_UNCONFIGURED   no writer is installed for this address
## [/codeblock]
## [param values] holds one entry per field in sealed declaration order.
##
## Override to intercept, clamp, or ignore incoming
## writes.
## [codeblock]
## func _apply_set(entity, comp, values):
##     if _frozen.has(entity):
##         return OK          # accepted, deliberately not written
##     return super(entity, comp, values)
## [/codeblock]
@warning_ignore("unused_parameter")
func _apply_set(entity: RID, comp: int, values: Array) -> Error:
	return _set_applier.call(values) if _set_applier.is_valid() \
	else ERR_UNCONFIGURED


# Runs one shell reader through the installed gather stage.
func _run_gather_set(
		entity: RID,
		comp: int,
		gatherer: Callable,
) -> Array:
	_set_gatherer = gatherer
	var values := _gather_set(entity, comp)
	_set_gatherer = Callable()
	return values


# Runs one shell writer through the installed apply stage.
func _run_apply_set(
		entity: RID,
		comp: int,
		values: Array,
		applier: Callable,
) -> Error:
	_set_applier = applier
	var verdict := _apply_set(entity, comp, values)
	_set_applier = Callable()
	return verdict


# Resolves an entity component through the hostile-path clamp.
func _entity_component_node(entity: RID, comp: int) -> Node:
	return _comp_node(_entity_wrapper(entity), comp)


# The node [param comp] addresses under [param wrapper], or null. An entity
# with no owner addresses nothing, which is the answer the map already gives
# rather than a check kept here.
func _comp_node(wrapper: NetwEntity, comp: int, path: String = "") -> Node:
	if wrapper == null:
		return null
	return wrapper.components.resolve_node(wrapper.owner, comp, path)

#endregion

## Calls [param method] on one entity component through the RPC carrier.
##
## [param comp] is the registration-time component id. [code]0[/code] names
## the entity root. The target method must be registered through Networked or
## annotated as an RPC.
func entity_call(
		entity: RID,
		comp: int,
		method: StringName,
		args: Array,
		peer: int,
) -> Error:
	var node := _entity_component_node(entity, comp)
	if node == null:
		return ERR_UNAVAILABLE if entity_get_state(entity) == EntityState.LIVE \
		else ERR_DOES_NOT_EXIST
	if method.is_empty() or not node.has_method(method):
		return ERR_DOES_NOT_EXIST
	var script := node.get_script() as Script
	var options := NetwScriptModel.get_rpc_options(script, method)
	if not options and (not script or not script.get_rpc_config().has(method)):
		return ERR_UNCONFIGURED
	_rpc_core.rpc_call(Callable(node, method), args, peer)
	return OK


## Hydrates the persisted fields of [param entity], answering the
## [NetwPromise] the read settles with its [enum @GlobalScope.Error].
##
## The wait is a database read, so the answer is a promise rather than the code
## itself and the caller subscribes instead of awaiting.
## [br][br][b]Server Only.[/b]
func persist_hydrate(entity: RID) -> NetwPromise:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return NetwPromise.resolved(ERR_DOES_NOT_EXIST)
	var engine := _native_core.persistence_engine_for(wrapper)
	if engine == null:
		return NetwPromise.resolved(ERR_UNCONFIGURED)
	return engine.hydrate()


## Flushes persisted [param keys] from [param entity], answering the
## [NetwPromise] the write settles with its [enum @GlobalScope.Error].
## [br][br][b]Server Only.[/b]
func persist_flush(entity: RID, keys: Array = []) -> NetwPromise:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return NetwPromise.resolved(ERR_DOES_NOT_EXIST)
	var engine := _native_core.persistence_engine_for(wrapper)
	if engine == null:
		return NetwPromise.resolved(ERR_UNCONFIGURED)
	return engine.flush(keys)


## Advances the persistence snapshot loop by [param delta] seconds.
##
## The loop is one pass over every persisted entity, and it flushes only the
## subset whose accumulator came due, so the cost of a pass is the census and
## not the write. Clients no-op, because every persistence trigger is
## server-gated.
##
## [method MultiplayerAPI.poll] already advances the loop by the wall-clock gap
## since the last poll, so a session saves on its own cadence and this verb is
## for a caller driving persistence time itself, such as a test.
## [br][br][b]Server Only.[/b]
func persist_tick(delta: float) -> void:
	_native_core.persistence_tick(delta)


## Saves [param table]'s committed rows into [param db] as one record.
##
## The unit of commit and of hydrate is the whole table, because routes must be
## re-minted as a set, so a table saves as one record rather than one record per
## row. Two thousand rows are one file on the file-system backend, not two
## thousand.
## [codeblock]
## db.<into>
##  ┖╴<schema name>          the one record
##       ┠╴ids               PackedStringArray, parallel to the row order
##       ┖╴<column key>      the committed storage array, verbatim
## [/codeblock]
## [param ids] names the row whose route is
## [code]table_read_routes(table)[i][/code]. Stable identity is the caller's
## domain, since they minted the rows, and it is what lets a fresh session
## rebuild its own indexes from [method persist_table_hydrate]. Save keys never
## ride the wire and routes never touch the disk.
##
## [br][br]Values are the committed storage arrays, which quantization never
## touches, so a quantized column saves at full precision. A
## [constant ColumnType.COLUMN_ENTITY] column is skipped with one warning,
## because a route is meaningless in the session that loads it.
## The answer is a [NetwPromise] because the write is a database write, and it
## resolves with the [enum @GlobalScope.Error] the write reached.
## [codeblock]
## Error
## ┠╴OK                  written
## ┠╴ERR_UNCONFIGURED    no database, no table name, or not the server
## ┠╴ERR_DOES_NOT_EXIST  the handle names no table
## ┖╴ERR_INVALID_DATA    ids.size() disagrees with the committed row count
## [/codeblock]
## [br][br][b]Server Only.[/b]
func persist_table_flush(
		table: RID,
		db: NetwDatabase,
		into: StringName,
		ids: PackedStringArray,
) -> NetwPromise:
	if not is_server():
		Netw.dbg.error("NetwMultiplayer.persist_table_flush is server-only")
		return NetwPromise.resolved(ERR_UNCONFIGURED)
	if db == null or into.is_empty():
		return NetwPromise.resolved(ERR_UNCONFIGURED)
	var schema := table_get_schema(table)
	if not schema.is_valid():
		return NetwPromise.resolved(ERR_DOES_NOT_EXIST)
	var routes := table_read_routes(table)
	if ids.size() != routes.size():
		return NetwPromise.resolved(ERR_INVALID_DATA)

	var values: Dictionary = { &"ids": ids }
	var skipped := PackedStringArray()
	for column in schema_get_column_count(schema):
		var key := schema_get_column_key(schema, column)
		if schema_get_column_type(schema, column) == ColumnType.COLUMN_ENTITY:
			skipped.append(String(key))
			continue
		var stored: Variant = table_read_column(table, column)
		values[key] = stored.duplicate() if stored != null else stored
	if not skipped.is_empty():
		Netw.dbg.warn(
			"NetwMultiplayer: table '%s' column(s) %s are routes, which mean "
			+ "nothing in the session that loads them, so they are not saved "
			+ "and hydrate zero-fills them.",
			[_schema_core.name_of(schema), ", ".join(skipped)],
			func(m): push_warning(m),
		)

	var names: Array[StringName] = []
	for key: StringName in values:
		names.append(key)
	db.declare_table(into, names)
	return db.transaction_promise(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(into, _schema_core.name_of(schema), values)
	)


## Loads [param table]'s saved record from [param db], mints fresh routes for
## its rows, and commits them.
##
## Routes are session-scoped, so a hydrate claims new ones rather than restoring
## the ones that were saved. The returned pairing is how a caller rebuilds its
## own indexes against the save keys it wrote.
## [codeblock]
## Dictionary
## ┠╴routes  (PackedInt64Array)    freshly claimed, the committed row order
## ┖╴ids     (PackedStringArray)   parallel, the keys the flush wrote
## [/codeblock]
## Both arrays are empty when no record exists, which is the first-play case
## rather than an error. A [constant ColumnType.COLUMN_ENTITY] column
## zero-fills, matching the skip at flush.
##
## The answer is a [NetwPromise] resolving with that [Dictionary], because the
## read is a database read and the rows are committed on the edge it settles.
## [br][br][b]Server Only.[/b]
func persist_table_hydrate(
		table: RID,
		db: NetwDatabase,
		into: StringName,
) -> NetwPromise:
	var out := {
		&"routes": PackedInt64Array(),
		&"ids": PackedStringArray(),
	}
	if not is_server():
		Netw.dbg.error("NetwMultiplayer.persist_table_hydrate is server-only")
		return NetwPromise.resolved(out)
	var schema := table_get_schema(table)
	if db == null or into.is_empty() or not schema.is_valid():
		return NetwPromise.resolved(out)

	var names: Array[StringName] = [&"ids"]
	for column in schema_get_column_count(schema):
		names.append(schema_get_column_key(schema, column))
	db.declare_table(into, names)
	var answer := NetwPromise.new()
	var stored := db.find_promise(into, _schema_core.name_of(schema))
	stored.catch_error(
		func(_code: int, _detail: String) -> void: answer.resolve(out),
	)
	stored.then(
		func(data: Dictionary) -> void:
			answer.resolve(_persist_table_commit(table, schema, data)),
	)
	return answer


# Claims fresh routes for the rows [param data] saved, writes every column back
# under them, and answers the route-to-save-key pairing the caller rebuilds its
# indexes from. An empty record claims nothing, which is the first play.
func _persist_table_commit(
		table: RID,
		schema: RID,
		data: Dictionary,
) -> Dictionary:
	var out := {
		&"routes": PackedInt64Array(),
		&"ids": PackedStringArray(),
	}
	if data.is_empty():
		return out

	var ids := PackedStringArray(data.get(&"ids", PackedStringArray()))
	var routes := claim_routes(ids.size())
	table_write_routes(table, routes)
	for column in schema_get_column_count(schema):
		var key := schema_get_column_key(schema, column)
		var type := schema_get_column_type(schema, column)
		var stride := schema_get_column_stride(schema, column)
		var wanted := SchemaCore.storage_type(type)
		var stored: Variant = data.get(key)
		if type == ColumnType.COLUMN_ENTITY or typeof(stored) != wanted \
				or stored.size() != routes.size() * stride:
			stored = SchemaCore.make_storage(type)
			stored.resize(routes.size() * stride)
		table_write_column(table, column, stored)
	table_commit(table)

	out[&"routes"] = routes
	out[&"ids"] = ids
	return out


## Flushes every persisted entity and drains the backends before quitting.
##
## A save that is still in flight when the process exits is a lost save, so this
## is the one persistence call that must complete rather than come due. It runs
## once. A second call while the drain is in flight is ignored.
## [codeblock]
## broadcast the shutdown ─▶ flush every engine ─▶ drain backends ─▶ quit
## [/codeblock]
## [br][br][b]Server Only.[/b]
func persist_shutdown() -> void:
	_native_core.persistence_shutdown()

#region Spawn

## Arms [param node] for replicated construction and returns its entity RID.
## [br][br][b]Server Only.[/b]
func replicate(node: Node, owner: NetwParticipant = null) -> RID:
	return _native_core.spawn_replicate(node, owner)


## Runs and replicates one configured spawn function.
## [br][br][b]Server Only.[/b]
func spawn_fn(
		function: Callable,
		args: Array = [],
		owner: NetwParticipant = null,
) -> RID:
	return _native_core.spawn_function(function, args, owner)


## Registers one host-less spawn constructor.
func spawn_register_constructor(id: StringName, function: Callable) -> void:
	_native_core.spawn_register_constructor(id, function)


## Runs and replicates one registered constructor.
## [br][br][b]Server Only.[/b]
func spawn_registered(
		id: StringName,
		args: Array = [],
		owner: NetwParticipant = null,
) -> RID:
	return _native_core.spawn_registered(id, args, owner)


## Adopts one already-present node into replication.
## [br][br][b]Server Only.[/b]
func adopt_in_place(root_node: Node) -> RID:
	return _native_core.spawn_adopt(root_node)


## Despawns one live entity.
## [br][br][b]Server Only.[/b]
func despawn(entity: RID, opts: NetwDespawnOpts = null) -> Error:
	if not is_server():
		return ERR_UNAUTHORIZED
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	wrapper.despawn(opts)
	return OK


## Returns one entity subtree's authored spawn state contribution.
func spawn_get_state(entity: RID) -> Array[Dictionary]:
	return _native_core.spawn_state_of(entity)


## Declares that [param entity] may be materialized on other peers.
##
## Declaring makes an entity eligible for spawning, it does not spawn it.
## Interest decides who actually receives it, and [method _spawn_reconcile]
## turns that decision into work.
## [codeblock]
## Error
## ┠╴OK                  the entity is eligible for materialization
## ┖╴ERR_DOES_NOT_EXIST  the RID names no entity this peer knows
## [/codeblock]
## [param recipe] is the value-only description a remote peer needs in order to
## construct the entity. It never carries a [Node] or an object reference,
## because a spawn plan has to survive serialization.
##
## Override to validate or enrich the recipe, then
## call [code]super()[/code].
## [br][br][b]Server Only.[/b]
@warning_ignore("unused_parameter")
func _spawn_declare(entity: RID, recipe: Variant) -> Error:
	return OK if _entity_wrapper(entity) else ERR_DOES_NOT_EXIST


## Withdraws [param entity] from materialization eligibility.
##
## Withdrawing stops future spawns; it does not despawn the copies peers
## already hold. Those leave through the interest layer's
## [enum LeavePolicy] instead.
##
## The twin of [method _spawn_declare].
## [br][br][b]Server Only.[/b]
@warning_ignore("unused_parameter")
func _spawn_undeclare(entity: RID) -> void:
	pass


## Plans the spawns and despawns this frame's interest matrix now implies.
##
## Reconciliation is pure and ordered: it reads the committed interest rows and
## the peer set, and produces a plan without materializing anything. Deciding
## and doing are separate so the plan is inspectable and its order is
## reproducible rather than dependent on iteration order.
## [codeblock]
## Error
## ┖╴OK   the plan is staged for the spawn pump to carry out
## [/codeblock]
## Runs once per tick after [method _interest_commit]. The plan it stages is
## carried out by [method _spawn_construct] on each receiving peer.
## [br][br][b]Server Only.[/b]
func _spawn_reconcile() -> Error:
	_spawn_reconcile_plan = NetwSpawnPlanner.reconcile(
		_spawn_reconcile_rows,
		_spawn_reconcile_peers,
	)
	return OK


## Builds the local [Node] for one entity the server told this peer to spawn.
##
## This is the receiving half of materialization and the one stage that turns a
## value-only recipe back into a scene. It constructs and returns the node
## without adding it to the tree; the spawn pump owns parenting so that
## reparenting stays the pump's decision.
##
## Returns [code]null[/code] when nothing can be built, which the pump reports
## rather than treating as an empty entity.
##
## Override to pool nodes, pick a variant scene, or
## substitute a proxy for an entity this peer does not need in full.
## [codeblock]
## func _spawn_construct(entity):
##     var node := _pool.pop_back() as Node
##     return node if node else super(entity)
## [/codeblock]
func _spawn_construct(_entity: RID) -> Node:
	return _spawn_constructor.call() as Node \
	if _spawn_constructor.is_valid() else null

#endregion

#region Carrier

## Emitted for raw packets received through
## [method SceneMultiplayer.send_bytes] that do not carry Networked framing.
## Networked reserves the two [NetwFrameEnvelope] magic first bytes, so
## application byte traffic must not start with them.
signal peer_packet(id: int, packet: PackedByteArray)

# The sender of the carrier frame currently being dispatched, or 0 when no
# relayed dispatch is on the stack. ReplicationCore._dispatch stamps it
# so _get_remote_sender_id answers with the frame's sender for handlers reached
# through the carrier, the same value a native @rpc handler would read. Nested
# dispatch saves and restores it.
var _relay_sender: int = 0


# Sinks one raw packet through the carrier intake verb.
func _on_inner_peer_packet(id: int, packet: PackedByteArray) -> void:
	_sink_verdict(_receive_inner_packet(id, packet), 0)


# Demuxes Networked carrier packets from application byte traffic.
func _receive_inner_packet(id: int, packet: PackedByteArray) -> Error:
	var header := _native_core.receive_header(id, packet)
	match header.kind:
		NetwCarrierFrame.Kind.FOREIGN:
			return OK
		NetwCarrierFrame.Kind.MALFORMED:
			return ERR_INVALID_DATA
	if header.kind == NetwCarrierFrame.Kind.RELIABLE:
		return _drive_carrier(id, packet.slice(header.payload_offset), true)
	if header.kind == NetwCarrierFrame.Kind.UNRELIABLE_ACKED:
		_native_core.count_state_ack_in()
		_note_state_ack(id, header.ack)
	_note_inbound_seq(id, header.seq)
	return _drive_carrier(
		id,
		packet.slice(header.payload_offset),
		false,
		header.seq,
	)


# Test surface for driving one decoded carrier datagram.
func _drive_carrier(
		sender: int,
		bytes: PackedByteArray,
		reliable: bool,
		seq: int = -1,
) -> Error:
	return _replication.receive_carrier(bytes, sender, reliable, seq)


# Applies the fixed verdict channel policy at a signal callback boundary.
func _sink_verdict(verdict: Error, route: int) -> void:
	_native_core.sink_verdict(verdict, route)


# Counts a gate verdict and applies its warning policy once per route.
func _warn_gate_verdict(
		verdict: Error,
		route: int,
		message: String,
		args: Array = [],
) -> void:
	if _native_core.warn_verdict(verdict, route):
		Netw.dbg.warn(message, args)


# Records [param seq] as the freshest inbound datagram from [param sender] when it
# is newer across the u16 half window, so a reordered datagram never rolls the
# echo backward.
func _note_inbound_seq(sender: int, seq: int) -> void:
	_native_core.note_inbound_seq(sender, seq)


# Records [param ack] as [param peer]'s confirmation of our sends when it is newer
# across the u16 half window. The confirmed seq only advances, so a stalled echo
# from a silent peer holds its baseline rather than corrupting it. Advancing it
# promotes peer's masked-lane in-flight rows through the pipeline; a
# stalled ack (this branch not taken) correctly leaves those rows untouched.
func _note_state_ack(peer: int, ack: int) -> void:
	if _native_core.note_peer_ack(peer, ack):
		_note_ack(peer, ack)


## Returns the freshest datagram seq [param peer] has echoed as held, or
## [code]-1[/code] when that peer has acked nothing. The masked delta lane reads
## this as each recipient's confirmed baseline seq.
func _peer_state_ack(peer: int) -> int:
	return _native_core.peer_ack(peer)


## Emits a standalone transport ack to every peer whose freshest inbound
## datagram seq no outbound datagram this tick already echoed, so the masked
## delta lane's confirmed baseline advances even for a peer that sends the
## author nothing to piggyback the echo on.
## [codeblock]
## author --seq N--> silent observer   (a client-authored broadcast)
## author <--ack N-- standalone echo   (the observer had no reply to ride)
##
## chatty pair: the echo piggybacks the real reply, nothing extra is sent
## [/codeblock]
## Only a fresh inbound seq produces an echo, so the rule self-throttles to the
## author's send rate. Driven once per tick by
## [method ReplicationCore.on_clock_tick] after the aggregation
## buffers flush, so a real datagram's piggybacked echo always wins.
func flush_standalone_acks() -> void:
	if not inner.multiplayer_peer:
		return
	for peer_id: int in _native_core.peers_owed_echo():
		if peer_id != 0 and peer_id not in inner.get_peers():
			continue
		_send_standalone_ack(peer_id, _native_core.inbound_seq(peer_id))


# Sends a zero-frame acked datagram to [param peer_id] carrying [param ack], the
# freshest inbound seq of theirs we hold. The acked shape's [magic | seq | ack]
# header round-trips through the receive path with no frames to dispatch
# (receive_carrier no-ops on the empty remainder), so this is the standalone form
# of the echo send_packet piggybacks on a real datagram. It bypasses that path's
# empty-payload drop deliberately: the whole point is a datagram with no payload.
func _send_standalone_ack(peer_id: int, ack: int) -> void:
	var seq := _native_core.next_send_seq(peer_id)
	var framed := NetwCarrierFrame.build(PackedByteArray(), false, seq, ack)
	_native_core.note_echoed_seq(peer_id, ack)
	_native_core.count_standalone_ack_out()
	_native_core.count_sent(framed.size())
	inner.send_bytes(framed, peer_id, MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)


# Clock tick callback that pumps registered senders, then services the transport
# so the tick's own frames leave with it and everything queued since the last
# tick arrives before the drives that read it.
#
# The order is load-bearing and is why this rides the tick rather than the frame
# boundary: the aggregation flush stamps a datagram's sequence and the standalone
# echo answers the freshest inbound one, so servicing before that pass would
# promote a masked baseline against a sequence the tick had not yet authored.
func _on_clock_tick(_delta: float, tick: int) -> void:
	_sink_verdict(_drive_tick(tick), 0)


# Test surface for driving one simulation tick and its transport service.
func _drive_tick(tick: int) -> Error:
	if not _layer_drivers.is_empty():
		var interest_verdict := interest_flush()
		if interest_verdict != OK:
			return interest_verdict
	_replication.on_clock_tick(tick)
	var err := _embedding.poll_transport()
	# The tick pump settles too. Idempotence across the two pumps is the queue's
	# job through its keys, never the scheduling site's.
	_native_core.settle_advance()
	_settle()
	_native_core.scene_pump_retired()
	return err


# The tick a received payload is stamped with: the session tick when the clock
# engine is configured, otherwise a local frame counter.
func _receive_tick() -> int:
	var clock := _native_core.clock_handle
	return clock.tick if clock.is_configured else _native_core.frame_counter


# Counts and returns one hostile-input verdict.
func _count_gate_verdict(verdict: Error, route: int = 0) -> Error:
	_native_core.count_verdict(verdict, route)
	return verdict


# Records which stage answered, then counts and reports its verdict.
func _finish_stage_verdict(stage: int, verdict: Error, route: int) -> Error:
	return _native_core.stage_verdict(stage, verdict, route) as Error


# Returns the shared route and liveness verdict for an entity frame.
func _entity_frame_verdict(route: int) -> Error:
	match _native_core.liveness_route_state(route):
		NetwLivenessCore.STATE_UNKNOWN:
			return ERR_DOES_NOT_EXIST
		NetwLivenessCore.STATE_LINGERING, \
		NetwLivenessCore.STATE_DEAD:
			return ERR_SKIP
	var entity := _native_core.wrapper_for_route(route) as NetwEntity
	if not entity or not is_instance_valid(entity.owner):
		return ERR_UNAVAILABLE
	return OK


## Verdict on one inbound sync frame from [param sender].
##
## Gates judge remote bytes before the wire [param route] resolves to an
## entity, so they key on [param route] and never on a [RID]. "Unknown route"
## is itself a verdict, and a [RID] never survives serialization.
## [codeblock]
## Error
## ┠╴OK                  live route, known channel, non-empty payload
## ┠╴ERR_DOES_NOT_EXIST  route names nothing this peer has seen
## ┠╴ERR_SKIP            route is lingering or a tombstone
## ┠╴ERR_UNAVAILABLE     route is live but its node is gone
## ┖╴ERR_INVALID_DATA    wrong channel, or an empty payload
## [/codeblock]
## [param channel] is a [enum NetwFrameEnvelope.Channel] and reaches here only
## as [constant NetwFrameEnvelope.Channel.SYNC],
## [constant NetwFrameEnvelope.Channel.SYNC_ROW],
## [constant NetwFrameEnvelope.Channel.SYNC_ROW_DELTA], or
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA]. [param comp] is the
## registration-time component id, [code]0[/code] for the entity root.
## [param flags] carries the frame's sync bits and [param tick] the authoring
## tick. [param payload] is the undecoded body.
##
## Override to refuse traffic the stock verdict admits. Call [code]super()[/code]
## first and only narrow it, because a gate that widens the stock verdict
## admits bytes the stages below assume were already refused. Every returned
## verdict is counted under its [enum Stat] twin.
## [codeblock]
## func _sync_admit_frame(sender, route, comp, channel, flags, tick, payload):
##     var verdict := super(sender, route, comp, channel, flags, tick, payload)
##     if verdict == OK and payload.size() > _budget:
##         return ERR_INVALID_DATA
##     return verdict
## [/codeblock]
func _sync_admit_frame(
		sender: int,
		route: int,
		comp: int,
		channel: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
) -> Error:
	var verdict := _entity_frame_verdict(route)
	if verdict == OK and channel not in [
		NetwFrameEnvelope.Channel.SYNC,
		NetwFrameEnvelope.Channel.SYNC_ROW,
		NetwFrameEnvelope.Channel.SYNC_ROW_DELTA,
		NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW,
		NetwFrameEnvelope.Channel.SYNC_DELTA,
	]:
		verdict = ERR_INVALID_DATA
	if verdict == OK and payload.is_empty():
		verdict = ERR_INVALID_DATA
	# The header travels so an override can judge it; the stock verdict reads
	# only the route, the channel, and the payload.
	var _header := [sender, comp, flags, tick]
	return verdict


## Verdict on one inbound spawn, despawn, or reparent frame from [param sender].
##
## Materialization is server-authored, so this gate refuses any sender but the
## server before it looks at anything else. It runs pre-resolution: the frame
## that creates a route necessarily arrives before that route resolves.
## [codeblock]
## Error
## ┠╴OK                 sender is the server, known channel, non-empty payload
## ┠╴ERR_UNAUTHORIZED   sender is not peer 1
## ┖╴ERR_INVALID_DATA   wrong channel, or an empty payload
## [/codeblock]
## [param channel] is a [enum NetwFrameEnvelope.Channel] and is admitted only as
## [constant NetwFrameEnvelope.Channel.SPAWN],
## [constant NetwFrameEnvelope.Channel.DESPAWN], or
## [constant NetwFrameEnvelope.Channel.REPARENT]. [param payload] is the
## undecoded spawn packet. Unlike the other two gates, no liveness verdict is
## consulted, so [param route] is unresolved by construction.
##
## Override to add an admission rule of your own, such as a cap on how many
## entities one bring-up may materialize. Call [code]super()[/code] first and
## only narrow the verdict.
func _spawn_admit_frame(
		sender: int,
		route: int,
		channel: int,
		payload: PackedByteArray,
) -> Error:
	return _native_core.spawn_admit_frame_default(
		sender,
		route,
		channel,
		payload,
	)


## Verdict on one inbound [constant NetwFrameEnvelope.Channel.TABLE] frame.
##
## The only gate in the addon that takes no route, because a table frame is
## route-0 addressed and the routes it carries are knowable only mid-decode.
## Everything it judges is in the header: who sent it, which table it names,
## and whether that table's sealed schema agrees.
## [codeblock]
## Error
## ┠╴OK                  server sender, a table this peer sealed, hash agrees
## ┠╴ERR_UNAUTHORIZED    sender is not peer 1
## ┠╴ERR_DOES_NOT_EXIST  no local table carries that wire id
## ┖╴ERR_INVALID_DATA    empty or truncated header, a schema hash
##                       disagreement, or an unimplemented flag bit
## [/codeblock]
## An extension may refuse a whole frame here. It may not refuse row 47: the
## decoder's only sanctioned mid-loop skips are the tombstone and stale-row
## drops, which are counted and never overridable.
##
## Override to narrow the stock verdict, calling [code]super()[/code] first.
## Widening it admits bytes the decoder assumes were already refused.
func _table_admit_frame(
		sender: int,
		channel: int,
		payload: PackedByteArray,
) -> Error:
	return _native_core.table_admit_frame_default(sender, channel, payload)


## Verdict on one inbound prediction command, acknowledgement or relay.
##
## The prediction lanes travel in opposite directions and each is authorized
## separately. A command is only ever honored by the server from the entity's
## own [member NetwEntity.controller], and an acknowledgement or a relay is
## only ever honored from the server. No peer may speak another's lane.
## [codeblock]
## Error
## ┠╴OK                  live route, correct lane for this sender
## ┠╴ERR_DOES_NOT_EXIST  route names nothing this peer has seen
## ┠╴ERR_SKIP            route is lingering or a tombstone
## ┠╴ERR_UNAVAILABLE     route is live but its node is gone
## ┠╴ERR_UNAUTHORIZED    a client sent a command, or a non-server sent an ack
## ┖╴ERR_INVALID_DATA    wrong channel, or an empty payload
## [/codeblock]
## [param channel] is a [enum NetwFrameEnvelope.Channel], admitted only as
## [constant NetwFrameEnvelope.Channel.PREDICT_COMMAND] (client to server),
## [constant NetwFrameEnvelope.Channel.PREDICT_ACK] (server to the owner) or
## [constant NetwFrameEnvelope.Channel.PREDICT_RELAY] (server to a subscriber).
## [param payload] is the undecoded lane body.
##
## Override to police input rate or command shape before the tape sees it. Call
## [code]super()[/code] first: the lane authorization above is what keeps one
## client from driving another's entity.
func _predict_admit_frame(
		sender: int,
		route: int,
		channel: int,
		payload: PackedByteArray,
) -> Error:
	var entity := _native_core.wrapper_for_route(route) as NetwEntity
	return NetwPredictionCore.admit_frame(
		channel,
		sender,
		entity.controller if entity else 0,
		is_server(),
		payload.is_empty(),
		_entity_frame_verdict(route),
	) as Error

#region Prediction decision hosting

## Chooses which input one prediction frame drives, and under what label.
##
## Every driven frame is labeled, including one that had no new input. A frame
## with nothing newer than the last driven repeats the input it already had
## rather than inventing one, so the tape never gains an entry no input
## justifies.
## [param latest_input_tick] is the newest input available, or a negative value
## when none has arrived at all. [param last_driven_input_tick] is what the
## previous frame drove. [param frame_tick] labels the pass when there is no
## input to take a label from.
## [codeblock]
## latest > driven  ──> FRESH,  labeled by the input
## latest <= driven ──> REPEAT, labeled by the input it repeats
## latest < 0       ──> REPEAT, labeled by the pass timing
## [/codeblock]
## Pure: it reads only its arguments and must
## return the same record for the same three ticks. Override to change which
## input a frame drives, never to record anything.
func _predict_drive(
		latest_input_tick: int,
		last_driven_input_tick: int,
		frame_tick: int,
) -> NetwPredictFold:
	return NetwPredictionCore.predict_fold(
		latest_input_tick,
		last_driven_input_tick,
		frame_tick,
	)


## Chooses whether authority replays a queued transition, holds, or runs dry.
##
## Authority replays whenever it holds a transition past [param buffer], which
## is zero by default, so the ordinary verdict is simply whether the queue has
## anything in it. Raising [param buffer] moves the boundary and nothing else:
## the queue sits that much deeper and every arrival waits that much longer.
## [param depth] is how many transitions are queued.
## [codeblock]
## depth > buffer  ──> REPLAY   a queued transition is replayed now
## depth > 0       ──> HOLD     something is queued, but not deep enough
## depth == 0      ──> STARVED  nothing to replay; the world still solves
## [/codeblock]
## Declining a transition is not free. A frame that holds still solves, so the
## world advances by a step belonging to no transition and the next drive spans
## two, which is the quantum fault
## [member NetwPredictStats.quantum_faults] counts.
##
## Pure.
func _predict_consume(depth: int, buffer: int) -> NetwPredict.ConsumeAction:
	var action := NetwPredictionCore.consume_action(depth, buffer)
	return action as NetwPredict.ConsumeAction


## Judges one acknowledged transition against the owner's prediction of it.
##
## Which comparison runs is the transition's own [param domain], not a setting.
## A transition whose antecedents were all declared equal has no tolerance to
## spend, so any inequality is a divergence. One whose antecedents were not is
## compared by tolerance instead, since the peers never claimed the exactness a
## fingerprint would test for.
## [param domain] is a [enum NetwPredictJournal.Domain] and [param verdict] a
## [enum NetwPredict.ExactVerdict]. [param predicted] is what this peer
## recorded and [param payload] what authority acknowledged, both keyed by
## property name. [param wiring] carries the entity's tolerances and angle
## fields.
## [codeblock]
## IN_DOMAIN  + UNEQUAL  ──> corrected, no epsilon consulted
## IN_DOMAIN  + EQUAL    ──> agreed,    no epsilon consulted
## IN_DOMAIN  + UNJUDGED ──> epsilon, because blind is not exact
## OUT_OF_DOMAIN         ──> epsilon
## [/codeblock]
## An empty [param predicted] means nothing was recorded at or before the
## acknowledgement, so there is no transition to judge. That reports
## [code]INF[/code] and corrects, because an unjudged transition must never
## pass as an agreeing one.
##
## [param field_sink] is filled with the per-property error on every branch. It
## is an argument rather than a return value so that anyone already holding
## [member NetwPredictionHandle.last_field_divergence] keeps their reference.
##
## Pure apart from filling [param field_sink].
func _predict_evaluate(
		domain: int,
		verdict: int,
		predicted: Dictionary,
		payload: Dictionary,
		wiring: NetwPredict.Wiring,
		field_sink: Dictionary,
) -> NetwPredictJudgement:
	return NetwPredictionCore.evaluate(
		domain,
		verdict,
		predicted,
		payload,
		{
			&"epsilon": wiring.epsilon,
			&"epsilon_overrides": wiring.epsilon_overrides,
			&"vote_excludes": wiring.vote_excludes,
			&"angle_fields": wiring.angle_fields,
		},
		field_sink,
	)


## Stages the single write that corrects one settled divergence.
##
## The whole recovery is decided here and applied by the shell in one write, so
## a correction has no tail: nothing is left outstanding to ease in over later
## frames, and the state recorded after a recovery is exactly what was staged.
## [param policy] is the [enum NetwPredict.RecoveryPolicy] the entity asked
## for and [param correction] the [enum NetwPredict.CorrectionMode] it resolved
## to. Both are passed because every policy but
## [constant NetwPredict.RecoveryPolicy.OBSERVE] is carried out by a mechanism,
## and OBSERVE is the one that declines to have one.
## [codeblock]
## OBSERVE                 ──> skip (the divergence was reported, and
##                             reporting it was the whole policy)
## REPLAY                  ──> restore all, write nothing (replay
##                             reaches the present under its own power)
## SNAP, a pose field at
##       its own teleport_at ──> restore all, teleport
## SNAP, out of domain     ──> restore all (partiality is an in-domain
##                             refinement)
## SNAP, suppressed        ──> skip
## SNAP, below threshold   ──> restore all but the withheld fields
## [/codeblock]
## [param projection] and [param current] are the acknowledged and present
## states, and [param pose_errors] the per-property distance between them. The
## teleport tier is measured per property against its own threshold, because
## the errors have different units and one number cannot be right for all of
## them. [param wiring] carries the withheld properties, converge rules, and
## thresholds; [param verdict] carries the domain and attribution;
## [param tick_delta] is the seconds per tick used to extrapolate under
## [constant NetwPredict.RestoreMode.EXTRAPOLATED].
##
## Pure. Override to change what a correction
## writes, never to write it: applying the staged result is the shell's job.
func _predict_recover(
		payload: Dictionary,
		policy: int,
		correction: int,
		snap_restore: int,
		projection: Dictionary,
		current: Dictionary,
		pose_errors: Dictionary,
		wiring: NetwPredict.Wiring,
		verdict: NetwPredict.Verdict,
		tick_delta: float,
) -> NetwPredictRecovery:
	return NetwPredictionCore.recover(
		payload,
		policy,
		correction,
		snap_restore,
		projection,
		current,
		pose_errors,
		{
			&"withheld": wiring.withheld,
			&"converge_rules": wiring.converge_rules,
			&"angle_fields": wiring.angle_fields,
			&"teleport_thresholds": wiring.teleport_thresholds,
			&"teleport_threshold": wiring.teleport_threshold,
			&"max_restore_ticks": wiring.max_restore_ticks,
		},
		{
			&"pose_unmeasured": verdict.pose_unmeasured,
			&"suppressed": verdict.suppressed,
			&"ack_age_ticks": verdict.ack_age_ticks,
			&"domain": verdict.domain,
			&"attribution": verdict.attribution,
			&"contact_window": verdict.contact_window,
		},
		tick_delta,
	)

#endregion

## Returns one counter from the unified diagnostics surface.
##
## Invalid [param stat] values return [code]0[/code].
func get_stat(stat: int) -> int:
	if stat < 0 or stat >= _STAT_NAMES.size():
		return 0
	return int(stats_snapshot().get(_STAT_NAMES[stat], 0))


## Returns every [enum Stat] value keyed by its lowercase name.
##
## Cumulative counters and instantaneous occupancy share this tooling-cadence
## snapshot. Use [method get_stat] for one value.
func stats_snapshot() -> Dictionary:
	var result := _relay_stats_snapshot()
	result[&"pending_live"] = _native_core.liveness_pending_live_count()

	var interest: Dictionary = _native_core.interest_monitor_snapshot()
	for counter: StringName in interest:
		result[StringName("interest_%s" % counter)] = interest[counter]

	result.merge(_table_core.counters())

	var predict_stats := lagcomp_metrics()
	result[&"predict_entities"] = predict_stats[&"entities"]
	result[&"predict_timelines"] = predict_stats[&"timelines"]
	result[&"predict_corrections"] = predict_stats[&"corrections"]
	result[&"predict_max_replay_depth"] = predict_stats[&"max_replay_depth"]
	result[&"predict_consumed"] = predict_stats[&"consumed"]
	result[&"predict_missing"] = predict_stats[&"missing"]
	result[&"predict_pending_actions"] = predict_stats[&"predict_pending_actions"] if predict_stats.has(&"predict_pending_actions") else predict_stats[&"pending_actions"]
	result[&"predict_effects_armed"] = predict_stats[&"effects_armed"]
	result[&"predict_gate_fallbacks"] = predict_stats[&"gate_fallbacks"]

	var joint_stats: Dictionary = predict_stats[&"joint"]
	result[&"joint_passes"] = joint_stats[&"joint_passes"]
	result[&"joint_members"] = joint_stats[&"joint_members"]
	result[&"joint_cells_relayed"] = joint_stats[&"cells_relayed"]
	result[&"joint_cells_substituted"] = joint_stats[&"cells_substituted"]
	result[&"joint_heal_snaps"] = joint_stats[&"heal_snaps"]
	result[&"joint_linger_held"] = joint_stats[&"linger_held"]

	var display_stats: NetwPumpStats = _native_core.display_book.stats
	result[&"display_runtimes"] = display_stats.runtimes
	result[&"display_starving"] = display_stats.starving
	result[&"display_sleeping"] = display_stats.sleeping
	result[&"display_projecting"] = display_stats.projecting
	result[&"display_snaps"] = display_stats.snaps
	result[&"display_max_display_lag"] = int(display_stats.max_display_lag)
	result[&"display_max_forecast_age"] = (
			int(display_stats.max_forecast_age)
	)
	result[&"verdict_does_not_exist"] = (
			_native_core.verdict_total(ERR_DOES_NOT_EXIST)
	)
	result[&"verdict_skip"] = _native_core.verdict_total(ERR_SKIP)
	result[&"verdict_unavailable"] = _native_core.verdict_total(ERR_UNAVAILABLE)
	result[&"verdict_unauthorized"] = (
			_native_core.verdict_total(ERR_UNAUTHORIZED)
	)
	result[&"verdict_invalid_data"] = (
			_native_core.verdict_total(ERR_INVALID_DATA)
	)
	result[&"verdict_busy"] = _native_core.verdict_total(ERR_BUSY)

	for name in _STAT_NAMES:
		if not result.has(name):
			result[name] = 0
	return result


# Collects the carrier and replication counters that predate [enum Stat].
func _relay_stats_snapshot() -> Dictionary:
	var repl := _replication.counters()
	var rpc_counters: Dictionary = _rpc_core.counters()
	return {
		&"drops_unknown_route": repl[&"drops_unknown_route"],
		&"drops_not_live": repl[&"drops_not_live"],
		&"drops_no_node": repl[&"drops_no_node"],
		&"drops_backlog_limit": rpc_counters[&"drops_backlog_limit"],
		&"drops_traversal": repl[&"drops_traversal"],
		&"drops_comp_unresolved": repl[&"drops_comp_unresolved"],
		&"sends_dropped_unroutable": (
				repl[&"sends_dropped_unroutable"] + rpc_counters[&"sends_dropped_unroutable"]
		),
		&"sends_dropped_not_live": (
				repl[&"sends_dropped_not_live"] + rpc_counters[&"sends_dropped_not_live"]
		),
		&"sync_drops_stale": repl[&"sync_drops_stale"],
		&"derived_sets_active": repl[&"derived_sets_active"],
		&"derived_frames_in": repl[&"derived_frames_in"],
		&"drops_derived_no_set": repl[&"drops_derived_no_set"],
		&"drops_derived_bad_sender": repl[&"drops_derived_bad_sender"],
		&"drops_derived_schema": repl[&"drops_derived_schema"],
		&"row_frames_out": repl[&"row_frames_out"],
		&"row_frames_full": repl[&"row_frames_full"],
		&"row_frames_stage_refused": repl[&"row_frames_stage_refused"],
		&"row_frames_ungathered": repl[&"row_frames_ungathered"],
		&"retained_frames_out": repl[&"retained_frames_out"],
		&"window_frames_out": repl[&"window_frames_out"],
		&"window_samples_out": repl[&"window_samples_out"],
		&"sync_sets_active": repl[&"sync_sets_active"],
		&"sync_frames_out": repl[&"sync_frames_out"],
		&"sync_frames_in": repl[&"sync_frames_in"],
		&"delta_frames_out": repl[&"delta_frames_out"],
		&"delta_frames_in": repl[&"delta_frames_in"],
		&"drops_sync_no_set": repl[&"drops_sync_no_set"],
		&"drops_sync_bad_sender": repl[&"drops_sync_bad_sender"],
		&"drops_sync_poisoned": repl[&"drops_sync_poisoned"],
		&"drops_sync_unknown_flag": repl[&"drops_sync_unknown_flag"],
		&"spawn_book_armed": repl[&"spawn_book_armed"],
		&"spawn_book_spawned": repl[&"spawn_book_spawned"],
		&"spawn_book_recv": repl[&"spawn_book_recv"],
		&"sent_packets": _native_core.sent_packets,
		&"sent_bytes": _native_core.sent_bytes,
		&"received_packets": _native_core.received_packets,
		&"received_bytes": _native_core.received_bytes,
		&"state_acks_out": _native_core.state_acks_out,
		&"state_acks_in": _native_core.state_acks_in,
		&"standalone_acks_out": _native_core.standalone_acks_out,
	}


# Drops all per-session state so the tick pump has nothing to touch after the
# session tears down. Deferred to avoid mutating registries mid-teardown,
# mirroring NetwMultiplayerCore.
func _on_tree_paused(_reason: String) -> void:
	_set_tree_paused(true)


func _on_tree_unpaused() -> void:
	_set_tree_paused(false)


# The engine-wide pause a session pause applies. Null outside a SceneTree, which
# is a session with nothing to pause rather than a failure.
func _set_tree_paused(value: bool) -> void:
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree:
		scene_tree.paused = value


func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


# Settles the record plane's clear so the emission cascade that ends a session
# has drained before the registry it names is emptied. Driven from the public
# signal rather than the native one, because a rig that ends a session by hand
# announces it there.
func _on_liveness_session_ended() -> void:
	_settle_schedule(_native_core.liveness_clear_session, _LIVENESS_CLEAR_KEY)


func _clear_session_state() -> void:
	_native_core.clear_seq_books()
	_replication.clear_session()
	_rpc_core.clear_session()
	_native_core.clear_verdicts()
	_clear_flat_family_state()


# Clears RID bridges whose records are scoped to one session.
func _clear_flat_family_state() -> void:
	for hooks: Array in _layer_monitor_hooks.values():
		for hook: Array in hooks:
			var source := hook[0] as Signal
			var callback := hook[1] as Callable
			if source.is_connected(callback):
				source.disconnect(callback)
	_layer_monitor_hooks.clear()
	_layer_drivers.clear()
	_native_core.layer_forget_all()
	_pending_scene_facets.clear()
	# Table declarations outlive the session that adopted them, the way field
	# sets do, so a re-entered session finds the same tables under the same
	# handles. Only the rows those declarations describe are per-session.
	_table_core.clear_session()
	_display_declarations.clear()
	_display_target_items.clear()
	_display_callbacks.clear()
	_display_pump_timing = null
	_sync_encoder = Callable()
	_sync_encode_meta.clear()
	_sync_decoder = Callable()
	_set_gatherer = Callable()
	_set_applier = Callable()
	_spawn_reconcile_rows.clear()
	_spawn_reconcile_peers = PackedInt32Array()
	_spawn_reconcile_plan = null
	_spawn_constructor = Callable()


func _clear_disconnected_peer(peer_id: int) -> void:
	# A reconnecting peer restarts its datagram sequence, so neither side may
	# keep the old connection's freshness state against it.
	_native_core.forget_peer_seqs(peer_id)
	_replication.clear_peer(peer_id)
	_rpc_core.handle_disconnect(peer_id)

#endregion

#region Session surface

## Emitted once for each accepted participant known to this peer.
##
## Fresh accepts emit on every peer. Late joiners also receive one emission per
## participant accepted before they connected.
signal participant_joined(participant: NetwParticipant)
## Emitted when this peer's participant has been accepted by the server.
signal local_participant_joined(participant: NetwParticipant)
## Emitted when [member local_player] is assigned or cleared.
signal local_player_changed(player: NetwEntity)
## Emitted when [member local_participant] changes [member NetwParticipant.current_scene].
signal local_scene_changed(from: NetwSceneHandle, to: NetwSceneHandle)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the server when a client requests permission to leave.
signal disconnect_requested(peer_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on every peer when the game is paused via
## [method NetwSessionHandle.pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via
## [method NetwSessionHandle.unpause].
signal tree_unpaused()
## Emitted when the session reaches
## [constant SessionState.ONLINE] with its role resolved. Pairs
## with [signal session_ended].
signal session_entered()
## Emitted when the session leaves
## [constant SessionState.ONLINE]. Pairs with
## [signal session_entered].
signal session_ended()
## Emitted on every [member state] edge, including the ones
## [signal session_entered] and [signal session_ended] do not cover
## (offline to connecting, and a connect that fails before it is online).
signal state_changed(old_state: SessionState, new_state: SessionState)
## Returns the [NetwPeerContext] for [param peer_id], creating one on first
## access.
func peer_get_context(peer_id: int) -> NetwPeerContext:
	return _roster.get_peer_context(peer_id)


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func peer_has_context(peer_id: int) -> bool:
	return _roster.has_peer_context(peer_id)


## Returns the accepted [ResolvedJoin] for [param peer_id], or [code]null[/code].
func peer_get_accepted_join(peer_id: int) -> ResolvedJoin:
	return _roster.get_accepted_join(peer_id)


## Registers [param service] as a session service, keyed by [param type] or its
## own script. Idempotent for the same instance. Fires [signal service_registered].
func register_service(service: Node, type: Script = null) -> void:
	_services.register(service, type)


## Unregisters [param service]. Fires [signal service_unregistered].
func unregister_service(service: Node, type: Script = null) -> void:
	_services.unregister(service, type)

## This session's service registry.
##
## The flat [method register_service] / [method get_service] verbs delegate
## here, so the two spellings never disagree. Reach the registry directly when
## the generic verbs read better with the noun in the receiver.
## [codeblock]
## api.get_service(BomberGamestate)      # flat verb
## api.services.of(BomberGamestate)      # same registry, same instance
## [/codeblock]
var services: NetwServiceRegistry:
	get:
		return _services


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.of(type)


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
func get_services(base: Script) -> Array[Node]:
	return _services.all(base)


## Clears the whole service registry. Called during teardown.
func clear_services() -> void:
	_services.clear()


## Retires [param peer_id] from the roster and drops its participant handle.
func peer_forget(peer_id: int) -> void:
	_roster.forget_peer(peer_id)
	_native_core.participant_forget(peer_id)


## Clears the connected-peer roster and every participant handle. Called during
## session teardown so a same-session re-host starts from an empty roster.
func clear_roster() -> void:
	_roster.clear()
	_native_core.participant_clear()

## Every accepted participant known by this peer.
##
## A participant appears here once its peer has an accepted [ResolvedJoin], so
## a connected peer that has not joined yet is absent. [member peers] is the
## roster row that exists from the moment a peer connects.
var participants: Array[NetwParticipant]:
	get:
		var result: Array[NetwParticipant] = []
		for rj: ResolvedJoin in _roster.get_accepted_joins():
			var accepted := peer_get_participant(rj.peer_id)
			if accepted:
				result.append(accepted)
		return result

## Every player entity across every live scene.
##
## A player lives inside a scene, so the session reads its players by asking
## every scene rather than keeping a second roster that could disagree with
## [member NetwSceneHandle.players]. Scenes partition the players, so a name is
## unique across this whole array and never merely within one scene.
## [codeblock]
## for player in api.players:
##     print(player.entity_id)
## [/codeblock]
var players: Array[NetwEntity]:
	get:
		var result: Array[NetwEntity] = []
		for scene: NetwSceneHandle in scene_instances():
			result.append_array(scene.players)
		return result


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code]. A
## participant exists once its peer has an accepted [ResolvedJoin].
func peer_get_participant(peer_id: int) -> NetwParticipant:
	if peer_get_accepted_join(peer_id) == null:
		return null
	_ensure_participant_row(peer_id)
	return _native_core.participant_of(peer_id) as NetwParticipant

## Every connected peer as a roster row, joined or not.
##
## A row exists from the moment a peer connects. The join frame enriches it with
## a [ResolvedJoin], which is what promotes the row into [member participants]
## and [method peer_get_participant]. This is the whole roster, the un-joined
## observers included.
var connected_participants: Array[NetwParticipant]:
	get:
		var result: Array[NetwParticipant] = []
		for participant: NetwParticipant in _native_core.participant_all():
			result.append(participant)
		return result


# Opens a roster row for a freshly connected peer. The row carries only the peer
# id until a join frame enriches it, so an un-joined peer is still a known row.
func _ensure_participant_row(peer_id: int) -> void:
	_native_core.participant_ensure(peer_id)


# The auth bucket a participant's identity is read from. Buckets are keyed by a
# GDScript type, so the session answers this rather than the core reading it.
func _read_peer_identity(peer_id: int) -> NetwIdentity:
	if not peer_has_context(peer_id):
		return null
	return peer_get_context(peer_id).get_bucket(NetwIdentityBucket).identity

## Whether this session is live.
##
## The session machine owns the fact: transport edges already drive
## [member state] through its own legal edges, so this answers what the
## transport answers, one hop later. A caller that needs the raw window reads
## [member MultiplayerAPI.multiplayer_peer] and says so.
var is_online: bool:
	get:
		return _native_core.is_online

## The current connection state.
var state: SessionState:
	get:
		return _native_core.state as SessionState

## The current role in the session.
var role: Role:
	get:
		return _native_core.role as Role

## Whether the local peer hosts the session, as either a listen or a dedicated
## server. Mirrors [member MultiplayerTree.is_host] but resolves through the
## session, so a root-installed session with no owning [MultiplayerTree] still
## answers.
var is_host: bool:
	get:
		return _native_core.is_host

## Whether the local peer plays a client, including a listen-server host that is
## also its own client. Mirrors [member MultiplayerTree.is_local_client] but
## resolves through the session, so a root-installed session with no owning
## [MultiplayerTree] still answers.
var is_local_client: bool:
	get:
		return _native_core.is_local_client

## The local player identity for this session, or [code]null[/code].
##
## Tracked off the liveness bus: the represented entity is the one whose route
## goes live carrying the local peer id, cleared when that route dies. Riding
## the bus rather than a per-registration write drops the clear-and-reset
## flicker a reparent used to cause, since a reparent keeps the route live and
## never emits [signal NetwMultiplayerCore.entity_dead].
##
## [signal local_player_changed] fires whenever this member changes.
var local_player: NetwEntity:
	get:
		return _native_core.local_player as NetwEntity

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	get:
		return peer_get_participant(get_unique_id())


# Adopts a newly live entity as local_player when it represents the local peer.
# A session-less peer (no multiplayer_peer) has no local player, matching the
# represented-peer test that treats a null peer as not-local.
func _on_liveness_entity_live(route: int, entity: NetwEntity) -> void:
	entity_live.emit(route, entity)
	_native_core.liveness_settle_local_player(route)


func _on_liveness_entity_lingering(route: int, entity: NetwEntity) -> void:
	entity_lingering.emit(route, entity)


func _on_liveness_entity_dead(route: int) -> void:
	entity_dead.emit(route)


## Disconnects [param peer_id] from the session.
##
## If [param reason] is non-empty, the peer receives [signal kicked] before
## the connection is closed.
## [br][br][b]Server Only.[/b]
func peer_kick(peer_id: int, reason: String = "") -> void:
	_native_core.session_kick(peer_id, reason)


## Asks the server to kick [param peer_id].
##
## The server emits [signal kick_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func peer_request_kick(peer_id: int, reason: String = "") -> void:
	_native_core.session_request_kick(peer_id, reason)


## Pauses the game on every peer, which each receives as
## [signal tree_paused] carrying [param reason].
##
## [br][br][b]Server Only.[/b]
func session_pause(reason: String = "") -> void:
	_native_core.session_pause(reason)


## Unpauses the game on every peer, which each receives as
## [signal tree_unpaused].
##
## [br][br][b]Server Only.[/b]
func session_unpause() -> void:
	_native_core.session_unpause()


## Warns every peer that this server is shutting down, which each receives as
## [signal server_disconnecting] carrying [param reason].
##
## The notice rides the session's own control channel rather than a node
## [code]@rpc[/code], so a session with no [MultiplayerTree] still warns its
## clients before it tears down.
##
## [br][br][b]Server Only.[/b]
func session_notify_shutdown(reason: String = "") -> void:
	_native_core.session_notify_shutdown(reason)


## Asks server authority for permission to leave, carrying [param reason].
##
## The server hears it as [signal disconnect_requested] and decides. Nothing
## here disconnects anyone, which is what separates it from
## [method NetwSessionHandle.leave].
##
## [br][br][b]Player request.[/b]
func session_request_leave(reason: String = "") -> void:
	_native_core.session_request_leave(reason)


## The [NetwSessionConfig] this session was registered with, or the default one
## until a [MultiplayerTree] registers its own.
##
## Never [code]null[/code], which is what lets a bare API answer
## [member session_app_id] and
## [member NetwSessionConfig.link_conditions] with defaults instead of holding
## a special inert mode.
var session_config: NetwSessionConfig:
	get:
		return _session_config


## The game-build tag admission gates on, from
## [member NetwSessionConfig.app_id]. Empty disables the gate.
var session_app_id: StringName:
	get:
		return _session_config.app_id


## The player cap the live host advertises, or zero while this session is not
## hosting.
##
## Whatever opened the host stamps the cap it resolved, so
## [method NetwServerInfo.from_session] answers a probe with a plain session
## fact rather than reading back the configuration the host was built from.
var session_advertised_max_players: int:
	get:
		return _native_core.session_advertised_max_players()
	set(value):
		_native_core.session_set_advertised_max_players(value)


## The [NetwAuthFlow] this session authenticates arriving peers with: a
## per-session override, else the flow built by the project-wide
## [method Netw.configure_auth] factory, else [code]null[/code] for open
## admission.
var session_auth_flow: NetwAuthFlow:
	get:
		return _effective_auth_flow()


## Prepares [param payload] as the local player's join, without assigning a
## transport peer.
##
## It validates the join identity, awaits the configured provider's credential
## preparation, and stores what Godot's authentication phase will send. A
## client submits it on reaching [constant SessionState.ONLINE]; a host holds it
## until an explicit [method session_submit_join].
##
## [br][br][b]Player request.[/b]
func session_prepare_join(payload: JoinPayload) -> Error:
	_clear_prepared_join()
	if payload == null:
		Netw.dbg.error("join_payload is null.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER
	if payload.username.is_empty():
		Netw.dbg.error("username is empty.", func(m): push_error(m))
		return ERR_INVALID_PARAMETER

	_auth.prepare()
	var prepare_err := await _auth.prepare_join_payload(payload)
	if prepare_err != OK:
		return prepare_err
	_auth.set_client_join_payload(payload)
	_prepared_join = payload
	return OK


## Submits [param payload] as the local player's join request.
##
## The request rides the session's own join channel rather than a node
## [code]@rpc[/code], so a session with no [MultiplayerTree] still joins. A host
## submits to itself locally. Clients normally submit their prepared payload
## automatically on [constant SessionState.ONLINE], so this is for rejoin and
## custom flows.
##
## [br][br][b]Player request.[/b]
func session_submit_join(payload: JoinPayload) -> void:
	if payload == null:
		return
	if payload == _prepared_join:
		_prepared_join = null
		_auth.set_client_join_payload(null)
	_encode_join_args(payload)
	if is_server():
		_native_core.session_receive_join(payload.serialize(), 1)
	else:
		_native_core.send_to(
			1,
			0,
			NetwFrameEnvelope.Channel.SESSION_JOIN,
			payload.serialize(),
			true,
			0,
			"",
			false,
		)


## Flushes persistence, closes the active peer, and returns to
## [constant SessionState.OFFLINE].
##
## It waits up to three seconds for the server to acknowledge the departure, so
## a caller that awaits it knows the peer is closed rather than closing.
func session_leave() -> void:
	if state == SessionState.OFFLINE:
		return

	Netw.dbg.trace("Session: leave called.")
	Netw.dbg.info("Disconnecting player.")
	_native_core.persistence_flush_all()
	_session_transition(SessionState.DISCONNECTING)
	if has_multiplayer_peer():
		multiplayer_peer.close()

	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree:
		var timer := scene_tree.create_timer(3.0)
		await Async.timeout(server_disconnected, timer)
	_session_transition(SessionState.OFFLINE)
	session_advertised_max_players = 0


## Overrides the join handler for this one session with [param handler] and its
## wire-arg [param quantizers], taking precedence over any
## [method Netw.configure_join] registration. Tests and the debugger use it. An
## invalid [Callable] restores the resolved default.
func session_set_join_handler(handler: Callable, quantizers: Array = []) -> void:
	_join_override = handler
	_join_override_quantizers = quantizers


## Overrides the auth flow for this one session with [param flow], taking
## precedence over any [method Netw.configure_auth] factory. Tests, the
## debugger, and a runtime-bound service flow use it.
func session_set_auth_flow(flow: NetwAuthFlow) -> void:
	_auth_flow_override = flow
	_auth.set_auth_flow(_effective_auth_flow())


## Overrides the probe reply for this one session with [param provider], taking
## precedence over any [method Netw.configure_server_info] registration. Tests,
## the debugger, and a session that genuinely differs use it. An invalid
## [Callable] clears the override.
func session_set_server_info_provider(provider: Callable) -> void:
	_auth.set_server_info_provider(provider)

#endregion

#region Session machine

# Binds the session's wire hooks and the four Callables the native core
# installs. The two edge hooks are not announcements: the core calls them after
# it has relayed the edge, so an entered session submits a client's prepared
# join and an offline one drops it, both strictly after every consumer of the
# public signal has already seen the edge.
#
# A client peer is still mid-handshake at assignment, so the connect completes
# on the relayed connection signals rather than at the edge. A failed handshake
# returns to OFFLINE without ever entering ONLINE, and a server that vanishes
# mid-session ends it through the same teardown a graceful leave takes.
func _session_install() -> void:
	_auth = AuthCoordinator.new(_roster)
	_auth.bind_api(inner)
	_auth.set_owner(self)
	_native_core.set_session_entered_hook(_on_session_entered)
	_native_core.set_session_edge_hook(_on_session_state_changed)
	_connect_once(connected_to_server, _on_session_connected)
	_connect_once(connection_failed, _on_session_connect_failed)
	_connect_once(server_disconnected, _on_session_server_dropped)
	_native_core.set_session_join_handler(_run_join_handler)
	_native_core.set_session_join_resolver(_resolve_inbound_join)


# Applies config as the session configuration, registered by a MultiplayerTree
# through object_configuration_add.
func _session_configure(config: NetwSessionConfig) -> void:
	_session_config = config
	_push_desired_role()
	_apply_auth_config()


# Restores the default configuration when the registering tree removes its own.
func _session_deconfigure() -> void:
	_session_config = NetwSessionConfig.new()
	_push_desired_role()
	_apply_auth_config()


# The hint the machine splits a server peer with. Pushed rather than read,
# because the machine holds no configuration, and pushed again at every edge
# that can resolve a role, because a config is authored live and the value it
# carried at registration is not the one that decides.
func _push_desired_role() -> void:
	_native_core.session_set_desired_role(_authored_desired_role())


# Applies the registered session facts to the auth wire engine. The dispatcher
# stays armed because every Networked session uses the base hello protocol and
# same-port probes share its isolated authentication phase.
func _apply_auth_config() -> void:
	_auth.set_auth_flow(_effective_auth_flow())
	_auth.set_app_tag(NetwMultiplayerCore.session_app_tag(_session_config.app_id))
	_auth.prepare()


# Resolves the effective flow: a per-session override, then a cached instance
# from the project-wide factory, else null.
func _effective_auth_flow() -> NetwAuthFlow:
	if _auth_flow_override != null:
		return _auth_flow_override
	if _bound_auth_flow == null:
		var factory := Netw.resolve_auth_factory()
		if factory.is_valid():
			_bound_auth_flow = factory.call(self)
	return _bound_auth_flow


# Rebinds session wire hooks after inner changes.
func _session_adopt_inner(new_inner: SceneMultiplayer) -> void:
	_auth.bind_api(new_inner)
	_apply_auth_config()


# Releases session wire hooks during NetwEmbeddingHandle.dispose.
func _session_dispose() -> void:
	_clear_prepared_join()
	_auth.clear()


# Advances state to next along a legal edge, running the exit hook for the old
# state then the enter hook for the new one. The only entry point allowed to
# move state, so setup and teardown stay paired.
func _session_transition(next: SessionState) -> void:
	_native_core.session_transition(next)


# Reacts to a peer handed to the session by _set_multiplayer_peer, which is the
# one edge every host, join, test rig and embedded-server path already crosses.
#
# A live transport peer drives the connect direction straight from the edge, so
# a bare multiplayer_peer = peer with no host or join verb still reaches ONLINE.
# A server peer is live at assignment; a client peer is still mid-handshake, so
# it waits in CONNECTING until the transport reports the connection. A null or
# OfflineMultiplayerPeer assignment while connecting collapses the machine back
# to OFFLINE, so a cancelled connect becomes this edge rather than a bespoke
# abort verb. A null assignment while already ONLINE is left alone, since a tree
# deletion nulls the peer that way; a graceful leave and a server crash own the
# ONLINE teardown instead.
func _session_on_peer_assigned(peer: MultiplayerPeer) -> void:
	var live := peer != null and not peer is OfflineMultiplayerPeer
	_push_desired_role()
	_native_core.session_peer_assigned(
		live,
		live and peer.get_connection_status() \
			== MultiplayerPeer.CONNECTION_CONNECTED,
		peer.get_unique_id() if live else 0,
	)


# Completes a client connect once the transport reports it reached the server.
func _on_session_connected() -> void:
	if state != SessionState.CONNECTING:
		return
	_push_desired_role()
	_native_core.session_resolve_online(get_unique_id())


# A failed handshake returns to OFFLINE without ever entering ONLINE.
func _on_session_connect_failed() -> void:
	if state == SessionState.CONNECTING:
		_session_transition(SessionState.OFFLINE)


# Ends the session when the transport reports the server vanished. A crash
# arrives while ONLINE and reuses the leave path's DISCONNECTING -> OFFLINE
# teardown. A leave has already moved to DISCONNECTING, and a disposing api that
# closes its own peer sets the embedding disposing, so neither is mistaken for a
# crash.
func _on_session_server_dropped() -> void:
	if embedding.is_disposing():
		return
	if state != SessionState.ONLINE:
		return
	_session_transition(SessionState.DISCONNECTING)
	_session_transition(SessionState.OFFLINE)


# The setup half the machine cannot own, which is the half that needs the wire.
# A client submits the join it prepared. A server has no handshake coming to
# bring it an identity, so it makes its own.
func _on_session_entered() -> void:
	if role == Role.CLIENT:
		_submit_prepared_join()
	else:
		_auth.synthesize_host_identity()


# The teardown half that owns a payload rather than a state: a session back at
# OFFLINE is no longer holding a join to send.
func _on_session_state_changed(_old_state: int, new_state: int) -> void:
	if new_state == SessionState.OFFLINE:
		_clear_prepared_join()


# Consumes the prepared client join before sending it. Clearing first makes the
# ONLINE edge idempotent even when the transport repeats its connected signal.
# A relayed transport can raise connected_to_server a poll before the server
# peer lands in get_peers, and the carrier drops a send to a peer it cannot yet
# see, so that first submit is resent once peer_connected reports the server. A
# transport whose server peer is already present arms nothing, since its
# peer_connected preceded the online edge, and the join lands on the first
# submit.
func _submit_prepared_join() -> void:
	var payload := _prepared_join
	if payload == null:
		return
	_prepared_join = null
	_auth.set_client_join_payload(null)
	session_submit_join(payload)
	if MultiplayerPeer.TARGET_PEER_SERVER not in get_peers():
		_resubmit_join = payload
		if not peer_connected.is_connected(_resubmit_join_on_server_peer):
			peer_connected.connect(_resubmit_join_on_server_peer)


# Resends the client join once the server peer connects, covering the relayed
# transport whose connected_to_server outran its server peer registration. The
# first submit dropped at the carrier, so this is the only delivery, not a
# double.
func _resubmit_join_on_server_peer(peer_id: int) -> void:
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		return
	if peer_connected.is_connected(_resubmit_join_on_server_peer):
		peer_connected.disconnect(_resubmit_join_on_server_peer)
	var payload := _resubmit_join
	_resubmit_join = null
	if payload:
		session_submit_join(payload)


# Drops the pending join frame, the credentials derived from it, and a resubmit
# still waiting on the server peer.
func _clear_prepared_join() -> void:
	_prepared_join = null
	if _auth:
		_auth.set_client_join_payload(null)
	_resubmit_join = null
	if peer_connected.is_connected(_resubmit_join_on_server_peer):
		peer_connected.disconnect(_resubmit_join_on_server_peer)


# The admission policy a session runs when nothing registered its own gate:
# resolve the identity, reject an invalid payload, and reject the loser of a
# username collision. This is the permissive baseline, so an unauthenticated
# session still cannot admit two players under one name.
func _default_join_gate(join_payload: JoinPayload, peer_id: int) -> ResolvedJoin:
	var rj := join_payload.resolve()
	if rj == null:
		Netw.dbg.warn("join: invalid payload from peer %d", [peer_id])
		return null
	if not _roster.resolve_username_collision(
		rj,
		players,
		inner.disconnect_peer,
	):
		return null
	return rj


# Resolves an inbound join into the ResolvedJoin the session admits, or null to
# reject it. The identity, the arg codec and the gate are all script, so the
# native receive installs this rather than implementing it.
func _resolve_inbound_join(
		join_payload: JoinPayload,
		sender: int,
) -> ResolvedJoin:
	if not _decode_join_args(join_payload):
		Netw.dbg.warn(
			"join: rejected malformed or mismatched args from peer %d",
			[sender],
		)
		return null
	_auth.resolve_identity(sender, join_payload)
	return _default_join_gate(join_payload, sender)


# Resolves this session's join handler: a per-session override, then the
# project-wide registration, then the built-in NetwDefaultJoin bound to this
# api.
func _resolve_join_handler() -> Callable:
	if _join_override.is_valid():
		return _join_override
	var registered := Netw.resolve_join_handler()
	if registered.is_valid():
		return registered
	if _default_join == null:
		_default_join = NetwDefaultJoin.new(self)
	return _default_join.spawn


# The wire-arg quantizers matching the resolved handler.
func _resolve_join_quantizers() -> Array:
	if _join_override.is_valid():
		return _join_override_quantizers
	if Netw.resolve_join_handler().is_valid():
		return Netw.resolve_join_quantizers()
	return []


# Packs a client's typed arg_values into arg_bytes plus a schema hash. No intent
# or no resolvable handler leaves empty bytes.
func _encode_join_args(payload: JoinPayload) -> void:
	NetwJoinCodec.encode(
		payload,
		_resolve_join_handler(),
		_resolve_join_quantizers(),
	)


# Verifies and decodes arg_bytes into arg_values against this server's resolved
# handler. Returns false to reject a schema mismatch or an undecodable request.
func _decode_join_args(payload: JoinPayload) -> bool:
	return NetwJoinCodec.decode(
		payload,
		_resolve_join_handler(),
		_resolve_join_quantizers(),
	)


# Invokes the resolved join handler once for the peer admitted as peer_id,
# passing the decoded join args after the ResolvedJoin. A returned
# NetwSceneHandle becomes the participant's current_scene, and a participant
# with no join intent spawns nothing. Installed on the native core, which calls
# it on server authority between the admission and its announcement.
func _run_join_handler(peer_id: int) -> void:
	if not is_server():
		return
	var participant := peer_get_participant(peer_id)
	if participant == null:
		return
	var rj := participant.join
	if rj == null or rj.arg_values.is_empty():
		return
	var handler := _resolve_join_handler()
	if not handler.is_valid():
		return
	var scene = await handler.callv([rj] + rj.arg_values)
	var record := NetwEntity.of(scene) if scene is Node else null
	if record != null:
		participant.current_scene = record.scene

#endregion

# Schedules [param fn] to run at the next settle, which is inside the pump this
# session already runs. A named [param key] coalesces: scheduling a key that is
# already queued moves it to the back rather than queueing it twice, which is
# what "run after the cascade that scheduled me" means and what a per-site
# scheduled flag used to spell for itself. An empty key never coalesces, so
# unkeyed effects run in enqueue order.
func _settle_schedule(fn: Callable, key: StringName = &"") -> void:
	_native_core.settle_schedule(fn, key)


# Runs fn once the session has pumped through a window of `seconds`, measured in
# the cadence this session actually pumps at. A teardown that has to stay
# reachable for a while is a count of pumps rather than a tree timer, so a
# session with no tree keeps the same window a rendered one does.
func _settle_after_seconds(
		fn: Callable,
		seconds: float,
		key: StringName = &"",
) -> void:
	_native_core.settle_schedule_after(fn, key, _linger_pumps(seconds))


# Withdraws a queued key, for a caller that did the work on the spot and has
# nothing left to settle. Unqueued keys are not an error.
func _settle_cancel(key: StringName) -> void:
	_native_core.settle_cancel(key)


# Drains the settle queue to a fixed point. Each pass takes the whole queue and
# runs it, so an effect scheduled during a pass runs in the next one, inside the
# same drain. Exceeding the pass bound names the keys still pending and clears
# them: a cycle is a defect, and a defect that hangs is worse than one that
# reports.
func _settle() -> void:
	var pending := _native_core.settle_drain()
	if not pending.is_empty():
		push_error(
			"Settle did not reach a fixed point in %d passes, still pending: %s"
			% [NetwMultiplayerCore.settle_max_passes(), ", ".join(pending)],
		)


func _poll() -> Error:
	# This runs once per idle frame, so the wall-clock gap since the last one is
	# that frame's delta. It is read up front because the peer view is pumped
	# with it, and that pump has to land before the transport reads.
	# Marking the poll is what announces it, and the order is load-bearing: a
	# view-pumped carrier delivers its queued packets on that signal, so reading
	# the transport first would see every one of them a frame late.
	var frame_delta := _native_core.poll_delta(Time.get_ticks_usec())
	var err := _embedding.poll_transport()
	# After intake and before the outbound flush, and both halves are contract.
	# An inbound frame that schedules an effect settles in the pump that read it,
	# and anything the settle queues for the wire leaves with this pump rather
	# than a frame later.
	# A clocked session counts its drain windows on the tick pump instead, which
	# is the cadence it sends at, so counting here as well would spend one window
	# twice over.
	if not _native_core.clock_handle.is_configured:
		_native_core.settle_advance()
	_settle()
	if not _native_core.clock_handle.is_configured:
		_native_core.scene_pump_retired()
	_native_core.clock_handle.count_poll()
	_native_core.clock_handle.poll_step()
	_native_core.advance_frame()
	_replication.on_poll()
	# Persistence accumulates in wall-clock seconds, so it counts on the poll
	# and never on the tick: a session that polls without rendering still saves.
	persist_tick(frame_delta)
	_sink_verdict(_native_core.display_pump(frame_delta), 0)
	return err


# Intercepts native @rpc dispatch. A call on a node inside a live NetwEntity is
# upgraded to a route-addressed RpcCore call, so it inherits liveness
# gating and interest-scoped fan-out and never races the target's spawn edge the
# way a NodePath-addressed native RPC does. Every other call, including
# session-lifecycle RPCs on nodes outside any entity, rides inner unchanged.
func _rpc(peer: int, object: Object, method: StringName, args: Array) -> Error:
	if object is Node:
		var entity := NetwEntity.of(object)
		if entity and _native_core.liveness_route_of(entity) > 0:
			_rpc_core.rpc_call(Callable(object, method), args, peer)
			return OK
	return inner.rpc(peer, object, method, args)


# The registration verb for every declaration node. Typed Networked configs
# install into this session, while engine configurations forward to inner.
func _object_configuration_add(object: Object, configuration: Variant) -> Error:
	if configuration is NetwObjectConfig:
		return _install_book.install(configuration as NetwObjectConfig, object)
	# A spawner registration is consumed, not forwarded, so the native
	# replicator never tracks the node and a double spawn is unrepresentable.
	# The node replicates through the Networked pipeline via NetwSpawnerCompat.
	if configuration is MultiplayerSpawner:
		return _replication._spawner_compat.consume(
			object as Node,
			configuration as MultiplayerSpawner,
		)
	# A synchronizer registration is consumed, not forwarded, so the native
	# replicator never tracks the node and its sync loop iterates empty sets.
	# The node synchronizes through the Networked pump via NetwSyncCompat.
	if configuration is MultiplayerSynchronizer and object is Node:
		return _replication._sync_compat.consume(
			object as Node,
			configuration as MultiplayerSynchronizer,
		)
	return inner.object_configuration_add(object, configuration)


func _object_configuration_remove(object: Object, configuration: Variant) -> Error:
	if configuration is NetwObjectConfig:
		return _install_book.uninstall(
			configuration as NetwObjectConfig,
			object,
		)
	if configuration is MultiplayerSpawner:
		return _replication._spawner_compat.consume_remove(
			object as Node,
			configuration as MultiplayerSpawner,
		)
	if configuration is MultiplayerSynchronizer and object is Node:
		return _replication._sync_compat.consume_remove(
			object as Node,
			configuration as MultiplayerSynchronizer,
		)
	return inner.object_configuration_remove(object, configuration)


func _set_multiplayer_peer(p_peer: MultiplayerPeer) -> void:
	inner.multiplayer_peer = p_peer
	_native_core.set_multiplayer_peer(p_peer)
	# The session machine reacts to the one edge every connect path crosses.
	_session_on_peer_assigned(p_peer)


func _get_multiplayer_peer() -> MultiplayerPeer:
	return _native_core.get_multiplayer_peer()


func _get_unique_id() -> int:
	return _native_core.get_unique_id()


func _get_peer_ids() -> PackedInt32Array:
	_native_core.set_peer_ids(inner.get_peers())
	return _native_core.get_peers()


func _get_remote_sender_id() -> int:
	# A carrier frame under dispatch answers with its own sender, so a handler
	# reached through the carrier reads the same value a native @rpc handler
	# would. Peer ids are always positive, so 0 means no relayed dispatch.
	if _relay_sender != 0:
		return _relay_sender
	return inner.get_remote_sender_id()



const DebugFeature := preload("res://addons/networked/debug/ui/debug_feature.gd")

## One drive pass's timing.
##
## [i]Deprecated.[/i] The record is [NetwPredict.Timing] now. It moved to the
## vocabulary leaf so the kernel can name the type without depending on the
## shell that steps it.
const PredictTiming := NetwPredict.Timing

# The columns of one pass's outcome, as native_open_drive and
# native_replay_drive both report it.
const DRIVE_RECORD_TRANSITION := 0
const DRIVE_RECORD_LABEL := 1
const DRIVE_RECORD_KIND := 2
const DRIVE_RECORD_FRESH := 3
const DRIVE_RECORD_RAN := 4
const DRIVE_RECORD_WIDTH := 5

const _OBSERVE_KEY_PREFIX := "lagcomp-observe-node?"

# Tri-state result of the action readiness check, shared by admission and drain.
enum _Readiness { NOT_READY, READY, READY_BY_DEADLINE }

## Maximum number of ticks a player action may be scheduled ahead of the
## server clock before it is denied.
var max_future_action_ticks: int = 8

## Ticks a [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] action waits
## for input-backed state at its view tick before it resolves best-effort.
##
## A state-ready action queues from the tick the server admits it. It resolves the
## moment the server has consumed and recorded authoritative state for the view
## tick, the input-backed slot [method sample] reads. Lost or late input can mean
## that slot never arrives, so this bound stops the wait from lasting forever. Once
## the wait reaches it the action resolves against the best available history and
## [signal action_gate_fallback] fires.
## [codeblock]
## queued_at                              state recorded at view_tick -> resolve (input-backed)
## queued_at + input_gate_deadline_ticks  still no state at view_tick -> resolve best-effort
## [/codeblock]
## Set it above the input arrival lag in ticks, about
## [member NetwClockHandle.recommended_display_offset] for the network and jitter
## part plus the input send cadence. A value below that resolves state-ready actions
## best-effort under normal latency, the artifact the gate exists to prevent.
var input_gate_deadline_ticks: int = 12

## Emitted when a state-ready action resolves through its deadline fallback.
signal action_gate_fallback(key: StringName, view_tick: int)

## Emitted on authority when an owner's claimed post-state for [param entry]
## disagrees with the one authority reached, charged to [param attribution].
##
## Authority holds a timeline per peer and checks it, so a peer whose simulation
## has drifted is discovered where the divergence can be acted on rather than
## only where it is felt. This is a report and never a repair: nothing is pushed
## back to [param peer] on the strength of it, so a game decides for itself
## whether a run of these is latency, a bug, or a client worth distrusting.
## [codeblock]
## api.peer_divergence.connect(
##     func(peer: int, entry: int, attribution: NetwPredictJournal.Attribution):
##         if attribution == NetwPredictJournal.Attribution.CLOSURE:
##             suspicion[peer] = suspicion.get(peer, 0) + 1
## )
## [/codeblock]
## [br][br][b]Server Only.[/b]
signal peer_divergence(
		peer: int,
		entry: int,
		attribution: NetwPredictJournal.Attribution,
)

# Flipped by NetwMultiplayer when a LagCompensation configurator registers.
var _configured := false
# Physics frames this session has run. A transition's cost in simulated time is
# the difference between the frames two consecutive drives ran on, and that cost
# has to match on every peer for a compared transition to mean anything.
var _physics_frame: int = 0
# The clock configuration a quantum report has already judged, so the report
# fires once per distinct configuration rather than once per drive.
var _quantum_config_reported: int = -1
# Entities whose archetype says the physics server integrates their body, each
# with the space it last resolved into so a release can restore what it held.
#   { NetwEntity -> { space: RID, dimension: int } }
var _gated_entities: Dictionary = { }
# Relay subscribers per entity slot, server-side.
var _relay_book := NetwPredictRelayBook.new()


## Admits [param peer] to [param entity]'s relayed command lane, or drops it
## when [param subscribed] is false.
##
## The interest gate is re-answered on every relay rather than remembered here,
## so a peer that leaves the entity's interest set stops receiving its commands
## without anything having to observe the exit. This returns the verdict for the
## request itself, which is what a subscriber learns.
##
## [br][br][b]Server Only.[/b]
func relay_subscribe(
		entity: NetwEntity,
		peer: int,
		subscribed: bool = true,
) -> Error:
	var api := self
	if api == null or not api.is_server():
		return ERR_UNAUTHORIZED
	if entity == null or not is_instance_valid(entity):
		return ERR_DOES_NOT_EXIST
	if not api._native_core.liveness_core.entity_is_valid(entity.rid):
		return ERR_DOES_NOT_EXIST
	var slot := entity.rid.get_id()
	if not subscribed:
		_relay_book.set_subscribed(slot, peer, false)
		return OK
	if not api.interest_admits(entity.rid, peer):
		return ERR_UNAUTHORIZED
	_relay_book.set_subscribed(slot, peer, true)
	return OK


# Re-emits one admitted command frame to every subscriber interest still
# admits, byte for byte. A subscriber that decoded a re-cut frame would be
# reading a command its author never wrote, so the payload is passed through
# rather than re-encoded, and the author is skipped because it already has it.
func _relay_command_frame(
		entity: NetwEntity,
		payload: PackedByteArray,
		author: int,
) -> void:
	var api := self
	if api == null or not api.is_server():
		return
	var slot := entity.rid.get_id()
	var subscribers := _relay_book.peers(slot)
	if subscribers.is_empty():
		return
	var native_core := api._native_core if api else null
	var route := native_core.liveness_route_of(entity) if native_core else -1
	if route <= 0:
		return
	for peer in subscribers:
		if peer == author:
			continue
		if not api.interest_admits(entity.rid, peer):
			_relay_book.set_subscribed(slot, peer, false)
			continue
		api._replication.send_to(
			peer,
			route,
			NetwFrameEnvelope.Channel.PREDICT_RELAY,
			payload,
			false,
		)

var _registry: NetwLagCompCore
var _recorder := _HistoryRecorder.new()
var _runner := _SimulationRunner.new()
# Per-entity prediction engine records, created by register_prediction, keyed by
# NetwEntity. The handle on NetwEntity.prediction reaches its record back through
# a weakref, so erasing an entry here is the whole release.
var _engines: Dictionary = { }
var _prediction_pool: NetwPredictionEngine
# One acknowledgement run judges a whole window of transitions, and the pool
# copies the row it is handed, so the carrier is refilled rather than reminted.


# The two questions an engine asks about its siblings, named so they are a
# contract rather than a reach into this file's storage.
#
# An island rollback re-runs every declared member together, and a contact
# classifier asks whether the body it touched is one this peer predicts. Both
# are questions about the registry, which the shell owns; neither is a question
# an engine can answer from its own state. Spelled here, an engine never depends
# on how the registry is stored, and this file stays free to change that.
func engine_for(entity: NetwEntity) -> NetwPredictEngine._PredictionEngine:
	return _engines.get(entity) as NetwPredictEngine._PredictionEngine


func native_prediction_slot(entity: NetwEntity) -> int:
	return _prediction_pool.slot_of(entity)


# True when [param entity] has a prediction engine on this peer, which is what
# makes a contacted body predicted rather than merely replicated.
func predicts(entity: NetwEntity) -> bool:
	return _engines.has(entity)


const _PredictionBoundaryOverlay := preload(
	"res://addons/networked/debug/prediction_boundary_overlay.gd"
)
var _prediction_overlays: Dictionary[NetwEntity, Node] = { }

# Env-gated JSONL drain of the public prediction surface, built lazily the
# first frame it is armed and never re-checked once found off. The every-N
# gate is safe while N stays under the journal ring depth, because the tap's
# export cursor guarantees no sealed row is ever skipped.
const _PredictTap := preload("res://addons/networked/replication/netw_predict_tap.gd")
const _TAP_EVERY_ENV := "NETW_PREDICT_TAP_EVERY"
var _tap = null
var _tap_off: bool = false
var _tap_every: int = 1
var _tap_frame: int = 0
var _pending_actions: Array[_PendingAction] = []
var _action_slots: Dictionary[String, int] = { }
var _observed_entities: Dictionary[NetwEntity, bool] = { }
var _gate_fallbacks: int = 0



## Applies [param config] to the engine. Called by [NetwMultiplayer] when a
## [NetwMultiplayer] installs a [NetwLagCompensationConfig]. The values live
## here, not on the node, so [method is_configured] stays true after a scene
## change frees the configurator.
@warning_ignore("unused_parameter")
func configure(node: LagCompensation, config: NetwLagCompensationConfig) -> void:
	max_future_action_ticks = config.max_future_action_ticks
	input_gate_deadline_ticks = config.input_gate_deadline_ticks


## True once a [LagCompensation] configurator has registered. Until then every
## query returns its safe empty result and every action is denied.
func is_configured() -> bool:
	return _configured


## Resolves the lag-compensation interface for [param node], logging an error
## when [param node] sits under a [MultiplayerTree] that has no
## [LagCompensation] node mounted.
##
## A node run standalone (no enclosing [MultiplayerTree], for example pressing
## [code]F6[/code] on a scene in isolation) resolves to [code]null[/code] quietly,
## so detached testing keeps working. A node mounted in a real session that depends
## on rewind or prediction but finds no [LagCompensation] is a misconfiguration,
## so [method NetwDbg.error] names it rather than crashing.
static func resolve_required(node: Node) -> NetwMultiplayer:
	# Resolve the session api directly, falling back to the enclosing tree when
	# a node's own multiplayer is not yet bound to the api at call time.
	var api := NetwMultiplayer.of(node)
	if api == null:
		var mt := MultiplayerTree.resolve(node)
		api = mt.api if mt else null
	if not api:
		return null
	if api.is_configured():
		return api
	Netw.dbg.error(
		"%s needs a LagCompensation node mounted under the MultiplayerTree, "
		+ "but none was found. Add one as a child of the tree to enable "
		+ "prediction and rewind.",
		[node.get_class() if node else "A node"],
		func(m: String) -> void: push_error(m),
	)
	return null


## Creates and wires the prediction engine record for [param entity]. Idempotent.
##
## [PredictionComponent] calls this on tree entry after pushing its exports into
## [member NetwEntity.prediction], and a code-first caller configures that handle
## and calls this directly. The engine resolves its role from authority, follows
## [signal NetwEntity.control_changed] and [signal NetwEntity.reparented], and
## steps in the deterministic simulation loop. Only the roles this peer simulates
## enter the loop, so a remote display costs nothing per tick.
func register_prediction(entity: NetwEntity) -> void:
	if not entity or _engines.has(entity):
		return
	var engine := NetwPredictEngine._PredictionEngine.new()
	_engines[entity] = engine
	_prediction_pool.slot_register(entity)
	entity.prediction._engine_ref = weakref(engine)
	engine._attach(self, entity)
	_attach_prediction_overlay(entity)


# The declaration model an engine wires on, resolved for [param entity] here
# because both set handles resolve through the replication registry and the
# liveness route, and both axes read this peer's authority. This is the one
# place a prediction engine's wiring depends on a live session.
func declaration_of(entity: NetwEntity) -> NetwPredictEngine.Declaration:
	var declaration := NetwPredictEngine.Declaration.new()
	if not entity:
		return declaration
	declaration.state = entity.state_binding
	declaration.input = entity.input_binding
	declaration.authority = entity.is_authority
	declaration.controlled_locally = entity.is_controlled_locally
	return declaration


## Releases [param entity]'s prediction engine record, restoring the set-handle
## hooks it held. [member NetwEntity.prediction] stays bound and keeps its config
## and counters, so a re-registration resumes where the engine left off.
func unregister_prediction(entity: NetwEntity) -> void:
	var engine := _engines.get(entity) as NetwPredictEngine._PredictionEngine
	if not engine:
		return
	_engines.erase(entity)
	_prediction_pool.slot_unregister(entity)
	_release_slot(entity)
	_detach_prediction_overlay(entity)
	engine._release()
	entity.prediction._engine_ref = null


func configure_native_prediction(
		entity: NetwEntity,
		binding: NetwPropertySetBinding,
		input_binding: NetwPropertySetBinding,
		schedule: int,
		role: int,
		declared_correction: int,
		restore: int,
		max_restore_ticks: int,
		island: int,
		carry: bool,
		witness: bool,
) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0 or not binding or not binding.set:
		return -1
	var node := binding.node()
	var declaration := NetwPredictDeclaration.new()
	for field: NetwPropertySet.Column in binding.set.columns:
		declaration.append_field(
			field.key,
			field.property_class,
			binding.carry_channel_of(field.key),
			binding.converge_stiffness_of(field.key),
			binding.teleport_only_of(field.key),
			binding.reconcile_only_of(field.key),
			binding.epsilon_override_of(field.key),
			binding.teleport_at_of(field.key),
			false,
			field.quantizer,
			_declared_property_type(node, field.key),
		)
	_prediction_pool.rewire(slot, declaration, _input_declaration(input_binding))
	_bind_declared_owner(entity, node)
	var correction: int = _prediction_pool.resolve_correction(
		slot,
		declared_correction,
	)
	if not _prediction_pool.configure(
		slot,
		schedule,
		role,
		correction,
		restore,
		max_restore_ticks,
		island,
		carry,
		witness,
	):
		return -1
	return correction


# The plane's owner is the object that holds the declared properties. The
# simulate step is adopted from it, falling back to the entity root, because a
# component may carry the property set while the root carries the body.
func _bind_declared_owner(entity: NetwEntity, node: Node) -> void:
	if not is_instance_valid(node):
		return
	var api := self
	if api == null:
		return
	api.predict_bind_owner(entity.rid, node)
	var handle := entity.prediction
	if handle == null:
		return
	if not handle.simulate.is_valid():
		var root := entity.owner
		if is_instance_valid(root) and root.has_method(&"_network_tick"):
			handle.simulate = Callable(root, &"_network_tick")
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return
	var route := self._native_core.liveness_route_of(entity)
	_prediction_pool.set_order_key(slot, route if route > 0 else -1)
	_prediction_pool.set_simulate(slot, handle.simulate)
	_prediction_pool.set_witness(slot, handle.witness_contacts)
	_prediction_pool.set_corridor(slot, handle.transport_corridor)
	for name: StringName in handle.sensors:
		_prediction_pool.set_sensor(slot, name, handle.sensors[name])


# An input row is canonicalized and shipped, never recovered, so the pool needs
# its codec columns and none of the recovery declarations.
func _input_declaration(
		binding: NetwPropertySetBinding,
) -> NetwPredictDeclaration:
	if not binding or not binding.set:
		return null
	var node := binding.node()
	var declaration := NetwPredictDeclaration.new()
	for field: NetwPropertySet.Column in binding.set.columns:
		declaration.append_field(
			field.key,
			field.property_class,
			&"",
			0.0,
			false,
			false,
			-1.0,
			-1.0,
			false,
			field.quantizer,
			_declared_property_type(node, field.key),
		)
	return declaration


func _declared_property_type(node: Node, key: StringName) -> int:
	if not is_instance_valid(node):
		return TYPE_NIL
	return NetwScriptModel.get_node_property_type(node, key)


func configure_native_prediction_axes(
		entity: NetwEntity,
		schedule: int,
		role: int,
		correction: int,
		restore: int,
		max_restore_ticks: int,
		island: int,
		carry: bool,
		witness: bool,
		island_declared: bool,
		island_approximate: bool,
) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.configure(
		slot,
		schedule,
		role,
		correction,
		restore,
		max_restore_ticks,
		island,
		carry,
		witness,
		island_declared,
		island_approximate,
	) if slot >= 0 else false


# One acknowledgement stages one recovery, so the carrier is refilled rather
# than reminted.


func _release_slot(entity: NetwEntity) -> void:
	# The subscribers go with the handle, because a handle is never reissued and
	# a row left behind would answer for an entity nothing predicts.
	if entity:
		_relay_book.release(entity.rid.get_id())


# Adds the read-only in-world overlay when the shared debug gate is active.
func _attach_prediction_overlay(entity: NetwEntity) -> void:
	if not DebugFeature.is_world_debug_enabled():
		return
	if not bool(
		ProjectSettings.get_setting(
			"debug/networked/prediction_boundary_overlay",
			true,
		),
	):
		return
	if not is_instance_valid(entity.owner):
		return
	var overlay := _PredictionBoundaryOverlay.new()
	overlay.name = "PredictionBoundaryOverlay"
	overlay.bind(entity)
	_prediction_overlays[entity] = overlay
	entity.owner.add_child.call_deferred(overlay)


# Releases the entity overlay without changing any prediction state.
func _detach_prediction_overlay(entity: NetwEntity) -> void:
	var overlay := _prediction_overlays.get(entity) as Node
	_prediction_overlays.erase(entity)
	if is_instance_valid(overlay):
		overlay.queue_free()


## Registers [param entity] for server-side authoritative recording, returning its
## [NetwTimeline]. Idempotent: a repeat call returns the existing timeline.
##
## [method NetwSyncPipeline.register_derived] calls this when a server-authored
## state set registers, and the server [PredictionComponent] roles read the same
## timeline back, so the trigger is state-set presence, not prediction. The
## created timeline is published to [member NetwEntity.timeline].
##
## [br][br][b]Server Only.[/b]
func register_timeline(entity: NetwEntity) -> NetwTimeline:
	if not entity:
		return null
	var slot := _registry.timeline_register(entity, NetwTimeline.DEFAULT_LIMIT)
	return _registry.timeline_history(slot) if slot >= 0 else null


## Returns the registered [NetwTimeline] for [param entity], or [code]null[/code].
##
## This is the enumeration seam the server rewind queries read.
func timeline_of(entity: NetwEntity) -> NetwTimeline:
	return _registry.timeline_history(_registry.timeline_slot_of(entity))


## Returns aggregate simulation counters for the debug overlay.
##
## The cumulative counters ([code]corrections[/code], [code]consumed[/code],
## [code]missing[/code], [code]max_replay_depth[/code]) sum since spawn, so a live
## monitor like [LagCompensationMonitor] reads them as deltas over an interval. The
## remaining keys are instantaneous occupancy.
## [codeblock]
## {
##   |- entities: int          # engine records stepped this tick
##   |- timelines: int         # rewindable entities recorded this tick
##   |- corrections: int       # summed reconciliation snaps since spawn
##   |- max_replay_depth: int  # worst replay window walked
##   |- consumed: int          # summed inputs the server consumed
##   |- missing: int           # summed input ticks stepped over as lost
##   |- pending_actions: int   # actions queued awaiting readiness
##   `- gate_fallbacks: int    # state-ready actions resolved best-effort
## }
## [/codeblock]
func metrics() -> Dictionary:
	var result := _runner.metrics()
	result[&"timelines"] = _registry.timeline_registered()
	result[&"pending_actions"] = _pending_actions.size()
	result[&"gate_fallbacks"] = _gate_fallbacks
	return result


## Returns [param entity]'s recorded state at or before [param tick] as a detached
## [NetwSnapshot], the analytic hit-validation read.
##
## Returns an empty [NetwSnapshot] when no [LagCompensation] node is mounted, off
## the server, or for an entity with no retained history at [param tick].
##
## [codeblock]
## var past := Netw.of(self).lag_compensation.sample(target, view_tick)
## if past.has_value(&"position") and hits(origin, dir, past.position):
##     apply_damage(target)
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
	return NetwSnapshot.from_dictionary(
		_registry.timeline_sample_entity(entity, tick),
	)
## Returns a [NetwAction] bound to [param authority].
##
## [param authority] must be a method [Callable] on the entity root or one of
## its children. The returned action predicts through the session's effect
## ledger ([method NetwMultiplayer.effect_arm]) and uses the mounted
## [LagCompensation] node for private request transport.
func action(authority: Callable) -> NetwAction:
	var slot := _assign_action_slot(authority) if _configured else 0
	return NetwAction.new(self, authority, slot)


## Advances the simulation loop by one tick: drains admitted actions, steps every
## registered prediction engine record, and on the server records authoritative
## history. Driven by the [LagCompensation] configurator's
## [signal NetwMultiplayer.on_tick] binding.
func tick_step(delta: float, tick: int) -> void:
	_drain_pending_actions(tick)
	_runner.tick_step(
		NetwPredict.Timing.new(
			tick,
			delta,
			_ticktime(),
			_physics_frame,
			_declared_quantum(),
		),
	)
	# The server holds the truth, so only it records authoritative history.
	var api := self
	if _configured and api and api.is_server():
		_recorder.record_tick(_registry, _engines, tick)


## Records the state produced by the preceding FRAME drive after its physics
## solve has completed. Driven before the next frame's tick loop.
func before_frame_step() -> void:
	# One solve completed since the last call, unless the clock held it. The
	# clock's decision still names the frame that just ended here, because this
	# runs before the tick loop resolves the frame now opening, so a held frame
	# costs a transition nothing and every other frame costs it one.
	if not _native_core or _native_core.clock_handle.is_simulating:
		_physics_frame += 1
	_runner.before_frame_step()
	var api := self
	if _configured and api and api.is_server():
		_recorder.record_frame(_registry, _engines, _current_tick())
	_drain_tap()


## Advances every FRAME-scheduled simulation once after the clock's tick loop.
## Tick callbacks only author and label input for these entities. This pass
## performs their one drive application for the physics frame.
##
## The pass closes by flushing the transport, because it is the only producer
## that runs after the tick loop and so the only one whose frames would otherwise
## wait for a flush it already missed. See
## [method ReplicationCore.on_frame_end].
func frame_step() -> void:
	_runner.frame_step(frame_timing())
	_apply_simulation_gate()
	var api := self
	if _configured and api:
		api._replication.on_frame_end()


# Holds or admits every gated body's space for the solve this frame is about to
# run. Called after the drives and before the physics server steps, which is the
# only window where the decision can still take effect.
func _apply_simulation_gate() -> void:
	if _gated_entities.is_empty():
		return
	var active := not _native_core or _native_core.clock_handle.is_simulating
	var stale: Array[NetwEntity] = []
	for entity: NetwEntity in _gated_entities:
		if not is_instance_valid(entity) or not is_instance_valid(entity.owner):
			stale.append(entity)
			continue
		var record: Dictionary = _gated_entities[entity]
		var resolved := _entity_space(entity)
		if resolved[&"space"] != record[&"space"]:
			# A reparent moved the body to another world. Give the world it left
			# its own clock back before adopting the new one.
			_set_space_active(record, true)
			_gated_entities[entity] = resolved
			record = resolved
		_set_space_active(record, active)
	for entity: NetwEntity in stale:
		_gated_entities.erase(entity)
	if _gated_entities.is_empty() and _native_core:
		while _native_core.clock_handle.is_gated:
			_native_core.clock_handle.release_gate()


# The physics space a predicted body actually steps in, with the server that
# owns it. Resolved from the node rather than from any viewport it sits under,
# because those are two different questions and only this one names the space.
func _entity_space(entity: NetwEntity) -> Dictionary:
	var node := entity.owner if entity else null
	if node is Node3D:
		var world_3d := (node as Node3D).get_world_3d()
		if world_3d:
			return { &"space": world_3d.space, &"dimension": 3 }
	elif node is CanvasItem:
		var world_2d := (node as CanvasItem).get_world_2d()
		if world_2d:
			return { &"space": world_2d.space, &"dimension": 2 }
	return { &"space": RID(), &"dimension": 0 }


func _set_space_active(record: Dictionary, active: bool) -> void:
	var space: RID = record[&"space"]
	if not space.is_valid():
		return
	match int(record[&"dimension"]):
		3:
			PhysicsServer3D.space_set_active(space, active)
		2:
			PhysicsServer2D.space_set_active(space, active)


# Arms or releases [param entity]'s gate as its declaration resolves. A gate is
# armed by the archetype that says the physics server integrates this body, so a
# game whose bodies it does not integrate never holds a frame.
func _sync_simulation_gate(entity: NetwEntity, wanted: bool) -> void:
	if not entity:
		return
	var held := _gated_entities.has(entity)
	if wanted == held:
		return
	if wanted:
		_gated_entities[entity] = _entity_space(entity)
		if _native_core:
			_native_core.clock_handle.arm_gate()
		return
	# Releasing must give the space back, because nothing else will: an
	# unarmed clock stops resolving and a held space would stay held forever.
	_set_space_active(_gated_entities[entity], true)
	_gated_entities.erase(entity)
	if _native_core:
		_native_core.clock_handle.release_gate()


# Drains every registered entity's public evidence to JSONL when the
# NETW_PREDICT_TAP directory is set. The tap reads only the public handle, so
# it never moves recorded state, and it meters its own wall cost.
func _drain_tap() -> void:
	if _tap_off:
		return
	if _tap == null:
		if not _PredictTap.armed():
			_tap_off = true
			return
		_tap = _PredictTap.new()
		_tap_every = maxi(
			1,
			OS.get_environment(_TAP_EVERY_ENV).to_int(),
		)
	_tap_frame += 1
	if _tap_frame % _tap_every != 0:
		return
	for entity: NetwEntity in _engines:
		if is_instance_valid(entity):
			_tap.drain(entity.entity_id, entity.prediction)


## Returns the prediction tap's self-reported cost, or an empty [Dictionary]
## while no tap is armed.
##
## The tap is the one instrument whose price once masqueraded as a game
## defect, so its cost is a first-class read a capture harness echoes beside
## the numbers the tap produced: bytes and lines written, drain calls, and
## the mean wall cost of one drain.
func tap_cost() -> Dictionary:
	return _tap.cost() if _tap else { }


## Flushes the prediction tap's buffered lines to disk, so a capture collected
## while the session still runs reads complete files. A no-op while no tap is
## armed. The tap flushes once per second on its own and closes with the
## session, so most readers never need this.
func flush_tap() -> void:
	if _tap:
		_tap.flush()


# Closes the tap with the session, which flushes its tail and prints its
# self-reported cost. Driven by the LagCompensation configurator's removal.
func _close_tap() -> void:
	if _tap:
		_tap.close()
		_tap = null


## Reads the clock once for one FRAME pass and returns it by value.
##
## A frame drive completes the solve of the tick before the one now opening, so
## the transition it authors is labeled [code]tick - 1[/code]. Callers that step
## an engine directly through
## [method NetwPredictionHandle.simulate_frame] capture
## here too, so a direct drive and a pumped drive agree about when they are.
func frame_timing() -> PredictTiming:
	if not _native_core:
		return NetwPredict.Timing.new(0, 0.0, 0.0, _physics_frame, 1)
	var clock := _native_core.clock_handle
	var ticktime := clock.ticktime
	return NetwPredict.Timing.new(
		clock.tick - 1,
		ticktime,
		ticktime,
		_physics_frame,
		_declared_quantum(),
		clock.is_simulating,
	)


# The fixed network tick duration, or 0.0 when no clock is resolvable. A reader
# that gets 0.0 keeps whatever step it already had rather than adopting a
# meaningless one.
func _ticktime() -> float:
	return _native_core.clock_handle.ticktime if _native_core else 0.0


# Physics steps one transition is declared to advance. The physics server runs
# exactly one step per frame, so this is whole by construction wherever the
# declaration is sound, and _report_quantum_misconfiguration says so when it is
# not.
func _declared_quantum() -> int:
	if not _native_core:
		return 1
	return maxi(1, int(round(_native_core.clock_handle.physics_factor)))


# A body the physics server integrates needs a whole number of steps per
# transition, because the server runs exactly one step per frame. A fractional
# ratio makes the count alternate on a phase each peer keeps privately, so two
# peers can never spend the same simulated time on the same transition and no
# other declaration can repair it.
func _report_quantum_misconfiguration(entity: NetwEntity) -> void:
	if not _native_core:
		return
	var config := hash([
			_native_core.clock_handle.tickrate, Engine.physics_ticks_per_second
	])
	if config == _quantum_config_reported:
		return
	_quantum_config_reported = config
	var factor: float = _native_core.clock_handle.physics_factor
	if is_equal_approx(factor, roundf(factor)):
		return
	push_error(
		(
				"Prediction: %s drives a solver body at %d physics steps per "
				+ "second against a tickrate of %d, so one transition costs "
				+ "%.3f steps. The physics server runs exactly one step per "
				+ "frame, so a fractional cost alternates on a phase each peer "
				+ "keeps privately and the two never advance the same simulated "
				+ "time. Make physics_ticks_per_second a whole multiple of "
				+ "tickrate."
		) % [
			entity.entity_id if is_instance_valid(entity) else &"entity",
			Engine.physics_ticks_per_second,
			_native_core.clock_handle.tickrate,
			factor,
		],
	)


## Submits a player action request to be resolved.
## [br][br][b]Server Only.[/b]
func submit_action(
		route: int,
		method: StringName,
		view_tick: int,
		data: Variant,
		key: StringName,
		timing_mode: NetwAction.TimingMode,
		requester: int,
) -> void:
	var api := self if _configured else null
	if requester == 0 and api and api.multiplayer_peer:
		requester = api.get_unique_id()

	var target_path: NodePath = NodePath()
	var anchor := api.root if api else null
	var native_core := api._native_core if api else null
	if anchor and native_core:
		var entity := native_core.wrapper_for_route(route) as NetwEntity
		if entity and is_instance_valid(entity.owner):
			target_path = anchor.get_path_to(entity.owner)

	var request := _PendingAction.new(
		target_path,
		method,
		view_tick,
		data,
		key,
		requester,
		timing_mode,
		_current_tick(),
	)
	if not _can_resolve_action(request):
		_deny_action_to(requester, key)
		return
	var current_tick := _current_tick()
	var readiness := _action_readiness(request, current_tick)
	if readiness != _Readiness.NOT_READY:
		_execute_ready_action(request, current_tick, readiness)
		return
	# Not ready yet, so queue it, unless it is scheduled too far ahead to wait.
	if view_tick > current_tick + max_future_action_ticks:
		_deny_action_to(requester, key)
		return
	_pending_actions.append(request)


func _assign_action_slot(authority: Callable) -> int:
	var target := authority.get_object() as Node
	if not target:
		return 0
	var entity := NetwEntity.of(target)
	if not entity:
		return 0
	var route := "%s:%s" % [entity.entity_id, authority.get_method()]
	if _action_slots.has(route):
		return _action_slots[route]
	var slot := _action_slots.size()
	_action_slots[route] = slot
	return slot


func _send_action_request(
		target_path: NodePath,
		method: StringName,
		view_tick: int,
		data: Variant,
		key: StringName,
		timing_mode: NetwAction.TimingMode,
) -> void:
	if not _configured:
		return
	# Routes are allocated server-side and learned from the spawn packet, so a
	# remote requester only ever reads. A client-minted route would name a
	# different entity on the server. A route of 0 fails resolution there and
	# the request is denied.
	var api := self
	var is_remote := api and api.multiplayer_peer \
			and not api.is_server()
	var route := 0
	var anchor := api.root if api else null
	var node := anchor.get_node_or_null(target_path) if anchor else null
	var entity := NetwEntity.of(node)
	var native_core := api._native_core if api else null
	if entity and native_core:
		route = native_core.liveness_route_of(entity)
		if route <= 0 and not is_remote:
			route = native_core.liveness_allocate_route(entity)
	if route <= 0:
		Netw.dbg.warn(
			"LagCompensation: action target '%s' has no native_core route; "
			+ "the request will be denied",
			[String(target_path)],
		)

	if is_remote:
		if api:
			var payload := var_to_bytes(
				[
					method,
					view_tick,
					data,
					key,
					timing_mode,
				],
			)
			api._replication.send_to(
				MultiplayerPeer.TARGET_PEER_SERVER,
				route,
				NetwFrameEnvelope.Channel.ACTION,
				payload,
				true,
			)
		return
	submit_action(route, method, view_tick, data, key, timing_mode, 0)


func _deny_action_to(requester: int, key: StringName) -> void:
	var api := self
	var transport := api if _configured and api \
			and api.multiplayer_peer else null
	var local_peer := transport.get_unique_id() if transport else 0
	var remote := transport and requester != 0 and requester != local_peer \
			and requester in transport.get_peers()
	if remote:
		transport._replication.send_to(
			requester,
			0,
			NetwFrameEnvelope.Channel.LAGCOMP_DENY,
			var_to_bytes(key),
			true,
		)
		return
	if api:
		api.effect_discard(key)


# Client receive for a denied action. Discards the optimistic effect keyed by
# the denial.
func _handle_deny(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var api := self
	if api:
		api.effect_discard(bytes_to_var(payload))


func _handle_predict_command_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var engine := _engines.get(entity) as NetwPredictEngine._PredictionEngine
	if engine:
		engine.receive_command_frame(payload)
	# Relayed after the engine consumed it, so a subscriber never sees a
	# command authority itself refused.
	_relay_command_frame(entity, payload, sender)


func _handle_predict_relay_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		_sender: int,
) -> void:
	var engine := _engines.get(entity) as NetwPredictEngine._PredictionEngine
	if engine:
		engine.receive_relayed_command_frame(payload)


func _handle_predict_relay_request_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var request := NetwPredictRelayBook.request_of(payload)
	if request < 0:
		return
	relay_subscribe(entity, sender, request == 1)


func _handle_predict_ack_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		_sender: int,
) -> void:
	var engine := _engines.get(entity) as NetwPredictEngine._PredictionEngine
	if engine:
		engine.receive_ack_frame(payload)


func _handle_action_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := self if _configured else null
	if not api or not api.is_server():
		return
	var array = bytes_to_var(payload) as Array
	if array == null or array.size() < 5:
		return
	var method: StringName = array[0]
	var view_tick: int = array[1]
	var data: Variant = array[2]
	var key: StringName = array[3]
	var timing_mode: int = array[4]

	submit_action(
		entity.route,
		method,
		view_tick,
		data,
		key,
		timing_mode,
		sender,
	)


func _node_from_tree_path(path: NodePath) -> Node:
	var api := self if _configured else null
	var anchor := api.root if api else null
	if not anchor:
		return null
	return anchor.get_node_or_null(path)


func _can_resolve_action(request: _PendingAction) -> bool:
	var target := _node_from_tree_path(request.target_path)
	return target != null and target.has_method(request.method)


func _drain_pending_actions(tick: int) -> void:
	if _pending_actions.is_empty():
		return
	var waiting: Array[_PendingAction] = []
	for request in _pending_actions:
		if not _can_resolve_action(request):
			_deny_action_to(request.requester, request.key)
			continue
		var readiness := _action_readiness(request, tick)
		if readiness == _Readiness.NOT_READY:
			waiting.append(request)
			continue
		_execute_ready_action(request, tick, readiness)
	_pending_actions = waiting


func _input_readiness(request: _PendingAction, tick: int) -> _Readiness:
	if tick - request.queued_at_tick >= input_gate_deadline_ticks:
		return _Readiness.READY_BY_DEADLINE
	var target := _node_from_tree_path(request.target_path)
	var entity := NetwEntity.of(target) if target else null
	if not entity:
		return _Readiness.NOT_READY
	var engine := _engines.get(entity) as NetwPredictEngine._PredictionEngine
	if engine and not engine.has_consumed_state_tick(request.view_tick):
		return _Readiness.NOT_READY
	var timeline := timeline_of(entity)
	if not timeline:
		return _Readiness.NOT_READY
	if timeline.state_at(request.view_tick).is_empty():
		return _Readiness.NOT_READY
	return _Readiness.READY


func _execute_ready_action(
		request: _PendingAction,
		execution_tick: int,
		readiness: _Readiness,
) -> void:
	if readiness == _Readiness.READY_BY_DEADLINE:
		_gate_fallbacks += 1
		action_gate_fallback.emit(request.key, request.view_tick)
	_execute_action(request, execution_tick)


# Single mode dispatch shared by admission and the drain, so both stay mode agnostic.
func _action_readiness(request: _PendingAction, tick: int) -> _Readiness:
	match request.timing_mode:
		NetwAction.TimingMode.IMMEDIATE:
			return _Readiness.READY
		NetwAction.TimingMode.TICK_ALIGNED:
			return _Readiness.READY if request.view_tick <= tick else _Readiness.NOT_READY
		NetwAction.TimingMode.TICK_ALIGNED_STATE_READY:
			if request.view_tick > tick:
				return _Readiness.NOT_READY
			return _input_readiness(request, tick)
	return _Readiness.READY


func _execute_action(request: _PendingAction, execution_tick: int) -> void:
	var target := _node_from_tree_path(request.target_path)
	if not target or not target.has_method(request.method):
		_deny_action_to(request.requester, request.key)
		return
	var clamped_tick := mini(request.view_tick, execution_tick)
	var ctx := NetwAction.Context.new(
		self,
		request.requester,
		clamped_tick,
		request.view_tick,
		execution_tick,
		request.key,
	)
	if request.data == null:
		target.call(request.method, ctx)
	else:
		target.call(request.method, ctx, request.data)


func _current_tick() -> int:
	return _native_core.clock_handle.tick if _native_core else 0


func _on_node_added(node: Node) -> void:
	_observe_node_entity(node)
	_settle_observe(node)


# Re-reads the node at the next settle, because a node enters the tree before
# whatever stamps an entity onto it has run. Keyed by instance, so a batch of
# adds in one cascade observes every one of them.
func _settle_observe(node: Node) -> void:
	var api := self
	if api == null:
		return
	api._settle_schedule(
		_observe_node_entity_ref.bind(weakref(node)),
		StringName("%s%d" % [_OBSERVE_KEY_PREFIX, node.get_instance_id()]),
	)


func _observe_node_entity_ref(node_ref: WeakRef) -> void:
	var node := node_ref.get_ref() as Node if node_ref else null
	if not is_instance_valid(node):
		return
	_observe_node_entity(node)


func _observe_node_entity(node: Node) -> void:
	var entity := NetwEntity.of(node)
	if not entity:
		return
	var api := self
	if api and not entity.entity_id.is_empty():
		api.effect_adopt(entity.entity_id)
	if _observed_entities.has(entity):
		return
	_observed_entities[entity] = true
	if not entity.spawned.is_connected(_on_entity_spawned):
		entity.spawned.connect(_on_entity_spawned.bind(entity))


func _on_entity_spawned(entity: NetwEntity) -> void:
	var api := self
	if api and entity and not entity.entity_id.is_empty():
		api.effect_adopt(entity.entity_id)


class _PendingAction extends RefCounted:
	var target_path: NodePath
	var method: StringName
	var view_tick: int
	var data: Variant
	var key: StringName
	var requester: int
	var timing_mode: int
	var queued_at_tick: int


	func _init(
			p_target_path: NodePath,
			p_method: StringName,
			p_view_tick: int,
			p_data: Variant,
			p_key: StringName,
			p_requester: int,
			p_timing_mode: int,
			p_queued_at_tick: int,
	) -> void:
		target_path = p_target_path
		method = p_method
		view_tick = p_view_tick
		data = p_data
		key = p_key
		requester = p_requester
		timing_mode = p_timing_mode
		queued_at_tick = p_queued_at_tick


## The one reading of the clock a single simulation pass gets, taken at the pump
## boundary and handed down by value to everything the pass drives.
##
## A prediction kernel decides from its antecedents alone, so it may not resolve
## a clock of its own. Two kernels in one pass that each asked would be free to
## disagree about which tick they were running, and a replay could reproduce
## neither answer. Capturing once makes the pass's timing an antecedent like the
## command and the previous state, which is what lets
## [method NetwMultiplayer.frame_step] drive a body with no clock in
## reach at all.
## [codeblock]
## clock ──> tick_step / frame_step  ── NetwPredict.Timing ──> engine ──> kernels
##             (the only reader)         (by value)      (no clock reference)
## [/codeblock]
# Steps every registered prediction engine each tick in the order the pool
# declares, so the server consumes every entity identically each run and a
# replayed trace is reproducible. Capability logic stays in the engine record;
# this owns only ordering and metric aggregation.
class _SimulationRunner extends RefCounted:
	var _engines: Array[NetwPredictEngine._PredictionEngine] = []
	# The pool's order, resolved back to engines and rebuilt only when the
	# roster changes, so re-resolving an unchanged roster every tick is not
	# paid.
	var _sorted: Array[NetwPredictEngine._PredictionEngine] = []
	# The pool's slot back to the engine that carries the body, rebuilt with
	# the order. Empty when the pool could not name the whole roster.
	var _by_slot: Dictionary[int, NetwPredictEngine._PredictionEngine] = { }
	var _sort_dirty: bool = true
	var _service: NetwMultiplayer


	func register(engine: NetwPredictEngine._PredictionEngine) -> void:
		if engine not in _engines:
			_engines.append(engine)
			_sort_dirty = true


	func unregister(engine: NetwPredictEngine._PredictionEngine) -> void:
		_engines.erase(engine)
		_sort_dirty = true


	func tick_step(timing: NetwPredict.Timing) -> void:
		for engine in _phase(NetwPredictionEngine.PASS_ISLAND_TICK):
			engine.prepare_island(NetwPredict.Schedule.TICK)
		# Every group replays before any member drives fresh, so a pass carries
		# the whole group to the present against one committed roster.
		for engine in _phase(NetwPredictionEngine.PASS_JOINT):
			engine.joint_pass(timing)
		for engine in _phase(NetwPredictionEngine.PASS_TICK):
			engine.network_tick(timing)


	func frame_step(timing: NetwPredict.Timing) -> void:
		for engine in _phase(NetwPredictionEngine.PASS_ISLAND_FRAME):
			engine.prepare_island(NetwPredict.Schedule.FRAME)
		for engine in _phase(NetwPredictionEngine.PASS_FRAME):
			engine.simulate_frame(timing)


	func before_frame_step() -> void:
		for engine in _phase(NetwPredictionEngine.PASS_FINALIZE_FRAME):
			engine.finalize_frame_state()


	# The engines one phase steps, in the order the pool declares. Both the
	# order and the membership are determinism contracts, so the pool answers
	# them and this resolves the answer back to the engines that carry the
	# bodies. A roster the pool cannot name whole is one mid-registration, and
	# the fallback steps every engine so a slice never silently skips one.
	func _phase(
			phase: NetwPredictionEngine.PassPhase,
	) -> Array[NetwPredictEngine._PredictionEngine]:
		if _sort_dirty:
			_rebuild_sorted()
		if _service == null or _by_slot.size() != _engines.size():
			return _sorted
		var out: Array[NetwPredictEngine._PredictionEngine] = []
		for slot: int in _service._prediction_pool.pass_slots(phase):
			var engine := _by_slot.get(slot) as NetwPredictEngine._PredictionEngine
			if engine:
				out.append(engine)
		return out


	func metrics() -> Dictionary:
		var corrections := 0
		var max_replay := 0
		var consumed := 0
		var missing := 0
		var folded := 0
		var joint := _joint_metrics()
		for engine in _engines:
			var handle := engine.handle()
			if not handle:
				continue
			corrections += handle.stats.corrections
			max_replay = maxi(max_replay, handle.stats.max_replay_depth)
			consumed += handle.stats.consumed
			missing += handle.stats.missing
			folded += handle.stats.folded
		return {
			&"entities": _engines.size(),
			&"corrections": corrections,
			&"max_replay_depth": max_replay,
			&"consumed": consumed,
			&"missing": missing,
			&"folded": folded,
			&"joint": joint,
		}


	# The replay groups' own cadence, summed over the engines that ran a pass.
	# A group that never replayed reports zeros rather than being absent, so a
	# reader can tell a quiet cadence from an unreported one.
	#
	# The depth histogram and the floor-move breakdown are per-group shapes
	# rather than scalars, so they stay on the member's own
	# [NetwPredictStats] where a handle reads them keyed to one group.
	func _joint_metrics() -> Dictionary:
		var passes := 0
		var members := 0
		var relayed := 0
		var substituted := 0
		var heals := 0
		var lingering := 0
		for engine in _engines:
			var handle := engine.handle()
			if not handle:
				continue
			passes += handle.stats.joint_passes
			members = maxi(members, handle.stats.joint_members)
			relayed += handle.stats.cells_relayed
			substituted += handle.stats.cells_substituted
			heals += handle.stats.heal_snaps
			lingering += handle.stats.linger_held
		return {
			&"joint_passes": passes,
			&"joint_members": members,
			&"cells_relayed": relayed,
			&"cells_substituted": substituted,
			&"heal_snaps": heals,
			&"linger_held": lingering,
		}


	# Stable order by entity id so the server consumes every entity identically each
	# run. Rebuilt only when an engine registers or unregisters, since entity ids
	# are fixed once spawned.
	func _rebuild_sorted() -> void:
		_by_slot.clear()
		_sorted = _pool_order()
		if _sorted.size() != _engines.size():
			_by_slot.clear()
			_sorted = _engines.duplicate()
			_sorted.sort_custom(
				func(
						a: NetwPredictEngine._PredictionEngine,
						b: NetwPredictEngine._PredictionEngine,
				) -> bool:
					return a.order_key() < b.order_key()
			)
		_sort_dirty = false


	# The pool holds the order key, so it holds the order. An engine the pool
	# does not name is an engine mid-registration, and the caller falls back to
	# sorting the roster it has rather than stepping a partial one.
	func _pool_order() -> Array[NetwPredictEngine._PredictionEngine]:
		var resolved: Array[NetwPredictEngine._PredictionEngine] = []
		if _service == null:
			return resolved
		for engine in _engines:
			var slot := _service.native_prediction_slot(engine._entity)
			if slot < 0:
				_by_slot.clear()
				return []
			_by_slot[slot] = engine
		for slot: int in _service._prediction_pool.ordered_slots():
			var engine := _by_slot.get(slot) as NetwPredictEngine._PredictionEngine
			if engine:
				resolved.append(engine)
		return resolved


# Records every registered entity's authoritative state snapshot after a tick. The
# server holds the truth, so the recorder runs only on server authority and reads
# each entity's state-set snapshot through NetwEntity.state_binding. This gives
# non-predicted state-synced entities rewind history too, without a prediction
# engine.
class _HistoryRecorder extends RefCounted:
	# Records the current snapshot_payload of every entity in registry into its
	# timeline at tick. A consuming engine keys its record at the input-backed tick
	# through NetwPredictEngine._PredictionEngine.history_record_tick.
	func record_tick(
			registry: NetwLagCompCore,
			engines: Dictionary,
			tick: int,
	) -> void:
		_record(
			registry,
			engines,
			tick,
			NetwPredict.Schedule.TICK,
			true,
		)


	func record_frame(
			registry: NetwLagCompCore,
			engines: Dictionary,
			tick: int,
	) -> void:
		_record(
			registry,
			engines,
			tick,
			NetwPredict.Schedule.FRAME,
			false,
		)


	func _record(
			registry: NetwLagCompCore,
			engines: Dictionary,
			tick: int,
			schedule: NetwPredict.Schedule,
			include_unregistered: bool,
	) -> void:
		var timelines := registry.timeline_entities()
		for entity in timelines:
			if not is_instance_valid(entity.owner):
				continue
			# A deactivated entity (a lingering despawn) freezes its history at the
			# despawn boundary instead of recording stale frozen copies, so its
			# retained window ages from the moment it died and expires cleanly when
			# it frees. A carrier outside the tree has no process mode to read,
			# and is live by its registration alone.
			if entity.owner.is_inside_tree() and not entity.owner.can_process():
				continue
			var state: NetwPropertySetBinding = entity.state_binding
			if state:
				var record_tick := tick
				var engine := engines.get(entity) as NetwPredictEngine._PredictionEngine
				if engine:
					if not engine.uses_schedule(schedule):
						continue
					record_tick = engine.history_record_tick(tick)
					# A consuming engine declines a slot on a tick that consumed no
					# input, so the ack's own slot keeps the state that consume
					# actually produced rather than a coasted body under the same key.
					if record_tick < 0 \
							and not engine.consumed_unslotted_transition():
						continue
				elif not include_unregistered:
					continue
				var payload := state.canonicalize_payload(state.snapshot_payload())
				if record_tick >= 0:
					timelines[entity].record_state(record_tick, payload)
				if engine:
					engine.finalize_recorded_state(payload)
