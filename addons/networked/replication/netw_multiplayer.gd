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
## [br]- The property boundary: [method _gather_set], [method _apply_set]
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
const PredictionCore := preload("res://addons/networked/replication/prediction_core.gd")

const RpcCore := preload("res://addons/networked/replication/rpc_core.gd")

const SessionRoster := preload("res://addons/networked/session/session_roster.gd")

const AuthProtocol := preload("res://addons/networked/session/auth/auth_protocol.gd")

## Project setting naming the default [NetwMultiplayer] implementation script.
const MULTIPLAYER_SCRIPT_SETTING := "networked/multiplayer_script"
## Environment variable naming the extension certified by a law-suite run.
const LAW_EXTENSION_ENV := "NETW_LAW_EXTENSION"

# Resolved once by law_extension_script, because the environment is read once
# per run and never per call.
static var _law_extension_script: Script
static var _law_extension_resolved := false
# One answer per decision seam, because a script's method table cannot change
# for a live session.
var _overridden_seams: Dictionary[StringName, bool] = { }

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
var _clock: ClockCore

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
## [method NetwClockHandle.is_configured] is the absence story rather than a
## null check. The handle reads only: [NetwClockConfig] is the one way to tune
## a clock, which is what keeps a written setting and a read setting from ever
## disagreeing.
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

# The lag-compensation core. Never null, inert until a LagCompensation
# configurator registers through object_configuration_add.
var _lagcomp: LagCompCore

# How long an armed effect waits for an answer before its own denial. Long
# enough that a round trip under ordinary loss still resolves the act.
const _EFFECT_TIMEOUT_TICKS := 120

# The session's optimistic act ledger, swept by the clock rather than by a
# reading of it.
var _effects := NetwEffectLedger.new()

# The persistence core. Never null. Registers nothing on clients and no-ops
# without a Netw.configure_persistence archetype.
var _persistence: PersistenceCore

var _liveness: LivenessShell

# The visibility and interest core for this tree.
var _interest: InterestCore

# The session's one entity numbering. Interest and prediction are holders of
# a slot rather than owners of one, so neither may retire what the other still
# reads.
var _entity_slots := NetwEntitySlots.new()

# The session lifecycle machine, driven by peer assignment. Never null. The
# handle follows it, so replacing the machine can never leave the session
# property reading a retired one.
var _session: SessionCore:
	set(value):
		_session = value
		_session_handle = NetwSessionHandle.new(value) if value else null

# The view handed out by the session property. Bound to whatever machine
# _session currently holds, so it is never null once the session exists.
var _session_handle: NetwSessionHandle

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
# constructs it, so scenes answer before any MultiplayerSceneManager node
# exists. Private, because the mechanism is reached through the flat scene_*
# family, the Netw.* doors, and NetwSceneHandle rather than named directly.
var _scenes: SceneCore

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
	OFFLINE = SessionCore.State.OFFLINE,
	## A host or join is in flight and may still fail.
	CONNECTING = SessionCore.State.CONNECTING,
	## The session carries traffic.
	ONLINE = SessionCore.State.ONLINE,
	## Teardown is in flight; no new traffic is admitted.
	DISCONNECTING = SessionCore.State.DISCONNECTING,
}

## The local peer's role in the current session.
##
## Server authority means [constant DEDICATED_SERVER] or
## [constant LISTEN_SERVER]. Very little should test for [constant CLIENT],
## because a listen-server host is also a player.
enum Role {
	## No session, so no role yet.
	NONE = SessionCore.Role.NONE,
	## A remote peer holding no authority.
	CLIENT = SessionCore.Role.CLIENT,
	## Server authority with no local player.
	DEDICATED_SERVER = SessionCore.Role.DEDICATED_SERVER,
	## Server authority held by a peer that is also playing.
	LISTEN_SERVER = SessionCore.Role.LISTEN_SERVER,
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
	SYNC_MODE_SNAP = ClockCore.SyncMode.SNAP,
	## Nudge the tick accumulator toward the target, snapping only once the
	## divergence exceeds the configured panic threshold.
	SYNC_MODE_STRETCH = ClockCore.SyncMode.STRETCH,
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
	## Every participant follows and the source scene retires, which is
	## [method SceneCore.change_to] applied to every request.
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
	DESPAWN = InterestCore.LeavePolicy.DESPAWN,
	## Keep the copy and stop updating it. It resumes from a stale pose when
	## interest returns, with no spawn cost.
	RETAIN = InterestCore.LeavePolicy.RETAIN,
	## Neither. The game decides, through the layer's own handler.
	CUSTOM = InterestCore.LeavePolicy.CUSTOM,
}

## Local presentation behavior when an interest layer stops admitting.
##
## Orthogonal to [enum LeavePolicy]: that decides what the wire does, this
## decides what the player sees. A [constant LeavePolicy.RETAIN] entity is
## still present, so something must say whether it is drawn.
enum PerceptionPolicy {
	## Hide the retained copy. It stops being drawn where it was last seen.
	HIDE = InterestCore.PerceptionPolicy.HIDE,
	## Keep drawing the retained copy at its last known pose.
	SHOW = InterestCore.PerceptionPolicy.SHOW,
	## Neither. The game decides, through the layer's own handler.
	CUSTOM = InterestCore.PerceptionPolicy.CUSTOM,
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
	## handle with [method rid_from_route].
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
	## Counter. Per-recipient masked frames sent.
	STAT_MASKED_FRAMES_OUT,
	## Counter. Masked frames that carried the full state rather than a delta,
	## which is what a peer gets before its baseline is established.
	STAT_MASKED_FRAMES_FULL,
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
	&"masked_frames_out",
	&"masked_frames_full",
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

var _schemas := NetwHandleLedger.new()
var _schema_core := SchemaCore.new()

var _tables := NetwHandleLedger.new()
var _table_core := TableCore.new()
var _table_by_name: Dictionary[StringName, RID] = { }
var _table_schema: Dictionary[RID, RID] = { }

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
		if _session:
			_session.set_auth_callback(value)

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
var _services: NetwServiceRegistry = NetwServiceRegistry.new()

# RID bridge for the compatibility layer objects retained until FL10.
var _layer_ledger := NetwHandleLedger.new()
var _layer_by_name: Dictionary[StringName, RID] = { }
var _layer_records: Dictionary[RID, NetwInterestLayer] = { }
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
var _spawn_reconcile_plan: NetwSpawnPlan
var _spawn_constructor: Callable

# The connected-peer roster and per-peer participant handles. They live on the
# API so a bare session with no tree still answers get_participant and
# get_peer_context. Participants are keyed by peer id and read their accepted
# join, identity, and context back through this same API.
var _roster: SessionRoster = SessionRoster.new()
var _participants: Dictionary[int, NetwParticipant] = { }

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


func _init(inner_api: SceneMultiplayer = null) -> void:
	inner = inner_api if inner_api else SceneMultiplayer.new()
	_native_core = NetwMultiplayerCore.new()
	_native_core.set_multiplayer_peer(inner.multiplayer_peer)
	var adopted_auth_callback := inner.auth_callback
	_replication = ReplicationCore.new(self)
	_rpc_core = RpcCore.new(self, _replication)
	_clock = ClockCore.new(self)
	_clock_handle = NetwClockHandle.new(_clock)
	_embedding = EmbeddingCore.new(self)
	_embedding_handle = NetwEmbeddingHandle.new(_embedding)
	_lagcomp = LagCompCore.new(self)
	_persistence = PersistenceCore.new(self)
	_liveness = LivenessShell.new(self)
	_display = DisplayCore.new(self)
	_interest = InterestCore.new(self)
	_session = SessionCore.new(self)
	_scenes = SceneCore.new(self)
	_connect_once(_scenes.local_scene_changed, local_scene_changed.emit)
	_connect_once(_session.state_changed, state_changed.emit)
	_connect_once(_session.session_entered, session_entered.emit)
	_connect_once(_session.session_ended, session_ended.emit)
	_connect_once(_session.session_ended, _on_session_ended)
	_connect_once(_session.paused, tree_paused.emit)
	_connect_once(_session.unpaused, tree_unpaused.emit)
	_connect_once(_session.kicked, kicked.emit)
	auth_callback = adopted_auth_callback
	# Dead routes drop their unreliable-property sequence records so they never
	# outlive the entity they track.
	_connect_once(_liveness.entity_dead, _replication.clear_route)
	# local_player follows the liveness bus so a root-installed session with no
	# owning MultiplayerTree still tracks the represented entity.
	_connect_once(_liveness.entity_live, _on_liveness_entity_live)
	_connect_once(_liveness.entity_lingering, _on_liveness_entity_lingering)
	_connect_once(_liveness.entity_dead, _on_liveness_entity_dead)
	# The tick pump binds once at construction. The signal lives on the
	# interface, so an inert clock simply never fires it.
	_connect_once(_clock.after_tick, _on_clock_tick)
	_connect_once(_clock.before_tick, before_tick.emit)
	_connect_once(_clock.on_tick, on_tick.emit)
	_connect_once(_clock.after_tick, after_tick.emit)
	_connect_once(_clock.before_tick_loop, before_tick_loop.emit)
	_connect_once(_clock.after_tick_loop, after_tick_loop.emit)
	_connect_once(_clock.clock_synchronized, clock_synchronized.emit)
	_connect_once(
		_clock.display_offset_insufficient,
		display_offset_insufficient.emit,
	)
	_connect_once(_clock.stability_changed, stability_changed.emit)
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
	if _overridden_seams.has(seam):
		return _overridden_seams[seam]
	# A script's method list carries its own declarations AND the ones it
	# inherits, so a seam a subclass redeclares is listed once more than this
	# class lists it. The count is the only thing that separates an override
	# from an inheritance.
	var script := get_script() as Script
	var base := script
	while base and base.get_global_name() != &"NetwMultiplayer":
		base = base.get_base_script()
	var overridden := base != null \
			and _seam_count(script, seam) > _seam_count(base, seam)
	_overridden_seams[seam] = overridden
	return overridden


static func _seam_count(script: Script, seam: StringName) -> int:
	var total := 0
	for method: Dictionary in script.get_script_method_list():
		total += 1 if StringName(method.get(&"name", &"")) == seam else 0
	return total


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


func _bind_inner_signals() -> void:
	_connect_once(inner.peer_packet, _on_inner_peer_packet)
	_connect_once(inner.peer_connected, _on_inner_peer_connected)
	_connect_once(inner.peer_disconnected, _on_inner_peer_disconnected)
	_connect_once(inner.connected_to_server, _on_inner_connected_to_server)
	_connect_once(inner.connection_failed, _on_inner_connection_failed)
	_connect_once(inner.server_disconnected, _on_inner_server_disconnected)
	_connect_once(inner.peer_authenticating, _on_inner_peer_authenticating)
	_connect_once(
		inner.peer_authentication_failed,
		_on_inner_peer_authentication_failed,
	)


# Connects a signal once and makes a failed local wiring invariant visible.
func _connect_once(
		source: Signal,
		callback: Callable,
		flags: int = 0,
) -> void:
	if source.is_connected(callback):
		return
	var verdict := source.connect(callback, flags)
	if verdict == OK:
		return
	_count_gate_verdict(ERR_UNAVAILABLE)
	Netw.dbg.error(
		"NetwMultiplayer: signal connection failed with error %d",
		[verdict],
	)
	assert(verdict == OK, "NetwMultiplayer signal connection failed")


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
	if inner.peer_authenticating.is_connected(_on_inner_peer_authenticating):
		inner.peer_authenticating.disconnect(_on_inner_peer_authenticating)
	if inner.peer_authentication_failed.is_connected(
		_on_inner_peer_authentication_failed,
	):
		inner.peer_authentication_failed.disconnect(
			_on_inner_peer_authentication_failed,
		)


# Re-emits [member inner]'s connection-lifecycle signals on this extension, the
# standard [MultiplayerAPIExtension] wrapper pattern, so consumers that bind to
# [code]tree.api.peer_connected[/code] and friends never need to reach into
# [member inner] directly.
func _on_inner_peer_connected(id: int) -> void:
	peer_connected.emit(id)


func _on_inner_peer_disconnected(id: int) -> void:
	peer_disconnected.emit(id)


func _on_inner_connected_to_server() -> void:
	connected_to_server.emit()


func _on_inner_connection_failed() -> void:
	connection_failed.emit()


func _on_inner_server_disconnected() -> void:
	server_disconnected.emit()


func _on_inner_peer_authenticating(peer_id: int) -> void:
	peer_authenticating.emit(peer_id)


func _on_inner_peer_authentication_failed(peer_id: int) -> void:
	peer_authentication_failed.emit(peer_id)

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
	if _scenes.local_scene_changed.is_connected(local_scene_changed.emit):
		_scenes.local_scene_changed.disconnect(local_scene_changed.emit)
	_scenes.dispose()
	if _session.session_entered.is_connected(session_entered.emit):
		_session.session_entered.disconnect(session_entered.emit)
	if _session.session_ended.is_connected(session_ended.emit):
		_session.session_ended.disconnect(session_ended.emit)
	if _session.session_ended.is_connected(_on_session_ended):
		_session.session_ended.disconnect(_on_session_ended)
	if _session.paused.is_connected(tree_paused.emit):
		_session.paused.disconnect(tree_paused.emit)
	if _session.unpaused.is_connected(tree_unpaused.emit):
		_session.unpaused.disconnect(tree_unpaused.emit)
	if _session.kicked.is_connected(kicked.emit):
		_session.kicked.disconnect(kicked.emit)
	_session.dispose()
	_unbind_inner_signals()
	if _clock.after_tick.is_connected(_on_clock_tick):
		_clock.after_tick.disconnect(_on_clock_tick)
	if _liveness.entity_dead.is_connected(_replication.clear_route):
		_liveness.entity_dead.disconnect(_replication.clear_route)
	if _liveness.entity_live.is_connected(_on_liveness_entity_live):
		_liveness.entity_live.disconnect(_on_liveness_entity_live)
	if _liveness.entity_lingering.is_connected(_on_liveness_entity_lingering):
		_liveness.entity_lingering.disconnect(_on_liveness_entity_lingering)
	if _liveness.entity_dead.is_connected(_on_liveness_entity_dead):
		_liveness.entity_dead.disconnect(_on_liveness_entity_dead)
	if peer_connected.is_connected(_ensure_participant_row):
		peer_connected.disconnect(_ensure_participant_row)
	if peer_disconnected.is_connected(_clear_disconnected_peer):
		peer_disconnected.disconnect(_clear_disconnected_peer)
	_display.dispose()
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
func entity_create() -> RID:
	return _liveness.core.entity_create()


## Reserves the next monotonic wire route.
## [br][br][b]Server Only.[/b]
func reserve_route() -> int:
	return _liveness.reserve_route()


## Returns [param entity]'s route, allocating one when it is unbound.
## [br][br][b]Server Only.[/b]
func entity_allocate_route(entity: RID) -> int:
	assert(is_server(), "NetwMultiplayer.entity_allocate_route is server-only")
	if not _liveness.core.entity_is_valid(entity):
		return 0
	var existing := entity_get_route(entity)
	if existing > 0:
		return existing
	var route := reserve_route()
	return route if entity_bind_route(entity, route) == OK else 0


## Binds [param entity] to [param route] and moves it to
## [constant EntityState.LIVE].
func entity_bind_route(entity: RID, route: int) -> Error:
	if not _liveness.core.entity_is_valid(entity):
		return ERR_DOES_NOT_EXIST
	if route <= 0:
		return ERR_INVALID_DATA
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		_liveness.bind_route(route, wrapper)
	elif not _liveness.core.bind_route(entity, route):
		return ERR_INVALID_DATA
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
## api.entity_bind_route(entity, api.entity_allocate_route(entity))
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
	if not _liveness.core.entity_is_valid(entity):
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
	_liveness.bind_route(route, wrapper)
	return OK


## Returns [param entity]'s wire route, or [code]0[/code].
func entity_get_route(entity: RID) -> int:
	var route := _liveness.core.route_of(entity)
	if route > 0:
		return route
	var wrapper := _entity_wrapper(entity)
	return wrapper.route if wrapper else 0


## Returns [param entity]'s [enum EntityState].
func entity_get_state(entity: RID) -> EntityState:
	return _liveness.core.state_of(entity) as EntityState


## Returns [param route]'s [enum EntityState].
func route_get_state(route: int) -> EntityState:
	return _liveness.core.route_state(route) as EntityState


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
	# rid_of, not parent.rid: an ancestor that never needed a handle has none
	# yet, and a walk must not stop early on that.
	return rid_of(parent.owner)


## Returns the peer [param entity] represents, or [code]0[/code] when it is
## server-owned.
##
## A non-zero result is the definition of a player entity, so this is the verb
## every peer-filtering caller reaches for rather than re-deriving the fact from
## a node name.
func entity_get_peer(entity: RID) -> int:
	var wrapper := _entity_wrapper(entity)
	return wrapper.peer_id if wrapper else 0


## Returns [param node]'s entity handle, or an invalid RID when it has none.
func rid_of(node: Node) -> RID:
	var wrapper := NetwEntity.of(node)
	if wrapper == null:
		return RID()
	if not wrapper.rid.is_valid():
		wrapper.rid = entity_create()
		_liveness._entities[wrapper.rid] = wrapper
	return wrapper.rid


## Returns the entity handle bound to [param route], or an invalid RID.
func rid_from_route(route: int) -> RID:
	var entity := _liveness.core.rid_from_route(route)
	if entity.is_valid():
		return entity
	for candidate: RID in _liveness._entities:
		var wrapper := _entity_wrapper(candidate)
		if wrapper and wrapper.route == route:
			return candidate
	return RID()


## Returns every live route in stable order.
func live_routes() -> PackedInt32Array:
	return _liveness.core.live_routes()


## Runs [param callback] when [param route] becomes live.
func when_live(
		route: int,
		callback: Callable,
		timeout_ticks: int = 0,
		on_timeout: Callable = Callable(),
) -> void:
	_liveness.when_live(route, callback, timeout_ticks, on_timeout)


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
		out[i] = _liveness.reserve_route()
	_liveness.bind_routes_data(out)
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
	_liveness.tombstone_routes_data(routes)
	_table_core.retire_routes(routes)
	_table_core.queue_lifecycle_removals(routes)
	return OK


# Returns the cached wrapper for an entity handle.
func _entity_wrapper(entity: RID) -> NetwEntity:
	return _liveness._entities.get(entity)

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
	if name.is_empty():
		return RID()
	var existing := _schema_core.find(name)
	if existing.is_valid():
		_schema_core.declare(existing, name)
		return existing
	var rid := _schemas.rid_create()
	_schema_core.declare(rid, name)
	return rid


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
	return _schema_core.add_column(schema, key, type, stride)


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
	_schema_core.set_column_quantizer(schema, column, quantizer)


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
	return _schema_core.seal(schema)


## Returns the schema declared under [param name], or an invalid RID.
##
## A declaration this session has not compiled yet is compiled here, because a
## class's [code]static var[/code] initializers run on first access rather than
## at load and may therefore have missed the session's own adoption sweep.
func schema_find(name: StringName) -> RID:
	var found := _schema_core.find(name)
	if found.is_valid() or NetwSchemaModel.find(name) == null:
		return found
	_adopt_schema_declarations()
	return _schema_core.find(name)


## Returns [param schema]'s sealed shape hash, or [code]0[/code] when it is not
## sealed.
##
## The shape is the name and, per column, key, type, stride, and quantizer. It
## is the value a TABLE frame carries and the value
## [method property_set_get_wire_hash] folds membership and lanes into, so two
## peers that declared a different type or a different bit width disagree here
## rather than misreading every later frame.
func schema_get_hash(schema: RID) -> int:
	return _schema_core.hash_of(schema)


## Returns how many columns [param schema] declares.
func schema_get_column_count(schema: RID) -> int:
	return _schema_core.column_count(schema)


## Returns one column's key, or an empty name when the address is invalid.
func schema_get_column_key(schema: RID, column: int) -> StringName:
	return _schema_core.column_key(schema, column)


## Returns one column's [enum ColumnType].
func schema_get_column_type(schema: RID, column: int) -> ColumnType:
	return _schema_core.column_type(schema, column) as ColumnType


## Returns how many elements one row occupies in [param column], or
## [code]0[/code] when the address is invalid.
func schema_get_column_stride(schema: RID, column: int) -> int:
	return _schema_core.column_stride(schema, column)


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
	var record := _schema_core.record_of(schema)
	if record == null or not record.sealed:
		return RID()
	var existing: RID = _table_by_name.get(record.name, RID())
	if existing.is_valid():
		return existing
	var rid := _tables.rid_create()
	if _table_core.declare(rid, record) != OK:
		return RID()
	_table_by_name[record.name] = rid
	_table_schema[rid] = schema
	return rid


## Returns the schema [param table] binds, or an invalid RID.
##
## Reflection goes through the schema family, because a column's key, type, and
## stride belong to the declaration rather than to any one binding of it.
func table_get_schema(table: RID) -> RID:
	return _table_schema.get(table, RID())


## Writes one [enum TableParam].
##
## Local configuration rather than shape, so a param never enters the wire hash
## and a later value is not a wire event.
func table_set_param(table: RID, param: TableParam, value: Variant) -> void:
	match param:
		TableParam.TABLE_PARAM_RELIABLE:
			_table_core.set_reliable(table, bool(value))


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
	var found: RID = _table_by_name.get(name, RID())
	if found.is_valid() or NetwSchemaModel.find(name) == null:
		return found
	_adopt_schema_declarations()
	_adopt_table_declarations()
	return _table_by_name.get(name, RID())


## Returns [param table]'s wire hash, the shape hash of the schema it binds.
##
## It is [method schema_get_hash] verbatim, because a table adds nothing to the
## shape: params are local and the store is not declaration. The value rides
## every frame, so a mid-session script reload that changed a schema is caught
## rather than decoded as garbage.
func table_get_wire_hash(table: RID) -> int:
	return _table_core.schema_hash(table)


## Records the row order [method table_commit] will publish.
##
## Row [code]i[/code] of every column belongs to [code]routes[i][/code], so
## this and the columns are one statement that only becomes true at the commit.
## The array is held by reference until then.
## [br][br][b]Server Only.[/b]
func table_write_routes(table: RID, routes: PackedInt64Array) -> Error:
	return _table_core.write_routes(table, routes)


## Records one column's buffer for the next commit.
##
## [param data] must be the storage array [enum ColumnType] names, and is held
## by reference until [method table_commit] copies it, so writing is a slot
## store rather than a copy.
## [br][br][b]Server Only.[/b]
func table_write_column(table: RID, column: int, data: Variant) -> Error:
	return _table_core.write_column(table, column, data)


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
	return _table_core.commit(table, _clock.tick)


## Returns the applied row order by reference. Treat it as read-only.
func table_read_routes(table: RID) -> PackedInt64Array:
	return _table_core.read_routes(table)


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
	return _table_core.read_column(table, column)


## Returns the routes the most recent applied wave added to [param table].
##
## Cohorts are derived here rather than read off the wire, which is what makes
## them exact under loss: a dropped datagram delays a row's data, and can never
## lose a birth or a death. They are stable until the next wave.
func table_read_births(table: RID) -> PackedInt64Array:
	return _table_core.read_births(table)


## Returns the routes the most recent applied wave removed from [param table].
## The twin of [method table_read_births].
func table_read_deaths(table: RID) -> PackedInt64Array:
	return _table_core.read_deaths(table)


## Returns [param route]'s row index in [param table], or [code]-1[/code] when
## it has no row.
func table_get_row(table: RID, route: int) -> int:
	return _table_core.row_of(table, route)


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
	return _table_core.rows_of(table, routes)


## Returns the tick of [param table]'s freshest applied row set, or
## [code]-1[/code] before anything has been applied.
##
## Poll this and compare it in your own loop when you would rather not connect
## [signal table_received]. Both styles are first-class.
func table_get_tick(table: RID) -> int:
	return _table_core.tick_of(table)


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
	if name.is_empty():
		return RID()
	var existing: RID = _layer_by_name.get(name, RID())
	if existing.is_valid():
		return existing
	var layer := _interest.layer(name)
	if layer == null:
		return RID()
	var rid := _layer_ledger.rid_create()
	_layer_by_name[name] = rid
	_layer_records[rid] = layer
	if _layer_declare(rid) != OK:
		_layer_records.erase(rid)
		_layer_by_name.erase(name)
		_layer_ledger.rid_free(rid)
		return RID()
	return rid


## Returns the layer named [param name], or an invalid RID.
func layer_find(name: StringName) -> RID:
	return _layer_by_name.get(name, RID())


## Frees [param layer] and removes its current memberships.
func layer_free(layer: RID) -> void:
	var record := _layer_record(layer)
	if record == null:
		return
	_layer_undeclare(layer)
	_disconnect_layer_monitor(layer)
	_layer_drivers.erase(layer)
	_layer_records.erase(layer)
	_layer_by_name.erase(record.layer_id)
	_layer_ledger.rid_free(layer)


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
		callback.call(true, rid_of(entity.owner), peer)
	var exited := func(entity: NetwEntity, peer: int) -> void:
		callback.call(false, rid_of(entity.owner), peer)
	var visible := func(entity: NetwEntity) -> void:
		callback.call(true, rid_of(entity.owner), get_unique_id())
	var hidden := func(entity: NetwEntity) -> void:
		callback.call(false, rid_of(entity.owner), get_unique_id())
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
	var bit := _interest._peer_bits.get(peer, -1) as int
	if bit < 0:
		return false
	return _interest_admits(entity, bit)


## Returns [param entity]'s committed peer-bit words.
func interest_get_row(entity: RID) -> PackedInt64Array:
	return _interest_row_of(entity)


## Returns the resolved layer RIDs for [param entity].
func interest_get_membership(entity: RID) -> Array[RID]:
	var wrapper := _entity_wrapper(entity)
	var out: Array[RID] = []
	if wrapper == null:
		return out
	for name: StringName in _interest.resolved_layer_ids(wrapper):
		var layer := layer_find(name)
		if not layer.is_valid():
			layer = layer_create(name)
		out.append(layer)
	return out


## Returns whether [param entity] has a visibility filter.
func interest_is_filtered(entity: RID) -> bool:
	var wrapper := _entity_wrapper(entity)
	return _interest.has_filter(wrapper) if wrapper else false


## Explains the committed interest verdict for [param peer].
func interest_explain(entity: RID, peer: int) -> String:
	var bit := _interest._peer_bits.get(peer, -1) as int
	if bit < 0:
		return "peer is not registered"
	return _interest_explain(entity, bit)


## Flushes driver mutations and the committed interest matrix.
func interest_flush() -> Error:
	var driver_verdict := _run_layer_drivers()
	if driver_verdict != OK:
		return driver_verdict
	return _interest.flush()


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
	for peer: int in record.viewers.keys():
		record.remove_viewer(peer)
	_interest._layers.erase(record.layer_id)
	_interest.forget_layer_row(record.layer_id)


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
	return _interest._engine_recompute()


## Publishes the delta that [method _interest_recompute] staged.
##
## Commit is the only point at which [method _interest_row_of] and
## [method _interest_admits] begin reporting the new matrix, which is what
## makes the fold atomic to everything downstream. Shares the committed matrix
## described on [method _layer_declare].
func _interest_commit() -> void:
	_interest._engine_commit()


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
	return _interest.committed_row(wrapper) if wrapper else PackedInt64Array()


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
	return _interest.bit_admits(wrapper, peer_bit) if wrapper else false


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
	return _interest.explain_bit(wrapper, peer_bit) \
	if wrapper else "entity is not registered"


# Resolves one owned layer record.
func _layer_record(layer: RID) -> NetwInterestLayer:
	if not _layer_ledger.rid_is_valid(layer):
		return null
	return _layer_records.get(layer)


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
## api.entity_bind_route(arena, api.reserve_route())
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
	var wrapper := _entity_wrapper(entity)
	return wrapper != null and wrapper.declares_scene


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
			if wrapper.stage != NetwEntity.Stage.UNBOUND:
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
			return _scene_stem(scene)
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
	var found := _scenes.scene(stem)
	return found.entity if found != null else RID()


## Returns every live scene whose stem is [param stem].
func scene_find_all(stem: StringName) -> Array[RID]:
	var out: Array[RID] = []
	for node: Node in _scenes.scenes_named(stem):
		out.append(rid_of(node))
	return out


## Returns the scene [param entity] belongs to, or an invalid RID.
##
## Self-inclusive: a declared scene answers with itself. Otherwise this walks
## the parent-entity chain, so membership follows the tree and self-heals on
## reparent without anything having to re-enroll the entity.
func scene_of(entity: RID) -> RID:
	var walker := entity
	while walker.is_valid():
		if scene_is_declared(walker):
			return walker
		walker = entity_get_parent(walker)
	# The wrapper tier still owns scenes the facet has not reached, so fall back
	# to the node walk rather than reporting a scene-less entity.
	# TODO: drop once every scene container carries the facet.
	var node := entity_get_node(entity)
	var wrapper := _scenes.scene_of(node) if node else null
	return rid_of(wrapper) if is_instance_valid(wrapper) else RID()


## Returns [param scene]'s interest layer, or an invalid RID.
##
## A scene's boundary is opened by [method scene_admit] rather than by
## [method layer_create], so this mints the handle for one that already exists
## instead of only answering handles a caller asked for. It stays a read: a
## scene nobody admitted anyone to has no boundary and gets no handle.
func scene_get_layer(scene: RID) -> RID:
	var layer_id := _scene_layer_id(scene)
	var found := layer_find(layer_id)
	if found.is_valid() or _interest.get_layer(layer_id) == null:
		return found
	return layer_create(layer_id)


# The stem naming [param scene]'s archetype: the label a script declared, or the
# content root's name when nothing did. Sits beside _scene_layer_id as the other
# place the container's type is read, so the stem a scene answers to and the key
# the registry files it under cannot disagree.
func _scene_stem(scene: RID) -> StringName:
	var wrapper := _entity_wrapper(scene)
	if wrapper != null and wrapper.scene_label != &"":
		return wrapper.scene_label
	var content := _scene_level(scene)
	return StringName(content.name) if content != null else &""


# The framework-derived layer id naming [param scene]'s admission boundary, or
# empty. This is the single place the scene container's type is named, so every
# other caller (including InterestCore) asks in terms of the scene RID and core
# stays free of any scene class.
func _scene_layer_id(scene: RID) -> StringName:
	var content := _scene_level(scene)
	if content == null:
		return &""
	var route := entity_get_route(scene)
	if route <= 0:
		# Pre-arm, so no route exists yet. The stem alone is the best available
		# key, and the layer is re-read once the route lands.
		return StringName("scene:%s" % content.name)
	return StringName("scene:%s#%d" % [content.name, route])


# The content root of [param scene], which is its container's only child. This
# and [method _scene_layer_id] are the only places a scene's node shape is read,
# so everything else asks in terms of the scene RID.
func _scene_level(scene: RID) -> Node:
	var node := entity_get_node(scene)
	if node == null or node.get_child_count() == 0:
		return null
	return node.get_child(0)


## Returns the scene this peer currently presents, or an invalid RID.
##
## A dedicated server presents nothing, so it always answers invalid.
func scene_get_current() -> RID:
	var node := _scenes.current_scene
	return rid_of(node) if is_instance_valid(node) else RID()


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
	for node: Node in _scenes.live_scenes():
		out.append(rid_of(node))
	return out


## Admits [param peer] to [param scene].
##
## Admission is an interest-layer edge, so every descendant entity inherits it
## through the parent clamp rather than being enrolled one by one. The result is
## the admission itself rather than the attempt: an answer of [constant OK]
## means [method scene_admits] now holds, so a caller that ignores it cannot
## mistake a dropped admission for a completed one.
## [br][br][b]Server Only.[/b]
func scene_admit(scene: RID, peer: int) -> Error:
	assert(is_server(), "NetwMultiplayer.scene_admit is server-only")
	if peer == 0:
		return ERR_INVALID_PARAMETER
	if entity_get_node(scene) == null:
		return ERR_DOES_NOT_EXIST
	_scenes.admit_peer(scene, peer)
	if not scene_admits(scene, peer):
		return ERR_UNAVAILABLE
	interest_flush()
	return OK


## Removes [param peer] from [param scene].
##
## A released peer is told so on its own side, because a client learns
## membership from awareness of the scene entity and would otherwise keep
## presenting a scene it no longer belongs to.
## [br][br][b]Server Only.[/b]
func scene_release(scene: RID, peer: int) -> void:
	assert(is_server(), "NetwMultiplayer.scene_release is server-only")
	if entity_get_node(scene) == null:
		return
	var core := _scenes
	core._notify_scene_released(scene, peer)
	core.release_peer(scene, peer)
	interest_flush()


## Returns whether [param scene] admits [param peer].
func scene_admits(scene: RID, peer: int) -> bool:
	var boundary := _interest.get_layer(_scene_layer_id(scene))
	return boundary != null and boundary.viewers.has(peer)


## Returns every peer [param scene] admits.
func scene_get_peers(scene: RID) -> PackedInt32Array:
	var out := PackedInt32Array()
	var boundary := _interest.get_layer(_scene_layer_id(scene))
	if boundary:
		for peer: int in boundary.viewers:
			out.append(peer)
	return out


## Returns every entity [param scene] encloses.
##
## Read off the container's subtree rather than off an enrollment book, so it
## answers alike on every peer and self-heals on reparent. A nested scene owns
## its own descendants, so the walk stops there and they report against it.
func scene_get_entities(scene: RID) -> Array[RID]:
	var out: Array[RID] = []
	var node := entity_get_node(scene)
	if node != null:
		_collect_scene_entities(node, out)
	return out


# Appends every entity root under [param node] to [param out], excluding
# [param node] itself so a scene never reports as its own member.
func _collect_scene_entities(node: Node, out: Array[RID]) -> void:
	for child: Node in node.get_children():
		var record := NetwEntity.of(child)
		var owns_record := record != null and record.owner == child
		if owns_record:
			out.append(rid_of(child))
			if record.declares_scene:
				continue
		_collect_scene_entities(child, out)


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
	_scenes.observe(scene, event, callback)


## Reverses [method scene_observe] for one [param callback].
func scene_unobserve(scene: RID, event: SceneEvent, callback: Callable) -> void:
	_scenes.unobserve(scene, event, callback)


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
	var node := _scenes.spawn(recipe, isolation)
	if not is_instance_valid(node):
		return RID()
	var entity := rid_of(node)
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
	if entity_get_node(scene) == null:
		return ERR_DOES_NOT_EXIST
	var content := _scene_level(scene)
	var stem := StringName(content.name) if content != null else &""
	if stem.is_empty():
		return ERR_DOES_NOT_EXIST
	if linger_seconds <= 0.0:
		_scenes.destroy(stem)
	else:
		_scenes.retire(stem, int(ceilf(linger_seconds * 60.0)))
	return OK


## Moves [param entity] into [param destination].
##
## The returned [NetwPromise] settles once reparenting, membership, and
## persistence have all landed. It rejects with
## [constant @GlobalScope.ERR_UNAVAILABLE] when the destination cannot be
## reached.
## [br][br][b]Server Only.[/b]
func scene_move(
		entity: RID,
		destination: RID,
		opts: SceneMoveOpts = null,
) -> NetwPromise:
	assert(is_server(), "NetwMultiplayer.scene_move is server-only")
	var node := entity_get_node(entity)
	var target := entity_get_node(destination)
	var wrapper := NetwEntity.of(node) if node else null
	if wrapper == null or target == null:
		var refused := NetwPromise.new()
		refused.reject(ERR_UNAVAILABLE, "scene_move: entity or destination is unreachable")
		return refused
	return _scenes.move(wrapper, target, opts.to_move_opts() if opts else null)


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
	if destination is String:
		return _scenes.request_change_path(destination, args)
	return _scenes.request_change(StringName(destination), args)


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
	_scenes.set_request_handler(handler)


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
	if not (destination is StringName or destination is String):
		return false
	var named := String(destination)
	if named == String(label):
		return true
	var asked := ResourceUID.ensure_path(named)
	if not asked.begins_with("res://"):
		return false
	# A stem is the declared root node's name, which need not match the file
	# name, so the declared path is checked as well as the basename.
	if asked.get_file().get_basename() == String(label):
		return true
	var declared := _scenes._scene_path_for(label)
	return not declared.is_empty() and ResourceUID.ensure_path(declared) == asked


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
	_scenes.request_reach = reach


## Returns how far an admitted scene request reaches.
func scene_get_request_reach() -> SceneReach:
	return _scenes.request_reach


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


# Reports [param entity]'s scene edges for as long as it lives, for an entity
# seated into a scene without ever routing. Routed entities reach the same watch
# through the liveness bus.
func _scene_watch_entity(entity: NetwEntity) -> void:
	_scenes.watch_entity(entity)


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
		if not _liveness.core.entity_is_valid(entity):
			return ERR_DOES_NOT_EXIST
		_pending_scene_facets[entity] = declared
		return OK
	if wrapper.stage != NetwEntity.Stage.UNBOUND:
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
	var node := _replication.resolve_comp_node(wrapper, comp, "")
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
		var target := _replication.resolve_comp_node(wrapper, int(value), "") \
		if wrapper else null
		display_set_target_node(entity, target)
		return
	_display._write_config(entity, param, value)


## Returns one [enum DisplayParam], or [code]null[/code] when invalid.
func display_get_param(entity: RID, param: DisplayParam) -> Variant:
	return _display._read_config(entity, param)


## Snaps one [param track] and clears its sample history.
func display_snap(entity: RID, track: StringName, value: Variant) -> void:
	_display._snap_entity_track(entity, track, value)


## Returns the most recently displayed value for [param track].
func display_get_value(entity: RID, track: StringName) -> Variant:
	return _display._display_value(entity, track)


## Returns [param entity]'s displayed authoring tick, or [code]-1[/code].
func display_get_tick(entity: RID) -> int:
	return _display._display_tick_of(entity)


## Returns one track or runtime diagnostic selected by [param stat].
func display_get_track_stat(
		entity: RID,
		track: StringName,
		stat: StringName,
) -> Variant:
	return _display._display_track_stat(entity, track, stat)


## Binds display output to [param node]. Pass [code]null[/code] to clear it.
func display_set_target_node(entity: RID, node: Node) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return
	if node == null:
		_display._write_config(
			entity,
			DisplayParam.DISPLAY_PARAM_VISUAL_ROOT,
			NodePath(""),
		)
		return
	if node != wrapper.owner and not wrapper.owner.is_ancestor_of(node):
		return
	_display._write_config(
		entity,
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
	_display._mark_runtime_dirty(entity)


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
	_display._mark_runtime_dirty(entity)


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
	var node := _replication.resolve_comp_node(wrapper, comp, "") \
	if wrapper else null
	if not is_instance_valid(node) or not (spec is NetwInterpolate):
		return ERR_INVALID_DATA
	NetwScriptModel.configure_node_property(node, track).interpolate(spec)
	_display._mark_runtime_dirty(entity)
	return OK


## Drops [param entity]'s display runtime and every track it held.
##
## Display state is per-entity, so removal takes all of an entity's tracks at
## once rather than one at a time. An entity with no runtime is a no-op, which
## makes teardown safe to repeat. Shares the sample history described on
## [method _display_declare].
func _display_undeclare(entity: RID) -> void:
	_display._remove_entity_runtime(entity)


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
	var node := _replication.resolve_comp_node(wrapper, int(declaration[0]), "") \
	if wrapper else null
	if not is_instance_valid(node):
		return ERR_UNAVAILABLE
	_display._record(
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
	return _display._pump_entity(entity, _display_pump_timing)


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
	if not _lagcomp.is_configured():
		return ERR_UNCONFIGURED
	_lagcomp.register_prediction(wrapper)
	return OK


## Removes [param entity] from prediction on this peer.
func predict_undeclare(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		_lagcomp.unregister_prediction(wrapper)


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
	if not _lagcomp.native_bind_owner(wrapper, owner):
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
		_lagcomp.native_unbind_owner(wrapper)


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
	_lagcomp.install_stepper(space, stepper)


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
		_lagcomp.relay_subscribe(wrapper, get_unique_id(), subscribed)
		return
	var route := _liveness.route_of(wrapper)
	if route <= 0:
		return
	_replication.send_to(
		MultiplayerPeer.TARGET_PEER_SERVER,
		route,
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST,
		NetwPredictRelayBook.request_bytes(subscribed),
		true,
	)


## Marks a local collision or other undeclared prediction contact.
func predict_notify_contact(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	var engine := _lagcomp.engine_for(wrapper) if wrapper else null
	if engine:
		engine.notify_contact()


## Samples one declared sensor on [param entity].
func predict_sensor_sample(
		entity: RID,
		name: StringName,
		default: Variant = null,
) -> Variant:
	var wrapper := _entity_wrapper(entity)
	var engine := _lagcomp.engine_for(wrapper) if wrapper else null
	return engine.sensor_sample(name, default) if engine else default


## Declares an authoritative history timeline for [param entity].
## [br][br][b]Server Only.[/b]
func timeline_declare(entity: RID) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	_lagcomp.register_timeline(wrapper)
	return OK


## Removes [param entity]'s authoritative history timeline.
func timeline_undeclare(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		_lagcomp.unregister_timeline(wrapper)


## Returns [param entity]'s state at or before [param tick].
## [br][br][b]Server Only.[/b]
func timeline_sample(entity: RID, tick: int) -> NetwSnapshot:
	return lagcomp_sample(entity, tick)


## Returns [param entity]'s state at or before [param tick].
## [br][br][b]Server Only.[/b]
func lagcomp_sample(entity: RID, tick: int) -> NetwSnapshot:
	var wrapper := _entity_wrapper(entity)
	return _lagcomp.sample(wrapper, tick) \
	if wrapper else NetwSnapshot.new()


## Rewinds [param entities] while [param body] runs, then restores them.
## [br][br][b]Server Only.[/b]
func lagcomp_rewind(entities: Array[RID], tick: int, body: Callable) -> void:
	var wrappers: Array[NetwEntity] = []
	for entity: RID in entities:
		var wrapper := _entity_wrapper(entity)
		if wrapper:
			wrappers.append(wrapper)
	_lagcomp.rewind(wrappers, tick, body)


## Returns a predicted action bound to [param authority].
func lagcomp_action(authority: Callable) -> NetwAction:
	return _lagcomp.action(authority)


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
	var ttl := timeout_ticks if timeout_ticks > 0 else _EFFECT_TIMEOUT_TICKS
	_effects.arm(key, revert, _clock.tick + ttl)


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
	if _effects.watch(key, confirmed, denied):
		return
	Netw.dbg.warn(
		"NetwMultiplayer: effect_watch refused, '%s' is not armed",
		[String(key)],
	)


## Resolves [param key] as kept. The pending revert is dropped unrun.
func effect_adopt(key: StringName) -> void:
	_effects.adopt(key)


## Resolves [param key] as reverted. The pending revert runs immediately.
func effect_discard(key: StringName) -> void:
	_effects.discard(key)


## Returns whether [param key] is armed and unresolved.
func effect_pending(key: StringName) -> bool:
	return _effects.pending(key)


func _sweep_effects(_delta: float, tick: int) -> void:
	_effects.sweep(tick)


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
	var result := _lagcomp.metrics()
	result[&"effects_armed"] = _effects.count()
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
	if not _liveness.core.entity_is_valid(entity) \
			or not _property_sets.rid_is_valid(set):
		return ERR_DOES_NOT_EXIST
	var record := _property_set_records.get(set) as NetwPropertySet
	if record == null or not record.sealed:
		return ERR_INVALID_DATA
	var wrapper: NetwEntity = _liveness._entities.get(entity)
	if wrapper == null:
		return ERR_UNAVAILABLE
	var node := _replication.resolve_comp_node(wrapper, comp, "")
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
	var wrapper: NetwEntity = _liveness._entities.get(entity)
	var node := _replication.resolve_comp_node(wrapper, comp, "") \
	if wrapper else null
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
	var wrapper: NetwEntity = _liveness._entities.get(entity)
	var node := _replication.resolve_comp_node(wrapper, comp, "") \
	if wrapper else null
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
	var node := _entity_component_node(entity, comp)
	if not is_instance_valid(node):
		return ERR_DOES_NOT_EXIST
	if not property in node:
		return ERR_INVALID_DATA
	_replication._sync_pipeline.send_property(node, property)
	return OK


## Sends one configured signal from an entity component.
func sync_send_signal(
		entity: RID,
		comp: int,
		signal_name: StringName,
		args: Array,
) -> Error:
	var node := _entity_component_node(entity, comp)
	if not is_instance_valid(node):
		return ERR_DOES_NOT_EXIST
	if not node.has_signal(signal_name):
		return ERR_INVALID_DATA
	_replication._sync_pipeline.send_signal(node, signal_name, args)
	return OK


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
	_replication.register_channel(channel, adapter, defer_when_unknown)


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
	_replication.send_to(peer, route, channel, payload, reliable)
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
	if config is NetwClockConfig:
		_clock.configure(null, config as NetwClockConfig)
		_clock._configured = true
		_connect_once(_clock.on_tick, _sweep_effects)
		_wire_lagcomp_service()
		return OK
	if config is NetwLagCompensationConfig:
		_lagcomp.configure(null, config as NetwLagCompensationConfig)
		_lagcomp._configured = true
		_wire_lagcomp_service()
		return OK
	if config is NetwSessionConfig:
		_session.configure(config as NetwSessionConfig)
		return OK
	if config is NetwSceneConfig:
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
		_scenes.configure(config as NetwSceneConfig)
		return OK
	return ERR_INVALID_PARAMETER


func _wire_lagcomp_service() -> void:
	if not _clock.is_configured() or not _lagcomp.is_configured():
		return
	_lagcomp._clock = _clock
	_connect_once(_clock.before_tick_loop, _lagcomp.before_frame_step)
	_connect_once(_clock.on_tick, _lagcomp.tick_step)
	_connect_once(_clock.after_tick_loop, _lagcomp.frame_step)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.ACTION,
		_lagcomp._handle_action_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_COMMAND,
		_lagcomp._handle_predict_command_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_ACK,
		_lagcomp._handle_predict_ack_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_RELAY,
		_lagcomp._handle_predict_relay_carrier,
	)
	_replication.register_channel(
		NetwFrameEnvelope.Channel.PREDICT_RELAY_REQUEST,
		_lagcomp._handle_predict_relay_request_carrier,
	)


## Returns whether [param peer] passes the liveness and interest send gate.
func sync_admits(peer: int, entity: RID) -> bool:
	var wrapper := _entity_wrapper(entity)
	return _replication.is_live_for(peer, wrapper) if wrapper else false


## Returns every peer currently admitted for [param entity].
func sync_get_recipients(entity: RID) -> PackedInt32Array:
	var wrapper := _entity_wrapper(entity)
	return PackedInt32Array(_replication.live_peers(wrapper)) \
	if wrapper else PackedInt32Array()


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
	return _replication.policy_admits(
		int(policy) as NetwScriptModel.Policy,
		sender,
		node,
		wrapper,
	)


## Sends the local player's control request to server authority.
## [br][br][b]Player request.[/b]
func entity_request_control(entity: RID) -> void:
	var wrapper := _entity_wrapper(entity)
	if wrapper:
		_replication.request_control(wrapper)


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
## This and [method _apply_set] are the only two points where replication
## touches node properties. Everything between them speaks values, never nodes,
## which is what lets the sync core be tested with no scene at all.
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
## [method _gather_set] and reads the same positional order.
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
	var wrapper := _entity_wrapper(entity)
	return _replication.resolve_comp_node(wrapper, comp, "") \
	if wrapper else null

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


## Moves [param entity] into [param destination].
## [br][br][b]Server Only.[/b]
func entity_move_to_scene(
		entity: RID,
		destination: Variant,
		opts: SceneCore.MoveOpts = null,
) -> NetwPromise:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return null
	return _scenes.move(wrapper, destination, opts)


## Hydrates the persisted fields of [param entity].
## [br][br][b]Server Only.[/b]
func persist_hydrate(entity: RID) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	var engine := _persistence.engine_for(wrapper)
	return await engine.hydrate() if engine else ERR_UNCONFIGURED


## Flushes persisted [param keys] from [param entity].
## [br][br][b]Server Only.[/b]
func persist_flush(entity: RID, keys: Array = []) -> Error:
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	var engine := _persistence.engine_for(wrapper)
	return await engine.flush(keys) if engine else ERR_UNCONFIGURED


## Advances the persistence snapshot loop by [param delta] seconds.
##
## The loop is one pass over every persisted entity, and it flushes only the
## subset whose accumulator came due, so the cost of calling this every frame is
## the census and not the write. Clients no-op, because every persistence
## trigger is server-gated.
## [br][br][b]Server Only.[/b]
func persist_tick(delta: float) -> void:
	_persistence.tick(delta)


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
) -> Error:
	if not is_server():
		Netw.dbg.error("NetwMultiplayer.persist_table_flush is server-only")
		return ERR_UNCONFIGURED
	if db == null or into.is_empty():
		return ERR_UNCONFIGURED
	var schema := table_get_schema(table)
	if not schema.is_valid():
		return ERR_DOES_NOT_EXIST
	var routes := table_read_routes(table)
	if ids.size() != routes.size():
		return ERR_INVALID_DATA

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
	@warning_ignore("redundant_await")
	return await db.transaction(
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
## [br][br][b]Server Only.[/b]
func persist_table_hydrate(
		table: RID,
		db: NetwDatabase,
		into: StringName,
) -> Dictionary:
	var out := {
		&"routes": PackedInt64Array(),
		&"ids": PackedStringArray(),
	}
	if not is_server():
		Netw.dbg.error("NetwMultiplayer.persist_table_hydrate is server-only")
		return out
	var schema := table_get_schema(table)
	if db == null or into.is_empty() or not schema.is_valid():
		return out

	var names: Array[StringName] = [&"ids"]
	for column in schema_get_column_count(schema):
		names.append(schema_get_column_key(schema, column))
	db.declare_table(into, names)
	@warning_ignore("redundant_await")
	var record := await db.table(into).fetch(_schema_core.name_of(schema))
	var data := record.to_dict() if record else { }
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
	_persistence.handle_shutdown()

#region Spawn

## Arms [param node] for replicated construction and returns its entity RID.
## [br][br][b]Server Only.[/b]
func replicate(node: Node, owner: NetwParticipant = null) -> RID:
	var wrapper := _replication._spawn_pipeline.replicate(node, owner)
	return wrapper.rid if wrapper else RID()


## Runs and replicates one configured spawn function.
## [br][br][b]Server Only.[/b]
func spawn_fn(
		function: Callable,
		args: Array = [],
		owner: NetwParticipant = null,
) -> RID:
	var node := _replication._spawn_pipeline.spawn(function, args, owner)
	return rid_of(node) if node else RID()


## Registers one host-less spawn constructor.
func spawn_register_constructor(id: StringName, function: Callable) -> void:
	_replication._spawn_pipeline.register_spawn_constructor(id, function)


## Runs and replicates one registered constructor.
## [br][br][b]Server Only.[/b]
func spawn_registered(
		id: StringName,
		args: Array = [],
		owner: NetwParticipant = null,
) -> RID:
	var node := _replication._spawn_pipeline.spawn_registered(id, args, owner)
	return rid_of(node) if node else RID()


## Adopts one already-present node into replication.
## [br][br][b]Server Only.[/b]
func adopt_in_place(root_node: Node) -> RID:
	var wrapper := _replication._spawn_pipeline.adopt_in_place(root_node)
	return wrapper.rid if wrapper else RID()


## Despawns one live entity.
## [br][br][b]Server Only.[/b]
func despawn(entity: RID, opts: NetwEntity.DespawnOpts = null) -> Error:
	if not is_server():
		return ERR_UNAUTHORIZED
	var wrapper := _entity_wrapper(entity)
	if wrapper == null:
		return ERR_DOES_NOT_EXIST
	wrapper.despawn(opts)
	return OK


## Returns one entity subtree's authored spawn state contribution.
func spawn_get_state(entity: RID) -> Array[Dictionary]:
	var node := entity_get_node(entity)
	return _replication._spawn_pipeline._collect_spawn_state(node) \
	if node else []


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
	_spawn_reconcile_plan = NetwSpawnReconciler.reconcile(
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

# Aggregation packet counters
var _sent_packets: int = 0
var _sent_bytes: int = 0
var _received_packets: int = 0
var _received_bytes: int = 0

var _frame_counter: int = 0
var _last_poll_usec: int = 0

# The settle queue. Rows are { key: StringName, fn: Callable }. An unkeyed row
# carries an empty key and never coalesces. Drained at the pump by _settle.
var _settle_queue: Array[Dictionary] = [ ]
# The drain's fixed-point bound. An effect that schedules an effect is legal
# and runs in the same drain. One that schedules forever must present as an
# error rather than as a hang, and eight passes is far past any real cascade.
const _SETTLE_PASSES := 8

# The sender of the carrier frame currently being dispatched, or 0 when no
# relayed dispatch is on the stack. ReplicationCore._dispatch stamps it
# so _get_remote_sender_id answers with the frame's sender for handlers reached
# through the carrier, the same value a native @rpc handler would read. Nested
# dispatch saves and restores it.
var _relay_sender: int = 0

# Outbound unreliable datagram sequence, one u16 counter per destination peer.
# Every unreliable datagram carries the next value so the receiver can drop
# state that a fresher datagram already superseded. Reliable datagrams are
# ordered by the transport and carry none.
var _unreliable_send_seqs: Dictionary = { }

# The freshest inbound unreliable datagram seq seen from each sender, peer -> u16.
# An outbound unreliable datagram to a peer this map knows echoes this value in
# the acked shape, telling that peer the newest datagram of theirs we hold.
var _inbound_freshest_seq: Dictionary = { }

# The freshest seq each peer has echoed back for our own sends, peer -> u16. This
# is the transport ack: it names the newest datagram of ours that peer provably
# holds, the conservative baseline every delta-against-baseline lane diffs against
# (masked state, masked broadcast). It flows consumer to author per peer-pair.
# Distinct from the consumption ack the SYNC_FLAG_ACKED bit carries, which names
# the input tick the server simulated, not a datagram it received.
var _peer_state_acks: Dictionary = { }

# The freshest inbound seq we have echoed back to each peer, peer -> u16. Compared
# against _inbound_freshest_seq after a tick's flush so a peer we hold fresh state
# for but sent no piggybacked echo to this pass gets a standalone acked datagram.
# This keeps the transport ack unconditional for a non-reciprocal masked flow, a
# client author broadcasting to a silent observer that sends it no datagram to
# piggyback on.
var _last_echoed_seq: Dictionary = { }

# Acked-shape datagram counters, free delivery observability.
var _state_acks_out: int = 0
var _state_acks_in: int = 0
# Of the acked-shape datagrams sent, those that carried no piggyback (a standalone
# transport ack this tick's flush emitted).
var _standalone_acks_out: int = 0

# Gate verdict totals keyed by Godot Error value. OK and local configuration
# failures are not remote-input verdicts and never enter this book.
var _verdict_counts := {
	ERR_DOES_NOT_EXIST: 0,
	ERR_SKIP: 0,
	ERR_UNAVAILABLE: 0,
	ERR_UNAUTHORIZED: 0,
	ERR_INVALID_DATA: 0,
	ERR_BUSY: 0,
}
var _warned_verdict_routes: Dictionary[StringName, bool] = { }


## Sends one framed carrier datagram to [param peer_id], prefixed with the
## [NetwFrameEnvelope] magic byte for its transfer mode. An unreliable datagram
## additionally carries a per-peer [code]u16[/code] sequence after the magic
## byte, the freshness stamp [NetwSyncPipeline] gates unreliable entity frames
## on. Aggregated sends arrive here from
## [method ReplicationCore.send_to]. Returns the assigned unreliable
## seq, or [code]-1[/code] for a reliable send or a dropped/empty packet, so a
## caller staging masked-delta rows can key them by the seq that will
## carry their acknowledgment.
func _send_packet(peer_id: int, bytes: PackedByteArray, reliable: bool) -> int:
	if bytes.is_empty():
		return -1
	# A peer that left mid-poll can still sit in a recipient list drawn from
	# liveness books that trail the connection by a cleanup signal. Native
	# send_bytes treats a departed target as a bug, so the carrier drops it here.
	if peer_id != 0 and peer_id not in inner.get_peers():
		return -1
	_sent_packets += 1
	_sent_bytes += bytes.size()
	var framed := PackedByteArray()
	var assigned_seq := -1
	if reliable:
		framed.resize(1)
		framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE
	else:
		var seq: int = (int(_unreliable_send_seqs.get(peer_id, 0)) + 1) & 0xFFFF
		_unreliable_send_seqs[peer_id] = seq
		assigned_seq = seq
		if _inbound_freshest_seq.has(peer_id):
			# We have heard from this peer, so echo the newest datagram of theirs
			# we hold in the acked shape: [magic | seq u16 | ack u16 | frames].
			framed.resize(5)
			framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED
			framed.encode_u16(1, seq)
			framed.encode_u16(3, int(_inbound_freshest_seq[peer_id]))
			_state_acks_out += 1
			# This real datagram already carried the echo, so the end-of-tick
			# standalone pass owes this peer nothing.
			_last_echoed_seq[peer_id] = int(_inbound_freshest_seq[peer_id])
		else:
			framed.resize(3)
			framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE
			framed.encode_u16(1, seq)
	framed.append_array(bytes)
	var transfer_mode := MultiplayerPeer.TRANSFER_MODE_RELIABLE \
	if reliable else MultiplayerPeer.TRANSFER_MODE_UNRELIABLE
	inner.send_bytes(
		framed,
		peer_id,
		transfer_mode,
	)
	return assigned_seq


# Sinks one raw packet through the carrier intake verb.
func _on_inner_peer_packet(id: int, packet: PackedByteArray) -> void:
	_sink_verdict(_receive_inner_packet(id, packet), 0)


# Demuxes Networked carrier packets from application byte traffic.
func _receive_inner_packet(id: int, packet: PackedByteArray) -> Error:
	if packet.is_empty():
		return ERR_INVALID_DATA
	var magic := packet[0]
	if magic == NetwFrameEnvelope.CARRIER_MAGIC_RELIABLE:
		_received_packets += 1
		_received_bytes += packet.size() - 1
		return _drive_carrier(id, packet.slice(1), true)
	if magic == NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE:
		# The u16 after the magic is the datagram's freshness stamp. A packet
		# too short to carry it is not valid Networked framing.
		if packet.size() < 3:
			return ERR_INVALID_DATA
		_received_packets += 1
		_received_bytes += packet.size() - 3
		var seq := packet.decode_u16(1)
		_note_inbound_seq(id, seq)
		return _drive_carrier(id, packet.slice(3), false, seq)
	if magic == NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED:
		# The acked shape carries the freshness u16 then the echo u16 of the
		# newest datagram of ours this peer holds, before the frames.
		if packet.size() < 5:
			return ERR_INVALID_DATA
		_received_packets += 1
		_received_bytes += packet.size() - 5
		_state_acks_in += 1
		var seq := packet.decode_u16(1)
		_note_inbound_seq(id, seq)
		_note_state_ack(id, packet.decode_u16(3))
		return _drive_carrier(id, packet.slice(5), false, seq)
	peer_packet.emit(id, packet)
	return OK


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
	if verdict == OK:
		return
	_count_gate_verdict(verdict)
	_emit_verdict_channel(verdict, route)


# Emits the warning or error policy without changing the verdict counter.
func _emit_verdict_channel(verdict: Error, route: int) -> void:
	if verdict == OK:
		return
	if verdict in [
		ERR_UNAUTHORIZED,
		ERR_INVALID_DATA,
		ERR_BUSY,
	]:
		var key := StringName("%d:%d" % [verdict, route])
		if _warned_verdict_routes.has(key):
			return
		_warned_verdict_routes[key] = true
		Netw.dbg.warn(
			"NetwMultiplayer: rejected carrier input with error %d on route %d",
			[verdict, route],
		)
	elif not _verdict_counts.has(verdict):
		Netw.dbg.error(
			"NetwMultiplayer: carrier sink failed with error %d",
			[verdict],
		)


# Counts a gate verdict and applies its warning policy once per route.
func _warn_gate_verdict(
		verdict: Error,
		route: int,
		message: String,
		args: Array = [],
) -> void:
	_count_gate_verdict(verdict)
	var key := StringName("%d:%d" % [verdict, route])
	if _warned_verdict_routes.has(key):
		return
	_warned_verdict_routes[key] = true
	Netw.dbg.warn(message, args)


# Records [param seq] as the freshest inbound datagram from [param sender] when it
# is newer across the u16 half window, so a reordered datagram never rolls the
# echo backward.
func _note_inbound_seq(sender: int, seq: int) -> void:
	if not _inbound_freshest_seq.has(sender) \
			or _seq_is_fresher(seq, int(_inbound_freshest_seq[sender])):
		_inbound_freshest_seq[sender] = seq


# Records [param ack] as [param peer]'s confirmation of our sends when it is newer
# across the u16 half window. The confirmed seq only advances, so a stalled echo
# from a silent peer holds its baseline rather than corrupting it. Advancing it
# promotes peer's masked-lane in-flight rows through the pipeline; a
# stalled ack (this branch not taken) correctly leaves those rows untouched.
func _note_state_ack(peer: int, ack: int) -> void:
	if not _peer_state_acks.has(peer) \
			or _seq_is_fresher(ack, int(_peer_state_acks[peer])):
		_peer_state_acks[peer] = ack
		_note_ack(peer, ack)


# Returns true when [param a] is fresher than [param b] on the u16 sequence ring,
# judged across the half window so wraparound stays correct.
static func _seq_is_fresher(a: int, b: int) -> bool:
	return a != b and ((a - b) & 0xFFFF) < 32768


## Returns the freshest datagram seq [param peer] has echoed as held, or
## [code]-1[/code] when that peer has acked nothing. The masked delta lane reads
## this as each recipient's confirmed baseline seq.
func _peer_state_ack(peer: int) -> int:
	return int(_peer_state_acks.get(peer, -1))


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
	for peer_id in _inbound_freshest_seq:
		var fresh := int(_inbound_freshest_seq[peer_id])
		if _last_echoed_seq.get(peer_id) == fresh:
			continue
		if peer_id != 0 and peer_id not in inner.get_peers():
			continue
		_send_standalone_ack(peer_id, fresh)


# Sends a zero-frame acked datagram to [param peer_id] carrying [param ack], the
# freshest inbound seq of theirs we hold. The acked shape's [magic | seq | ack]
# header round-trips through the receive path with no frames to dispatch
# (receive_carrier no-ops on the empty remainder), so this is the standalone form
# of the echo send_packet piggybacks on a real datagram. It bypasses that path's
# empty-payload drop deliberately: the whole point is a datagram with no payload.
func _send_standalone_ack(peer_id: int, ack: int) -> void:
	var seq: int = (int(_unreliable_send_seqs.get(peer_id, 0)) + 1) & 0xFFFF
	_unreliable_send_seqs[peer_id] = seq
	var framed := PackedByteArray()
	framed.resize(5)
	framed[0] = NetwFrameEnvelope.CARRIER_MAGIC_UNRELIABLE_ACKED
	framed.encode_u16(1, seq)
	framed.encode_u16(3, ack)
	_last_echoed_seq[peer_id] = ack
	_state_acks_out += 1
	_standalone_acks_out += 1
	_sent_packets += 1
	_sent_bytes += framed.size()
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
	_settle()
	return err


# The tick a received payload is stamped with: the session tick when the clock
# engine is configured, otherwise a local frame counter.
func _receive_tick() -> int:
	return _clock.tick if _clock.is_configured() else _frame_counter


# Counts and returns one hostile-input verdict.
func _count_gate_verdict(verdict: Error) -> Error:
	if verdict != OK and _verdict_counts.has(verdict):
		_verdict_counts[verdict] += 1
	return verdict


# Counts one gate verdict and applies its fixed output policy.
func _finish_gate_verdict(verdict: Error, route: int) -> Error:
	_count_gate_verdict(verdict)
	_emit_verdict_channel(verdict, route)
	return verdict


# Returns the shared route and liveness verdict for an entity frame.
func _entity_frame_verdict(route: int) -> Error:
	match _liveness.route_state(route):
		LivenessShell.State.UNKNOWN:
			return ERR_DOES_NOT_EXIST
		LivenessShell.State.LINGERING, \
		LivenessShell.State.DEAD:
			return ERR_SKIP
	var entity := _liveness.entity_of(route)
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
## as [constant NetwFrameEnvelope.Channel.SYNC] or
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
	var verdict := OK
	if sender != 1:
		verdict = ERR_UNAUTHORIZED
	elif channel not in [
		NetwFrameEnvelope.Channel.SPAWN,
		NetwFrameEnvelope.Channel.DESPAWN,
		NetwFrameEnvelope.Channel.REPARENT,
	]:
		verdict = ERR_INVALID_DATA
	elif payload.is_empty():
		verdict = ERR_INVALID_DATA
	var _wire_route := route
	return verdict


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
	if channel != NetwFrameEnvelope.Channel.TABLE:
		return ERR_INVALID_DATA
	if sender != 1:
		_table_core.count_bad_sender()
		return ERR_UNAUTHORIZED
	if payload.is_empty():
		return ERR_INVALID_DATA
	return _table_core.admit_header(TableCore.peek_header(payload))


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
	var entity := _liveness.entity_of(route)
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
## [codeblock]
## Dictionary
## ┠╴label : int                     the tick this transition is filed under
## ┠╴fresh : bool                    true when a newer input drove it
## ┖╴kind  : NetwPredict.DriveKind   FRESH or REPEAT
## [/codeblock]
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
) -> Dictionary:
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
## [codeblock]
## Dictionary
## ┠╴action : NetwPredict.ConsumeAction   REPLAY, HOLD, or STARVED
## ┖╴warmed : bool                        the latch, carried to the next pass
## [/codeblock]
## [param depth] is how many transitions are queued and [param warmed] is the
## previous pass's latch.
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
func _predict_consume(depth: int, buffer: int, warmed: bool) -> Dictionary:
	return NetwPredictionCore.consume_plan(depth, buffer, warmed)


## Judges one acknowledged transition against the owner's prediction of it.
##
## Which comparison runs is the transition's own [param domain], not a setting.
## A transition whose antecedents were all declared equal has no tolerance to
## spend, so any inequality is a divergence. One whose antecedents were not is
## compared by tolerance instead, since the peers never claimed the exactness a
## fingerprint would test for.
## [codeblock]
## Dictionary
## ┠╴divergence : float   magnitude of the disagreement, INF when unjudged
## ┠╴corrected  : bool    true when a recovery must be staged
## ┖╴in_domain  : bool    whether the transition claimed reproducibility
## [/codeblock]
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
) -> Dictionary:
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
## [codeblock]
## Dictionary
## ┠╴restore  : Dictionary   the payload to apply, by property name
## ┠╴write    : Dictionary   what the display is told moved
## ┠╴teleport : bool         the body kept nothing worth blending from
## ┖╴skip     : bool         this recovery declines to write at all
## [/codeblock]
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
) -> Dictionary:
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
	result[&"pending_live"] = _liveness.pending_live_count()

	var interest_stats := _interest.monitor_snapshot()
	result[&"interest_layers"] = interest_stats[&"layers"]
	result[&"interest_entities_filtered"] = (
			interest_stats[&"entities_filtered"]
	)
	result[&"interest_visible_edges"] = interest_stats[&"visible_edges"]
	result[&"interest_dirty_entities"] = interest_stats[&"dirty_entities"]
	result[&"interest_relay_backlog"] = interest_stats[&"relay_backlog"]
	result[&"interest_transitions_total"] = (
			interest_stats[&"transitions_total"]
	)
	result[&"interest_vanished_dirty_skips"] = (
			interest_stats[&"vanished_dirty_skips"]
	)

	result.merge(_table_core.counters())

	var predict_stats := lagcomp_metrics()
	result[&"predict_entities"] = predict_stats[&"entities"]
	result[&"predict_timelines"] = predict_stats[&"timelines"]
	result[&"predict_corrections"] = predict_stats[&"corrections"]
	result[&"predict_max_replay_depth"] = predict_stats[&"max_replay_depth"]
	result[&"predict_consumed"] = predict_stats[&"consumed"]
	result[&"predict_missing"] = predict_stats[&"missing"]
	result[&"predict_pending_actions"] = predict_stats[&"pending_actions"]
	result[&"predict_effects_armed"] = predict_stats[&"effects_armed"]
	result[&"predict_gate_fallbacks"] = predict_stats[&"gate_fallbacks"]

	var joint_stats: Dictionary = predict_stats[&"joint"]
	result[&"joint_passes"] = joint_stats[&"joint_passes"]
	result[&"joint_members"] = joint_stats[&"joint_members"]
	result[&"joint_cells_relayed"] = joint_stats[&"cells_relayed"]
	result[&"joint_cells_substituted"] = joint_stats[&"cells_substituted"]
	result[&"joint_heal_snaps"] = joint_stats[&"heal_snaps"]
	result[&"joint_linger_held"] = joint_stats[&"linger_held"]

	var display_stats := _display._stats_snapshot()
	result[&"display_runtimes"] = display_stats[&"runtimes"]
	result[&"display_starving"] = display_stats[&"starving"]
	result[&"display_sleeping"] = display_stats[&"sleeping"]
	result[&"display_projecting"] = display_stats[&"projecting"]
	result[&"display_snaps"] = display_stats[&"snaps"]
	result[&"display_max_display_lag"] = display_stats[&"max_display_lag"]
	result[&"display_max_forecast_age"] = (
			display_stats[&"max_forecast_age"]
	)
	result[&"verdict_does_not_exist"] = (
			_verdict_counts[ERR_DOES_NOT_EXIST]
	)
	result[&"verdict_skip"] = _verdict_counts[ERR_SKIP]
	result[&"verdict_unavailable"] = _verdict_counts[ERR_UNAVAILABLE]
	result[&"verdict_unauthorized"] = _verdict_counts[ERR_UNAUTHORIZED]
	result[&"verdict_invalid_data"] = _verdict_counts[ERR_INVALID_DATA]
	result[&"verdict_busy"] = _verdict_counts[ERR_BUSY]

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
		&"masked_frames_out": repl[&"masked_frames_out"],
		&"masked_frames_full": repl[&"masked_frames_full"],
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
		&"sent_packets": _sent_packets,
		&"sent_bytes": _sent_bytes,
		&"received_packets": _received_packets,
		&"received_bytes": _received_bytes,
		&"state_acks_out": _state_acks_out,
		&"state_acks_in": _state_acks_in,
		&"standalone_acks_out": _standalone_acks_out,
	}


# Drops all per-session state so the tick pump has nothing to touch after the
# session tears down. Deferred to avoid mutating registries mid-teardown,
# mirroring LivenessShell.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


func _clear_session_state() -> void:
	_unreliable_send_seqs.clear()
	_inbound_freshest_seq.clear()
	_peer_state_acks.clear()
	_last_echoed_seq.clear()
	_replication.clear_session()
	_rpc_core.clear_session()
	for verdict in _verdict_counts:
		_verdict_counts[verdict] = 0
	_warned_verdict_routes.clear()
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
	_layer_records.clear()
	_layer_by_name.clear()
	_layer_ledger.clear()
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
	_unreliable_send_seqs.erase(peer_id)
	_inbound_freshest_seq.erase(peer_id)
	_peer_state_acks.erase(peer_id)
	_last_echoed_seq.erase(peer_id)
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
## [constant SessionCore.State.ONLINE] with its role resolved. Pairs
## with [signal session_ended].
signal session_entered()
## Emitted when the session leaves
## [constant SessionCore.State.ONLINE]. Pairs with
## [signal session_entered].
signal session_ended()
## Emitted on every [member state] edge, including the ones
## [signal session_entered] and [signal session_ended] do not cover
## (offline to connecting, and a connect that fails before it is online).
signal state_changed(old_state: SessionState, new_state: SessionState)
# The session's DisplayCore, pumped every frame from the session poll. Owned
# for the session lifetime, so it needs no scene anchor.
var _display: DisplayCore


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
	service_registered.emit(service)


## Unregisters [param service]. Fires [signal service_unregistered].
func unregister_service(service: Node, type: Script = null) -> void:
	_services.unregister(service, type)
	service_unregistered.emit(service)

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
	_participants.erase(peer_id)


## Clears the connected-peer roster and every participant handle. Called during
## session teardown so a same-session re-host starts from an empty roster.
func clear_roster() -> void:
	_roster.clear()
	_participants.clear()

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
	if not _participants.has(peer_id):
		_participants[peer_id] = NetwParticipant.new(self, peer_id)
	return _participants[peer_id]

## Every connected peer as a roster row, joined or not.
##
## A row exists from the moment a peer connects. The join frame enriches it with
## a [ResolvedJoin], which is what promotes the row into [member participants]
## and [method peer_get_participant]. This is the whole roster, the un-joined
## observers included.
var connected_participants: Array[NetwParticipant]:
	get:
		var result: Array[NetwParticipant] = []
		for peer_id: int in _participants:
			result.append(_participants[peer_id])
		return result


# Opens a roster row for a freshly connected peer. The row carries only the peer
# id until a join frame enriches it, so an un-joined peer is still a known row.
func _ensure_participant_row(peer_id: int) -> void:
	if not _participants.has(peer_id):
		_participants[peer_id] = NetwParticipant.new(self, peer_id)

## Whether this session is live.
##
## The session machine owns the fact: transport edges already drive
## [method SessionCore.transition], so this answers what the transport answers,
## one hop later. A caller that genuinely needs the raw transport window reads
## [member MultiplayerAPI.multiplayer_peer] and says so.
var is_online: bool:
	get:
		return state == SessionState.ONLINE

## The current connection state.
var state: SessionState:
	get:
		return _session.state as SessionState

## The current role in the session.
var role: Role:
	get:
		return _session.role as Role

## Whether the local peer hosts the session, as either a listen or a dedicated
## server. Mirrors [member MultiplayerTree.is_host] but resolves through the
## session, so a root-installed session with no owning [MultiplayerTree] still
## answers.
var is_host: bool:
	get:
		return role == Role.DEDICATED_SERVER or role == Role.LISTEN_SERVER

## Whether the local peer plays a client, including a listen-server host that is
## also its own client. Mirrors [member MultiplayerTree.is_local_client] but
## resolves through the session, so a root-installed session with no owning
## [MultiplayerTree] still answers.
var is_local_client: bool:
	get:
		return role == Role.CLIENT or role == Role.LISTEN_SERVER

## The local player identity for this session, or [code]null[/code].
##
## Tracked off the liveness bus: the represented entity is the one whose route
## goes live carrying the local peer id, cleared when that route dies. Riding
## the bus rather than a per-registration write drops the clear-and-reset
## flicker a reparent used to cause, since a reparent keeps the route live and
## never emits [signal LivenessShell.entity_dead].
##
## [signal local_player_changed] fires whenever this member changes.
var local_player: NetwEntity:
	set(value):
		if local_player == value:
			return
		local_player = value
		local_player_changed.emit(value)

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	get:
		return peer_get_participant(get_unique_id())


# Adopts a newly live entity as local_player when it represents the local peer.
# A session-less peer (no multiplayer_peer) has no local player, matching the
# represented-peer test that treats a null peer as not-local.
func _on_liveness_entity_live(route: int, entity: NetwEntity) -> void:
	entity_live.emit(route, entity)
	if not has_multiplayer_peer():
		return
	if entity.peer_id == 0 or entity.peer_id != get_unique_id():
		return
	local_player = entity


func _on_liveness_entity_lingering(route: int, entity: NetwEntity) -> void:
	entity_lingering.emit(route, entity)


func _on_liveness_entity_dead(route: int) -> void:
	entity_dead.emit(route)
	if local_player and local_player.route == route:
		local_player = null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var mt := root as MultiplayerTree
	if not mt:
		return SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)


## Disconnects [param peer_id] from the session.
##
## If [param reason] is non-empty, the peer receives [signal kicked] before
## the connection is closed.
## [br][br][b]Server Only.[/b]
func peer_kick(peer_id: int, reason: String = "") -> void:
	_session.kick(peer_id, reason)


## Asks the server to kick [param peer_id].
##
## The server emits [signal kick_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func peer_request_kick(peer_id: int, reason: String = "") -> void:
	_session.request_kick(peer_id, reason)

#endregion

# Schedules [param fn] to run at the next settle, which is inside the pump this
# session already runs. A named [param key] coalesces: scheduling a key that is
# already queued moves it to the back rather than queueing it twice, which is
# what "run after the cascade that scheduled me" means and what a per-site
# scheduled flag used to spell for itself. An empty key never coalesces, so
# unkeyed effects run in enqueue order.
func _settle_schedule(fn: Callable, key: StringName = &"") -> void:
	if key != &"":
		_settle_cancel(key)
	_settle_queue.append({ "key": key, "fn": fn })


# Withdraws a queued key, for a caller that did the work on the spot and has
# nothing left to settle. Unqueued keys are not an error.
func _settle_cancel(key: StringName) -> void:
	if key == &"":
		return
	for index in range(_settle_queue.size() - 1, -1, -1):
		if _settle_queue[index]["key"] == key:
			_settle_queue.remove_at(index)


# Drains the settle queue to a fixed point. Each pass takes the whole queue and
# runs it, so an effect scheduled during a pass runs in the next one, inside the
# same drain. Exceeding the pass bound names the keys still pending and clears
# them: a cycle is a defect, and a defect that hangs is worse than one that
# reports.
func _settle() -> void:
	var passes := 0
	while not _settle_queue.is_empty():
		if passes >= _SETTLE_PASSES:
			var pending := PackedStringArray()
			for row: Dictionary in _settle_queue:
				pending.append(String(row["key"]) if row["key"] != &"" else "<unkeyed>")
			_settle_queue.clear()
			push_error(
				"Settle did not reach a fixed point in %d passes, still pending: %s"
				% [_SETTLE_PASSES, ", ".join(pending)],
			)
			return
		passes += 1
		var batch := _settle_queue
		_settle_queue = [ ]
		for row: Dictionary in batch:
			var fn: Callable = row["fn"]
			if fn.is_valid():
				fn.call()


func _poll() -> Error:
	# This runs once per idle frame, so the wall-clock gap since the last one is
	# that frame's delta. It is read up front because the peer view is pumped
	# with it, and that pump has to land before the transport reads.
	var now_usec := Time.get_ticks_usec()
	var frame_delta := float(now_usec - _last_poll_usec) / 1_000_000.0 \
	if _last_poll_usec > 0 else 0.0
	_last_poll_usec = now_usec
	# Ahead of the transport read, and the order is load-bearing: a view-pumped
	# carrier delivers its queued packets on this signal, so reading the
	# transport first would see every one of them a frame late.
	poll_started.emit(frame_delta)
	var err := _embedding.poll_transport()
	# After intake and before the outbound flush, and both halves are contract.
	# An inbound frame that schedules an effect settles in the pump that read it,
	# and anything the settle queues for the wire leaves with this pump rather
	# than a frame later.
	_settle()
	_clock.mark_poll()
	_clock.poll_step()
	_frame_counter += 1
	_replication.on_poll()
	_sink_verdict(_display.pump(frame_delta), 0)
	return err


# Intercepts native @rpc dispatch. A call on a node inside a live NetwEntity is
# upgraded to a route-addressed RpcCore call, so it inherits liveness
# gating and interest-scoped fan-out and never races the target's spawn edge the
# way a NodePath-addressed native RPC does. Every other call, including
# session-lifecycle RPCs on nodes outside any entity, rides inner unchanged.
func _rpc(peer: int, object: Object, method: StringName, args: Array) -> Error:
	if object is Node:
		var entity := NetwEntity.of(object)
		if entity and _liveness.route_of(entity) > 0:
			_rpc_core.rpc_call(Callable(object, method), args, peer)
			return OK
	return inner.rpc(peer, object, method, args)


# The registration verb for every declaration node. Typed Networked configs
# install into this session, while engine configurations forward to inner.
func _object_configuration_add(object: Object, configuration: Variant) -> Error:
	if configuration is NetwObjectConfig:
		var installed := service_install(configuration as NetwObjectConfig)
		if installed == OK and configuration is NetwClockConfig \
				and object is MultiplayerClock:
			_clock._attach_node(object as MultiplayerClock)
		return installed
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
	if configuration is NetwClockConfig:
		_clock._configured = false
		return OK
	if configuration is NetwLagCompensationConfig:
		_lagcomp._configured = false
		_lagcomp._close_tap()
		return OK
	if configuration is NetwSessionConfig:
		_session.deconfigure()
		return OK
	if configuration is NetwSceneConfig:
		_scenes.deconfigure()
		return OK
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
	_session.on_peer_assigned(p_peer)


func _get_multiplayer_peer() -> MultiplayerPeer:
	return _native_core.get_multiplayer_peer()


func _get_unique_id() -> int:
	return _native_core.get_unique_id()


func _get_peer_ids() -> PackedInt32Array:
	_native_core.set_peer_ids(inner.get_peers())
	return _native_core.get_peer_ids()


func _get_remote_sender_id() -> int:
	# A carrier frame under dispatch answers with its own sender, so a handler
	# reached through the carrier reads the same value a native @rpc handler
	# would. Peer ids are always positive, so 0 means no relayed dispatch.
	if _relay_sender != 0:
		return _relay_sender
	return inner.get_remote_sender_id()
