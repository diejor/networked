## The per-entity prediction kernel and the wire layouts its two lanes carry.
##
## One engine exists per predicting [NetwEntity]. It reads its configuration and
## writes its evidence through the entity's [NetwPredictionHandle], and owns the
## predicted timeline, the consume cursors and the reconciliation math. Nothing
## here reads a clock or decides an order: the shell steps every engine in a
## deterministic order and hands each pass its own timing.
##
## [codeblock]
## LagCompCore     the shell: clock, ordering, rewind, actions
##   \- _SimulationRunner           steps each engine, in entity-id order
##        \- _PredictionEngine      one per entity, this file
##             \- NetwPredictionHandle   the entity's stable config and evidence
## [/codeblock]
extends RefCounted

# A restore stated whole before it is performed: the payload to write and the
# provenance the journal owes for it, so the decision to restore is separable
# from the write that carries it out.
class _StagedRestore extends RefCounted:
	var payload: Dictionary
	var operator: NetwPredictJournal.Operator
	var basis: int
	var provenance_owner: _PredictionEngine
	var evidence_free: bool
	# Whether the pool planned this write and so already charged it to the
	# episode. Charging it a second time would double the operator series the
	# structural budget is spent out of.
	var pool_planned: bool

	func _init(
			p_payload: Dictionary,
			p_operator: NetwPredictJournal.Operator,
			p_basis: int,
			p_provenance_owner: _PredictionEngine,
			p_evidence_free: bool,
			p_pool_planned: bool = false,
	) -> void:
		payload = p_payload
		operator = p_operator
		basis = p_basis
		provenance_owner = p_provenance_owner
		evidence_free = p_evidence_free
		pool_planned = p_pool_planned


	func is_valid() -> bool:
		return basis >= -1 \
				and operator >= NetwPredictJournal.Operator.NONE \
				and operator <= NetwPredictJournal.Operator.JOINT_REBASE


# One re-run of an authored entry: the tape entry it closes, the timeline label
# it reads, and the input the re-run drives with.
class _ReplayStep extends RefCounted:
	var index: int
	var label: int
	var input: Dictionary

	func _init(p_index: int, p_label: int, p_input: Dictionary) -> void:
		index = p_index
		label = p_label
		input = p_input


# Every entry a restore left unacknowledged, stated whole before any of it runs,
# and the live input the replay restores when it ends.
class _ReplayPlan extends RefCounted:
	var steps: Array[_ReplayStep] = []
	var live_input: Dictionary

	func _init(p_live_input: Dictionary) -> void:
		live_input = p_live_input


	# The tape is ordered and a replay re-runs each entry once, so an index that
	# does not advance is a plan that would re-run or reorder a transition.
	func is_valid() -> bool:
		var previous_index := -1
		for step: _ReplayStep in steps:
			if step.index < 0 or step.index <= previous_index:
				return false
			previous_index = step.index
		return true


# What one attempt at a carry found, which is the whole of the evidence the
# pool's judgement reads. Absent evidence is not a refusal: a rule whose
# recorded transitions are missing was never invoked, so nothing about it has
# been observed and the pool is told that rather than handed a verdict.
class _CarryAttempt extends RefCounted:
	var value: Variant = null
	var evidence: bool = true
	var same_type: bool = true
	var finite: bool = true
	var within_envelope: bool = true
	var pure: bool = true
	var faithful: bool = true
	var residual: String = ""


## What a session resolved about one entity's declarations, as one record an
## engine is handed.
##
## Both set handles resolve through the replication registry and the liveness
## route, and both axes read this peer's authority, so an engine that reached
## for them could only ever be wired by a live session. It is given one of these
## instead, which is what lets a rig drive the wiring path a session drives.
## [codeblock]
## engine.rewire(shell.declaration_of(entity))  # adopt a resolved model
## engine.rewire()                              # re-resolve, or keep the model
## [/codeblock]
class Declaration:
	extends RefCounted

	## The state set handle the engine gathers and reconciles through. Null
	## leaves the engine unwired, which is what a routeless entity resolves to.
	var state: NetwPropertySetBinding

	## The input set handle the engine records and sends through. Null leaves
	## the engine unwired.
	var input: NetwPropertySetBinding

	## Whether this peer holds authority, the axis separating the roles that
	## consume a received command from the roles that author one.
	var authority: bool = false

	## Whether this peer authors the entity's commands.
	## [member NetwEntity.is_controlled_locally] answers false for an owner with
	## no multiplayer peer, so a rig declares this rather than deriving it.
	var controlled_locally: bool = false

	## The authoritative history a server role reads. A session's registry owns
	## one per routed entity and answers for it, so this is what a rig with no
	## registry declares in its place.
	var timeline: NetwTimeline


class _PredictionEngine extends RefCounted:
	const TAPE_HISTORY_LIMIT := 256
	# Where an entity with no route yet falls in the pass. Larger than any
	# route a session mints, so it sorts after every routed entity.
	const _UNROUTED_ORDER_KEY := 1 << 62
	# The owner may speculate through at most one quarter of the history ring.
	# At the standard 60 Hz network rate this is about one second, leaving three
	# quarters of the ring for acknowledgement and recovery evidence.
	# Acknowledgement records repeated per send. The lane is unreliable, so it
	# re-sends what the owner has not confirmed, but an owner whose confirmation
	# never arrives must not grow the frame without bound. Its own
	# acknowledgement floor advances the moment any frame lands.
	const ACK_WINDOW_MAX := 64
	# Top buckets for the consume-cadence histograms. Anything past the cap
	# lands in the cap's bucket, so the dictionaries stay bounded whatever the
	# session does.
	const ARRIVAL_BUCKET_MAX := NetwPredictStats.ARRIVAL_BUCKETS - 1
	const REPLAY_DEPTH_BUCKET_MAX := NetwPredictStats.REPLAY_DEPTH_BUCKETS - 1
	const RAW_FP_ENV := "NETW_PREDICT_RAW_FP"
	const STATE_FAMILY_POSE := 0
	const STATE_FAMILY_MOMENTUM := 1
	const STATE_FAMILY_CONTROLLER := 2
	# Consecutive non-shrinking recoveries that promote the next one to a full
	# closure. Fixed rather than configurable: convergence is a guarantee the
	# kernel owes at every configuration, not a knob whose wrong value can
	# forfeit it.
	const ESCALATE_NONSHRINK := 3
	# Physics steps one transition may report before the count saturates. A peer
	# that spent this many steps on one transition is not slightly out of step,
	# and the exact number stops the fingerprint taking a fresh value per stall.
	const QUANTUM_STEPS_MAX := 15
	# How the fields past their own epsilon stood when a write was judged. Only
	# ALL_WITHHELD is budget-neutral; see _trigger_shape.
	const TRIGGER_NONE := 0
	const TRIGGER_MIXED := 1
	const TRIGGER_ALL_WITHHELD := 2
	# A clean proof spans two measured acknowledgement ages, floored at three
	# distinct authority rows and capped below the evidence ring.
	const QUARANTINE_RUN_CAP := ACK_WINDOW_MAX * 4
	# The reported floor sources, as the pool numbers them.
	const JOINT_FLOOR_SOURCES := {
		&"ack": NetwPredictionEngine.JOINT_FLOOR_ACK,
		&"state": NetwPredictionEngine.JOINT_FLOOR_STATE,
		&"relay": NetwPredictionEngine.JOINT_FLOOR_RELAY,
		&"epoch": NetwPredictionEngine.JOINT_FLOOR_EPOCH,
	}

	var _iface_ref: WeakRef
	var _entity: NetwEntity
	var _handle: NetwPredictionHandle
	# The implementation every decision on this engine is taken through. The
	# stock one until a shell hands over the one a game installed, so an engine
	# built without a shell -- which is every kernel-altitude rig -- still
	# decides rather than crashing on a null.
	var _kernel: NetwMultiplayer
	var _role: NetwPredict.Role = NetwPredict.Role.REMOTE
	var _correction: NetwPredict.CorrectionMode = \
			NetwPredict.CorrectionMode.REPLAY
	# The last declaration model handed in, which is what a shell-less engine
	# re-wires on.
	var _declaration := Declaration.new()
	var _state_binding: NetwPropertySetBinding
	var _input_binding: NetwPropertySetBinding
	var _timeline: NetwTimeline
	# The step a replay re-runs at, adopted from whatever timing the last pass
	# carried. The engine holds no clock of its own so a kernel cannot find one.
	var _tick_delta: float = 1.0 / 60.0
	var _registered: bool = false

	# Field key -> velocity field key, the derivative pairs an EXTRAPOLATED snap
	# restore projects forward. Built once per rewire from the state set specs,
	# empty when no field declares a project_channel present in the set.
	# The fields the recovery reads as the body's POSE: the ones it can advance
	# to the present, so the teleport tier and transport both measure over values
	# they are able to land at now rather than at the acknowledgement.
	#
	# Held apart from _wiring.projection on purpose. That map answers "what
	# carries this field forward" and this set answers "which fields is the pose
	# error made of", and the two only coincide while the sole forward model is a
	# declared derivative channel. A field that gains a forward model does not
	# thereby become part of the pose the teleport tier measures, and the reverse
	# is equally false, so a caller has to say which question it is asking.
	#
	# Not the same set as the STATE_FAMILY_POSE marks in _wiring.state_family_of, which
	# are stamped before the HOLD check below and so also cover a field that
	# declares a channel it must never extrapolate along. _non_pose_eligibility
	# reads those marks and is deliberately the wider question.
	# Per-field fractions a recovery steps toward the rebase rather than writing
	# it outright, read off each property's own declaration at wire time.
	# Fields a sub-teleport in-domain recovery leaves on the predicted body,
	# read off each property's teleport_only() mark at wire time.
	# Fields whose own divergence never triggers a correction, read off each
	# property's reconcile_only() mark at wire time.
	# One open contraction question per field, held as two flat columns rather
	# than a row object so arming one allocates nothing: the error a recovery
	# wrote against, and the drive frontier it wrote at.
	#
	# The basis is what makes the answer mean anything. A recovery lands on the
	# body now while the next comparison judges a transition driven at the
	# acknowledgement, so at any acknowledgement age above zero the comparisons
	# immediately following a write were all driven before it existed and report
	# the error it already answered. Holding the question until a comparison
	# judges a transition driven past the write is what separates "this write did
	# not help" from "nothing has judged this write yet". A second write to the
	# same field replaces the question rather than queueing it.
	var _ledger_pending_error: Dictionary[StringName, float] = { }
	var _ledger_pending_basis: Dictionary[StringName, int] = { }
	# Recorded post-states a recovery write moved, so a fidelity judgement never
	# asks a rule to reproduce one. Keyed by the recording index, bounded with the
	# tape it indexes into.
	var _carry_dirty_records: Dictionary[int, bool] = { }
	# Everything the declaration reached, as one record. The tables are rebuilt
	# only when the wiring changes; the scalars are refreshed at the head of
	# every comparison, because a game may move them at any time.
	var _wiring := NetwPredict.Wiring.new()
	# Refilled per comparison rather than allocated: this path runs on every
	# authoritative frame of every predicted entity.
	var _verdict := NetwPredict.Verdict.new()
	# Whether the correction being applied crossed the teleport tier, latched for
	# the recovered emission.
	var _last_correction_teleported: bool = false

	# The config hash the property-class report last judged, so it fires once per
	# distinct config across the rewires a role change triggers.
	var _validated_class_hash: int = 0

	# Whether this stream has seen its gain-edge full row yet. A masked stream
	# opens with a whole row, and every later frame merges its changed fields over
	# the last, so past the gain edge the merged row is the sender's coherent row
	# for that frame's tick and a correction may consume it. Before the gain edge
	# the merged row is a partial mosaic authoritative at no single tick, so
	# corrections wait. An unmasked set reports a whole row on every frame and so
	# arms on its first, leaving the gate transparent to every non-masked recipe.
	var _stream_reconstructed: bool = false

	# Predict cursor.
	var _latest_input_tick: int = -1
	var _last_driven_input_tick: int = -1
	var _last_frame_transition_tick: int = -1
	# The physics frame the previous drive opened on, and the step count it
	# produced. Negative means no drive has been measured against this loop yet.
	var _last_drive_frame: int = -1
	var _last_quantum: int = 1
	# The physics frame and declared quantum the newest pass carried, adopted
	# from its NetwPredict.Timing so the engine never resolves a clock of its own.
	var _frame_index: int = 0
	var _declared_quantum_value: int = 1
	var _last_recorded_input_tick: int = -1
	var _frame_input: Dictionary = { }
	var _stall_input: Dictionary = { }
	# FRAME tape authoring and post-solve entry-keyed prediction history.
	var _tape_epoch: int = 0
	var _next_tape_entry_index: int = 0
	var _last_driven_entry_index: int = -1
	var _last_recorded_entry_index: int = -1
	var _authored_tape: Array[Dictionary] = []
	var _entry_history := NetwTimeline.create(TAPE_HISTORY_LIMIT)
	# Owner lane, authority side: transition -> {label, fresh, command}. The
	# command rides with its transition, so a queued entry is never missing the
	# input it needs.
	var _command_queue: Dictionary = { }
	var _command_epoch: int = -1
	# Authority lane, owner side: the highest transition authority has
	# acknowledged, which floors both lanes' redundancy windows.
	var _ack_of_acks: int = -1
	# Freshest transition authority proved through either the ack or state lane.
	var _latest_authority_ack: int = -1
	# Whether incoming acks are known to carry this tape's numbering. Ack frames
	# are epoch-stamped and re-license the domain after a rewire; state rows are
	# not, so they stay out of the frontier until the ack lane confirms.
	var _ack_domain_confirmed: bool = true
	# Authority lane, authority side: the highest transition the owner has
	# confirmed receiving an acknowledgement for, which trims the re-send run.
	var _owner_ack_floor: int = -1
	# Entry-indexed replay cursor over the owner lane's queue.
	var _replay_cursor: int = -1
	var _replay_warmed: bool = false
	# Owner-lane transitions admitted since the last consume boundary, flushed
	# into the arrival histogram once per authority frame.
	var _arrivals_this_frame: int = 0
	# The TICK tier's standing-buffer latch, the counterpart of _replay_warmed.
	var _consume_warmed: bool = false
	var _last_replayed_label: int = -1
	var _last_replayed_fresh: bool = false
	# Consume cursors.
	var _next_input_tick: int = -1
	var _ack: int = -1
	var _last_input: Dictionary = { }
	# Whether the last consume step moved the ack, which decides both whether the
	# state stream authors a frame this tick and which timeline slot the recorder
	# writes.
	var _ack_advanced: bool = false

	# Pauses non-teleport recoveries after a contact, so a settling transient is
	# not corrected through.
	var _cooldown_until_tick: int = -1

	# Recovery-convergence evidence, fed to escalation_after: consecutive
	# recoveries whose divergence did not shrink, the dominant-axis sign and
	# magnitude of the last one (magnitude negative while there is none), and
	# the promotion the next staged recovery consumes.
	var _nonshrink_streak: int = 0
	var _last_recovery_direction := StringName()
	var _last_recovery_sign: int = 0
	var _last_recovery_divergence: float = -1.0
	var _escalate_next: bool = false
	# The realized-solve detail behind one transition's witness fingerprint,
	# which is what the operator gates read to decide a contact was one the
	# closure reproduces. The pool keeps the fingerprint and the class bits; the
	# contact classes behind them have no native home. Bounded with the journal.
	var _witness_details: Dictionary[int, Dictionary] = { }
	# The execution facts the transition now open was fingerprinted against, held
	# so the solve folds the same ones the open did even when island membership
	# moved between them.
	var _open_topology_facts: Dictionary = { }
	# This engine's transition to the pool's, for the drives the seam feeds. The
	# two numbers coincide under TICK and diverge under FRAME, where the pool
	# indexes its own tape.
	var _pool_transitions: Dictionary = { }
	# Fallback closes speculation while retaining local input authoring.
	var _fallback_latched: bool = false
	# The last out-of-transition body write, stamped onto the next new row.
	var _pending_provenance: Dictionary = { }
	# Witness state spans consecutive solves but never changes the simulation.
	var _previous_witness_sleeping: bool = false
	var _has_previous_witness: bool = false
	var _invalid_witness_reported: bool = false
	var _invalid_command_predictor_reported: bool = false
	var _joint_refusal_reported: bool = false
	# The command matrix: what drove each transition of an entity this peer
	# simulates but does not own, and where that command came from.
	#   { transition -> { command: Dictionary, origin: CommandOrigin } }
	var _relayed_epoch: int = -1
	# The freshest RELAYED cell. Predicted cells never move it, because the
	# whole point of the read is how stale real authorship has become.
	var _last_relayed_transition: int = -1
	var _newest_matrix_transition: int = -1
	var _raw_fp_enabled: bool = false
	# Conditional operators wait for the ack lane to align the basis witness.
	# The state survives only until that verdict or a newer state supersedes it.
	var _witness_verdicts: Dictionary[int, int] = { }
	var _authority_witness_classes: Dictionary[int, int] = { }
	var _deferred_operator_states: Dictionary[int, Dictionary] = { }
	var _operator_deferred_basis: int = -1

	# Declared-island state. _out_of_domain_until is the label an open window
	# stops covering, or -1 when none is open; a window opens on a fact that makes
	# some antecedent unequal and stays open a cooldown past it, because a contact
	# keeps perturbing the bodies after the frame that reported it. _e_digest is
	# the fingerprint of the declared world facts the last drive ran against, and
	# _island_epoch is the world version the game last told us about.
	var _out_of_domain_until: int = -1
	var _e_digest: int = 0
	var _island_epoch: int = -1
	var _island_gap_reported: bool = false
	# The declared sensors' values from the newest pre-drive sample, the ones
	# the current digest was folded over. The drive reads these back so the
	# transition and its digest describe the same world.
	# TODO: carry samples per tape entry once a replaying tier declares
	# sensors, so a re-run transition reads the world its original drive read.
	var _sensor_samples: Dictionary = { }
	# Produced membership and fidelity are committed only at transition boundaries.
	# The seeded latch tells a first declaration from a later change, because only
	# a change is a fact the two peers adopted on different transitions.
	var _island_members: Array[NetwEntity] = []
	var _island_roster_seeded: bool = false
	var _simulated_members: Dictionary[NetwEntity, bool] = { }
	# The newest authoritative transition this member has not been replayed
	# from yet, and the oldest relayed cell filed since the last pass. A JOINT
	# group takes the minimum across its members rather than replaying once per
	# row, so one tick costs one pass whatever arrived during it.
	var _joint_basis: int = -1
	var _joint_relay_floor: int = -1
	var _joint_epoch_floor: int = -1
	# The transitions this member belongs to its group for. A member outside
	# tenure at a transition is skipped by the pass, because it was not in the
	# world the group is re-running.
	var _tenure_begin: int = -1
	var _tenure_end: int = -1
	# Members that left the roster and are still stepped, because a pass whose
	# floor precedes their departure has to carry them to it.
	var _joint_lingering: Dictionary[NetwEntity, bool] = { }
	var _realized_contact_entities: Dictionary[NetwEntity, bool] = { }

	# What the owner claims it reached, keyed by transition, held on authority
	# until authority has run that transition itself. The owner ships a claim
	# ahead of the consume cursor, so a verdict taken on arrival would judge a row
	# authority has not written yet.
	#   { transition: int -> { pre_fp, post_fp, e_digest, family columns } }
	var _owner_claims: Dictionary = { }


	# Binds to the interface and entity, following control transfer and reparent so
	# the role re-resolves in place.
	func _attach(iface: LagCompCore, entity: NetwEntity) -> void:
		_iface_ref = weakref(iface)
		_kernel = iface._api()
		_entity = entity
		_handle = entity.prediction
		_apply_scene_island_defaults()
		if not entity.control_changed.is_connected(_on_control_changed):
			entity.control_changed.connect(_on_control_changed)
		if not entity.reparented.is_connected(_on_reparented):
			entity.reparented.connect(_on_reparented)
		_rewire(_resolved_declaration())


	# Leaves the loop and restores the set-handle hooks, keeping the handle's config
	# and counters so a re-registration resumes.
	# Arms this entity's simulation gate when its archetype says the physics
	# server integrates the body, and releases it when the declaration changes
	# or the engine leaves.
	func _refresh_simulation_gate(force_release: bool = false) -> void:
		var iface := _iface()
		if not iface:
			return
		iface._sync_simulation_gate(
			_entity,
			not force_release and _handle.archetype \
					== NetwPredict.Archetype.SOLVER_BODY,
		)


	func _release() -> void:
		_refresh_simulation_gate(true)
		_clear_island_promotions()
		_handle._simulation_subjects.clear()
		_unregister_from_loop()
		# A release installs the empty feed, which is what a fresh record is, so
		# un-wiring and wiring are the same operation with different content.
		var feed := NetwPredict.Feed.new()
		if _state_binding:
			_state_binding.apply_state_feed(feed)
		if _input_binding:
			_input_binding.apply_input_feed(feed)
		if _entity:
			if _entity.control_changed.is_connected(_on_control_changed):
				_entity.control_changed.disconnect(_on_control_changed)
			if _entity.reparented.is_connected(_on_reparented):
				_entity.reparented.disconnect(_on_reparented)


	func handle() -> NetwPredictionHandle:
		return _handle


	# The accessors below exist so the handle asks for facts by name instead of
	# reaching through this record's privates. The handle is the game-facing
	# surface and this is the implementation behind it, so every question it asks
	# is part of the seam whether or not it is spelled as one. Spelled as one,
	# a later engine can answer them differently.


	# Re-resolves the role and rebuilds the declaration tables, on [param
	# declaration] when one is handed in and on the session's current answer
	# otherwise. The handle calls this when a setter changes a fact the tables
	# were built from.
	func rewire(declaration: Declaration = null) -> void:
		_rewire(declaration if declaration else _resolved_declaration())


	# The value the declared sensor [param name] sampled before the drive now
	# running, or [param default] when nothing has sampled it.
	func sensor_sample(name: StringName, default: Variant = null) -> Variant:
		return _sensor_samples.get(name, default)


	# The shell that steps this engine, or null when it is unattached. The handle
	# reads it for the frame timing only the shell can resolve.
	func shell():
		return _iface()


	# A detached copy of the per-field teleport distances the declarations
	# resolved to, so a reader cannot edit the table the tier measurement uses.
	func teleport_distances() -> Dictionary:
		return _wiring.teleport_thresholds.duplicate()


	# What this entity's declarations actually reach, or an empty report before a
	# state set is bound. The engine resolves its own binding, because which set
	# the report describes is not a question the handle should have to answer.
	func reachability_report() -> Dictionary:
		if not _state_binding or not _state_binding.set:
			return { }
		return _reachability_report(_state_binding.set)


	# Publishes the committed roster into the stats surface.
	#
	# Written at the two commit points rather than computed on read, so the
	# reported roster is the one the engine acted on rather than one recomputed
	# later from whatever the members happen to be by then.
	func _publish_island_roster() -> void:
		var members := PackedStringArray()
		for member: NetwEntity in _island_members:
			members.append(String(member.entity_id))
		members.sort()
		var simulated := PackedStringArray()
		for member: NetwEntity in _simulated_members:
			simulated.append(String(member.entity_id))
		simulated.sort()
		_handle.stats.island_members = members
		_handle.stats.simulated_members = simulated


	func network_tick(timing: NetwPredict.Timing) -> void:
		_adopt_timing(timing)
		if _handle.schedule == NetwPredict.Schedule.TICK:
			simulate_tick(timing)
			return
		match _role:
			NetwPredict.Role.PREDICT:
				_predict_author_tick(timing.tick)
			NetwPredict.Role.HOST_LOCAL:
				_host_local_author_tick(timing.tick)
			NetwPredict.Role.REMOTE:
				if _fallback_latched:
					_fallback_author_tick(timing.tick)
			NetwPredict.Role.SIMULATE:
				pass


	func simulate_tick(timing: NetwPredict.Timing) -> void:
		_adopt_timing(timing)
		match _role:
			NetwPredict.Role.PREDICT:
				_predict_step(timing.delta, timing.tick)
			NetwPredict.Role.CONSUME:
				_consume_step(timing.delta, timing.tick)
			NetwPredict.Role.HOST_LOCAL:
				_host_local_step(timing.delta, timing.tick)
			NetwPredict.Role.REMOTE:
				if _fallback_latched:
					_fallback_author_step(timing.tick)
			NetwPredict.Role.SIMULATE:
				_simulated_step(timing.delta, timing.tick)


	func simulate_frame(timing: NetwPredict.Timing) -> void:
		_adopt_timing(timing)
		if _handle.schedule != NetwPredict.Schedule.FRAME:
			return
		match _role:
			NetwPredict.Role.PREDICT:
				# A frame the clock held bought no simulated time, so it opens no
				# transition. Commands keep flowing: holding the world must never
				# hold the player's input.
				if not timing.simulating \
						or timing.tick <= _last_frame_transition_tick:
					_handle.stats.authoring_clamped += 1
					_send_command_frame()
					return
				_predict_frame_step(timing)
			NetwPredict.Role.CONSUME:
				_consume_frame_step(timing)
			NetwPredict.Role.HOST_LOCAL:
				_host_local_frame_step(timing)
			NetwPredict.Role.REMOTE:
				if _fallback_latched:
					if timing.tick <= _last_frame_transition_tick:
						_handle.stats.authoring_clamped += 1
						_send_command_frame()
						return
					_fallback_author_frame_step(timing)
			NetwPredict.Role.SIMULATE:
				# A promoted remote shares the held world, so it steps when the
				# world does and never on a frame the clock declined.
				if timing.simulating:
					_simulated_frame_step(timing)


	# Commits produced membership and promotion before a schedule-tier transition.
	func prepare_island(schedule: NetwPredict.Schedule) -> void:
		if _handle.schedule != schedule or _role not in [
			NetwPredict.Role.PREDICT,
			NetwPredict.Role.CONSUME,
			NetwPredict.Role.HOST_LOCAL,
		]:
			return
		_refresh_island_membership(_role == NetwPredict.Role.PREDICT)


	# Keeps the step a receive-driven path replays at. A correction arrives on no
	# pump, so it has no timing of its own and reuses the last one a pass carried.
	func _adopt_timing(timing: NetwPredict.Timing) -> void:
		if timing.ticktime > 0.0:
			_tick_delta = timing.ticktime
		_frame_index = timing.frame
		_declared_quantum_value = maxi(1, timing.quantum)

#region Kernels

	# The decision half of every drive, compare, and recovery the engine
	# performs, fenced away from the half that writes the result.
	#
	# Every kernel here answers from `NetwPredictionCore`. Each one is a
	# function of its arguments and nothing else, which is what lets a
	# transition be reproduced from its journal row alone, and a static with no
	# self is how the language enforces that from the inside.
	#
	# The forwarders exist for the two kernels whose GDScript callers hold a
	# `NetwPredict.Wiring` or a `NetwPredict.Verdict`, which cross as the
	# dictionaries the native signature takes.


	static func predict_fold(
			latest_input_tick: int,
			last_driven_input_tick: int,
			frame_tick: int,
	) -> Dictionary:
		return NetwPredictionCore.predict_fold(
			latest_input_tick,
			last_driven_input_tick,
			frame_tick,
		)


	# TODO: retire the warm latch once the tick tier shares this consume. A
	# latch that never changes an answer should either earn one or go.
	static func consume_plan(depth: int, buffer: int, warmed: bool) -> Dictionary:
		return NetwPredictionCore.consume_plan(depth, buffer, warmed)


	static func evaluate(
			domain: NetwPredictJournal.Domain,
			verdict: NetwPredict.ExactVerdict,
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


	static func measure(field_sink: Dictionary, tolerances: Dictionary) -> int:
		return NetwPredictionCore.measure(field_sink, tolerances)


	static func attribute(
			pre_equal: bool,
			command_equal: bool,
			environment_equal: bool,
			topology_equal: bool = true,
			raw_equal: bool = true,
			witness_equal: bool = true,
			local_evidence: int = NetwPredictJournal.EVIDENCE_WITNESS,
			peer_evidence: int = NetwPredictJournal.EVIDENCE_WITNESS,
			evidence_complete: bool = true,
	) -> NetwPredictJournal.Attribution:
		return NetwPredictionCore.attribute(
			pre_equal,
			command_equal,
			environment_equal,
			topology_equal,
			raw_equal,
			witness_equal,
			local_evidence,
			peer_evidence,
			evidence_complete,
		) as NetwPredictJournal.Attribution


	static func compared_state(payload: Dictionary, causal: Dictionary) -> Dictionary:
		return NetwPredictionCore.compared_state(payload, causal)


	static func raw_state_fingerprint(payload: Dictionary) -> int:
		return NetwPredictionCore.raw_state_fingerprint(payload)


	static func fact_fingerprint(facts: Dictionary) -> int:
		return NetwPredictionCore.fact_fingerprint(facts)


	static func contact_count_bucket(count: int) -> int:
		return NetwPredictionCore.contact_count_bucket(count)


	static func differing_family(
			local: PackedInt32Array,
			peer: PackedInt32Array,
	) -> NetwPredictJournal.StateFamily:
		return NetwPredictionCore.differing_family(
			local,
			peer,
		) as NetwPredictJournal.StateFamily


	static func recover(
			payload: Dictionary,
			policy: NetwPredict.RecoveryPolicy,
			correction: NetwPredict.CorrectionMode,
			snap_restore: NetwPredict.RestoreMode,
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


	static func _teleport_reached(
			pose_errors: Dictionary,
			thresholds: Dictionary,
			default_threshold: float,
	) -> bool:
		return NetwPredictionCore.teleport_reached(
			pose_errors,
			thresholds,
			default_threshold,
		)


	static func escalation_after(
			streak: int,
			last_sign: int,
			last_divergence: float,
			divergence: float,
			sign: int,
	) -> Dictionary:
		return NetwPredictionCore.escalation_after(
			streak,
			last_sign,
			last_divergence,
			divergence,
			sign,
		)


	static func delta_direction(field: StringName, delta: Variant) -> Dictionary:
		return NetwPredictionCore.delta_direction(field, delta)


	static func guard_projection(
			projection: Dictionary,
			field_divergence: Dictionary,
			wiring: NetwPredict.Wiring,
			ack_age_ticks: int,
			tick_delta: float,
	) -> Dictionary:
		return NetwPredictionCore.guard_projection(
			projection,
			field_divergence,
			wiring.epsilon,
			wiring.epsilon_overrides,
			wiring.max_restore_ticks,
			ack_age_ticks,
			tick_delta,
		)


	static func domain_of(
			declared: bool,
			approximate: bool,
			label: int,
			window_until: int,
	) -> NetwPredictJournal.Domain:
		return NetwPredictionCore.domain_of(
			declared,
			approximate,
			label,
			window_until,
		) as NetwPredictJournal.Domain


	static func window_after(label: int, cooldown: int, window_until: int) -> int:
		return NetwPredictionCore.window_after(label, cooldown, window_until)


	static func environment_digest(epoch: int, samples: Dictionary) -> int:
		return NetwPredictionCore.environment_digest(epoch, samples)


	static func _pose_delta(
			target: Variant,
			current: Variant,
			is_angle: bool = false,
	) -> Variant:
		return NetwPredictionCore.pose_delta(target, current, is_angle)


	static func transport(
			predicted: Dictionary,
			authority: Dictionary,
			current: Dictionary,
			pose_fields: Dictionary,
			angles: Dictionary = { },
	) -> Dictionary:
		return NetwPredictionCore.transport(
			predicted,
			authority,
			current,
			pose_fields,
			angles,
		)


	static func converge_toward(
			restore: Dictionary,
			current: Dictionary,
			rules: Dictionary,
			angles: Dictionary,
	) -> Dictionary:
		return NetwPredictionCore.converge_toward(
			restore,
			current,
			rules,
			angles,
		)


	static func project_payload(
			payload: Dictionary,
			projection: Dictionary,
			age: float,
	) -> Dictionary:
		return NetwPredictionCore.project_payload(payload, projection, age)

#endregion


	func finalize_frame_state() -> void:
		if _handle.schedule != NetwPredict.Schedule.FRAME:
			return
		if _role != NetwPredict.Role.PREDICT:
			return
		var state := _capture()
		if _last_driven_entry_index > _last_recorded_entry_index:
			_entry_history.record_state(_last_driven_entry_index + 1, state)
			_close_journal_row(_last_driven_entry_index, state)
			_last_recorded_entry_index = _last_driven_entry_index
		if _last_driven_input_tick > _last_recorded_input_tick:
			_timeline.record_state(_last_driven_input_tick + 1, state)
			_last_recorded_input_tick = _last_driven_input_tick


	func uses_schedule(schedule: NetwPredict.Schedule) -> bool:
		return _handle.schedule == schedule


	func tape_transitions() -> Array[Dictionary]:
		if _role == NetwPredict.Role.PREDICT:
			return _authored_tape.duplicate(true)
		var out: Array[Dictionary] = []
		var indices: Array = _command_queue.keys()
		indices.sort()
		for index: int in indices:
			var queued := _command_queue[index] as Dictionary
			out.append({
				"index": index,
				"label": int(queued.get("label", -1)),
				"fresh": bool(queued.get("fresh", false)),
			})
		return out


	func transition_state_at(entry_index: int) -> Dictionary:
		# A TICK transition is its tick, so its post-state lives in the predicted
		# timeline rather than in the FRAME tier's entry history.
		if _handle.schedule == NetwPredict.Schedule.TICK:
			return _timeline.state_at(entry_index + 1) if _timeline else { }
		return _entry_history.state_at(entry_index + 1)


	func journal() -> NetwPredictJournal:
		var snapshot := NetwPredictJournal.new(TAPE_HISTORY_LIMIT)
		var iface := _iface()
		if not iface:
			return snapshot
		snapshot.clear(iface.native_journal_epoch(_entity))
		for transition: int in _native_journal_transitions():
			snapshot._append_native(
				transition,
				_native_journal_row(transition),
				_witness_details.get(transition, { }),
			)
		return snapshot


	## Returns a detached copy of the engine's episode evidence.
	func episode() -> Dictionary:
		return _handle._episode_report(_pool_episode())


	# Publishes the episode to the handle after every evidence mutation.
	func _sync_episode() -> void:
		if not _handle:
			return
		var pool := _pool_episode()
		if not pool.is_empty():
			_handle._store_episode(pool)


	# The pool's episode scalars, read in one call rather than one per guard.
	func _episode_stats() -> PackedInt64Array:
		var iface := _iface()
		if iface:
			return iface.native_episode_stats(_entity)
		var absent := PackedInt64Array()
		absent.resize(NetwPredictionEngine.STAT_EPISODE_COUNT)
		absent[NetwPredictionEngine.STAT_EPISODE_STATE] = -1
		absent[NetwPredictionEngine.STAT_EPISODE_OPENED] = -1
		absent[NetwPredictionEngine.STAT_EPISODE_LAST_COMPARISON] = -1
		return absent


	# Whether the pool holds an episode still gathering evidence.
	func _episode_open() -> bool:
		var iface := _iface()
		return iface != null and iface.native_episode_state(_entity) \
				== NetwPredict.EpisodeState.OPEN


	# The pool's episode in the shape the public report projects.
	#
	# The contact detail behind a breach and behind the generator's witness
	# fingerprint stays here, because a peer compares a fingerprint and the
	# pool never retains what only one peer saw.
	func _pool_episode() -> Dictionary:
		var iface := _iface()
		if not iface:
			return { }
		var report := iface.native_episode(_entity)
		if not report or not report.active():
			return { }
		var writes: Array = []
		for i in report.write_count():
			writes.append({
				&"episode": report.write_episode(i),
				&"write_id": report.write_id(i),
				&"operator": report.write_operator(i),
				&"basis": report.write_basis(i),
				&"delta_fp": report.write_delta_fp(i),
				&"target": report.write_target(i),
				&"outcome": report.write_outcome(i),
				&"meter_before": report.write_meter_before(i),
				&"meter_after": report.write_meter_after(i),
				&"verdicts": report.write_verdicts(i),
				&"verify_run": report.write_verify_run(i),
				&"judged_transition": report.write_judged_transition(i),
				&"best_meter": report.write_best_meter(i),
				&"trigger_shape": report.write_trigger_shape(i),
				&"evidence_free": report.write_evidence_free(i),
				&"null_operator": report.write_null_operator(i),
			})
		var comparisons: Array = []
		for i in report.comparison_count():
			comparisons.append({
				&"transition": report.comparison_transition(i),
				&"meter": report.comparison_meter(i),
				&"agrees": report.comparison_agrees(i),
				&"write_id": report.comparison_write_id(i),
			})
		var decisions: Array = []
		for i in report.decision_count():
			decisions.append({
				&"operator": report.decision_operator(i),
				&"basis": report.decision_basis(i),
				&"eligible": report.decision_eligible(i),
				&"applied": report.decision_applied(i),
				&"eligibility": report.decision_eligibility(i),
			})
		var taint: Array = []
		for i in report.taint_count():
			taint.append(report.taint_at(i))
		var reopen_chain: Array = []
		for i in report.reopen_count():
			reopen_chain.append(report.reopen_at(i))
		var breach := report.breach_transition()
		return {
			&"id": report.id(),
			&"opened_transition": report.opened_transition(),
			&"generator_row_copy": _pool_generator_row(report),
			&"attribution": report.attribution(),
			&"writes": writes,
			&"comparisons": comparisons,
			&"decisions": decisions,
			&"taint": taint,
			&"secondary_generators": _pool_secondary_generators(report),
			&"reopen_chain": reopen_chain,
			&"reopened_from": report.reopened_from(),
			&"state": report.state(),
			&"non_contraction_used": report.non_contraction_used(),
			&"withheld_non_contractions": report.withheld_non_contractions(),
			&"evidence_free_non_contractions": \
					report.evidence_free_non_contractions(),
			&"nc_no_trigger": report.no_trigger_non_contractions(),
			&"nc_mixed_trigger": report.mixed_trigger_non_contractions(),
			&"closure_used": report.closure_used(),
			&"agreement_run": report.agreement_run(),
			&"last_comparison_transition": report.last_comparison_transition(),
			&"agreement_write_id": report.agreement_write_id(),
			&"last_write_id": report.last_write_id(),
			&"closed_transition": report.closed_transition(),
			&"fallback_transition": report.fallback_transition(),
			&"resume_ack_age": report.resume_ack_age(),
			&"quarantine_target": report.quarantine_target(),
			&"quarantine_clean_run": report.quarantine_clean_run(),
			&"reseed_transition": report.reseed_transition(),
			&"aligned_transition": report.aligned_transition(),
			&"evidence_dropped": report.evidence_dropped(),
			&"transport_decided": report.transport_decided(),
			&"dissipate_decided": report.dissipate_decided(),
			&"demoted": report.demoted(),
			&"breach_transition": breach,
			&"breach_witness": _witness_details.get(breach, { }).duplicate(true),
		}


	func _pool_generator_row(report: NetwPredictEpisodeReport) -> Dictionary:
		if not report.generator_present():
			return {
				&"status": NetwPredictionHandle.GENERATOR_UNKNOWN_BEYOND_RETENTION,
			}
		var transition := report.generator_transition()
		return {
			&"transition": transition,
			&"label": report.generator_label(),
			&"c_hash": report.generator_c_hash(),
			&"e_digest": report.generator_e_digest(),
			&"pre_fp": report.generator_pre_fp(),
			&"topo_fp": report.generator_topology_fp(),
			&"raw_fp": report.generator_raw_fp(),
			&"witness_fp": report.generator_witness_fp(),
			&"evidence_mask": report.generator_evidence_mask(),
			&"pre_pose_fp": report.generator_pre_pose_fp(),
			&"pre_momentum_fp": report.generator_pre_momentum_fp(),
			&"pre_controller_fp": report.generator_pre_controller_fp(),
			&"post_fp": report.generator_post_fp(),
			&"post_pose_fp": report.generator_post_pose_fp(),
			&"post_momentum_fp": report.generator_post_momentum_fp(),
			&"post_controller_fp": report.generator_post_controller_fp(),
			&"domain": report.generator_domain(),
			&"attribution": report.generator_attribution(),
			&"flags": report.generator_flags(),
			&"witness_detail": _witness_details.get(transition, { }),
			&"beyond_retention": report.generator_beyond_retention(),
		}


	func _pool_secondary_generators(
			report: NetwPredictEpisodeReport,
	) -> Array:
		var out: Array = []
		for i in report.secondary_generator_count():
			out.append({
				&"transition": report.secondary_generator_transition(i),
				&"label": report.secondary_generator_label(i),
				&"domain": report.secondary_generator_domain(i),
				&"attribution": report.secondary_generator_attribution(i),
				&"flags": report.secondary_generator_flags(i),
				&"beyond_retention": \
						report.secondary_generator_beyond_retention(i),
			})
		return out


	# Opens one behavioral episode at the first actionable settled comparison.
	func _open_episode(
			transition: int,
			attribution: NetwPredictJournal.Attribution,
	) -> void:
		var iface := _iface()
		if not iface or not iface.native_open_episode(
			_entity,
			int(_pool_transitions.get(transition, transition)),
			attribution,
		):
			return
		_sync_episode()
		_handle.episode_opened.emit(_handle._episode_report(_pool_episode()))


	# Copies one row and its debug-only forensic sidecar out of bounded storage.
	func _pin_row(transition: int) -> Dictionary:
		var native_row := _native_journal_row(transition)
		if not native_row:
			return {
				&"status": NetwPredictionHandle.GENERATOR_UNKNOWN_BEYOND_RETENTION,
			}
		var pre := native_row.pre_families()
		var post := native_row.post_families()
		var row := {
			&"transition": native_row.transition(),
			&"label": native_row.label(),
			&"kind": native_row.kind(),
			&"c_hash": native_row.c_hash(),
			&"e_digest": native_row.e_digest(),
			&"pre_fp": native_row.pre_fp(),
			&"topo_fp": native_row.topo_fp(),
			&"raw_fp": native_row.raw_fp(),
			&"witness_fp": native_row.witness_fp(),
			&"witness_class_bits": native_row.witness_class_bits(),
			&"aligned_error": native_row.aligned_error(),
			&"evidence_mask": native_row.evidence_mask(),
			&"witness_detail": _witness_details.get(transition, { }),
			&"pre_pose_fp": pre[0],
			&"pre_momentum_fp": pre[1],
			&"pre_controller_fp": pre[2],
			&"post_fp": native_row.post_fp(),
			&"post_pose_fp": post[0],
			&"post_momentum_fp": post[1],
			&"post_controller_fp": post[2],
			&"episode_id": native_row.episode_id(),
			&"write_id": native_row.write_id(),
			&"operator": native_row.op(),
			&"basis": native_row.basis(),
			&"differing_family": native_row.differing_family(),
			&"domain": native_row.domain(),
			&"attribution": native_row.attribution(),
			&"flags": native_row.flags(),
		}
		return row


	# Holds one state until the ack lane supplies its aligned witness verdict.
	func _defer_operator_for_witness(
			recv_tick: int,
			basis: int,
			payload: Dictionary,
	) -> bool:
		var stats := _episode_stats()
		if stats[NetwPredictionEngine.STAT_EPISODE_ACTIVE] == 0 \
				or _witness_verdicts.has(basis):
			return false
		var transport_waits := _handle.transport_corridor.is_valid() \
				and stats[NetwPredictionEngine.STAT_EPISODE_TRANSPORT_DECIDED] \
				== 0
		var dissipate_waits := _has_dissipate_fields() \
				and stats[NetwPredictionEngine.STAT_EPISODE_DISSIPATE_DECIDED] \
				== 0 \
				and _handle.resolved_recovery_policy() \
				!= NetwPredict.RecoveryPolicy.OBSERVE
		if not transport_waits and not dissipate_waits:
			return false
		if _operator_deferred_basis >= 0 \
				and _operator_deferred_basis != basis:
			_record_unavailable_operator(
				_operator_deferred_basis,
				transport_waits,
				dissipate_waits,
			)
			_deferred_operator_states.erase(_operator_deferred_basis)
			_operator_deferred_basis = -1
			return false
		_deferred_operator_states[basis] = {
			&"recv_tick": recv_tick,
			&"payload": payload.duplicate(true),
		}
		_operator_deferred_basis = basis
		return true


	# Records that witness evidence never arrived before a newer basis replaced it.
	func _record_unavailable_operator(
			basis: int,
			transport_waited: bool,
			dissipate_waited: bool,
	) -> void:
		var stats := _episode_stats()
		if transport_waited \
				and stats[NetwPredictionEngine.STAT_EPISODE_TRANSPORT_DECIDED] \
				== 0:
			_charge_decision({
				&"operator": NetwPredictJournal.Operator.TRANSPORT_DELTA,
				&"basis": basis,
				&"eligible": false,
				&"applied": false,
				&"eligibility": {
					&"witness_aligned": false,
				},
			})
		if dissipate_waited \
				and stats[NetwPredictionEngine.STAT_EPISODE_DISSIPATE_DECIDED] \
				== 0:
			_charge_decision({
				&"operator": NetwPredictJournal.Operator.DISSIPATE,
				&"basis": basis,
				&"eligible": false,
				&"applied": false,
				&"eligibility": {
					&"witness_aligned": false,
				},
			})
		_sync_episode()


	# Re-evaluates the retained state once its ack witness has been compared.
	func _retry_deferred_operator(basis: int) -> void:
		if _operator_deferred_basis != basis \
				or not _deferred_operator_states.has(basis):
			return
		var state: Dictionary = _deferred_operator_states[basis]
		_deferred_operator_states.erase(basis)
		_operator_deferred_basis = -1
		_on_state(
			int(state[&"recv_tick"]),
			basis,
			state[&"payload"],
		)


	# Attempts the conditional operator once and records every eligibility fact.
	func _try_transport(
			predicted: Dictionary,
			authority: Dictionary,
			current: Dictionary,
			basis: int,
			escalated: bool,
	) -> Dictionary:
		var stats := _episode_stats()
		if not _handle.transport_corridor.is_valid() \
				or stats[NetwPredictionEngine.STAT_EPISODE_ACTIVE] == 0 \
				or stats[NetwPredictionEngine.STAT_EPISODE_TRANSPORT_DECIDED] \
				!= 0:
			return { }
		var candidate := transport(
			predicted,
			authority,
			current,
			_wiring.pose_fields,
			_wiring.angle_fields,
		)
		var basis_clean := _witness_row_clean(basis, true)
		var recent_clean := _recent_witness_clean(basis)
		var non_pose := _non_pose_eligibility(predicted, authority)
		# Per field, for the same reason the tier itself is: the transport's write
		# has to be smaller than a teleport, and "smaller" is only answerable in
		# each field's own units.
		var deltas := _transport_deltas(
			current,
			candidate.get(&"restore", { }),
		)
		var below_teleport := _transport_moved(deltas) \
				and not _teleport_reached(
					deltas,
					_wiring.teleport_thresholds,
					_handle.teleport_threshold,
				)
		var iface := _iface()
		var observing := _handle.resolved_recovery_policy() \
				== NetwPredict.RecoveryPolicy.OBSERVE
		var corridor_clear := false
		var prior_ok := iface != null and iface.native_transport_admissible(
			_entity,
			bool(candidate.get(&"valid", false)),
			basis_clean,
			recent_clean,
			bool(non_pose[&"agrees"]),
			below_teleport,
			escalated,
			observing,
		)
		if prior_ok:
			var corridor := _handle.transport_corridor
			var proposed := current.duplicate()
			proposed.merge(candidate[&"restore"], true)
			corridor_clear = bool(corridor.call(current, proposed))
		var eligible := prior_ok and corridor_clear
		var decision := {
			&"operator": NetwPredictJournal.Operator.TRANSPORT_DELTA,
			&"basis": basis,
			&"eligible": eligible,
			&"applied": eligible,
			&"eligibility": {
				&"candidate": bool(candidate.get(&"valid", false)),
				&"basis_witness_clean": basis_clean,
				&"recent_witness_clean": recent_clean,
				&"non_pose": non_pose,
				# Per field, the same shape transport() reports its own delta in.
				# One maximum could not be read against the distances the fields
				# declared, which is what below_teleport is now decided by.
				&"delta": deltas,
				&"below_teleport": below_teleport,
				&"policy": not escalated and not observing \
						and _correction == NetwPredict.CorrectionMode.SNAP,
				&"corridor_clear": corridor_clear,
			},
		}
		_charge_decision(decision)
		_sync_episode()
		return candidate if eligible else { }


	func _witness_row_clean(transition: int, require_peer: bool) -> bool:
		var iface := _iface()
		if not iface:
			return false
		return iface.native_witness_row_clean(
			_entity,
			int(_pool_transitions.get(transition, transition)),
			require_peer,
		)


	# Requires a contiguous clean local witness run from the basis through now.
	func _recent_witness_clean(basis: int) -> bool:
		var expected := basis
		for transition: int in _native_journal_transitions():
			if transition < basis:
				continue
			if transition != expected:
				return false
			var row := _native_journal_row(transition)
			if not row or not row.flags() \
					& NetwPredictJournal.ROW_CLOSED:
				return expected > basis
			if not _witness_row_clean(transition, false):
				return false
			expected += 1
		return expected > basis


	# Compares every declared causal field outside the pose family at the basis.
	func _non_pose_eligibility(
			predicted: Dictionary,
			authority: Dictionary,
	) -> Dictionary:
		var agrees := true
		var fields: Dictionary = { }
		for field: StringName in _wiring.causal_fields:
			if int(_wiring.state_family_of.get(
				field,
				STATE_FAMILY_CONTROLLER,
			)) == STATE_FAMILY_POSE:
				continue
			var error := INF
			if predicted.has(field) and authority.has(field):
				error = NetwPredictionHandle._error(
					predicted[field],
					authority[field],
					_wiring.angle_fields.has(field),
				)
			var epsilon := float(
				_wiring.epsilon_overrides.get(field, _handle.divergence_epsilon),
			)
			var field_agrees := not NetwPredictionHandle._triggers(error, epsilon)
			fields[field] = {
				&"error": error,
				&"epsilon": epsilon,
				&"agrees": field_agrees,
			}
			agrees = agrees and field_agrees
		return { &"agrees": agrees, &"fields": fields }


	# Measures the pose change the partial transport would write, per field, so the
	# "smaller than a teleport" test reads each one against the distance that field
	# declared rather than one number that can only be right for one of them.
	func _transport_deltas(
			current: Dictionary,
			restore: Dictionary,
	) -> Dictionary:
		var deltas: Dictionary[StringName, float] = { }
		if restore.is_empty():
			return deltas
		for field: StringName in restore:
			if not current.has(field):
				# A write whose own size cannot be measured is not eligible, and an
				# empty answer is what the caller reads that from.
				return { } as Dictionary[StringName, float]
			deltas[field] = NetwPredictionHandle._error(
				current[field],
				restore[field],
				_wiring.angle_fields.has(field),
			)
		return deltas


	# Whether a transport's staged write moves anything at all. A transport that
	# writes the value already held is not a repair, whatever its eligibility says.
	func _transport_moved(deltas: Dictionary) -> bool:
		for field: StringName in deltas:
			if float(deltas[field]) > 0.0:
				return true
		return false


	# Whether transport is still gathering its bounded verification verdicts.
	func _transport_pending() -> bool:
		return _operator_pending(NetwPredictJournal.Operator.TRANSPORT_DELTA)


	# Whether the clean momentum-only no-write attempt is still gathering proof.
	func _dissipate_pending() -> bool:
		return _operator_pending(NetwPredictJournal.Operator.DISSIPATE)


	# Whether the newest write naming this operator still awaits its verdict.
	func _operator_pending(operator: NetwPredictJournal.Operator) -> bool:
		var iface := _iface()
		if not iface:
			return false
		return iface.native_episode_operator_pending(_entity, operator)


	# Whether this recipe exposes at least one withheld momentum field.
	func _has_dissipate_fields() -> bool:
		var iface := _iface()
		return iface.native_dissipate_declared(_entity) if iface else false


	# Attempts one bounded no-write window for a clean momentum-only error.
	func _try_dissipate(
			basis: int,
			meter: int,
			domain: NetwPredictJournal.Domain,
			escalated: bool,
	) -> bool:
		var stats := _episode_stats()
		if stats[NetwPredictionEngine.STAT_EPISODE_ACTIVE] == 0 \
				or stats[NetwPredictionEngine.STAT_EPISODE_DISSIPATE_DECIDED] \
				!= 0 \
				or not _has_dissipate_fields():
			return false
		var eligibility := _dissipate_eligibility(basis, domain, meter, escalated)
		var eligible := bool(eligibility[&"eligible"])
		_charge_decision({
			&"operator": NetwPredictJournal.Operator.DISSIPATE,
			&"basis": basis,
			&"eligible": eligible,
			&"applied": eligible,
			&"eligibility": eligibility,
		})
		if eligible:
			_record_episode_write(
				NetwPredictJournal.Operator.DISSIPATE,
				basis,
				0,
				_entity.entity_id if _entity else &"body",
				false,
				true,
			)
		else:
			_sync_episode()
		return eligible


	# Proves that only withheld momentum fields cross their declared zero region.
	func _dissipate_eligibility(
			basis: int,
			domain: NetwPredictJournal.Domain,
			meter: int,
			escalated: bool,
	) -> Dictionary:
		var tolerances := _meter_tolerances(domain)
		var fields: Dictionary = { }
		var momentum_active := false
		var other_active := false
		for field: StringName in _handle.last_field_divergence:
			if _wiring.trigger_excludes.has(field) \
					and not _wiring.withheld.has(field):
				continue
			var error := float(_handle.last_field_divergence.get(field, INF))
			var tolerance := float(tolerances.get(
				field,
				_wiring.epsilon_overrides.get(
					field,
					_handle.divergence_epsilon,
				),
			))
			var active := NetwPredictionHandle._triggers(error, tolerance)
			var momentum := _wiring.withheld.has(field)
			fields[field] = {
				&"error": error,
				&"epsilon": tolerance,
				&"active": active,
				&"momentum": momentum,
			}
			if not active:
				continue
			if momentum:
				momentum_active = true
			elif not _wiring.trigger_excludes.has(field):
				other_active = true
		var basis_clean := _witness_row_clean(basis, true)
		var recent_clean := _recent_witness_clean(basis)
		var observing := _handle.resolved_recovery_policy() \
				== NetwPredict.RecoveryPolicy.OBSERVE
		var iface := _iface()
		return {
			&"eligible": iface != null and iface.native_dissipate_admissible(
				_entity,
				momentum_active,
				other_active,
				basis_clean,
				recent_clean,
				escalated,
				observing,
				meter,
			),
			&"momentum_active": momentum_active,
			&"other_active": other_active,
			&"basis_witness_clean": basis_clean,
			&"recent_witness_clean": recent_clean,
			&"mechanism": not observing \
					and _correction == NetwPredict.CorrectionMode.SNAP,
			&"fields": fields,
		}


	# Attaches later mismatches as taint or an agreeing-pre secondary generator.
	func _record_episode_divergence(transition: int) -> void:
		var iface := _iface()
		if not iface:
			return
		iface.native_record_episode_divergence(
			_entity,
			int(_pool_transitions.get(transition, transition)),
		)
		_sync_episode()


	# Retires the episode the pool closed on its verified agreement run.
	func _record_episode_comparison(previous_state: int) -> void:
		var state := _iface().native_episode_state(_entity) if _iface() else -1
		if previous_state == NetwPredict.EpisodeState.OPEN \
				and state == NetwPredict.EpisodeState.CLOSED:
			_close_episode()
			return
		_sync_episode()


	# Records one operator attempt as episode evidence and resets the clean run.
	func _record_episode_write(
			operator: NetwPredictJournal.Operator,
			basis: int,
			delta_fp: int,
			target: StringName,
			evidence_free: bool = false,
			null_operator: bool = false,
	) -> int:
		var iface := _iface()
		if not iface:
			return 0
		# Read here, where last_field_divergence still describes the comparison
		# that provoked this write. The verification window runs for several
		# transitions and the divergence moves inside it, so a verdict taken at
		# the end of the window cannot re-derive what the write was answering
		# when it was staged.
		var write_id := iface.native_record_episode_write(
			_entity,
			operator,
			basis,
			delta_fp,
			target,
			_handle.ack_age_ticks,
			_trigger_shape(),
			evidence_free,
			null_operator,
		)
		_sync_episode()
		return write_id


	# Stamps the payload fingerprint onto the write the pool planned, which
	# plans an operator without seeing the bytes the shell then wrote.
	func _stamp_episode_write_delta(delta_fp: int) -> void:
		var iface := _iface()
		if iface:
			iface.native_stamp_episode_write_delta(_entity, delta_fp)


	# Spends escalation evidence against the newest write the pool left pending,
	# or against the live trigger shape when there is none to name.
	func _record_non_contraction() -> void:
		var iface := _iface()
		if not iface:
			return
		iface.native_record_episode_escalation(_entity, _trigger_shape())
		_sync_episode()


	# What the fields past their own epsilon looked like when this recovery was
	# judged.
	#
	# TRIGGER_ALL_WITHHELD is the case K1 exists for: every field that materially
	# demanded the recovery is one no sub-teleport restore may write. "Every",
	# not "any" -- a recovery answering both a withheld field and a writable one
	# did have an operator for part of its job, and failing at that part is
	# ordinary non-contraction.
	#
	# TRIGGER_NONE is not withheld and is charged. In domain a correction is
	# raised by the exact fingerprint verdict, which can disagree while every
	# field sits inside its tolerance, so a recovery can be staged with nothing
	# over epsilon at all. Nothing was forbidden there, because nothing was
	# asking, and the aligned error the ladder failed to shrink was already
	# small.
	func _trigger_shape() -> int:
		var saw_trigger := false
		var saw_writable := false
		for field: StringName in _handle.last_field_divergence:
			if _wiring.trigger_excludes.has(field) or not _wiring.causal_fields.has(field):
				continue
			var epsilon := float(
				_wiring.epsilon_overrides.get(field, _handle.divergence_epsilon),
			)
			if not NetwPredictionHandle._triggers(
				float(_handle.last_field_divergence[field]),
				epsilon,
			):
				continue
			saw_trigger = true
			if not _wiring.withheld.has(field):
				saw_writable = true
		if not saw_trigger:
			return TRIGGER_NONE
		return TRIGGER_MIXED if saw_writable else TRIGGER_ALL_WITHHELD


	# Retires the episode after the domain-aware verified agreement run.
	func _close_episode() -> void:
		_handle._store_episode(_pool_episode())
		_handle.episode_closed.emit(_handle._episode_report(_pool_episode()))


	# Whether either structural recovery evidence budget is exhausted.
	func _episode_budget_exhausted() -> bool:
		var iface := _iface()
		return iface.native_episode_budget_exhausted(_entity) if iface \
				else false


	# Closes speculation and starts the coherent-authority quarantine proof.
	func _enter_fallback(
			transition: int,
			attribution: NetwPredictJournal.Attribution,
			demoted: bool = false,
	) -> void:
		var stream_was_reconstructed := _stream_reconstructed
		_fallback_latched = true
		var iface := _iface()
		if iface:
			iface.native_enter_quarantine(
				_entity,
				transition,
				stream_was_reconstructed,
				attribution,
				demoted,
			)
		_sync_episode()
		_handle.episode_fallback.emit(_handle._episode_report(_pool_episode()))
		_rewire(_resolved_declaration())


	# Counts distinct coherent authority frames while speculation is closed.
	func _on_quarantine_state_frame(header: Dictionary) -> void:
		var iface := _iface()
		if not _fallback_latched or iface == null:
			return
		var plan := iface.native_quarantine_state(
			_entity,
			int(header.get(&"tick", -1)),
			int(header.get(&"ack", -1)),
			_state_columns(header.get(&"payload", { })),
			bool(header.get(&"whole", true)),
		)
		_sync_episode()
		_begin_reseed(plan)


	# Counts one transition only when its authority witness summary arrives.
	func _apply_quarantine_witness(basis: int) -> void:
		var iface := _iface()
		if not _fallback_latched or iface == null:
			return
		var plan := iface.native_quarantine_witness(
			_entity,
			basis,
			int(_authority_witness_classes.get(basis, -1)),
		)
		_sync_episode()
		_begin_reseed(plan)


	# Seeds the closure the pool's proof selected and reopens a fresh command
	# epoch. The seed arrives advanced to the present tick, because a payload
	# applied at its own basis would replay the past into a live world.
	func _begin_reseed(plan: NetwPredictWritePlan) -> void:
		if plan == null or plan.skip():
			return
		var basis := plan.basis()
		_restore(
			_plan_of(plan)[&"write"],
			NetwPredictJournal.Operator.RESEED,
			basis,
			null,
			true,
			true,
		)
		var reseed_provenance := _pending_provenance.duplicate(true)
		_fallback_latched = false
		_rewire(_resolved_declaration())
		_pending_provenance = reseed_provenance
		_sync_episode()


	# A seed payload advanced to the present tick, the way a recovery's is. A
	# payload applied at its own basis seeds the body ack_age ticks in the past
	# and lets it fork from there.
	#
	# The channel projection stays gated on
	# NetwPredict.RestoreMode.EXTRAPOLATED, because an entity that declared
	# NetwPredict.RestoreMode.EXACT wants the acknowledged value and a reseed is
	# not entitled to overrule that for being a different operator. The
	# derivative-disagreement guard the recover path applies has nothing to
	# judge here: value and rate both come from the same authority closure, so
	# the only question is how far to advance it.
	func _advanced_seed(payload: Dictionary, basis: int) -> Dictionary:
		if payload.is_empty():
			return payload
		var carried := _carry_payload(payload, basis)
		if _handle.snap_restore != NetwPredict.RestoreMode.EXTRAPOLATED \
				or _wiring.projection.is_empty():
			return carried
		var span := clampi(
			_transitions_since(basis),
			0,
			_handle.max_restore_ticks,
		)
		if span <= 0:
			return carried
		return project_payload(
			carried,
			_wiring.projection,
			float(span) * _tick_delta,
		)


	# How stale a payload keyed at [param basis] is, in transitions, counted in
	# whichever book this tier records them in.
	#
	# [member NetwPredictionHandle.ack_age_ticks] answers this for the NEWEST
	# acknowledgement, and a reseed's payload is not always that one: the proof
	# accepts an older retained row when no in-window one has a clean witness.
	# Advancing such a payload by the live age understates its staleness by
	# exactly the gap. The alignment path passes the live acknowledgement and is
	# unaffected, which is why one expression serves both.
	func _transitions_since(basis: int) -> int:
		if basis < 0:
			return 0
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			return maxi(0, _next_tape_entry_index - basis - 1)
		return maxi(0, _last_driven_input_tick - basis)


	# Applies the one evidence-free post-epoch horizon alignment.
	func _finish_reseed_alignment(
			recv_tick: int,
			ack: int,
			payload: Dictionary,
	) -> void:
		# The shell stages this alignment itself, so nothing has minted its
		# write. NetwPredictionEngine.align_reseed plans the same operator and
		# has no caller yet.
		_restore(
			_advanced_seed(payload, ack),
			NetwPredictJournal.Operator.RESEED_ALIGN,
			ack,
			null,
			true,
		)
		# The timeline records the payload as authority stated it, not as the
		# seed advanced it. The record is what later transitions are compared
		# against, and it has to hold what authority said at that transition.
		if _timeline:
			_timeline.record_state(ack + 1, payload)
		if _correction == NetwPredict.CorrectionMode.REPLAY:
			_replay_reseed_horizon(ack)
		var iface := _iface()
		if iface:
			iface.native_adopt_alignment(
				_entity,
				ack,
				_last_driven_entry_index
				if _handle.schedule == NetwPredict.Schedule.FRAME
				else _latest_input_tick,
			)
		_sync_episode()
		_handle.state_evaluated.emit(recv_tick, ack, 0.0, false)


	# Re-runs the current epoch's unacknowledged commands from the aligned seed.
	func _replay_reseed_horizon(ack: int) -> void:
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			_replay_authored_entries(ack)
			return
		if not _timeline or not _input_binding:
			return
		var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
		var live_input := _input_binding.snapshot_payload()
		for entry: Dictionary in window:
			_run(entry[&"input"], _tick_delta, int(entry[&"tick"]), false)
			_timeline.record_state(int(entry[&"tick"]) + 1, _capture())
		_input_binding.apply_payload(live_input)
		_handle.stats.max_replay_depth = maxi(
			_handle.stats.max_replay_depth,
			window.size(),
		)


	# --- Owner and authority lanes ---


	# Ships the transitions still in flight with the command that drove each
	# fresh one, floored by what authority has acknowledged. Every send repeats
	# the whole unacknowledged range, so a lost datagram heals on the next frame
	# rather than on a retransmit.
	func _send_command_frame() -> void:
		var route := _route()
		if route < 0:
			return
		var bytes := build_command_frame()
		if not bytes.is_empty():
			_handle.stats.command_frames_sent += 1
		_send_lane(
			1,
			route,
			NetwFrameEnvelope.Channel.PREDICT_COMMAND,
			bytes,
		)


	# The frame the owner lane would ship this step, or empty when there is
	# nothing outstanding. Split from the send so the bytes are testable and so
	# the kernel half stays a pure function of the tape and the ack floor.
	func build_command_frame() -> PackedByteArray:
		var authors_commands := _role == NetwPredict.Role.PREDICT \
				or _role == NetwPredict.Role.REMOTE and _fallback_latched
		if not authors_commands or _authored_tape.is_empty():
			return PackedByteArray()
		var frame := NetwPredictCommandFrame.create(_input_schema())
		if frame == null:
			return PackedByteArray()
		frame.set_epoch(_tape_epoch)
		frame.set_ack_of_acks(_ack_of_acks)
		var window := maxi(1, _input_binding.set.window + 2)
		var keys := _input_keys()
		var rows: Array = []
		var oldest := maxi(
			_ack_of_acks + 1,
			int(_authored_tape.back().get("index", 0)) - window + 1,
		)
		for entry: Dictionary in _authored_tape:
			var index := int(entry.get("index", -1))
			if index < oldest:
				continue
			var label := int(entry.get("label", -1))
			var fresh := bool(entry.get("fresh", false))
			if not frame.append_transition(index, label, fresh):
				break
			rows.append(entry)
			if not fresh:
				continue
			var input := _timeline.input_at(label)
			var values: Array = []
			for key: StringName in keys:
				values.append(input.get(key, _stall_input.get(key)))
			frame.append_payload(values)
		if rows.is_empty():
			return PackedByteArray()
		# The owner claims a fingerprint only for what it has finished. A row still
		# open holds a zero authority would read as a claim of zero, so the section
		# covers the closed prefix of the window and stops at the first row the
		# owner cannot yet speak for.
		var iface := _iface()
		if not iface:
			return PackedByteArray()
		var closed := iface.native_journal_last_closed(_entity)
		for entry: Dictionary in rows:
			var index := int(entry.get("index", -1))
			if index > closed:
				break
			var row := _native_journal_row(index)
			if not row:
				break
			frame.append_evidence(
				row.evidence_mask(),
				row.pre_fp(),
				row.post_fp(),
				row.e_digest(),
				row.topo_fp(),
				row.witness_fp(),
				row.pre_families(),
				row.post_families(),
				row.raw_fp(),
			)
		return frame.to_bytes()


	# Ships what authority actually ran for each transition it replayed, floored
	# by what the owner has confirmed. A frame that drove nothing acknowledges
	# nothing, so the lane never claims a transition authority held.
	func _send_ack_frame() -> void:
		var route := _route()
		if route < 0 or not _entity:
			return
		_send_lane(
			_entity.controller,
			route,
			NetwFrameEnvelope.Channel.PREDICT_ACK,
			build_ack_frame(),
		)


	# The frame the authority lane would ship this step, or empty when authority
	# has acknowledged nothing outstanding.
	func build_ack_frame() -> PackedByteArray:
		if _role != NetwPredict.Role.CONSUME or _ack < 0:
			return PackedByteArray()
		# Authority acknowledges a transition only once its drive has produced the
		# state, because the fingerprint is the acknowledgement. An open row would
		# ship a zero the owner would read as a divergence against every one of
		# its own correctly predicted states.
		var iface := _iface()
		if not iface:
			return PackedByteArray()
		var closed := iface.native_journal_last_closed(_entity)
		var frontier := mini(_ack, closed)
		_handle.stats.journal_closed = closed
		_handle.stats.ack_frontier = frontier
		if frontier < 0:
			return PackedByteArray()
		return iface.native_build_ack_frame(
			_entity,
			_command_epoch,
			_ack,
			_owner_ack_floor,
		)


	# Decodes one owner frame on authority. A frame that lost a command is
	# dropped whole and counted, never parsed into a transition authority would
	# then have to invent a command for.
	# Files one relayed window into the command matrix. The relay is a fan-out
	# of the author's own frame, so it decodes with the author's codecs and
	# carries the author's labels, and a cell already filed under a transition
	# is left alone because the redundancy window re-sends what it already sent.
	#
	# A transition older than the matrix retains is dropped and counted rather
	# than filed, since a floor the group already moved past cannot be lowered
	# by a late frame.
	func receive_relayed_command_frame(payload: PackedByteArray) -> void:
		var frame := NetwPredictCommandFrame.from_bytes(
			_input_schema(),
			payload,
		)
		if frame == null:
			_handle.stats.frames_dropped_invalid += 1
			return
		var epoch := frame.epoch()
		var bumped := false
		if epoch != _relayed_epoch:
			bumped = _relayed_epoch >= 0
			_relayed_epoch = epoch
			_clear_command_cells()
			_last_relayed_transition = -1
			_newest_matrix_transition = -1
		var indices := frame.indices()
		var fresh_flags := frame.fresh_flags()
		var keys := _input_keys()
		var fresh_index := 0
		for at in indices.size():
			var index := indices[at]
			if not fresh_flags[at]:
				continue
			var values := frame.payload_at(fresh_index)
			fresh_index += 1
			if index < _matrix_floor():
				_handle.stats.relayed_dropped_late += 1
				continue
			var standing := int(command_cell_at(index).get(&"origin", -1))
			# A relayed cell replaces a predicted one, because authorship
			# supersedes a guess about it. Two relays of the same transition
			# are the redundancy window re-sending what it already sent.
			if standing == NetwPredict.CommandOrigin.RELAYED:
				continue
			var command: Dictionary = { }
			for position in keys.size():
				if position < values.size():
					command[keys[position]] = values[position]
			var displaced := standing == NetwPredict.CommandOrigin.PREDICTED
			_file_command_cell(index, command, NetwPredict.CommandOrigin.RELAYED)
			_last_relayed_transition = maxi(_last_relayed_transition, index)
			_handle.stats.relayed_recorded += 1
			if _handle.reconcile_mode != NetwPredict.Reconcile.JOINT:
				continue
			if bumped:
				bumped = false
				_joint_epoch_floor = maxi(_joint_epoch_floor, index)
				_note_floor_move(&"epoch", index)
			# The author's own command superseding a guess is what makes the
			# transitions past it worth re-running. A cell that arrived where
			# nothing stood in for it changes no history.
			if displaced:
				var floored := index - 1
				_joint_relay_floor = floored if _joint_relay_floor < 0 \
						else mini(_joint_relay_floor, floored)
				_note_floor_move(&"relay", floored)


	# Files one cell and keeps the matrix bounded by the transition it names.
	func _file_command_cell(
			transition: int,
			command: Dictionary,
			origin: NetwPredict.CommandOrigin,
	) -> void:
		_newest_matrix_transition = maxi(_newest_matrix_transition, transition)
		var iface := _iface()
		if iface:
			iface.native_joint_record(
				_entity,
				transition,
				[],
				command,
				false,
				origin == NetwPredict.CommandOrigin.RELAYED,
				origin == NetwPredict.CommandOrigin.PREDICTED,
			)


	# The oldest transition the matrix still answers for.
	func _matrix_floor() -> int:
		return _newest_matrix_transition - TAPE_HISTORY_LIMIT + 1


	# The pool bounds its own cell history, so a re-keyed epoch is the only
	# thing the shell still has to say about it.
	func _clear_command_cells() -> void:
		var iface := _iface()
		if iface:
			iface.native_joint_clear(_entity)


	# Transitions since the last relayed cell, or -1 when none was ever filed.
	# Predicted cells do not answer this, because a predictor asking how stale
	# its information is means the real thing rather than its own last guess.
	func relayed_command_age(transition: int) -> int:
		if _last_relayed_transition < 0:
			return -1
		return transition - _last_relayed_transition


	# The freshest relayed command, or an empty dictionary when none was filed.
	func last_relayed_command() -> Dictionary:
		if _last_relayed_transition < 0:
			return { }
		var cell := command_cell_at(_last_relayed_transition)
		return cell.get(&"command", { })


	# What drove one transition, with where it came from, or empty for a
	# transition the matrix does not answer for.
	# What drove one transition, with where it came from, or empty for a
	# transition no relayed or substituted command answers for. An authored or
	# coasted cell is not one of these, so it reads as absent here exactly as
	# it always has.
	func command_cell_at(transition: int) -> Dictionary:
		var iface := _iface()
		if iface == null:
			return { }
		match iface.native_joint_provenance(_entity, transition):
			NetwPredict.CellProvenance.RELAYED:
				return {
					&"command": iface.native_joint_command(_entity, transition),
					&"origin": NetwPredict.CommandOrigin.RELAYED,
				}
			NetwPredict.CellProvenance.SUBSTITUTED:
				return {
					&"command": iface.native_joint_command(_entity, transition),
					&"origin": NetwPredict.CommandOrigin.PREDICTED,
				}
		return { }


	func receive_command_frame(payload: PackedByteArray) -> void:
		if _role != NetwPredict.Role.CONSUME:
			return
		_handle.stats.command_frames_received += 1
		var frame := NetwPredictCommandFrame.from_bytes(
			_input_schema(),
			payload,
		)
		if frame == null:
			_handle.stats.frames_dropped_invalid += 1
			return
		_admit_command_frame(frame, _input_keys())


	# Files a decoded owner frame into the replay queue, deduplicating the
	# redundancy overlap the way the lane's re-sent windows require.
	func _admit_command_frame(
			frame: NetwPredictCommandFrame,
			keys: Array,
	) -> void:
		var epoch := frame.epoch()
		if epoch != _command_epoch:
			_command_epoch = epoch
			_command_queue.clear()
			_pool_transitions.clear()
			var iface := _iface()
			if iface:
				iface.native_tape_reset(_entity, epoch)
			# Transitions are injective only within an epoch, so a claim from the
			# previous one names a transition that is about to be reused.
			_owner_claims.clear()
			# The journal is transition-keyed like the claims: a retained row
			# would answer for a reused index and the ack lane would ship the
			# previous epoch's fingerprints and witness bits as this epoch's
			# verdicts. Re-keying keeps every acknowledgement inside the epoch
			# that produced it.
			_witness_details.clear()
			_witness_verdicts.clear()
			_deferred_operator_states.clear()
			_operator_deferred_basis = -1
			_owner_ack_floor = -1
			_replay_cursor = -1
			_replay_warmed = false
			_last_replayed_label = -1
			_last_replayed_fresh = false
			_ack = -1
			_last_input = { }
		_owner_ack_floor = maxi(_owner_ack_floor, frame.ack_of_acks())
		var indices := frame.indices()
		var labels := frame.labels()
		var fresh_flags := frame.fresh_flags()
		# The fingerprint section covers the closed prefix of the same transition
		# run, so entry i of the columns describes transition i of the run.
		var post_fps := frame.post_fps()
		var pre_fps := frame.pre_fps()
		var e_digests := frame.e_digests()
		var pre_family_fps := frame.pre_family_fps()
		var post_family_fps := frame.post_family_fps()
		var topo_fps := frame.topo_fps()
		var witness_fps := frame.witness_fps()
		var raw_fps := frame.raw_fps()
		var evidence_masks := frame.evidence_masks()
		for i in post_fps.size():
			if i >= indices.size():
				break
			var claimed := indices[i]
			if claimed < 0:
				continue
			_owner_claims[claimed] = {
				&"pre_fp": pre_fps[i],
				&"post_fp": post_fps[i],
				&"e_digest": e_digests[i],
				&"topo_fp": topo_fps[i],
				&"witness_fp": witness_fps[i],
				&"raw_fp": raw_fps[i],
				&"evidence_mask": evidence_masks[i],
				&"pre_pose_fp": pre_family_fps[i * 3],
				&"pre_momentum_fp": pre_family_fps[i * 3 + 1],
				&"pre_controller_fp": pre_family_fps[i * 3 + 2],
				&"post_pose_fp": post_family_fps[i * 3],
				&"post_momentum_fp": post_family_fps[i * 3 + 1],
				&"post_controller_fp": post_family_fps[i * 3 + 2],
			}
		while _owner_claims.size() > TAPE_HISTORY_LIMIT:
			var claims: Array = _owner_claims.keys()
			claims.sort()
			_owner_claims.erase(claims.front())
		var fresh_seen := 0
		for at in indices.size():
			var index := indices[at]
			if index < 0:
				continue
			var fresh := fresh_flags[at] != 0
			var label := labels[at]
			var command: Dictionary = { }
			if fresh:
				if fresh_seen >= frame.payload_count():
					return
				var values := frame.payload_at(fresh_seen)
				fresh_seen += 1
				for i in mini(keys.size(), values.size()):
					command[keys[i]] = values[i]
			if _command_queue.has(index):
				continue
			_command_queue[index] = {
				"label": label,
				"fresh": fresh,
				"command": command,
			}
			_arrivals_this_frame += 1
			# The TICK lane feeds the same drain the input stream fed. The
			# transition is the tick, so its command records at its label and the
			# consume cursor walks it with depth measured in ticks.
			if _handle.schedule == NetwPredict.Schedule.TICK \
					and fresh and label >= 0:
				_timeline.record_input(label, command)
				if _next_input_tick < 0:
					_next_input_tick = label
		while _command_queue.size() > TAPE_HISTORY_LIMIT:
			var queued: Array = _command_queue.keys()
			queued.sort()
			_command_queue.erase(queued.front())
		if _replay_cursor < 0 and not _command_queue.is_empty():
			var open_at: Array = _command_queue.keys()
			open_at.sort()
			_replay_cursor = int(open_at.front())
		_handle.stats.command_queue_depth = _command_queue.size()
		_refresh_tape_diagnostics()


	func receive_ack_frame(payload: PackedByteArray) -> void:
		if _role != NetwPredict.Role.PREDICT and not _fallback_latched:
			return
		var frame := NetwPredictAckFrame.from_bytes(payload)
		if frame == null:
			_handle.stats.frames_dropped_invalid += 1
			return
		if frame.epoch() != _tape_epoch:
			return
		# An epoch-matched ack frame proves authority speaks this tape's
		# numbering, so state-row acks may feed the frontier again.
		_ack_domain_confirmed = true
		var epoch_iface := _iface()
		if epoch_iface:
			epoch_iface.native_confirm_reseed_epoch(_entity)
		_on_ack_run(
			frame.base(),
			frame.pre_fps(),
			frame.c_hashes(),
			frame.e_digests(),
			frame.post_fps(),
			frame.pre_family_fps(),
			frame.post_family_fps(),
			frame.topo_fps(),
			frame.witness_fps(),
			frame.raw_fps(),
			frame.evidence_masks(),
			frame.flags(),
			frame.witness_class_bits(),
		)


	# Consumes one acknowledgement run. Authority sends the fingerprint of the
	# state its own replay produced, so the owner compares two independently
	# recorded post-states and needs no authoritative payload to reach a verdict.
	#
	# A transition that disagrees is charged to an antecedent here, where both
	# peers' command hashes and environment digests are in hand. _on_state sees
	# only its own row and one authoritative payload, so it could name the
	# disagreement but never its cause.
	func _on_ack_run(
			base: int,
			pre_fps: PackedInt32Array,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			post_fps: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
			flags: PackedByteArray,
			witness_class_bits: PackedByteArray = PackedByteArray(),
	) -> void:
		for i in flags.size():
			var transition := base + i
			_ack_of_acks = maxi(_ack_of_acks, transition)
			_latest_authority_ack = maxi(_latest_authority_ack, transition)
			if i < witness_class_bits.size():
				_authority_witness_classes[transition] = witness_class_bits[i]
				if _fallback_latched:
					_apply_quarantine_witness(transition)
			var row := _native_journal_row(transition)
			var complete := _run_complete(
				i,
				pre_fps,
				c_hashes,
				e_digests,
				pre_family_fps,
				post_family_fps,
				topo_fps,
				witness_fps,
				raw_fps,
				evidence_masks,
			)
			if flags[i] & NetwPredictAckFrame.FLAG_SUBSTITUTED:
				_admit_ack_to_pool(
					transition,
					i,
					complete,
					true,
					pre_fps,
					c_hashes,
					e_digests,
					post_fps,
					pre_family_fps,
					post_family_fps,
					topo_fps,
					witness_fps,
					raw_fps,
					evidence_masks,
				)
				_handle.stats.substituted += 1
			elif row and i < post_fps.size():
				# The lane re-sends a run until it is trimmed, so a transition
				# already judged must not be judged again.
				if not (row.flags() & NetwPredictJournal.ROW_ACKED):
					var pool_verdict := _admit_ack_to_pool(
						transition,
						i,
						complete,
						false,
						pre_fps,
						c_hashes,
						e_digests,
						post_fps,
						pre_family_fps,
						post_family_fps,
						topo_fps,
						witness_fps,
						raw_fps,
						evidence_masks,
					)
					var matched := row.post_fp() == post_fps[i]
					_record_ack_verdict(transition, matched)
					if not matched and pool_verdict:
						_charge_divergence(
							transition,
							row,
							pool_verdict,
							witness_fps,
							evidence_masks,
							i,
							complete,
						)
		_handle.stats.ack_confirmed = _ack_of_acks
		while _authority_witness_classes.size() > TAPE_HISTORY_LIMIT:
			var retained := _authority_witness_classes.keys()
			retained.sort()
			_authority_witness_classes.erase(retained[0])
		_refresh_owner_ack_age()


	# Whether the run carried every column for one of its rows. A run is trimmed
	# to what fit the lane, so its tail can still be judged equal or unequal
	# while being unable to name what differed.
	static func _run_complete(
			index: int,
			pre_fps: PackedInt32Array,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
	) -> bool:
		var family_end := (index + 1) * 3
		return index < pre_fps.size() \
				and index < c_hashes.size() \
				and index < e_digests.size() \
				and index < topo_fps.size() \
				and index < witness_fps.size() \
				and index < raw_fps.size() \
				and index < evidence_masks.size() \
				and family_end <= pre_family_fps.size() \
				and family_end <= post_family_fps.size()


	# Hands the pool the far peer's columns for one acknowledged transition, so
	# the pool reaches a verdict from the two rows rather than from this lane's.
	#
	# A run that carried no columns for this row hands over zeros rather than
	# whatever the lane last held, because an absent column and a zeroed one are
	# the same evidence and only the completeness flag separates them.
	func _admit_ack_to_pool(
			transition: int,
			index: int,
			complete: bool,
			substituted: bool,
			pre_fps: PackedInt32Array,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			post_fps: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
	) -> NetwPredictVerdict:
		var iface := _iface()
		var pool_transition := int(_pool_transitions.get(transition, -1))
		if iface == null or pool_transition < 0:
			return null
		var family_end := (index + 1) * 3
		var absent := PackedInt32Array([0, 0, 0])
		return iface.native_admit_ack(
			_entity,
			pool_transition,
			pre_fps[index] if complete else 0,
			c_hashes[index] if complete else 0,
			e_digests[index] if complete else 0,
			post_fps[index] if index < post_fps.size() else 0,
			pre_family_fps.slice(index * 3, family_end) if complete else absent,
			post_family_fps.slice(index * 3, family_end) if complete else absent,
			topo_fps[index] if complete else 0,
			witness_fps[index] if complete else 0,
			raw_fps[index] if complete else 0,
			evidence_masks[index] if complete else 0,
			complete,
			substituted,
		)


	# The witness columns are indexed rather than addressed because the caller
	# is already walking them.
	func _charge_divergence(
			transition: int,
			row: NetwPredictJournalRow,
			pool_verdict: NetwPredictVerdict,
			witness_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
			index: int,
			complete: bool,
	) -> void:
		var local_evidence := row.evidence_mask()
		var peer_evidence := evidence_masks[index] if complete else 0
		var both_witness := complete \
				and bool(local_evidence & NetwPredictJournal.EVIDENCE_WITNESS) \
				and bool(peer_evidence & NetwPredictJournal.EVIDENCE_WITNESS)
		var witness_equal := both_witness \
				and row.witness_fp() == witness_fps[index]
		_witness_verdicts[transition] = 1 if witness_equal else 2
		while _witness_verdicts.size() > TAPE_HISTORY_LIMIT:
			var verdict_transitions := _witness_verdicts.keys()
			verdict_transitions.sort()
			_witness_verdicts.erase(verdict_transitions[0])
		_handle.last_attribution = pool_verdict.attribution()
		_handle.last_attributed_transition = transition
		if not complete:
			_retry_deferred_operator(transition)
			return
		_retry_deferred_operator(transition)


	# One place counts a fingerprint verdict, so the state-frame path and the ack
	# lane can never both count the same transition.
	func _record_ack_verdict(transition: int, matched: bool) -> void:
		_handle.stats.fp_verified += 1
		if matched:
			return
		_handle.stats.fp_mismatches += 1
		if _handle.stats.first_divergent_transition < 0:
			_handle.stats.first_divergent_transition = transition


	# The sealed shape one command payload row carries, which the frame codec
	# plans both lanes against.
	func _input_schema() -> SchemaRecord:
		if not _input_binding or not _input_binding.set:
			return null
		return _input_binding.set.volatile_schema


	# The volatile input fields in set order, parallel to a payload row, which is
	# what turns a decoded row back into a command keyed by field.
	func _input_keys() -> Array[StringName]:
		var keys: Array[StringName] = []
		if not _input_binding or not _input_binding.set:
			return keys
		for field in _input_binding.set.columns:
			if field.lane == NetwPropertySet.Lane.VOLATILE:
				keys.append(field.key)
		return keys


	# Both lanes are unreliable and batched with the tick pump, since each send
	# already repeats the range still in flight.
	func _send_lane(
			peer: int,
			route: int,
			channel: NetwFrameEnvelope.Channel,
			bytes: PackedByteArray,
	) -> void:
		if bytes.is_empty():
			return
		var iface := _iface()
		var api := iface._api() if iface else null
		if not api:
			return
		api._replication.send_to(peer, route, channel, bytes, false, 0, "", true)


	func _route() -> int:
		var iface := _iface()
		var api := iface._api() if iface else null
		var liveness := api._liveness if api else null
		return liveness.route_of(_entity) if liveness and _entity else -1


	# Closes the authority-side journal row with the state its drive produced. The
	# recorder owns the server's post-solve capture, so the close happens where
	# that capture already lands rather than duplicating it here.
	func finalize_recorded_state(payload: Dictionary) -> void:
		if _role == NetwPredict.Role.PREDICT:
			return
		var transition := _recorded_transition()
		var fingerprint := _state_fingerprint(payload)
		_close_journal_row(transition, payload)
		_judge_owner_claim(transition, fingerprint)


	# Judges what the owner claimed for a transition authority has just finished.
	#
	# Authority holds a timeline per peer and checks it here, which is the whole
	# of the check: nothing is pushed back, so a mismatch leaves the owner's
	# simulation exactly where it was and leaves the game to decide what a run of
	# them means.
	func _judge_owner_claim(transition: int, fingerprint: int) -> void:
		if not _owner_claims.has(transition):
			return
		var claim: Dictionary = _owner_claims[transition]
		_owner_claims.erase(transition)
		_handle.stats.client_fp_verified += 1
		if int(claim[&"post_fp"]) == fingerprint:
			return
		_handle.stats.client_mismatches += 1
		# Authority ran the owner's own command unless it substituted one, so the
		# command antecedent is answered by whether it had to. The environment is
		# answered by the digest the owner shipped beside its fingerprint, which is
		# the only way authority can tell a world it disagrees about from a step
		# function it disagrees about.
		var row := _native_journal_row(transition)
		if not row:
			return
		var substituted := bool(
			row.flags() & NetwPredictJournal.ROW_SUBSTITUTED,
		)
		var both_witness := bool(
			row.evidence_mask() & NetwPredictJournal.EVIDENCE_WITNESS,
		) and bool(
			int(claim[&"evidence_mask"])
			& NetwPredictJournal.EVIDENCE_WITNESS,
		)
		var iface := _iface()
		var pool_transition := int(_pool_transitions.get(transition, -1))
		if iface and pool_transition >= 0:
			iface.native_mark_witness_match(
				_entity,
				pool_transition,
				both_witness and int(claim[&"witness_fp"]) \
						== row.witness_fp(),
			)
		var attribution := attribute(
			int(claim[&"pre_fp"]) == row.pre_fp(),
			not substituted,
			int(claim[&"e_digest"]) == row.e_digest(),
			int(claim[&"topo_fp"]) == row.topo_fp(),
			int(claim[&"raw_fp"]) == row.raw_fp(),
			int(claim[&"witness_fp"]) == row.witness_fp(),
			row.evidence_mask(),
			int(claim[&"evidence_mask"]),
		)
		if iface and pool_transition >= 0:
			iface.native_mark_attribution(
				_entity,
				pool_transition,
				attribution,
			)
		var local_family := PackedInt32Array()
		var peer_family := PackedInt32Array()
		if attribution == NetwPredictJournal.Attribution.PRE_STATE:
			local_family = row.pre_families()
			peer_family = PackedInt32Array([
				int(claim[&"pre_pose_fp"]),
				int(claim[&"pre_momentum_fp"]),
				int(claim[&"pre_controller_fp"]),
			])
		elif attribution == NetwPredictJournal.Attribution.CLOSURE:
			local_family = row.post_families()
			peer_family = PackedInt32Array([
				int(claim[&"post_pose_fp"]),
				int(claim[&"post_momentum_fp"]),
				int(claim[&"post_controller_fp"]),
			])
		if iface and pool_transition >= 0:
			iface.native_mark_differing_family(
				_entity,
				pool_transition,
				differing_family(local_family, peer_family),
			)
		if iface and _entity:
			iface.peer_divergence.emit(_entity.controller, transition, attribution)


	# The transition the recorder's slot belongs to. FRAME replays acknowledge the
	# entry index they replayed, every other tier drives its own label.
	func _recorded_transition() -> int:
		if _handle.schedule == NetwPredict.Schedule.FRAME \
				and _role == NetwPredict.Role.CONSUME:
			return _ack
		return _handle.stats.last_drive_label


	# The route, the session-global integer identity that replicates to every
	# peer, so two peers step the same roster in the same order without
	# exchanging anything about it. An entity with no route yet steps after
	# every routed one rather than before, which keeps a roster
	# mid-declaration from reordering what it already declared.
	func order_key() -> int:
		var route := _route()
		return route if route > 0 else _UNROUTED_ORDER_KEY


	func history_record_tick(fallback_tick: int) -> int:
		if _role == NetwPredict.Role.CONSUME \
				and _handle.schedule == NetwPredict.Schedule.FRAME:
			return _last_replayed_label + 1 \
			if _ack_advanced and _last_replayed_fresh else -1
		if _role == NetwPredict.Role.CONSUME and _ack >= 0:
			# A tick that consumed nothing produced no new authoritative state, so
			# the slot keeps the snapshot the last real consume left there instead
			# of being overwritten with a body that has coasted past the ack.
			return _ack + 1 if _ack_advanced else -1
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			return _last_driven_input_tick + 1 if _ack_advanced else -1
		return fallback_tick


	# True when the last pass replayed a transition that declines a history
	# slot. A REPEAT entry re-runs its label, so the label's slot keeps the
	# fresh entry's state, but the journal row the replay opened still owes its
	# close, or the acknowledgement frontier stalls behind it forever.
	func consumed_unslotted_transition() -> bool:
		return _role == NetwPredict.Role.CONSUME \
				and _handle.schedule == NetwPredict.Schedule.FRAME \
				and _ack_advanced and not _last_replayed_fresh


	func has_consumed_state_tick(state_tick: int) -> bool:
		if _role != NetwPredict.Role.CONSUME:
			return true
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			return _last_replayed_label + 1 >= state_tick
		return _ack >= 0 and _ack + 1 >= state_tick


	func resolved_correction_mode() -> NetwPredict.CorrectionMode:
		return _correction


	func record_server_input(tick: int, input: Dictionary) -> void:
		if _role != NetwPredict.Role.CONSUME or not _timeline:
			return
		_timeline.record_input(tick, input)
		if _next_input_tick < 0:
			_next_input_tick = tick


	func _iface() -> LagCompCore:
		return _iface_ref.get_ref() as LagCompCore if _iface_ref else null


	# The correction this slot will actually run. AUTO is the owner's answer,
	# and the pool holds it because the pool holds the bind.
	func _resolve_correction(declared: int) -> NetwPredict.CorrectionMode:
		var iface := _iface()
		if not iface:
			return NetwPredictionHandle.resolve_correction_mode_for(
				_entity.owner if _entity else null,
				declared,
			)
		return iface.native_resolve_correction(_entity, declared) \
				as NetwPredict.CorrectionMode


	func _native_journal_row(transition: int) -> NetwPredictJournalRow:
		var iface := _iface()
		if not iface:
			return null
		var pool_transition := int(
			_pool_transitions.get(transition, transition),
		)
		return iface.native_journal_row(_entity, pool_transition)


	func _native_journal_transitions() -> PackedInt64Array:
		var iface := _iface()
		if not iface:
			return PackedInt64Array()
		var reverse: Dictionary = { }
		for transition: int in _pool_transitions:
			reverse[_pool_transitions[transition]] = transition
		var out := PackedInt64Array()
		for pool_transition: int in iface.native_journal_transitions(_entity):
			out.append(int(reverse.get(pool_transition, pool_transition)))
		return out


	# Declaration state is the session's answer, so the shell resolves it and an
	# engine with no shell re-wires on the model it was last handed.
	func _resolved_declaration() -> Declaration:
		var iface := _iface()
		if iface and _entity:
			return iface.declaration_of(_entity)
		return _declaration


	func _on_control_changed(_previous_peer: int, _peer: int) -> void:
		_rewire(_resolved_declaration())


	func _on_reparented(_reparent: NetwEntity.ReparentOpts) -> void:
		_apply_scene_island_defaults()
		_rewire(_resolved_declaration())


	# Inherits the containing scene's rule unless this entity declared its own.
	func _apply_scene_island_defaults() -> void:
		if _handle.island.declared and not _handle.island.inherited:
			return
		var host := _entity.scene.record
		var inherited := host.prediction.island._inheritable() \
		if host != null and host != _entity else null
		if inherited == null and not _handle.island.inherited:
			return
		_handle.island = inherited


	# Resolves the role from the declared authority and binds the set handles.
	# Idempotent so it re-runs on control transfer.
	func _rewire(declaration: Declaration) -> void:
		_declaration = declaration
		if not _entity or not is_instance_valid(_entity.owner):
			return
		var state_binding := declaration.state
		var input_binding := declaration.input
		if not state_binding or not input_binding:
			return
		var was_wired := _state_binding != null
		var same_stream := _state_binding == state_binding
		_state_binding = state_binding
		_input_binding = input_binding

		_unregister_from_loop()
		# The role decides the whole feed, so it is built here as one record and
		# installed once below. A fresh record IS the cleared state, so a role
		# that declares nothing un-installs the previous role's wiring.
		var feed := NetwPredict.Feed.new()
		_timeline = null
		_wiring.projection = { }
		_wiring.pose_fields = { }
		_wiring.teleport_thresholds = { }
		_wiring.state_family_of = { }
		_wiring.causal_fields = { }
		_wiring.angle_fields = { }
		_wiring.converge_rules = { }
		_cooldown_until_tick = -1
		# A rewire re-keys the transitions a pending write was staged against, so
		# no comparison after it can still judge that write.
		_ledger_pending_error.clear()
		_ledger_pending_basis.clear()
		# A rewire re-keys the transitions the evidence was gathered over, so a
		# streak cannot mean anything across it.
		_reset_recovery_trackers()
		_sync_episode()
		_pending_provenance = { }
		_witness_details = { }
		_pool_transitions = { }
		_open_topology_facts = { }
		_previous_witness_sleeping = false
		_has_previous_witness = false
		_invalid_witness_reported = false
		_invalid_command_predictor_reported = false
		_joint_refusal_reported = false
		_witness_verdicts = { }
		_authority_witness_classes = { }
		_deferred_operator_states = { }
		_operator_deferred_basis = -1
		_raw_fp_enabled = not OS.get_environment(RAW_FP_ENV).is_empty()
		_latest_input_tick = -1
		_last_driven_input_tick = -1
		_last_frame_transition_tick = -1
		# A rewire is peer-local, so the next drive re-anchors on the declared
		# quantum rather than reporting the gap the handoff opened.
		_last_drive_frame = -1
		_last_quantum = _declared_quantum_value
		_refresh_simulation_gate()
		_last_recorded_input_tick = -1
		_frame_input = { }
		_stall_input = input_binding.snapshot_payload()
		_tape_epoch = (_tape_epoch + 1) & 0xFF
		_next_tape_entry_index = 0
		_last_driven_entry_index = -1
		_last_recorded_entry_index = -1
		_authored_tape.clear()
		_entry_history = NetwTimeline.create(TAPE_HISTORY_LIMIT)
		# Reconstruction is a fact about the field stream, not about transition
		# numbering. The masked lane earns it once, at the gain edge where every
		# field arrives together, and the binding's merged row stays coherent
		# from then on. A rewire re-keys transitions and keeps that binding, so
		# clearing this would demand a second gain edge the sender has no reason
		# to produce: past the edge it ships only the fields that changed, and a
		# row carrying every field again is a coincidence a latched value like a
		# steering sign can withhold forever. The comparison is the only thing
		# that reads it, so the entity would predict unreconciled and report
		# agreement for the rest of the session. Only a genuinely new stream
		# owes a fresh edge.
		if not same_stream:
			_stream_reconstructed = false
		_handle.stats.fp_verified = 0
		_handle.stats.fp_mismatches = 0
		_handle.stats.chain_breaks = 0
		# A rewire re-keys the transitions a window was expressed in, so an open
		# window cannot mean anything across it and is dropped rather than carried.
		_out_of_domain_until = -1
		_e_digest = 0
		_island_epoch = -1
		_sensor_samples = { }
		_command_queue.clear()
		_command_epoch = -1
		_ack_of_acks = -1
		_latest_authority_ack = -1
		# In-flight state rows can still carry the dead tape's ack numbering.
		# Hold the state lane out of the frontier until the epoch-checked ack
		# lane proves authority is producing this tape's numbering.
		if was_wired:
			_ack_domain_confirmed = false
		_owner_ack_floor = -1
		_handle.stats.command_queue_depth = 0
		_handle.stats.ack_confirmed = -1
		_handle.stats.first_divergent_transition = -1
		_handle.last_attributed_transition = -1
		_replay_cursor = -1
		_replay_warmed = false
		_consume_warmed = false
		_last_replayed_label = -1
		_last_replayed_fresh = false
		_handle.stats.tape_epoch = -1
		_handle.stats.tape_index = -1
		_handle.stats.tape_queue_depth = 0

		var iface := _iface()
		if iface:
			_adopt_timing(iface.frame_timing())
		_role = _resolve_axes()
		if _role != NetwPredict.Role.PREDICT:
			_clear_island_promotions()
		_correction = _resolve_correction(_handle.correction_mode)
		_error_on_retained_predicted_props(state_binding.set)
		_build_restore_projection(state_binding)
		_validate_property_classes(state_binding.set)
		if iface:
			# The declared mode goes in and the RESOLVED one comes back, because
			# AUTO is the bound owner's answer and the bind happens in there.
			var resolved := iface.configure_native_prediction(
				_entity,
				state_binding,
				_input_binding,
				_handle.schedule,
				_role,
				_handle.correction_mode,
				_handle.snap_restore,
				_handle.max_restore_ticks,
				_pool_island(),
				not _wiring.carry_rules.is_empty(),
				_handle.witness_contacts.is_valid(),
			)
			if resolved >= 0:
				_correction = resolved as NetwPredict.CorrectionMode
			iface.native_tape_reset(_entity, _tape_epoch)
			_push_carry_rules(iface)
		match _role:
			NetwPredict.Role.PREDICT:
				# Owning client owns a local predicted timeline. The server's
				# authoritative history lives in the registry, never here.
				_timeline = NetwTimeline.create()
				_entity.timeline = _timeline
				# The command lane is the input carrier: each sample rides with
				# the transition it drove, so the input set never pumps SYNC and
				# survives as declaration and codec.
				feed.input_volatile_external = true
				# The authoritative state reconciles against the prediction rather
				# than snapping the predicted body, so the network never overwrites it.
				feed.state_write_gate = false
				feed.state_on_applied = _on_state_frame
				_latest_input_tick = -1
				_register_with_loop()
			NetwPredict.Role.CONSUME:
				# Server reads the registry timeline; received input records into it.
				_timeline = _registry_timeline()
				feed.input_write_gate = false
				feed.input_on_applied = _on_input_frame
				_next_input_tick = -1
				_ack = -1
				_last_input = { }
				_register_with_loop()
			NetwPredict.Role.HOST_LOCAL:
				_timeline = _registry_timeline()
				_register_with_loop()
			NetwPredict.Role.SIMULATE:
				# Authority rows are decoded without snapping, then projected to the
				# current transition before each independent rebase.
				_timeline = NetwTimeline.create()
				_entity.timeline = _timeline
				feed.input_volatile_external = true
				feed.state_write_gate = false
				feed.state_on_applied = _on_simulated_state_frame
				_register_with_loop()
			NetwPredict.Role.REMOTE:
				if _fallback_latched:
					# Fallback still owns the command lane. It receives authority
					# state for display but runs no speculative simulation.
					_timeline = NetwTimeline.create()
					_entity.timeline = _timeline
					feed.input_volatile_external = true
					feed.state_on_applied = _on_quarantine_state_frame
					_register_with_loop()
		state_binding.apply_state_feed(feed)
		input_binding.apply_input_feed(feed)
		# The rewire changed what simulates the body, which is an input to role
		# resolution, so the display has to answer the question again.
		var display_api := _entity.multiplayer
		if display_api:
			display_api._display._mark_role_dirty(_entity.rid)


	# Server roles read the registry-owned timeline, get-or-creating it so the order
	# of state-set registration and engine wiring does not matter. Where no
	# registry answers, the declaration carries the history instead.
	func _registry_timeline() -> NetwTimeline:
		var iface := _iface()
		return iface.register_timeline(_entity) if iface else _declaration.timeline


	# Reads the per-field recovery marks once per rewire, so the reconcile path
	# only reads the maps.
	#
	# The forward model comes from each property's own carry_along() or
	# carry_step() mark rather
	# than from its NetwInterpolate spec. The two decide different things: one is
	# how a recovery advances an acknowledged value to the present, the other is
	# how a remote display extrapolates past its newest sample. Reading the
	# display spec for both meant a forecast-tail choice silently moved the
	# teleport tier's boundary. ANGLE is still read from the spec, because it
	# describes the value's own topology and every comparison needs it.
	func _build_restore_projection(binding: NetwPropertySetBinding) -> void:
		# The wiring record IS what this function produces, so it is replaced
		# rather than cleared field by field. Replaced before the early return
		# below, so a rewire onto an invalid node leaves an empty wiring exactly
		# as clearing did, rather than leaving the previous entity's tables in
		# force.
		_wiring = NetwPredict.Wiring.new()
		_refresh_wiring_scalars()
		_carry_dirty_records = { }
		var node := binding.node()
		if not is_instance_valid(node) or not binding.set:
			return
		var field_keys: Dictionary[StringName, bool] = { }
		for field in binding.set.columns:
			field_keys[field.key] = true
			_wiring.state_family_of[field.key] = STATE_FAMILY_CONTROLLER
			if field.property_class == NetwPropertySet.PropertyClass.CAUSAL:
				_wiring.causal_fields[field.key] = true
			else:
				# The class is the declaration that this value is not an
				# antecedent, so it is also the declaration that its disagreement
				# is not evidence of a fork. Both halves of the vote's exclusion
				# are written here, one per source, so the set the vote reads is
				# never a set some other reader also depends on.
				_wiring.vote_excludes[field.key] = true
		for field in binding.set.columns:
			var spec := NetwScriptModel.get_node_property_interpolator(node, field.key)
			if spec and spec.mode == NetwInterpolate.MODE_ANGLE:
				_wiring.angle_fields[field.key] = true
			# A channel this set does not carry cannot be read at the same tick as
			# the field it advances, so the pair is not one the engine can honour.
			var channel := binding.carry_channel_of(field.key)
			if channel == &"" or not field_keys.has(channel):
				continue
			_wiring.state_family_of[field.key] = STATE_FAMILY_POSE
			_wiring.state_family_of[channel] = STATE_FAMILY_MOMENTUM
			_wiring.projection[field.key] = channel
			_wiring.pose_fields[field.key] = true
		# The per-field recovery facts live on the property marks, so this is
		# where they enter the engine: one read per rewire, one source per fact.
		for field in binding.set.columns:
			var rate := binding.converge_stiffness_of(field.key)
			if rate > 0.0:
				_wiring.converge_rules[field.key] = rate
			if binding.teleport_only_of(field.key):
				_wiring.withheld[field.key] = true
			if binding.reconcile_only_of(field.key):
				_wiring.trigger_excludes[field.key] = true
				_wiring.vote_excludes[field.key] = true
			var threshold := binding.epsilon_override_of(field.key)
			if threshold >= 0.0:
				_wiring.epsilon_overrides[field.key] = threshold
			# A declared tier distance is also an enrolment: the field is saying
			# what its own error means, which is only answerable if the tier
			# measurement reads it. A carry channel enrols too, and keeps
			# inheriting the entity default, so nothing declared before this
			# existed changes meaning.
			var tier := binding.teleport_at_of(field.key)
			if tier >= 0.0:
				_wiring.teleport_thresholds[field.key] = tier
				_wiring.pose_fields[field.key] = true
			# A step is declared per node, so it is read off this body's own
			# overlay rather than the declarations its script shares.
			var step := NetwScriptModel.get_node_property_carry(node, field.key)
			if not step.is_valid():
				continue
			if _wiring.projection.has(field.key):
				push_error(
					(
							"PredictionComponent: '%s' declares both carry_along() "
							+ "and carry_step(). One field, one forward model: "
							+ "keep the step for a rate that varies across the "
							+ "acknowledgement window, the channel for one that "
							+ "holds still. The channel is being used."
					) % [field.key],
				)
				continue
			_wiring.carry_rules[field.key] = step
		# Give every compared field a row up front. A field that never triggered
		# and was never repaired is the reading worth having, and it would
		# otherwise be an absent key indistinguishable from a field that is not
		# declared at all. Counts already accumulated survive the rewire.
		for field: StringName in _wiring.causal_fields:
			_ledger_row(field)


	# Restates the forward model to the pool, which holds every rule it judges.
	#
	# After the rewire, never before: the pool discards a rule declared against
	# the field table the rewire replaced.
	func _push_carry_rules(iface: LagCompCore) -> void:
		for field: StringName in _wiring.carry_rules:
			iface.native_set_carry(_entity, field, _wiring.carry_rules[field])


	# A copy of [param payload] with every rule-declaring field folded forward
	# across the transitions the owner drove after [param basis].
	#
	# A rule is a second, partial statement of the transition function, so it is
	# never trusted, only checked. Three things can be checked and all three are:
	# that it did not write the body it describes, that it reproduces transitions
	# the owner already recorded, and that what it returns is the same type, is
	# finite, and is nearer than a teleport. Anything else refuses the carry and
	# leaves the acknowledged value, which is what an undeclared field writes, so
	# a broken rule can never be worse than declaring none.
	func _carry_payload(payload: Dictionary, basis: int) -> Dictionary:
		if _wiring.carry_rules.is_empty():
			return payload
		var iface := _iface()
		if not iface:
			return payload
		var entries := _carry_entries(basis)
		var out := payload
		var copied := false
		for field: StringName in _wiring.carry_rules:
			if not payload.has(field):
				continue
			var carried: Variant = _carry_field(
				iface,
				field,
				payload[field],
				entries,
			)
			if carried == null:
				continue
			if not copied:
				out = payload.duplicate()
				copied = true
			out[field] = carried
		return out


	# One field's carry, gathered as evidence here and decided by the pool.
	func _carry_field(
			iface: LagCompCore,
			field: StringName,
			acknowledged: Variant,
			entries: Array[Dictionary],
	) -> Variant:
		if entries.is_empty() \
				or not iface.native_carry_eligible(_entity, field):
			_charge_carry(
				field,
				iface.native_decline_carry(_entity, field),
				"",
			)
			return null
		var attempt := _attempt_carry(
			field,
			_wiring.carry_rules[field],
			acknowledged,
			entries,
		)
		if not attempt.evidence:
			_charge_carry(
				field,
				iface.native_decline_carry(_entity, field),
				"",
			)
			return null
		var verdict := iface.native_judge_carry(
			_entity,
			field,
			attempt.same_type,
			attempt.finite,
			attempt.within_envelope,
			attempt.pure,
			attempt.faithful,
		)
		_charge_carry(field, verdict, attempt.residual)
		return attempt.value \
				if verdict == NetwPredictionEngine.CARRY_CARRIED else null


	# Replays the rule against the recorded past, then folds it forward.
	#
	# A rule that already disagrees with the past is never folded, so the fold's
	# purity bracket only ever covers a rule the replay accepted.
	func _attempt_carry(
			field: StringName,
			rule: Callable,
			acknowledged: Variant,
			entries: Array[Dictionary],
	) -> _CarryAttempt:
		var attempt := _CarryAttempt.new()
		_replay_carry(attempt, field, rule, entries)
		if not attempt.evidence or not attempt.faithful:
			return attempt
		var guarded := _state_binding != null
		var before := _state_fingerprint(_capture()) if guarded else 0
		_fold_carry(attempt, field, rule, acknowledged, entries)
		attempt.pure = not guarded or _state_fingerprint(_capture()) == before
		return attempt


	# Charges [param verdict] to the field's ledger row and explains a
	# retirement the pool has just reached, which it reports exactly once.
	func _charge_carry(
			field: StringName,
			verdict: int,
			residual: String,
	) -> void:
		var row := _ledger_row(field)
		if verdict == NetwPredictionEngine.CARRY_CARRIED:
			row.carried += 1
			return
		row.declined += 1
		match verdict:
			NetwPredictionEngine.CARRY_UNFAITHFUL:
				row.infidelity += 1
			NetwPredictionEngine.CARRY_RETIRED_INFIDELITY:
				row.infidelity += 1
				var iface := _iface()
				_warn_carry_retired(
					field,
					_carry_infidelity_reason(
						iface.native_carry_infidelity(_entity, field) \
								if iface else row.infidelity,
						residual,
					),
				)
			NetwPredictionEngine.CARRY_RETIRED_SCHEDULE:
				_warn_carry_retired(
					field,
					"It needs prediction.schedule = FRAME: the tick tier "
					+ "re-anchors its own state record to authority on every "
					+ "correction, so too "
					+ "little of it is the owner's own to replay a rule against. "
					+ "The rule itself is not in question.",
				)
			NetwPredictionEngine.CARRY_RETIRED_IMPURE:
				_warn_carry_retired(
					field,
					"It wrote to the body it is supposed to describe. A rule is a "
					+ "function of its recorded CarryContext alone: reading the "
					+ "live world or writing anything makes it disagree with the "
					+ "history it is replayed against.",
				)


	# The transitions a fold walks, newest last, in the numbering its tier records
	# state under.
	#
	# The two tiers keep the same history in different books. FRAME drives once
	# per physics frame and records per tape entry, TICK once per tick and records
	# per tick, and both record the state a transition ran FROM under that
	# transition's own index.
	func _carry_entries(basis: int) -> Array[Dictionary]:
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			return _scope_entries(basis)
		var out: Array[Dictionary] = []
		for tick in range(basis + 1, _latest_input_tick + 1):
			out.append({
				&"index": tick,
				&"label": tick,
				&"input": _timeline.input_at(tick),
			})
		return out


	# The declared state one transition ran from, out of its tier's own book.
	func _carry_state_at(index: int) -> Dictionary:
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			return _entry_history.state_at(index)
		return _timeline.state_at(index)


	# Folds one rule across [param entries] onto [param attempt].
	#
	# A step that returns the wrong type, a non-finite value, or lands further
	# from the acknowledged value than a teleport would move the body is not
	# advancing it, whatever it computed. The envelope is THIS field's declared
	# distance: the rule advances one field, in one unit, so the entity default
	# is only the right number for it by coincidence.
	func _fold_carry(
			attempt: _CarryAttempt,
			field: StringName,
			rule: Callable,
			start: Variant,
			entries: Array[Dictionary],
	) -> void:
		var value: Variant = start
		for entry: Dictionary in entries:
			var index := int(entry.get(&"index", -1))
			var state := _carry_state_at(index)
			if state.is_empty():
				attempt.evidence = false
				return
			var stepped: Variant = _call_carry(
				rule,
				value,
				state,
				entry.get(&"input", { }),
				int(entry.get(&"label", -1)),
			)
			if stepped == null:
				attempt.finite = false
				return
			if typeof(stepped) != typeof(start):
				attempt.same_type = false
				return
			value = stepped
		if _teleport_reached(
			{
				field: NetwPredictionHandle._error(
					value,
					start,
					_wiring.angle_fields.has(field),
				),
			},
			_wiring.teleport_thresholds,
			_handle.teleport_threshold,
		):
			attempt.within_envelope = false
			return
		attempt.value = value


	# One rule invocation, refusing a non-finite result rather than writing it.
	func _call_carry(
			rule: Callable,
			value: Variant,
			state: Dictionary,
			input: Variant,
			label: int,
	) -> Variant:
		var ctx := NetwPredictionHandle.CarryContext.new()
		ctx.state = state
		ctx.input = input if input is Dictionary else { }
		ctx.delta = _tick_delta
		ctx.label = label
		var result: Variant = rule.call(value, ctx)
		match typeof(result):
			TYPE_FLOAT:
				return result if is_finite(result as float) else null
			TYPE_VECTOR2:
				return result if (result as Vector2).is_finite() else null
			TYPE_VECTOR3:
				return result if (result as Vector3).is_finite() else null
		return result


	# Replays the rule over one transition the owner already recorded and asks
	# whether it reaches the state that transition actually reached.
	#
	# This is the only check that can see a rule reading the live world instead of
	# the transition's, or one whose arithmetic is simply wrong, because both
	# reproduce the recorded past incorrectly while looking entirely reasonable at
	# the moment of the write. One transition per recovery is enough: a rule that
	# disagrees does so on most of them, and the retirement counter integrates.
	func _replay_carry(
			attempt: _CarryAttempt,
			field: StringName,
			rule: Callable,
			entries: Array[Dictionary],
	) -> void:
		# Oldest first, because the pairs nearest the present are the ones a
		# recovery has most recently landed in.
		for i in entries.size():
			var index := int(entries[i].get(&"index", -1))
			# Both ends have to be the owner's own drive. A recorded state stops
			# being one when a recovery write lands before the next transition is
			# authored, and on the tick tier also when the acknowledged slot is
			# re-anchored to authority's payload. A rule states what a DRIVE does,
			# so judging it across either would convict it of the engine's own
			# correction or of disagreeing with a peer it never restated.
			if _carry_dirty_records.has(index) \
					or _carry_dirty_records.has(index + 1):
				continue
			var from := _carry_state_at(index)
			var reached := _carry_state_at(index + 1)
			if from.is_empty() or reached.is_empty() \
					or not from.has(field) or not reached.has(field):
				continue
			var stepped: Variant = _call_carry(
				rule,
				from[field],
				from,
				entries[i].get(&"input", { }),
				int(entries[i].get(&"label", -1)),
			)
			if stepped == null or typeof(stepped) != typeof(reached[field]):
				attempt.faithful = false
				return
			# Judged at the field's own declared tolerance, in the field's own
			# units. A rule only has to be as good as the error the recovery is
			# allowed to leave behind.
			var tolerance := float(
				_wiring.epsilon_overrides.get(field, _handle.divergence_epsilon),
			)
			var residual := NetwPredictionHandle._error(
				stepped,
				reached[field],
				_wiring.angle_fields.has(field),
			)
			if residual > tolerance:
				attempt.faithful = false
				attempt.residual = "%.4f against a tolerance of %.4f" % [
					residual,
					tolerance,
				]
				return
			return
		attempt.evidence = false


	# Marks one recorded state as something other than a drive result, bounded
	# with the tape it indexes into.
	func _mark_carry_dirty(index: int) -> void:
		_carry_dirty_records[index] = true
		while _carry_dirty_records.size() > TAPE_HISTORY_LIMIT:
			var oldest := _carry_dirty_records.keys()
			oldest.sort()
			_carry_dirty_records.erase(oldest[0])


	# Why a rule that failed its fidelity gate is being retired.
	#
	# Built here rather than inline so a law can read the diagnosis without
	# capturing log output, and worded to name the LIKELY cause rather than the
	# only one anybody thought of when the message was first written. A rule that
	# fails this gate is far more often describing a field the solver shares than
	# misbehaving: the residual is then the solver's own contribution, which the
	# game does not author and no rule can restate. Racing measured 1.667 rad/s
	# stated against 0.329 realized -- the rule was right about the game's term,
	# and the term was a fifth of the transition.
	static func _carry_infidelity_reason(count: int, residual: String) -> String:
		var measured := ""
		if not residual.is_empty():
			measured = " The last replay left a residual of %s." % [residual]
		return (
				"It did not reproduce %d transitions the owner had already "
				+ "recorded.%s Two things produce that, and the second is the "
				+ "common one. Either the rule is not a function of its recorded "
				+ "CarryContext alone -- reading the live world or writing "
				+ "anything makes it disagree with the history it is replayed "
				+ "against -- or this field is one the SOLVER also moves, in "
				+ "which case the residual IS the solver's contribution and no "
				+ "rule can reproduce it. carry_step() advances a field whose "
				+ "whole change the game authors; a field the solver shares is "
				+ "not one of those, however correct the rule is about the "
				+ "game's own share."
		) % [count, measured]


	# Says once that a rule is out of the loop for good.
	#
	# [param reason] carries its WHOLE explanation. A rule retired for needing
	# the frame tier, or for describing a field the solver shares, is not an
	# impure rule, and sending the reader to hunt a purity bug that is not there
	# is how a legal declaration costs somebody a measurement round.
	func _warn_carry_retired(field: StringName, reason: String) -> void:
		push_warning(
			(
					"PredictionComponent: the carry_step() rule for '%s' is "
					+ "retired, so every recovery now writes the acknowledged "
					+ "value. %s"
			) % [field, reason],
		)


	# A copy of [param payload] with every carry-declaring field advanced by
	# [param age] seconds through its declared channel. Fields with no channel, no
	# channel value in the payload, or an unprojectable type restore verbatim.
	func _extrapolated_payload(payload: Dictionary, age: float) -> Dictionary:
		return project_payload(payload, _wiring.projection, age)


	# How far the predicted pose sits from where the authoritative payload says it
	# should be, PER FIELD, over the fields enrolled in the tier: the ones that
	# declared a carry channel, and the ones that named their own teleport_at().
	#
	# Per field rather than one maximum, because the maximum has no unit. A body
	# whose pose spans metres, radians and radians per second reduced to one scalar
	# and compared to one threshold compares a rotation rate to a distance, and
	# Phase 1 measured what that costs: declaring a carry channel on a momentum
	# field enrolled rad/s in a measurement whose threshold meant metres, and
	# teleports tripled on FEWER triggers.
	#
	# An empty answer means there was nothing to measure, which is not the same as
	# measuring zero: the caller reports it as unmeasured, and a recovery that
	# cannot measure a pose error is not entitled to claim it sits below the tier.
	# A declared field simply absent from this payload is not that case -- it drops
	# out of the maximum, as it always did.
	func _pose_errors_against(payload: Dictionary) -> Dictionary[StringName, float]:
		var errors: Dictionary[StringName, float] = { }
		if _wiring.pose_fields.is_empty():
			return errors
		var span := clampi(_handle.ack_age_ticks, 0, _handle.max_restore_ticks)
		var target := _extrapolated_payload(payload, float(span) * _tick_delta)
		var current := _capture()
		for field: StringName in _wiring.pose_fields:
			if current.has(field) and target.has(field):
				errors[field] = NetwPredictionHandle._error(
					current[field],
					target[field],
					_wiring.angle_fields.has(field),
				)
		return errors


	# The fields a sub-teleport recovery leaves on the predicted body entirely,
	# read off the teleport_only() marks at wire time. A converging field is not
	# among them: it is written, just not all the way, so withholding it too
	# would be declaring the same field twice.
	# The three entity-wide defaults, read from the handle rather than cached
	# behind a dirty flag. A flag is only as good as the writers that set it, and
	# these are plain properties a game moves directly -- nineteen sites in this
	# repository mutate them on a live handle and expect the next comparison to
	# see it. Three assignments per comparison cannot miss one.
	func _refresh_wiring_scalars() -> void:
		if not _handle:
			return
		_wiring.epsilon = _handle.divergence_epsilon
		_wiring.teleport_threshold = _handle.teleport_threshold
		_wiring.max_restore_ticks = _handle.max_restore_ticks


	func _error_on_retained_predicted_props(set: NetwPropertySet) -> void:
		for field in set.columns:
			if field.lane == NetwPropertySet.Lane.RETAINED:
				push_error(
					(
							"PredictionComponent: predicted state property '%s' "
							+ "is retained. Predicted state must be volatile so "
							+ "timeline snapshots arrive atomically."
					) % [field.key],
				)


	# Checks the declared property classes against the tolerances and the wire
	# grammar that read them. Every trigger here is a declaration that cannot mean
	# what it says, so it is caught where the entity is wired rather than at the
	# first divergence it would silently misjudge.
	func _validate_property_classes(set: NetwPropertySet) -> void:
		# A predicted entity rewires on every role change, so the report is keyed
		# to the config it judges and fires only when that config changes, not once
		# per rewire. A masked causal property is NOT reported: the reconstruction
		# invariant means the owner rebuilds the full row from the masked stream and
		# can reconcile it, so riding a masked set is no longer a mistake.
		var config_hash := _property_class_report_hash(set)
		if config_hash == _validated_class_hash:
			return
		_validated_class_hash = config_hash
		# One home for every finding, and one emitter for every severity. The
		# report is what a game can ask for; these are the parts of it a game
		# would not think to ask about.
		_emit_reachability_findings(_reachability_report(set)[&"findings"])
		_report_authority_model_mismatches()


	# Routes each finding to the channel its severity names, so the report has one
	# emitter rather than five call sites with their own opinions.
	#
	# error is a declaration that cannot mean what it says. warning is a legal
	# declaration whose whole cost is that nobody knows to look -- it must not go
	# to the debug channel, which stays silent until a game arms logging, which is
	# right for a diagnostic someone went looking for and wrong for this. debug is
	# for a fact worth having on request and not worth interrupting for.
	func _emit_reachability_findings(findings: Array) -> void:
		for finding: Dictionary in findings:
			var message := String(finding[&"message"])
			match StringName(finding[&"severity"]):
				&"error":
					push_error(message)
				&"warning":
					push_warning(message)
				_:
					Netw.dbg.warn("%s", [message], func(m): push_warning(m))


	# What each declared state field can expect from the recovery ladder, and what
	# the entity as a whole does about a breach.
	#
	# Every question here is answerable at wire time from declarations alone, and
	# every one of them has cost this campaign a measurement round to answer by
	# hand: whether a field can trigger, which operators may write it, whether its
	# forward model is live under the schedule the entity actually resolved,
	# which domain its comparisons run in, and what happens on breach. A
	# declaration that is legal, accepted, and then silently inert is the failure
	# mode this exists to make visible.
	# What each declared state field can expect from the recovery ladder, and
	# what the entity as a whole does about a breach.
	func _reachability_report(set: NetwPropertySet) -> Dictionary:
		return NetwPredictionReachability.of(
			set,
			_wiring,
			_handle,
			_entity.entity_id if _entity else &"",
			_retired_carry_rules(),
		)


	# Which declared rules the pool has taken out of the loop, asked of the pool
	# so the lint and the recovery never disagree about a retirement.
	func _retired_carry_rules() -> Dictionary[StringName, bool]:
		var out: Dictionary[StringName, bool] = { }
		var iface := _iface()
		if not iface:
			return out
		for field: StringName in _wiring.carry_rules:
			if iface.native_carry_retired(_entity, field):
				out[field] = true
		return out



	func _report_authority_model_mismatches() -> void:
		NetwPredictionReachability.report_authority_model(
			_entity,
			_input_binding,
			NetwPredictCommandFrame.create(_input_schema()) \
					if _input_binding and _input_binding.set else null,
		)

	# The config a property-class report judges, folded to one hash so the report
	# fires once per distinct config rather than once per rewire.
	func _property_class_report_hash(set: NetwPropertySet) -> int:
		var parts := PackedStringArray()
		for column: NetwPropertySet.Column in set.columns:
			# The recovery marks are part of the config this report judges, so a
			# config that changes only in them still earns a fresh report.
			parts.append("%s:%d:%d:%.4f:%d:%d:%s:%d" % [
				column.key,
				column.property_class,
				1 if column.quantizer else 0,
				column.epsilon_override,
				1 if column.explicit_teleport_only else 0,
				1 if column.explicit_reconcile_only else 0,
				column.carry_channel,
				1 if _wiring.carry_rules.has(column.key) else 0,
			])
		parts.append("masked:%d" % (1 if set.masked else 0))
		var node := _entity.owner if _entity else null
		var script := node.get_script() as Script if is_instance_valid(node) \
				else null
		if script:
			var broadcast_set := NetwPropertySet.from_script(
				script,
				NetwPropertySet.Record.RECORD_BROADCAST,
			)
			if broadcast_set:
				for column: NetwPropertySet.Column in broadcast_set.columns:
					parts.append("broadcast:%s" % column.key)
		parts.append("controller:%d" % (1 if _entity.controller > 0 else 0))
		return hash("
".join(parts))


	# A promoted member declares no island of its own, so its simulation
	# subjects are what seat it in one.
	func _pool_island() -> int:
		if _handle.reconcile_mode == NetwPredict.Reconcile.JOINT:
			return NetwPredictionEngine.ISLAND_JOINT
		if _handle.island.declared \
				or not _handle._simulation_subjects.is_empty():
			return NetwPredictionEngine.ISLAND_DECLARED
		return NetwPredictionEngine.ISLAND_NONE


	# Resolves the two axes onto the handle and returns the role they name. The
	# axes are the decision. The role is the name that decision has always had,
	# derived rather than chosen, so the two can never disagree.
	func _resolve_axes() -> NetwPredict.Role:
		var is_server := _declaration.authority
		var controlled_here := _declaration.controlled_locally
		if controlled_here:
			_handle.input_source = NetwPredict.InputSource.LOCAL
			# A closed-delay entity is simulated only where its commands are
			# authoritative. The peer that authors the command still authors it,
			# which is why the input axis is untouched, but it displays the answer
			# instead of guessing it. That is the whole mechanism: the speculative
			# mode is never taken, so no speculative transition exists to diverge.
			_handle.sim_mode = (
					NetwPredict.SimMode.AUTHORITATIVE if is_server
					else NetwPredict.SimMode.DISPLAY if _delay_closed()
					else NetwPredict.SimMode.SPECULATIVE
			)
		elif is_server:
			_handle.input_source = NetwPredict.InputSource.RECEIVED
			_handle.sim_mode = NetwPredict.SimMode.AUTHORITATIVE
		elif not _handle._simulation_subjects.is_empty():
			_handle.input_source = NetwPredict.InputSource.PREDICTED
			_handle.sim_mode = NetwPredict.SimMode.SPECULATIVE
		else:
			_handle.input_source = NetwPredict.InputSource.NONE
			_handle.sim_mode = NetwPredict.SimMode.DISPLAY
		return NetwPredictionHandle.role_for_axes(
			_handle.input_source,
			_handle.sim_mode,
		)


	# True when this entity declared that it never speculates. The policy is read
	# as declared rather than resolved, because the derivation behind
	# resolved_recovery_policy only ever answers with one of the two rebases:
	# closing the delay is a claim only the game can make about its own entity,
	# never one a body type can imply.
	func _delay_closed() -> bool:
		return _fallback_latched or _handle.recovery_policy \
				== NetwPredict.RecoveryPolicy.DELAY_CLOSED


	# Opens the recovery cooldown from the freshest predict tick, and
	# with it the out-of-domain window when the contact was against a body this
	# peer does not simulate equivalently. Predict role only, so a server or
	# remote peer that never springs ignores it.
	func notify_contact() -> void:
		if _role != NetwPredict.Role.PREDICT:
			return
		_cooldown_until_tick = _latest_input_tick + _handle.collision_cooldown_ticks
		_report_island_gap()
		if not _contact_is_equivalent():
			_out_of_domain_until = window_after(
				_latest_input_tick,
				_handle.collision_cooldown_ticks,
				_out_of_domain_until,
			)


	# Produces the interest-bounded roster and commits hysteretic promotion.
	func _refresh_island_membership(apply_promotion: bool = true) -> void:
		if not _handle.island.declared:
			_clear_island_promotions()
			_island_members.clear()
			return
		var found: Dictionary[NetwEntity, bool] = { }
		for explicit: NetwEntity in _handle.island.participants:
			if _valid_island_member(explicit):
				found[explicit] = true
		var api := _entity.multiplayer
		if api:
			for layer: StringName in _handle.island.producers:
				for candidate: NetwEntity in api._interest.shared_entities(
						_entity,
						layer,
				):
					if _valid_island_member(candidate):
						found[candidate] = true
		var members: Array[NetwEntity] = []
		members.assign(found.keys())
		members.sort_custom(_entity_id_less)
		if not apply_promotion:
			_clear_island_promotions()
			_commit_island_members(members)
			return
		_apply_island_promotions(_pool_promoted(members))
		_commit_island_members(members)


	# The roster the pool promoted, as the set the promotion pass applies. The
	# policy, the contact deferral and the promotion budget are all the pool's,
	# so what crosses is evidence about each member rather than a decision.
	func _pool_promoted(
			members: Array[NetwEntity],
	) -> Dictionary[NetwEntity, bool]:
		var promoted: Dictionary[NetwEntity, bool] = { }
		var iface := _iface()
		if iface == null:
			return promoted
		var island := _handle.island
		# The pass steps the owner in the same order as its members, so one
		# ranking over the whole group is what the order key has to carry.
		var ranked: Array[NetwEntity] = members.duplicate()
		ranked.append(_entity)
		ranked.sort_custom(_entity_id_less)
		var order_keys := PackedInt64Array()
		var fidelities := PackedInt32Array()
		var distances := PackedFloat64Array()
		var eligible := PackedByteArray()
		var contact := PackedByteArray()
		for member: NetwEntity in members:
			order_keys.append(ranked.find(member))
			fidelities.append(int(island.fidelity.get(member, -1)))
			distances.append(_distance_squared(member))
			eligible.append(1 if _can_automatically_simulate(member) else 0)
			contact.append(1 if _realized_contact_entities.has(member) else 0)
		for member: NetwEntity in iface.native_island_commit(
				_entity,
				ranked.find(_entity),
				members,
				order_keys,
				fidelities,
				distances,
				eligible,
				contact,
				island.promotion,
				island.promotion_count,
				island.promotion_meters,
				_ledger_drive_frontier(),
		):
			promoted[member] = true
		return promoted


	# Adopts a committed roster, reopening the out-of-domain window when it
	# changed.
	#
	# Membership is an antecedent. It is folded into the topology fingerprint, and
	# two peers cannot adopt a join or a leave on the same transition, so the
	# transitions spanning a change are not ones either peer can claim to
	# reproduce. This is the same reason an epoch bump reopens the window, and it
	# is the difference between a join reading as a contact-shaped disturbance and
	# a join charging every divergence to
	# [constant NetwPredictJournal.Attribution.TOPOLOGY].
	#
	# The first roster a session commits is a declaration rather than a change, so
	# it opens nothing.
	func _commit_island_members(members: Array[NetwEntity]) -> void:
		var changed := not _same_roster(members, _island_members)
		if _island_roster_seeded and changed:
			_out_of_domain_until = window_after(
				_latest_input_tick,
				_handle.collision_cooldown_ticks,
				_out_of_domain_until,
			)
		_island_roster_seeded = true
		_island_members = members
		_admit_reconcile_mode()
		# Only a roster that MOVED is republished. This runs on every drive, and
		# the reported roster is a handful of strings that have to be built and
		# sorted to exist, so publishing unconditionally would put allocation
		# churn in the hot path to restate a value that almost never changes.
		if changed:
			_publish_island_roster()


	# Publishes the group's admitted mode to the owner and every member.
	#
	# A member decides on its own handle whether the transition it substitutes
	# is worth recording, so a mode that stayed on the owner that resolved it
	# would be read by nobody.
	func _admit_reconcile_mode() -> void:
		var admitted := _admitted_reconcile_mode()
		_handle.reconcile_mode = admitted
		var iface := _iface()
		if iface == null:
			return
		# This runs on every roster commit, so only a member whose mode MOVED
		# is republished. Re-asserting an unchanged subscription every drive
		# would put a reliable frame per member per tick on the wire.
		for member: NetwEntity in _simulated_members:
			var engine := iface.engine_for(member)
			if engine == null or engine._handle.reconcile_mode == admitted:
				continue
			engine._handle.reconcile_mode = admitted
			engine._follow_relay_subscription(admitted)


	# A group that replays its members together needs the commands they were
	# authored with, and a member no group replays needs none, so the relay
	# subscription follows the admitted mode rather than being declared beside
	# it.
	func _follow_relay_subscription(mode: NetwPredict.Reconcile) -> void:
		var iface := _iface()
		var api := iface._api() if iface else null
		if api == null or _entity == null or not is_instance_valid(_entity):
			return
		api.predict_relay_subscribe(
			_entity.rid,
			mode == NetwPredict.Reconcile.JOINT,
		)


	# Resolves the island's requested reconcile mode against what this peer can
	# actually re-run, once per roster commit.
	#
	# A JOINT group restores every member to a shared floor and replays them
	# together, so it is only a promise this peer can keep when the owner and
	# every promoted member step on a re-runnable tier. A group that cannot keep
	# it runs INDEPENDENT, named once per engine.
	func _admitted_reconcile_mode() -> NetwPredict.Reconcile:
		var requested := _handle.island.reconcile
		if requested != NetwPredict.Reconcile.JOINT:
			return requested
		var refusals: Array[String] = []
		if not _is_steppable():
			refusals.append(
				"%s owner on a %s tier" % [
					_entity.entity_id,
					NetwPredict.Schedule.keys()[_handle.schedule],
				],
			)
		for member: NetwEntity in _simulated_members:
			var engine := _iface().engine_for(member) if _iface() else null
			if engine and not engine._is_steppable():
				refusals.append(
					"%s on a %s tier" % [
						member.entity_id,
						NetwPredict.Schedule.keys()[engine._handle.schedule],
					],
				)
		if refusals.is_empty():
			return NetwPredict.Reconcile.JOINT
		if not _joint_refusal_reported:
			_joint_refusal_reported = true
			push_error(
				"NetwPredictIsland.reconcile: JOINT admitted for none of "
				+ "this island's members (%s), so it runs INDEPENDENT."
				% ", ".join(refusals),
			)
		return NetwPredict.Reconcile.INDEPENDENT


	# Both rosters are sorted by entity id, so identity is an element-wise walk.
	static func _same_roster(
			a: Array[NetwEntity],
			b: Array[NetwEntity],
	) -> bool:
		if a.size() != b.size():
			return false
		for i in a.size():
			if a[i] != b[i]:
				return false
		return true


	# Returns whether a locally present participant may enter this island.
	# A scene wrapper is a container, never an interaction participant, so no
	# producer or explicit add may seat one in the island.
	func _valid_island_member(member: NetwEntity) -> bool:
		return member != null and member != _entity \
				and is_instance_valid(member) \
				and is_instance_valid(member.owner) \
				and not member.declares_scene \
				and member.multiplayer == _entity.multiplayer \
				and member.scene.entity == _entity.scene.entity


	# Automatic policies select only prediction-aware display participants.
	func _can_automatically_simulate(member: NetwEntity) -> bool:
		if not member.prediction.is_registered():
			return false
		return member.prediction.sim_mode == NetwPredict.SimMode.DISPLAY \
				or not member.prediction._simulation_subjects.is_empty()


	# Commits promotion claims on participant handles.
	func _apply_island_promotions(
			desired: Dictionary[NetwEntity, bool],
	) -> void:
		var frontier := _ledger_drive_frontier()
		for member: NetwEntity in _simulated_members:
			if not desired.has(member) and is_instance_valid(member):
				_close_tenure(member, frontier)
				member.prediction._set_simulated_by(_entity, false)
		var predictors := _handle.island.command_predictors
		for member: NetwEntity in desired:
			if not _simulated_members.has(member):
				_open_tenure(member, frontier + 1)
			member.prediction._set_simulated_by(
				_entity,
				true,
				predictors.get(member, Callable()),
			)
		var changed := desired.size() != _simulated_members.size()
		if not changed:
			for member: NetwEntity in desired:
				if not _simulated_members.has(member):
					changed = true
					break
		_simulated_members = desired
		if changed:
			_publish_island_roster()


	# Drops every promotion owned by this subject.
	func _clear_island_promotions() -> void:
		var frontier := _ledger_drive_frontier()
		for member: NetwEntity in _simulated_members:
			if is_instance_valid(member):
				_close_tenure(member, frontier)
				member.prediction._set_simulated_by(_entity, false)
		_simulated_members.clear()


	# Opens a member's tenure at the first transition it will belong to the
	# group for.
	func _open_tenure(member: NetwEntity, transition: int) -> void:
		_joint_lingering.erase(member)
		var iface := _iface()
		var engine: _PredictionEngine = \
				iface.engine_for(member) if iface else null
		if engine == null:
			return
		engine._tenure_begin = transition
		engine._tenure_end = -1


	# Closes a member's tenure at its last transition and holds it there.
	#
	# A departed member is not freed while the group's replay horizon still
	# precedes its departure, because a pass from that floor has to step it
	# through the transitions it was still part of.
	func _close_tenure(member: NetwEntity, transition: int) -> void:
		var iface := _iface()
		var engine: _PredictionEngine = \
				iface.engine_for(member) if iface else null
		if engine == null:
			return
		engine._tenure_end = transition
		_joint_lingering[member] = true


	func _distance_squared(member: NetwEntity) -> float:
		return _entity_position(_entity).distance_squared_to(
			_entity_position(member),
		)


	func _entity_position(member: NetwEntity) -> Vector3:
		var root := member.owner if member else null
		if root is Node3D:
			return (root as Node3D).global_position
		if root is Node2D:
			var point := (root as Node2D).global_position
			return Vector3(point.x, point.y, 0.0)
		return Vector3.ZERO


	func _entity_id_less(a: NetwEntity, b: NetwEntity) -> bool:
		var a_id := String(a.entity_id)
		var b_id := String(b.entity_id)
		if a_id == b_id:
			return a.get_instance_id() < b.get_instance_id()
		return a_id < b_id


	# True only when every body this entity actually touched is one this peer
	# simulates the way authority does.
	#
	# Equivalence is read off the axes rather than measured off geometry: a peer
	# whose simulation of a participant is DISPLAY is running a frozen proxy, and
	# contact against a frozen proxy is definitionally not the contact authority
	# resolved. That is a declaration mismatch, not a tolerance question, so no
	# amount of geometric agreement would change the answer.
	#
	# What the question is asked ABOUT depends on whether the game observes its
	# contacts. A declared [member NetwPredictionHandle.witness_contacts] names the
	# bodies actually touched, so the answer is about those. Without one nothing
	# was observed, so the question widens to every body this entity COULD have
	# touched, which is the whole declared island. Widening is the conservative
	# direction and it is what an absent observation earns.
	#
	# That distinction is what lets static world geometry answer honestly. Every
	# peer solves against the same track, so touching it is reproducible, which
	# is the rule [enum NetwPredict.BreachResponse] states and
	# [method _contact_breaches_boundary] already applies. Static geometry names
	# no entity, so an observed contact set that is empty IS the static case and
	# equivalence holds. Read without a witness the same emptiness means nothing
	# was watched, which is why it must not take that branch.
	#
	# An undeclared island still answers false. A contact against a body nobody
	# named is where a peer is least entitled to exactness.
	func _contact_is_equivalent() -> bool:
		var participants := _island_participants()
		if participants.is_empty():
			return false
		var contacts: Array[NetwEntity] = participants
		if _handle.witness_contacts.is_valid():
			contacts = []
			contacts.assign(_realized_contact_entities.keys())
		for participant in contacts:
			if participant not in participants:
				return false
			var handle := participant.prediction as NetwPredictionHandle
			if handle == null \
					or handle.sim_mode == NetwPredict.SimMode.DISPLAY:
				return false
		return true


	# The declared participants that are still live entities.
	func _island_participants() -> Array[NetwEntity]:
		if not _island_members.is_empty():
			return _island_members.duplicate()
		var out: Array[NetwEntity] = []
		for participant: NetwEntity in _handle.island.participants:
			if participant and is_instance_valid(participant):
				out.append(participant)
		return out


	# Names the gap once when a contact is reported against no declared island at
	# all. Silence there would let an entity look in-domain forever while
	# colliding with bodies nobody ever claimed the peers agree about.
	func _report_island_gap() -> void:
		if _island_gap_reported or not _island_participants().is_empty():
			return
		_island_gap_reported = true
		push_warning(
			"Prediction: %s reported contact with no declared island. Declare the "
			% _entity.entity_id
			+ "bodies it touches on prediction.island, or its contact "
			+ "transitions will keep claiming an exactness they cannot hold.",
		)


	# True while a sub-teleport recovery is paused: the body is asleep or inside a
	# post-contact cooldown, where predicted and authoritative legitimately differ
	# and writing would fight the solver.
	func _corrections_suppressed() -> bool:
		var iface := _iface()
		return _handle.sleeping \
				or (iface != null and iface.native_probation_pending(_entity)) \
				or _latest_input_tick < _cooldown_until_tick

	# --- Predict (owning client) ---


	# Captures a tick-labeled input without driving. When another tick arrives in
	# the same frame, the older label carries the pre-solve state.
	func _predict_author_tick(tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		if _latest_input_tick > _last_driven_input_tick:
			_timeline.record_state(_latest_input_tick + 1, _capture())
			_last_recorded_input_tick = _latest_input_tick
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_frame_input = input
		_input_binding.authored_tick = tick


	# Applies the newest authored input after the tick-rate governor admits it.
	func _predict_frame_step(timing: NetwPredict.Timing) -> void:
		_last_frame_transition_tick = timing.tick
		if _speculation_horizon_full():
			_handle.stats.speculation_held += 1
			_send_command_frame()
			return
		var plan := _kernel._predict_drive(
			_latest_input_tick,
			_last_driven_input_tick,
			timing.tick,
		)
		var label: int = plan[&"label"]
		var fresh: bool = plan[&"fresh"]
		_author_tape_entry(label, fresh)
		_record_drive(
			_last_driven_entry_index,
			label,
			plan[&"kind"],
			_frame_input,
			timing.tick,
				false,
		)
		_run(_frame_input, timing.delta, label, fresh)
		if fresh:
			_last_driven_input_tick = label
		_send_command_frame()


	# Captures the command lane while fallback displays authority state.
	func _fallback_author_tick(tick: int) -> void:
		_predict_author_tick(tick)


	# Authors one FRAME command without opening a speculative transition.
	func _fallback_author_frame_step(timing: NetwPredict.Timing) -> void:
		_last_frame_transition_tick = timing.tick
		var plan := _kernel._predict_drive(
			_latest_input_tick,
			_last_driven_input_tick,
			timing.tick,
		)
		var label: int = plan[&"label"]
		var fresh: bool = plan[&"fresh"]
		_author_tape_entry(label, fresh)
		if fresh:
			_last_driven_input_tick = label
		_send_command_frame()


	# Authors one TICK command without running or journaling speculation.
	func _fallback_author_step(tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_input_binding.authored_tick = tick
		_prepare_tick_tape(tick)
		_author_tape_entry(tick, true)
		_last_driven_input_tick = tick
		_send_command_frame()


	func _predict_step(delta: float, tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_input_binding.authored_tick = tick
		if _speculation_horizon_full():
			_handle.stats.speculation_held += 1
			_send_command_frame()
			return
		# A TICK drive is its own transition, so the tape entry is degenerate:
		# index, label, and tick are the same number and every entry is fresh.
		# The lane codec implies contiguous indices, so a clock re-anchor that
		# gaps the tick sequence starts a new tape epoch instead of straddling it.
		_prepare_tick_tape(tick)
		_author_tape_entry(tick, true)
		_last_driven_input_tick = tick
		_record_drive(tick, tick, NetwPredict.DriveKind.FRESH, input, tick, false)
		_run(input, delta, tick, true)
		var state := _capture()
		_timeline.record_state(tick + 1, state)
		_close_journal_row(tick, state)
		# The row is closed before the send, so the frame's fingerprint section
		# can claim the transition it just drove rather than trailing by a tick.
		_send_command_frame()


	# Steps a remote participant through F with a substituted predicted command.
	#
	# A JOINT group replays its members together, so every member needs the
	# history the replay reads back: an opened row carrying the command that
	# drove it, a recorded transition state, and a closed row with the post
	# fingerprint. Recording opens that row, which is why the close below is
	# reached at all. Outside a JOINT group the member keeps the cheaper
	# contract it shipped with, where every receive rewrites and nothing is
	# evidence.
	func _simulated_step(delta: float, tick: int) -> void:
		var input := _predicted_command(tick)
		var joint := _handle.reconcile_mode == NetwPredict.Reconcile.JOINT
		if joint:
			_record_drive(
				tick,
				tick,
				NetwPredict.DriveKind.SUBSTITUTED,
				input,
				tick,
					false,
			)
			# A predicted cell never displaces a relayed one, because the
			# author's own command is the answer this guess was standing in for.
			var existing := command_cell_at(tick)
			if int(existing.get(&"origin", -1)) \
					!= NetwPredict.CommandOrigin.RELAYED:
				_file_command_cell(
					tick,
					input,
					NetwPredict.CommandOrigin.PREDICTED,
				)
		else:
			_handle.stats.drive_seq += 1
			_mark_idle_drive(tick, NetwPredict.DriveKind.SUBSTITUTED)
		_run(input, delta, tick, false)
		_handle.stats.substituted += 1
		if joint:
			var state := _capture()
			_timeline.record_state(tick + 1, state)
			_close_journal_row(tick, state)


	# Applies the same one-drive FRAME cadence used by the subject island.
	func _simulated_frame_step(timing: NetwPredict.Timing) -> void:
		if timing.tick <= _last_frame_transition_tick:
			return
		_last_frame_transition_tick = timing.tick
		_simulated_step(timing.delta, timing.tick)


	# Merges a per-participant predictor over the COAST zero-input baseline.
	func _predicted_command(tick: int) -> Dictionary:
		var command := _coast_command()
		var predictor := _handle._predicted_command_callable()
		if not predictor.is_valid():
			return _canonical_input(command)
		var predicted: Variant = predictor.call(_entity, tick)
		if predicted is Dictionary:
			for key: Variant in predicted:
				command[key] = predicted[key]
			return _canonical_input(command)
		if not _invalid_command_predictor_reported:
			_invalid_command_predictor_reported = true
			push_error(
				"Prediction island command predictor for %s must return a "
				% _entity.entity_id
				+ "Dictionary. COAST was used instead.",
			)
		return _canonical_input(command)


	# Builds the type-preserving zero command that defines COAST.
	func _coast_command() -> Dictionary:
		var out: Dictionary = { }
		for key: StringName in _input_binding.snapshot_payload():
			out[key] = _zero_value(_input_binding.node().get(key))
		return out


	func _zero_value(value: Variant) -> Variant:
		match typeof(value):
			TYPE_BOOL:
				return false
			TYPE_INT:
				return 0
			TYPE_FLOAT:
				return 0.0
			TYPE_STRING:
				return ""
			TYPE_STRING_NAME:
				return &""
			TYPE_VECTOR2:
				return Vector2.ZERO
			TYPE_VECTOR2I:
				return Vector2i.ZERO
			TYPE_VECTOR3:
				return Vector3.ZERO
			TYPE_VECTOR3I:
				return Vector3i.ZERO
			TYPE_VECTOR4:
				return Vector4.ZERO
			TYPE_VECTOR4I:
				return Vector4i.ZERO
			TYPE_COLOR:
				return Color(0.0, 0.0, 0.0, 0.0)
			TYPE_ARRAY:
				return []
			TYPE_DICTIONARY:
				return { }
		return value


	# Independently rebases a simulated remote on every reconstructed state row.
	func _on_simulated_state_frame(header: Dictionary) -> void:
		_stream_reconstructed = _stream_reconstructed \
				or bool(header.get("whole", true))
		if not _stream_reconstructed:
			return
		var payload: Dictionary = header.get("payload", { })
		if payload.is_empty():
			return
		var recv_tick := int(header.get("tick", -1))
		var iface := _iface()
		var current_tick := iface._current_tick() if iface else recv_tick
		var age_ticks := clampi(
			maxi(0, current_tick - recv_tick),
			0,
			_handle.max_restore_ticks,
		)
		var target := payload
		if _handle.snap_restore == NetwPredict.RestoreMode.EXTRAPOLATED:
			target = _extrapolated_payload(
				payload,
				float(age_ticks) * _tick_delta,
			)
		var divergence := NetwPredictionHandle.divergence_by_field(
			_capture(),
			target,
			_wiring.angle_fields,
			_handle.last_field_divergence,
		)
		_handle.is_reconciling = true
		_handle.stats.corrections += 1
		# Under JOINT this row is a basis rather than a place to rebase: the
		# group replays from one floor, and a member that snapped where its own
		# row landed would enter that replay from a state no other member's
		# history knows about. The unextrapolated payload is the basis: the pass
		# re-runs the transitions the extrapolation was standing in for.
		if _handle.reconcile_mode == NetwPredict.Reconcile.JOINT:
			_note_joint_basis(recv_tick - 1, payload, &"state")
		else:
			_restore(target)
		_handle.is_reconciling = false
		# A simulated remote rebases on every row it accepts, so this row has no
		# reason to report. Stated rather than assumed, because the reason belongs
		# to the row that carries it and never to the one before.
		_handle.last_verdict_reason = NetwPredict.VerdictReason.NONE
		_handle.state_evaluated.emit(recv_tick, -1, divergence, true)


	# Re-keys a TICK tape when its clock jumps past the contiguous lane.
	func _prepare_tick_tape(tick: int) -> void:
		if not _authored_tape.is_empty() \
				and int(_authored_tape.back().get("index", -1)) != tick - 1:
			_tape_epoch = (_tape_epoch + 1) & 0xFF
			_authored_tape.clear()
			_witness_details.clear()
			_witness_verdicts.clear()
			_deferred_operator_states.clear()
			_operator_deferred_basis = -1
			_ack_of_acks = -1
			_ack_domain_confirmed = false
		_next_tape_entry_index = tick


	# True once another speculative transition would exceed the bounded horizon.
	func _speculation_horizon_full() -> bool:
		_refresh_owner_ack_age()
		return _handle.ack_age_ticks >= NetwPredict.ACK_AGE_MAX


	# Publishes the speculative span from the freshest proof on either lane.
	func _refresh_owner_ack_age() -> void:
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			_handle.ack_age_ticks = maxi(
				0,
				_next_tape_entry_index - _latest_authority_ack - 1,
			)
		else:
			_handle.ack_age_ticks = maxi(
				0,
				_last_driven_input_tick - _latest_authority_ack,
			)


	# Drives reconciliation from a received state frame. The set handle fires this
	# with the decoded header after the authoritative row arrives (the predicted body
	# is not snapped, since the predict binding is write-gated).
	func _on_state_frame(header: Dictionary) -> void:
		_stream_reconstructed = _stream_reconstructed \
				or bool(header.get("whole", true))
		_on_state(
			int(header.get("tick", -1)),
			int(header.get("ack", -1)),
			header.get("payload", { }),
		)


	func _on_state(recv_tick: int, ack: int, payload: Dictionary) -> void:
		if ack < 0:
			return
		# The entity-wide defaults, taken once for this comparison. Everything
		# below reads them off the wiring record instead of the handle, so this is
		# the one place a live mutation has to be picked up.
		_refresh_wiring_scalars()
		if _ack_domain_confirmed:
			_latest_authority_ack = maxi(_latest_authority_ack, ack)
			_refresh_owner_ack_age()
		# One reason per comparison, cleared before it runs, so no row can inherit
		# the explanation an earlier receive left behind. Same for the tier
		# measurement, which only some comparisons take.
		_handle.last_verdict_reason = NetwPredict.VerdictReason.NONE
		_handle.last_tier_errors.clear()
		var reseed_iface := _iface()
		if reseed_iface and reseed_iface.native_reseed_align_pending(_entity):
			if reseed_iface.native_reseed_epoch_confirmed(_entity):
				_finish_reseed_alignment(recv_tick, ack, payload)
			else:
				_handle.last_verdict_reason = \
						NetwPredict.VerdictReason.REALIGN_PENDING
				_handle.state_evaluated.emit(recv_tick, ack, 0.0, false)
			return
		if reseed_iface and not reseed_iface.native_admit_post_reseed(
			_entity,
			ack,
		):
			_handle.last_verdict_reason = \
					NetwPredict.VerdictReason.RESEED_IGNORED
			_handle.state_evaluated.emit(recv_tick, ack, 0.0, false)
			return
		var frame_entry: Dictionary = { }
		var ack_label := ack
		var predicted: Dictionary
		if _handle.schedule == NetwPredict.Schedule.FRAME:
			frame_entry = _authored_entry_at(ack)
			if frame_entry.is_empty():
				return
			ack_label = int(frame_entry.get("label", -1))
			predicted = _entry_history.state_at(ack + 1)
			if predicted.is_empty():
				return
			_handle.last_compare_staleness = 0
		else:
			predicted = _timeline.latest_state_at_or_before(ack + 1)
			var compared_tick := \
					_timeline.latest_state_tick_at_or_before(ack + 1)
			_handle.last_compare_staleness = (
					-1 if compared_tick < 0 else ack + 1 - compared_tick
			)
		# A correction may consume any frame once the stream has reconstructed,
		# i.e. seen its gain-edge full row. Past that edge each masked frame merges
		# into the sender's coherent row for its tick, so the comparison and any
		# rebase land on a state that truly existed rather than on a partial
		# mosaic. Before the edge corrections wait. An unmasked set arms on its
		# first frame, so the gate is transparent to every non-masked recipe.
		var divergence := 0.0
		var corrected := false
		var settled := false
		var meter := 0
		var native_row := _native_journal_row(ack)
		var domain := native_row.domain() \
				if native_row else NetwPredictJournal.Domain.OUT_OF_DOMAIN
		# Read before the pool compares, because the pool retires an episode
		# inside that call and a closure is a transition rather than a state.
		var episode_state_before := _iface().native_episode_state(_entity) \
				if _iface() else -1
		# The pool consumes its probation inside compare_state, so the verdict
		# has to be sampled before the comparison it is a verdict about.
		var probation_before := _iface().native_probation_pending(_entity) \
				if _iface() else false
		# A frame that reaches no verdict must not read as one that found
		# agreement. Both leave divergence at zero, and a reader with only the
		# number cannot tell a peer that matched from a comparison that never
		# ran, which is how an entity can speculate unreconciled for a whole
		# session while every report says it agrees.
		_handle.stats.comparisons_ran += 1 if _stream_reconstructed else 0
		_handle.stats.comparisons_skipped += 0 if _stream_reconstructed else 1
		if not _stream_reconstructed:
			_handle.last_field_divergence.clear()
			_handle.last_verdict_reason = \
					NetwPredict.VerdictReason.AWAITING_RECONSTRUCTION
		if _stream_reconstructed:
			# The row is read after the fingerprint pass, so a transition this
			# frame's own state frame was able to judge is judged by that verdict
			# rather than waiting a frame for its label to be worth reading. Both
			# reads are scalar: this runs on every authoritative frame of every
			# predicted entity, where materializing a row would allocate to deliver
			# two bytes.
			var exact_verdict := _exact_verdict_of(native_row.flags()) \
					if native_row else NetwPredict.ExactVerdict.UNJUDGED
			var verdict := _kernel._predict_evaluate(
				domain,
				exact_verdict,
				predicted,
				payload,
				_wiring,
				_handle.last_field_divergence,
			)
			divergence = verdict[&"divergence"]
			corrected = verdict[&"corrected"]
			settled = domain == NetwPredictJournal.Domain.OUT_OF_DOMAIN \
					or exact_verdict != NetwPredict.ExactVerdict.UNJUDGED
			var pool_verdict := _pool_compare(
				ack,
				predicted,
				payload,
				_meter_tolerances(domain),
			)
			# A game that overrides the seam has replaced the decision, so its
			# answer outranks the pool's. Nothing else does.
			if pool_verdict and not _kernel.overrides_seam(&"_predict_evaluate"):
				divergence = pool_verdict.divergence()
				corrected = pool_verdict.corrected()
				settled = pool_verdict.settled()
				meter = pool_verdict.meter()
			else:
				meter = measure(
					_handle.last_field_divergence,
					_meter_tolerances(domain),
				)
			# The exact predicate is authoritative in domain. Preserve its
			# binary verdict if a conservative quantizer bound overlaps one
			# adjacent canonical value.
			if corrected and meter == 0:
				meter = 1
			elif not corrected:
				meter = 0
		# Read before this comparison stages anything, so a write is judged by the
		# comparison after it rather than the one that asked for it.
		if _stream_reconstructed:
			_ledger_note_comparison(corrected, ack)
		var attribution := _attribution_for(ack)
		# The probation verdict, taken on the first comparison that actually
		# judged the resumed body. Clean arms the ladder; dirty goes straight
		# back to quarantine without spending budget and without pricing the
		# flap, because a resume that was never clean is not the body flapping
		# -- it is the quarantine having ended on a proof that could not see
		# divergence. Nine of nine resumes on the wall capture were already past
		# epsilon here, and the doubling ladder read every one of them as
		# instability that more observation would settle.
		if probation_before and _stream_reconstructed:
			if corrected:
				_open_episode(ack, attribution)
				# The verdict is reported as it was judged, and the reason says
				# why it wrote nothing. Reporting corrected = false here made the
				# one comparison that causes a re-quarantine read exactly like a
				# comparison that agreed, on the signal and in every capture
				# taken off it, so a re-quarantine could only ever be inferred
				# from the demotion it produced.
				_handle.last_verdict_reason = \
						NetwPredict.VerdictReason.PROBATION_REQUARANTINE
				# Before anything is done about it, like every other divergence.
				# An observe-only game hears a re-quarantining disagreement as
				# loudly as a correcting one, which is the contract this signal
				# is under and the branch was outside of.
				_handle.divergence_detected.emit(ack, attribution)
				_enter_fallback(ack, attribution)
				_handle.state_evaluated.emit(recv_tick, ack, divergence, true)
				return
		# Supervision follows the verdict that a recovery is owed, never the one
		# that decided it. A fingerprint and a tolerance answer different
		# questions about the same divergence, and only the first needs the
		# acknowledgement lane, so gating the episode on a settled comparison
		# leaves every tolerance-decided recovery outside the contract that
		# bounds it: no episode, no operator ladder, no escalation evidence, no
		# budget, and no report on
		# [signal NetwPredictionHandle.divergence_detected]. An out-of-domain
		# entity never saw that, because its comparisons settle by definition.
		# An in-domain one whose verdict arrives behind its state saw nothing
		# else.
		if corrected:
			if _episode_open():
				_record_episode_divergence(ack)
			else:
				_open_episode(ack, attribution)
			_handle.divergence_detected.emit(ack, attribution)
		# The aligned error stays behind the settled gate. It is the journal's
		# record of a comparison that reached a verdict, and an unsettled one
		# reached none.
		if settled:
			var iface := _iface()
			var pool_transition := int(_pool_transitions.get(ack, -1))
			if iface and pool_transition >= 0:
				iface.native_mark_aligned_error(
					_entity,
					pool_transition,
					divergence,
				)
		# An agreeing comparison has to reach the episode too, or one opened
		# from an unsettled divergence could never assemble the agreement run
		# that retires it.
		_record_episode_comparison(episode_state_before)
		# Each of the exits below judged a divergence and answers it with no write,
		# so each names itself AND reports the verdict it reached. S2 named the
		# reasons and left the flag alone, which made state_evaluated mean "a
		# write happened" here and "a divergence was judged" on the path below.
		# One meaning now: the verdict. A reader that wants the write reads
		# [signal recovered], and last_verdict_reason says why this one has none.
		if corrected and _episode_budget_exhausted():
			_handle.last_verdict_reason = \
					NetwPredict.VerdictReason.EVIDENCE_EXHAUSTED
			_enter_fallback(ack, attribution)
			_handle.state_evaluated.emit(
				recv_tick,
				ack,
				divergence,
				corrected,
			)
			return
		if corrected and (_transport_pending() or _dissipate_pending()):
			_handle.last_verdict_reason = (
					NetwPredict.VerdictReason.TRANSPORT_PENDING
					if _transport_pending()
					else NetwPredict.VerdictReason.DISSIPATE_PENDING
			)
			_handle.state_evaluated.emit(
				recv_tick,
				ack,
				divergence,
				corrected,
			)
			return
		if corrected and _defer_operator_for_witness(
			recv_tick,
			ack,
			payload,
		):
			_handle.last_verdict_reason = \
					NetwPredict.VerdictReason.WITNESS_DEFERRED
			_handle.state_evaluated.emit(
				recv_tick,
				ack,
				divergence,
				corrected,
			)
			return
		# A group member's divergence is answered by the group's next pass, so
		# the ladder does not run for it. The ladder exists to make bounded,
		# budgeted, partial recovery converge; the pass is total and terminates
		# at the present, and running both would double-write the same body.
		if corrected and _handle.reconcile_mode == NetwPredict.Reconcile.JOINT:
			_note_joint_basis(ack, payload, &"state")
		elif corrected:
			# Re-resolve from the handle so a runtime correction_mode change is live.
			_correction = _resolve_correction(_handle.correction_mode)
			var before := _capture()
			var correction_write: Dictionary = { }
			# A promoted recovery reports an unmeasurable pose error rather than
			# its measured one: past the escalation the recovery is no longer
			# entitled to claim it sits below the teleport tier, whatever the
			# measurement says.
			var escalated := _pool_escalation_pending()
			var transported := _try_transport(
				predicted,
				payload,
				before,
				ack,
				escalated,
			)
			if transported.is_empty() and _try_dissipate(
					ack,
					meter,
					domain,
					escalated,
			):
				_handle.last_verdict_reason = \
						NetwPredict.VerdictReason.DISSIPATED
				_handle.state_evaluated.emit(
					recv_tick,
					ack,
					divergence,
					corrected,
				)
				return
			_escalate_next = false
			_handle.is_reconciling = true
			_handle.stats.corrections += 1
			var plan: Dictionary
			if not transported.is_empty():
				plan = {
					&"restore": transported[&"restore"],
					&"write": transported[&"restore"],
					&"teleport": false,
					&"skip": false,
				}
				_last_correction_teleported = false
				_restore(
					plan[&"restore"],
					NetwPredictJournal.Operator.TRANSPORT_DELTA,
					ack,
				)
				correction_write = plan[&"write"]
			else:
				var recovery_projection := guard_projection(
					_wiring.projection,
					_handle.last_field_divergence,
					_wiring,
					_handle.ack_age_ticks,
					_tick_delta,
				)
				# A declared rule advances its field here rather than inside
				# recover(), which is a pure kernel function with no access to the
				# transitions a fold has to walk. What reaches recover() is the
				# already-advanced payload, so the channel projection below and a
				# rule never touch the same field.
				var carried_payload := _carry_payload(payload, ack)
				# Published before it is spent, because it is not the number any
				# other row carries. The tier is measured against the EXTRAPOLATED
				# target, so a field's tier error and its compared divergence are
				# different quantities, and reading the second in place of the
				# first is how a capture explains a teleport with a number that
				# did not cause it: measured 2026-07-29, only 8 of 60 teleports in
				# one arm had a compared divergence anywhere near the distance the
				# tier fired on.
				var tier_errors: Dictionary[StringName, float] = { }
				if not escalated:
					tier_errors = _pose_errors_against(payload)
				_handle.last_tier_errors = tier_errors
				# The recorded payload remains raw. Only the body write is staged.
				var pool_plan := _pool_recover(
					ack,
					ack_label,
					predicted,
					carried_payload,
					before,
					tier_errors,
					domain,
					attribution,
				)
				var pool_planned := pool_plan != null \
						and not _kernel.overrides_seam(&"_predict_recover")
				if pool_planned:
					plan = _plan_of(pool_plan)
					escalated = pool_plan.escalated()
					if escalated:
						_reset_recovery_trackers()
						_cooldown_until_tick = _latest_input_tick \
								+ _handle.collision_cooldown_ticks
				else:
					plan = _kernel._predict_recover(
						carried_payload,
						_handle.resolved_recovery_policy(),
						_correction,
						_handle.snap_restore,
						recovery_projection,
						before,
						tier_errors,
						_wiring,
						_verdict.fill(
							domain,
							attribution,
							_out_of_domain_until >= 0 \
									and ack_label < _out_of_domain_until,
							_corrections_suppressed(),
							escalated or _wiring.pose_fields.is_empty(),
							_handle.ack_age_ticks,
						),
						_tick_delta,
					)
					_track_recovery_convergence(
						escalated, plan, divergence, predicted, payload,
					)
				_last_correction_teleported = plan[&"teleport"]
				if not plan[&"skip"]:
					var operator := NetwPredictJournal.Operator.REBASE_EXACT
					if bool(plan[&"teleport"]):
						operator = NetwPredictJournal.Operator.FULL_CLOSURE
					elif _correction == NetwPredict.CorrectionMode.SNAP \
							and _handle.snap_restore \
							== NetwPredict.RestoreMode.EXTRAPOLATED \
							and not recovery_projection.is_empty():
						operator = NetwPredictJournal.Operator.REBASE_PROJECTED
					_restore(plan[&"restore"], operator, ack, null, false, \
							pool_planned)
					correction_write = plan[&"write"]
				else:
					# The ladder ran and its recovery declined. This is the one
					# no-write path the signal already reported honestly, and it is
					# named for the same reason as the rest: agreement and a refusal
					# are not the same row.
					_handle.last_verdict_reason = \
							NetwPredict.VerdictReason.DECLINED
			# Anchor the authoritative state at its keyed tick so a later packet
			# carrying the same ack compares against the corrected value, not the
			# stale prediction it just replaced. Without this a duplicate ack (the
			# server is input starved and re-sends the same ack) re-triggers this
			# correction every tick until the ack advances past the stale entry.
			if _handle.schedule == NetwPredict.Schedule.TICK:
				_timeline.record_state(ack + 1, payload)
				# This slot now holds authority's answer rather than the
				# transition the owner drove, so no rule may be judged across it.
				if not _wiring.carry_rules.is_empty():
					_mark_carry_dirty(ack + 1)
			# REPLAY re-runs unacked inputs over the restored state (kinematic). SNAP
			# stops at the restore (dynamic): the predicted body resumes forward from
			# truth next tick and the display chase absorbs the snap, since a solver
			# body cannot be stepped per input without a physics fork.
			#
			# A recovery that declined to write has nothing to replay over. Re-running
			# the unacknowledged commands would advance the body from the prediction
			# the recovery just refused to correct, which is a repair the policy said
			# not to make.
			var replays: bool = not plan[&"skip"] \
					and _correction == NetwPredict.CorrectionMode.REPLAY
			if replays and _handle.schedule == NetwPredict.Schedule.FRAME:
				_replay_authored_entries(ack)
			elif replays:
				var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
				_handle.stats.max_replay_depth = maxi(_handle.stats.max_replay_depth, window.size())
				var live_input := _input_binding.snapshot_payload()
				for entry in window:
					_run(entry["input"], _tick_delta, entry["tick"], false)
					var state := _capture()
					_timeline.record_state(entry["tick"] + 1, state)
					_close_journal_row(entry["tick"], state)
				_input_binding.apply_payload(live_input)
			_emit_recovered(ack, attribution, before, correction_write)
			_handle.is_reconciling = false
		elif _stream_reconstructed:
			# An evaluation that found nothing to correct is the shrink the
			# escalation evidence was waiting for: the divergence closed, so the
			# next recovery starts a new sequence rather than inheriting an old
			# streak from one long past.
			_reset_recovery_trackers()

		if _handle.schedule == NetwPredict.Schedule.FRAME:
			_timeline.trim_before(ack_label)
			_entry_history.trim_before(ack)
		else:
			_timeline.trim_before(ack)
		_handle.state_evaluated.emit(recv_tick, ack, divergence, corrected)


	# Builds the fixed per-field zero region for the comparison's own domain.
	func _meter_tolerances(
			domain: NetwPredictJournal.Domain,
	) -> Dictionary:
		var tolerances: Dictionary[StringName, float] = { }
		var node := _state_binding.node() if _state_binding else null
		if not is_instance_valid(node) or not _state_binding.set:
			return tolerances
		for column: NetwPropertySet.Column in _state_binding.set.columns:
			if _wiring.trigger_excludes.has(column.key):
				if _wiring.causal_fields.has(column.key) \
						and _wiring.epsilon_overrides.has(column.key):
						tolerances[column.key] = _wiring.epsilon_overrides[column.key]
				continue
			if _wiring.withheld.has(column.key) \
					and _wiring.epsilon_overrides.has(column.key):
				tolerances[column.key] = _wiring.epsilon_overrides[column.key]
				continue
			if domain == NetwPredictJournal.Domain.OUT_OF_DOMAIN:
				tolerances[column.key] = float(_wiring.epsilon_overrides.get(
					column.key,
					_handle.divergence_epsilon,
				))
				continue
			if not column.quantizer:
				tolerances[column.key] = 0.0
				continue
			var type := NetwScriptModel.get_node_property_type(node, column.key)
			tolerances[column.key] = column.quantizer.max_error(type)
		return tolerances


	# The ladder is the pool's once a slot is open, so the shell asks rather
	# than keeping a second streak that could answer differently.
	func _pool_escalation_pending() -> bool:
		var iface := _iface()
		if iface == null or native_slot() < 0:
			return _escalate_next
		return iface.native_escalation_pending(_entity)


	func native_slot() -> int:
		var iface := _iface()
		return iface.native_prediction_slot(_entity) if iface else -1


	func _pool_recover(
			basis: int,
			current_label: int,
			predicted: Dictionary,
			payload: Dictionary,
			before: Dictionary,
			tier_errors: Dictionary,
			domain: int,
			attribution: int,
	) -> NetwPredictWritePlan:
		var iface := _iface()
		if iface == null or not _state_binding or not _state_binding.set:
			return null
		return iface.native_recover(
			_entity,
			_state_columns(predicted),
			_state_columns(payload),
			_state_columns(before),
			_tolerance_columns(tier_errors),
			basis,
			current_label,
			_handle.resolved_recovery_policy(),
			_wiring.epsilon,
			_wiring.teleport_threshold,
			_wiring.max_restore_ticks,
			_handle.ack_age_ticks,
			_handle.collision_cooldown_ticks,
			_tick_delta,
			domain,
			attribution,
			_out_of_domain_until >= 0 and current_label < _out_of_domain_until,
			_corrections_suppressed(),
			_wiring.pose_fields.is_empty(),
		)


	# One member's restore row out of a joint plan, keyed the way the body
	# writer reads a payload.
	func _restore_payload(plan: NetwPredictJointPlan, index: int) -> Dictionary:
		var payload: Dictionary = { }
		if not _state_binding or not _state_binding.set:
			return payload
		var declared := _state_binding.set.columns
		for at: int in declared.size():
			if plan.restore_has(index, at):
				payload[declared[at].key] = plan.restore_at(index, at)
		return payload


	# The plan as the rest of the correction path already reads one.
	func _plan_of(plan: NetwPredictWritePlan) -> Dictionary:
		var restore: Dictionary = { }
		var write: Dictionary = { }
		var declared := _state_binding.set.columns
		for at: int in declared.size():
			var key: StringName = declared[at].key
			if plan.restore_has(at):
				restore[key] = plan.restore_at(at)
			if plan.write_has(at):
				write[key] = plan.write_at(at)
		return {
			&"restore": restore,
			&"write": write,
			&"teleport": plan.teleport(),
			&"skip": plan.skip(),
		}


	# The pool's verdict on one acknowledged transition, judged from its own
	# journal row and its own state rather than from anything staged here.
	#
	# Null when the pool holds no row for the transition, which is the only
	# case the shell still has to answer for itself.
	func _pool_compare(
			transition: int,
			predicted: Dictionary,
			payload: Dictionary,
			meter_tolerances: Dictionary,
	) -> NetwPredictVerdict:
		var iface := _iface()
		var pool_transition := int(_pool_transitions.get(transition, -1))
		if iface == null or pool_transition < 0 or not _state_binding \
				or not _state_binding.set:
			return null
		var corrections: Dictionary[StringName, float] = { }
		for column: NetwPropertySet.Column in _state_binding.set.columns:
			if _wiring.vote_excludes.has(column.key):
				continue
			corrections[column.key] = float(_wiring.epsilon_overrides.get(
				column.key,
				_wiring.epsilon,
			))
		return iface.native_compare_state(
			_entity,
			transition,
			pool_transition,
			_state_columns(predicted),
			_state_columns(payload),
			_tolerance_columns(corrections),
			_tolerance_columns(meter_tolerances),
			_wiring.epsilon,
			_stream_reconstructed,
			_ack_domain_confirmed,
		)


	# A payload as the pool's field table orders it. A field the payload never
	# carried lands as null, which is the absence the pool's own row records.
	func _state_columns(payload: Dictionary) -> Array:
		var columns: Array = []
		if not _state_binding or not _state_binding.set:
			return columns
		var declared := _state_binding.set.columns
		columns.resize(declared.size())
		for at: int in declared.size():
			columns[at] = payload.get(declared[at].key)
		return columns


	func _tolerance_columns(tolerances: Dictionary) -> PackedFloat64Array:
		var row := PackedFloat64Array()
		if not _state_binding or not _state_binding.set:
			return row
		var declared := _state_binding.set.columns
		row.resize(declared.size())
		for at: int in declared.size():
			row[at] = float(tolerances.get(declared[at].key, -1.0))
		return row


	# What a divergence at [param transition] is charged to.
	#
	# The acknowledgement lane is where both peers' command hashes and environment
	# digests meet, so a charge it already made is the best answer available and
	# is read off the transition's own journal row. Failing that, a substituted
	# row is still decisive on its own: authority declared it ran a command the
	# owner never authored, and no further evidence could change that.
	#
	# Anything else is UNKNOWN rather than charged to the suspect that
	# happens to be tested last. A tolerance failure on an out-of-domain
	# transition is not a bug with an address, and naming one would turn the
	# attribution into a decoration.
	func _attribution_for(transition: int) -> NetwPredictJournal.Attribution:
		var row := _native_journal_row(transition)
		var charged := row.attribution() \
				if row else NetwPredictJournal.Attribution.UNKNOWN
		if charged != NetwPredictJournal.Attribution.UNKNOWN:
			return charged
		if _handle.last_attributed_transition == transition:
			return _handle.last_attribution
		if row and row.flags() & NetwPredictJournal.ROW_SUBSTITUTED:
			return NetwPredictJournal.Attribution.COMMAND
		return NetwPredictJournal.Attribution.UNKNOWN


	# Whether either fingerprint path has reached a verdict for a row yet.
	# Nothing infers a verdict from an unacked row: an in-domain transition
	# arriving before its acknowledgement falls back to the tolerance compare
	# rather than correcting against a comparison that never ran.
	func _exact_verdict_of(flags: int) -> NetwPredict.ExactVerdict:
		if not (flags & NetwPredictJournal.ROW_ACKED):
			return NetwPredict.ExactVerdict.UNJUDGED
		return NetwPredict.ExactVerdict.EQUAL if flags & NetwPredictJournal.ROW_MATCHED \
				else NetwPredict.ExactVerdict.UNEQUAL


	# Records whether the prediction for a transition fingerprinted equal to the
	# authority state that acknowledged it.
	#
	# Replays the client's unacknowledged FRAME entries after a kinematic restore.
	func _replay_authored_entries(ack: int) -> void:
		var live_input := _input_binding.snapshot_payload()
		var replay_input := _timeline.input_at(
			int(_authored_entry_at(ack).get("label", -1)),
		)
		var plan := _ReplayPlan.new(live_input)
		for entry: Dictionary in _authored_tape:
			var index := int(entry.get("index", -1))
			if index <= ack:
				continue
			var label := int(entry.get("label", -1))
			if bool(entry.get("fresh", false)):
				var authored_input := _timeline.input_at(label)
				if not authored_input.is_empty():
					replay_input = authored_input
			plan.steps.append(_ReplayStep.new(index, label, replay_input))
		_run_replay_plan(plan)


	func _run_replay_plan(plan: _ReplayPlan) -> void:
		if not plan.is_valid():
			push_error("Prediction replay plan is invalid.")
			return
		for step: _ReplayStep in plan.steps:
			_run(step.input, _tick_delta, step.label, false)
			_close_replayed_entry(step.index)
		_input_binding.apply_payload(plan.live_input)
		_handle.stats.max_replay_depth = maxi(
			_handle.stats.max_replay_depth,
			plan.steps.size(),
		)


	# Records what one re-run entry produced, the write every replay path owes
	# whether it re-ran this entity alone or as one member of a scope.
	func _close_replayed_entry(index: int) -> void:
		var state := _capture()
		_entry_history.record_state(index + 1, state)
		_close_journal_row(index, state)


	# The entries this member authored past [param ack], each carried with the
	# command that drove it. A scope rollback needs every member's entries in hand
	# before it steps any of them, because the members advance together through one
	# entry at a time rather than one member at a time.
	func _scope_entries(ack: int) -> Array[Dictionary]:
		var out: Array[Dictionary] = []
		if _handle.schedule == NetwPredict.Schedule.TICK:
			var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
			for entry: Dictionary in window:
				var tick := int(entry.get("tick", -1))
				out.append({
					&"index": tick,
					&"label": tick,
					&"input": entry.get("input", { }),
				})
			return out
		var replay_input := _timeline.input_at(
			int(_authored_entry_at(ack).get("label", -1)),
		)
		for entry: Dictionary in _authored_tape:
			var index := int(entry.get("index", -1))
			if index <= ack:
				continue
			var label := int(entry.get("label", -1))
			if bool(entry.get("fresh", false)):
				var authored_input := _timeline.input_at(label)
				if not authored_input.is_empty():
					replay_input = authored_input
			out.append({
				&"index": index,
				&"label": label,
				&"input": replay_input,
			})
		return out


	# True when this peer can re-run this member's own step. A member this peer
	# only displays is a proxy playing back a stream, so there is no step to
	# re-run and guessing one would invent a history authority never had. The
	# member's tier is the rest of the answer: TICK replays exactly, STEPPED
	# replays only when its space's driver is installed, and FRAME's solver
	# cannot be stepped per input at all.
	func _is_steppable() -> bool:
		if _handle.sim_mode == NetwPredict.SimMode.DISPLAY \
				or not _handle.simulate.is_valid():
			return false
		match _handle.schedule:
			NetwPredict.Schedule.TICK:
				return true
			NetwPredict.Schedule.STEPPED:
				return _has_stepper()
			_:
				return false


	func _has_stepper() -> bool:
		var iface := _iface()
		if iface == null:
			return false
		var space := iface._entity_space(_entity)
		return iface.stepper_for(space[&"space"]) != null



	# The members a scope rollback re-runs, in entity-id order.
	#
	# The order is the whole reason this is not a set. A cross-entity write lands
	# in the order the members ran, so a resim that visited them in a different
	# order would reach a different state from the same commands.
	func _scope_members(ack: int) -> Array:
		var out: Array = [self]
		var iface := _iface()
		if iface:
			for participant: NetwEntity in _island_participants():
				var engine: _PredictionEngine = iface.engine_for(participant)
				if not engine or engine == self or not engine._is_steppable():
					continue
				# A member with no recorded state at the rollback point was not
				# simulating then, so there is nothing to roll it back to.
				if engine.transition_state_at(ack).is_empty():
					continue
				out.append(engine)
		out.sort_custom(
			func(a: _PredictionEngine, b: _PredictionEngine) -> bool:
				return a.order_key() < b.order_key(),
		)
		return out


	# Restores every declared island member to the rollback point and re-runs them
	# together, one entry at a time, in entity-id order within each entry.
	#
	# Every member is re-run unconditionally. Gating a member on whether it looked
	# like it diverged is what erases a cross-entity write: the push one member
	# applied to another only exists because the pusher ran, so a rollback that
	# re-runs the pusher and holds the pushed keeps the cause and drops the
	# effect. Re-running everyone regenerates the interaction instead of
	# preserving a stale copy of its result.
	func _replay_scope(ack: int) -> void:
		var members := _scope_members(ack)
		if members.size() <= 1:
			_replay_authored_entries(ack)
			return
		var plans: Array[Dictionary] = []
		var last_entry := ack
		for member: _PredictionEngine in members:
			var entries := member._scope_entries(ack)
			for entry: Dictionary in entries:
				last_entry = maxi(last_entry, int(entry[&"index"]))
			plans.append({
				&"member": member,
				&"entries": entries,
				&"live": member._input_binding.snapshot_payload(),
			})
			# This entity was already put back by the recovery that staged it, so
			# restoring it again would overwrite authority's payload with the
			# prediction the recovery just replaced.
			if member != self:
				member._restore(
					member.transition_state_at(ack),
					NetwPredictJournal.Operator.REBASE_EXACT,
					ack,
					self,
				)
		for index in range(ack + 1, last_entry + 1):
			for plan: Dictionary in plans:
				var member: _PredictionEngine = plan[&"member"]
				for entry: Dictionary in plan[&"entries"]:
					if int(entry[&"index"]) != index:
						continue
					member._run(
						entry[&"input"],
						member._tick_delta,
						int(entry[&"label"]),
						false,
					)
					member._close_replayed_entry(index)
					break
		for plan: Dictionary in plans:
			var member: _PredictionEngine = plan[&"member"]
			member._input_binding.apply_payload(plan[&"live"])
			member._handle.stats.max_replay_depth = maxi(
				member._handle.stats.max_replay_depth,
				(plan[&"entries"] as Array).size(),
			)


	# Replays this group from the shared floor its members' bases select, once
	# per tick rather than once per authoritative row.
	#
	# A group member's receive path records a basis and stops there, so N rows
	# arriving inside one tick cost one pass instead of N replays. The pass is
	# total where the recovery ladder is partial: every member in tenure is
	# stepped through every transition past the floor, because a cross-entity
	# write only exists if the member that applied it ran.
	func joint_pass(timing: NetwPredict.Timing) -> void:
		if _handle.reconcile_mode != NetwPredict.Reconcile.JOINT \
				or _role not in [
					NetwPredict.Role.PREDICT,
					NetwPredict.Role.CONSUME,
					NetwPredict.Role.HOST_LOCAL,
				]:
			return
		var iface := _iface()
		if iface == null:
			return
		var plan := iface.native_joint_pass(_entity, timing.tick - 1)
		if plan == null or not plan.valid():
			return
		var members := _joint_group()
		var floor_transition := plan.floor()
		if plan.heal():
			for member: _PredictionEngine in members:
				member._joint_heal()
			_handle.stats.heal_snaps += 1
		else:
			_run_joint_pass(plan, members, floor_transition)
		_release_lingering(floor_transition)
		for member: _PredictionEngine in members:
			member._joint_basis = -1
			member._joint_relay_floor = -1
			member._joint_epoch_floor = -1


	# The members this pass steps, in entity-id order, lingering ones included.
	#
	# The order is the whole reason this is not a set. A cross-entity write lands
	# in the order the members ran, so a pass that visited them in a different
	# order would reach a different state from the same commands.
	func _joint_group() -> Array:
		var out: Array = [self]
		var iface := _iface()
		if iface == null:
			return out
		for member: NetwEntity in _simulated_members:
			_append_joint_member(out, iface, member)
		for member: NetwEntity in _joint_lingering:
			if not _simulated_members.has(member):
				_append_joint_member(out, iface, member)
		out.sort_custom(
			func(a: _PredictionEngine, b: _PredictionEngine) -> bool:
				return a.order_key() < b.order_key(),
		)
		return out


	func _append_joint_member(
			out: Array,
			iface: LagCompCore,
			member: NetwEntity,
	) -> void:
		if not is_instance_valid(member):
			return
		var engine: _PredictionEngine = iface.engine_for(member)
		if engine == null or engine == self or not engine._is_steppable():
			return
		out.append(engine)


	# Carries out the pool's plan. The restores and the step order are the
	# pool's decision, and the body writes stay here because a pool slot cannot
	# reach a node.
	func _run_joint_pass(
			plan: NetwPredictJointPlan,
			members: Array,
			floor_transition: int,
	) -> void:
		var by_slot: Dictionary[int, _PredictionEngine] = { }
		for member: _PredictionEngine in members:
			var slot := member.native_slot()
			if slot >= 0:
				by_slot[slot] = member
		var stepped: Array = []
		var live: Dictionary = { }
		for at: int in plan.restore_count():
			var member: _PredictionEngine = by_slot.get(plan.restore_slot(at))
			if member == null:
				continue
			stepped.append(member)
			live[member] = member._input_binding.snapshot_payload()
			member._restore(
				member._restore_payload(plan, at),
				NetwPredictJournal.Operator.JOINT_REBASE,
				floor_transition,
				self,
			)
		if stepped.is_empty():
			return
		var before: Dictionary = { }
		var ran: Dictionary = { }
		for member: _PredictionEngine in stepped:
			before[member] = member._capture()
			ran[member] = 0
		for at: int in plan.step_count():
			var member: _PredictionEngine = by_slot.get(plan.step_slot(at))
			if member == null:
				continue
			_count_joint_cell(plan.step_provenance(at))
			var transition := plan.step_transition(at)
			member._run(
				plan.step_command(at),
				member._tick_delta,
				transition,
				false,
			)
			member._close_replayed_entry(transition)
			ran[member] = int(ran[member]) + 1
		var present := plan.present()
		for member: _PredictionEngine in stepped:
			member._input_binding.apply_payload(live[member])
			# Depth is what this member actually re-ran, not what the pass
			# spanned. A member the pass skipped every transition of has a
			# replay depth of zero, and a counter that reported the span would
			# say it was carried when it was not.
			member._handle.stats.max_replay_depth = maxi(
				member._handle.stats.max_replay_depth,
				int(ran[member]),
			)
			member._note_joint_writes(before[member])
		var depth := present - floor_transition
		_handle.stats.joint_passes += 1
		_handle.stats.joint_members = stepped.size()
		_handle.stats.joint_floor = floor_transition
		_handle.stats.joint_present = present
		_handle.stats.joint_depth[depth] = int(
			_handle.stats.joint_depth.get(depth, 0),
		) + 1


	func _count_joint_cell(provenance: int) -> void:
		match provenance:
			NetwPredict.CellProvenance.RELAYED:
				_handle.stats.cells_relayed += 1
			NetwPredict.CellProvenance.SUBSTITUTED:
				_handle.stats.cells_substituted += 1


	# What drove one of this member's transitions, and where the pass got it,
	# as [code]{ command, provenance }[/code].
	# Files this transition's replay cell with the pool, whose pass restores
	# from its own track rather than from the shell's timeline.
	func _record_joint_cell(transition: int, state: Dictionary) -> void:
		var iface := _iface()
		if iface == null:
			return
		var origin := int(command_cell_at(transition).get(&"origin", -1))
		iface.native_joint_record(
			_entity,
			transition,
			_state_columns(state),
			_joint_cell_at(transition)[&"command"],
			_timeline != null and not _timeline.input_at(transition).is_empty(),
			origin == NetwPredict.CommandOrigin.RELAYED,
			origin == NetwPredict.CommandOrigin.PREDICTED,
		)


	func _joint_cell_at(transition: int) -> Dictionary:
		var cell := command_cell_at(transition)
		var origin := int(cell.get(&"origin", -1))
		var authored := _timeline.input_at(transition)
		var provenance := NetwPredict.joint_cell(
			not authored.is_empty(),
			origin == NetwPredict.CommandOrigin.RELAYED,
			origin == NetwPredict.CommandOrigin.PREDICTED,
		)
		var command := _coast_command()
		match provenance:
			NetwPredict.CellProvenance.AUTHORED:
				command = authored
			NetwPredict.CellProvenance.RELAYED, \
			NetwPredict.CellProvenance.SUBSTITUTED:
				command = cell.get(&"command", { })
		return { &"command": command, &"provenance": provenance }


	# Frees the members whose departure the replay horizon has passed, and
	# counts the ones still held.
	func _release_lingering(floor_transition: int) -> void:
		var held := 0
		for member: NetwEntity in _joint_lingering.keys():
			if not is_instance_valid(member):
				_joint_lingering.erase(member)
				continue
			var iface := _iface()
			var engine: _PredictionEngine = \
					iface.engine_for(member) if iface else null
			if engine == null or engine._tenure_end < floor_transition:
				_joint_lingering.erase(member)
				continue
			held += 1
		_handle.stats.linger_held = held


	# A basis older than the history that could replay it is answered the way a
	# keyframe is: snap to the newest recorded state and replay nothing.
	func _joint_heal() -> void:
		var newest := transition_state_at(_ledger_drive_frontier())
		if newest.is_empty():
			return
		_restore(newest, NetwPredictJournal.Operator.JOINT_REBASE, -1)


	# Records the newest authoritative transition this member owes a replay
	# from, instead of restoring where the row arrived.
	func _note_joint_basis(
			basis: int,
			payload: Dictionary,
			source: StringName,
	) -> void:
		_joint_basis = maxi(_joint_basis, basis)
		if _handle.schedule == NetwPredict.Schedule.TICK:
			_timeline.record_state(basis + 1, payload)
			# This slot now holds authority's answer rather than the transition
			# the owner drove, so no rule may be judged across it.
			if not _wiring.carry_rules.is_empty():
				_mark_carry_dirty(basis + 1)
		_note_floor_move(source, basis)


	func _note_floor_move(source: StringName, basis: int) -> void:
		_handle.stats.floor_moves_by_source[source] = int(
			_handle.stats.floor_moves_by_source.get(source, 0),
		) + 1
		var iface := _iface()
		if iface:
			iface.native_joint_note_basis(
				_entity,
				basis,
				JOINT_FLOOR_SOURCES[source],
			)


	# Charges the pass's own writes to the per-field recovery ledger, which
	# would otherwise see every joint correction as a body that moved by itself.
	func _note_joint_writes(before: Dictionary) -> void:
		var after := _capture()
		var deltas: Dictionary = { }
		for field: StringName in after:
			if not before.has(field):
				continue
			var delta := _pose_delta(
				after[field],
				before[field],
				_wiring.angle_fields.has(field),
			)
			if delta == null or _delta_negligible(delta):
				continue
			deltas[field] = delta
		if not deltas.is_empty():
			_ledger_note_writes(deltas)


	# Diffs against the pre-correction capture. Staged writes override immediate
	# readback because a PhysicsServer-backed setter may not sync its Node until
	# the next physics frame.
	func _emit_recovered(
			transition: int,
			attribution: NetwPredictJournal.Attribution,
			before: Dictionary,
			correction_write: Dictionary = { },
	) -> void:
		var after := _capture()
		for field: StringName in correction_write:
			after[field] = correction_write[field]
		var deltas: Dictionary = { }
		for field: StringName in after:
			if not before.has(field):
				continue
			var delta := _pose_delta(
				after[field],
				before[field],
				_wiring.angle_fields.has(field),
			)
			if delta == null or _delta_negligible(delta):
				continue
			deltas[field] = delta
		if deltas.is_empty():
			return
		_ledger_note_writes(deltas)
		_handle.recovered.emit(
			transition,
			deltas,
			_last_correction_teleported,
			attribution,
		)


	# The ledger row for one field, created on first mention so a field that never
	# reaches either counter still reads as a row of zeros rather than absent.
	func _ledger_row(field: StringName) -> NetwPredictionHandle.FieldRecovery:
		# has-then-index rather than get() with a default, which would construct
		# the default on every call including the hits, inside the comparison path.
		if not _handle.field_recovery.has(field):
			_handle.field_recovery[field] = NetwPredictionHandle.FieldRecovery.new()
		return _handle.field_recovery[field]


	# The transition index a drive has most recently reached, which is what a
	# recovery landing now is written past.
	func _ledger_drive_frontier() -> int:
		return _last_driven_entry_index \
				if _handle.schedule == NetwPredict.Schedule.FRAME \
				else _latest_input_tick


	# Hands one operator verdict to the pool, which holds the episode the
	# public report is built from.
	func _charge_decision(decision: Dictionary) -> void:
		var iface := _iface()
		if not iface:
			return
		iface.native_record_episode_decision(
			_entity,
			int(decision[&"operator"]),
			int(decision[&"basis"]),
			bool(decision[&"eligible"]),
			bool(decision[&"applied"]),
			decision[&"eligibility"] as Dictionary,
		)


	# Charges this comparison to every field that was past its own tolerance, and
	# settles any earlier write this comparison is entitled to judge.
	#
	# Both halves read the same refreshed divergence, so they run together and
	# before anything this comparison stages. Judging a write against the very
	# comparison that provoked it would score the error it was answering.
	func _ledger_note_comparison(corrected: bool, ack: int) -> void:
		# The common comparison agrees and has no write outstanding, and this runs
		# on every one of them, so it costs nothing before it has work.
		if not _ledger_pending_basis.is_empty():
			_ledger_settle_writes(ack)
		if not corrected:
			return
		for field: StringName in _handle.last_field_divergence:
			if _wiring.trigger_excludes.has(field) or not _wiring.causal_fields.has(field):
				continue
			var error := float(_handle.last_field_divergence[field])
			var epsilon := float(
				_wiring.epsilon_overrides.get(field, _handle.divergence_epsilon),
			)
			if not NetwPredictionHandle._triggers(error, epsilon):
				continue
			var row := _ledger_row(field)
			row.triggered += 1


	# Settles every outstanding write this comparison has reached past.
	func _ledger_settle_writes(ack: int) -> void:
		var settled: Array[StringName] = []
		for field: StringName in _ledger_pending_basis:
			if ack <= _ledger_pending_basis[field]:
				continue
			settled.append(field)
			if float(_handle.last_field_divergence.get(field, INF)) \
					< _ledger_pending_error[field]:
				var row := _ledger_row(field)
				row.contracted += 1
		for field: StringName in settled:
			_ledger_pending_basis.erase(field)
			_ledger_pending_error.erase(field)


	# Charges one recovery's realized per-field writes, and arms each for the
	# contraction verdict a later comparison delivers.
	func _ledger_note_writes(deltas: Dictionary) -> void:
		for field: StringName in deltas:
			if not _wiring.causal_fields.has(field):
				continue
			var row := _ledger_row(field)
			row.repaired += 1
			if _handle.last_field_divergence.has(field):
				_ledger_pending_error[field] = float(
					_handle.last_field_divergence[field],
				)
				_ledger_pending_basis[field] = _ledger_drive_frontier()


	func _delta_negligible(delta: Variant) -> bool:
		match typeof(delta):
			TYPE_FLOAT:
				return absf(delta as float) < 0.000001
			TYPE_VECTOR2:
				return (delta as Vector2).length() < 0.000001
			TYPE_VECTOR3:
				return (delta as Vector3).length() < 0.000001
		return true


	# Feeds one staged recovery into the escalation evidence, or spends it. A
	# skipped recovery repaired nothing so it neither counts nor resets, a
	# replay converges by construction, and a teleport is already the full
	# closure escalation would promote to, so it starts the count over.
	func _track_recovery_convergence(
			escalated: bool,
			plan: Dictionary,
			divergence: float,
			predicted: Dictionary,
			payload: Dictionary,
	) -> void:
		if escalated:
			_record_non_contraction()
			_reset_recovery_trackers()
			# The promoted closure lands mid-disturbance, so the same settling
			# window a fresh contact earns covers the snap it just applied.
			_cooldown_until_tick = _latest_input_tick \
					+ _handle.collision_cooldown_ticks
			return
		if plan[&"skip"] \
				or _correction == NetwPredict.CorrectionMode.REPLAY:
			return
		if plan[&"teleport"]:
			return
		var direction := _divergence_direction(predicted, payload)
		var direction_key: StringName = direction[&"key"]
		var same_field := direction_key == _last_recovery_direction
		var comparable_sign := _last_recovery_sign if same_field else 0
		# The ladder's safety argument is "it shrinks, or it escalates", and it
		# was being discharged on the aggregate divergence from evaluate() -- a
		# max over fields. A recovery that shrinks sphere_position while the
		# field that actually triggered holds flat reads as a shrink, so the
		# streak never grows and the escalation that would promote to a full
		# closure never fires for the field that needs it. Charge the verdict to
		# the triggering field instead, which is what the per-field ledger
		# already got right (:9465) and what this decision did not.
		var trigger := _escalation_field(predicted, payload)
		var field_divergence := divergence
		if trigger != StringName():
			field_divergence = float(
				_handle.last_field_divergence.get(trigger, divergence),
			)
		# Two errors are only comparable when they belong to the same field.
		# Against the aggregate that was free; per field it is not, and a
		# comparison across a change of dominant field would read a switch from
		# metres to rad/s as a shrink or a growth that never happened.
		var comparable_divergence := _last_recovery_divergence if same_field \
				else -1.0
		var verdict := escalation_after(
			_nonshrink_streak,
			comparable_sign,
			comparable_divergence,
			field_divergence,
			direction[&"sign"],
		)
		_nonshrink_streak = verdict[&"streak"]
		_last_recovery_direction = direction_key
		_last_recovery_sign = verdict[&"sign"]
		_last_recovery_divergence = field_divergence
		_escalate_next = verdict[&"escalate"]


	# Forgets the recovery-convergence evidence. Called where the divergence
	# story restarts: a shrink to agreement, a teleport, a rewire, an epoch
	# bump, and the escalation the evidence just spent itself on.
	func _reset_recovery_trackers() -> void:
		_nonshrink_streak = 0
		_last_recovery_direction = StringName()
		_last_recovery_sign = 0
		_last_recovery_divergence = -1.0
		_escalate_next = false


	# The field this recovery is answering for, for escalation purposes: the one
	# whose own error stands furthest past its own epsilon.
	#
	# Measured in epsilons rather than in raw magnitude, because the fields being
	# ranked do not share a unit. Racing compares metres, radians and rad/s in
	# one closure, and a raw max hands the answer to whichever field happens to
	# carry the largest numbers rather than to whichever is most out of
	# tolerance. A field excluded from triggering is excluded here too: it is
	# not what demanded the recovery.
	func _escalation_field(
			predicted: Dictionary,
			payload: Dictionary,
	) -> StringName:
		var dominant := StringName()
		var top := 0.0
		# A field declaring a tolerance of 0.0 triggers on any error and so has no
		# scale to be ranked ON. It used to be skipped outright as a division
		# guard, which let a field raise a correction and then be absent from the
		# ranking that decides which divergence that correction answers. It cannot
		# be normalized either: any ratio against zero outranks every real field
		# however small the error, so an exact field 1e-9 out would outrank a
		# position ten metres out. So it ranks LAST -- the answer only when no
		# field with a positive tolerance triggered, worst raw error first among
		# those that did.
		var exact := StringName()
		var exact_top := 0.0
		for field: StringName in _handle.last_field_divergence:
			if _wiring.trigger_excludes.has(field) or not _wiring.causal_fields.has(field):
				continue
			if not predicted.has(field) or not payload.has(field):
				continue
			var epsilon := float(
				_wiring.epsilon_overrides.get(field, _handle.divergence_epsilon),
			)
			var error := float(_handle.last_field_divergence[field])
			if not NetwPredictionHandle._triggers(error, epsilon):
				continue
			if epsilon <= 0.0:
				if error > exact_top:
					exact_top = error
					exact = field
				continue
			var ratio := error / epsilon
			if ratio > top:
				top = ratio
				dominant = field
		return dominant if dominant != StringName() else exact


	# The field and dominant axis of the divergence this recovery answers.
	func _divergence_direction(
			predicted: Dictionary,
			payload: Dictionary,
	) -> Dictionary:
		var dominant := _escalation_field(predicted, payload)
		# No field is past its own tolerance, so the ranking falls back to raw
		# magnitude: there is still a delta with a sign, and the overshoot test
		# is entitled to see it.
		if dominant == StringName():
			var top := 0.0
			for field: StringName in _handle.last_field_divergence:
				var error := float(_handle.last_field_divergence[field])
				if error > top and predicted.has(field) and payload.has(field):
					top = error
					dominant = field
		if dominant == StringName():
			return { &"key": StringName(), &"sign": 0 }
		return delta_direction(dominant, _pose_delta(
			payload[dominant],
			predicted[dominant],
			_wiring.angle_fields.has(dominant),
		))

	# --- Host-local (listen-server host controlling its own entity) ---


	func _host_local_author_tick(tick: int) -> void:
		var input := _input_binding.snapshot_payload()
		if _timeline:
			_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_frame_input = input
		_input_binding.authored_tick = tick


	func _host_local_frame_step(timing: NetwPredict.Timing) -> void:
		# A frame the clock held bought no simulated time, so it opens no
		# transition. The host authors no command lane, so a held frame ends
		# the pass rather than falling through to a send.
		if not timing.simulating \
				or timing.tick <= _last_frame_transition_tick:
			_handle.stats.authoring_clamped += 1
			return
		_last_frame_transition_tick = timing.tick
		var plan := _kernel._predict_drive(
			_latest_input_tick,
			_last_driven_input_tick,
			timing.tick,
		)
		var label: int = plan[&"label"]
		var fresh: bool = plan[&"fresh"]
		_record_drive(
			label,
			label,
			plan[&"kind"],
			_frame_input,
			timing.tick,
			false,
		)
		_run(_frame_input, timing.delta, label, fresh)
		_ack_advanced = fresh
		if fresh:
			_last_driven_input_tick = label
			_state_binding.authored_tick = label
			_state_binding.reconcile_ack = label


	func _host_local_step(delta: float, tick: int) -> void:
		# The host is the authority and the controller at once, so it simulates from
		# its own gathered input and publishes the result. No prediction, no
		# reconciliation against itself.
		var input := _input_binding.snapshot_payload()
		if _timeline:
			_timeline.record_input(tick, input)
		_record_drive(tick, tick, NetwPredict.DriveKind.FRESH, input, tick, false)
		_run(input, delta, tick, true)
		_state_binding.authored_tick = tick
		_state_binding.reconcile_ack = tick
		# Authoritative state is recorded by the recorder after the tick.

	# --- Consume (server) ---


	func _on_input_frame(header: Dictionary) -> void:
		# Record every sample of the received redundancy window into the server
		# timeline, healing a lost input tick, then open the consume cursor on the
		# oldest tick of the first window so the server consumes the stream from its
		# start. Recording is idempotent per tick.
		var samples: Array = header.get("samples", [])
		if samples.is_empty():
			var tick := int(header.get("tick", -1))
			if tick >= 0:
				samples = [{ "tick": tick, "payload": header.get("payload", { }) }]
		var oldest := -1
		for sample: Dictionary in samples:
			var stick := int(sample.get("tick", -1))
			if stick < 0:
				continue
			_timeline.record_input(stick, sample.get("payload", { }))
			oldest = stick if oldest < 0 else mini(oldest, stick)
		if _next_input_tick < 0 and oldest >= 0:
			_next_input_tick = oldest


	# Consumes at most one queued transition per tick, through the same standing
	# buffer verdict the FRAME tier replays under, with the depth measured in
	# ticks rather than in entries.
	#
	# One tick advances the ack by one. A backlog is worked off at the tick rate
	# rather than by consuming several at once, so authority never runs a
	# transition its own clock has not reached, and a peer reading the ack cannot
	# see it jump a span no single tick produced.
	func _consume_step(delta: float, server_tick: int) -> void:
		var previous_ack := _ack
		if _next_input_tick >= 0:
			_resync_if_stranded()
			var plan := _kernel._predict_consume(
				_queued_span(),
				maxi(0, _handle.consume_buffer_ticks),
				_consume_warmed,
			)
			_consume_warmed = plan[&"warmed"]
			match plan[&"action"]:
				NetwPredict.ConsumeAction.REPLAY:
					_consume_one(delta)
				NetwPredict.ConsumeAction.HOLD:
					_handle.stats.held += 1
					_mark_idle_drive(_ack, NetwPredict.DriveKind.HOLD)
				NetwPredict.ConsumeAction.STARVED:
					_handle.stats.starved += 1
					_mark_idle_drive(_ack, NetwPredict.DriveKind.STARVED)
		_ack_advanced = _ack != previous_ack
		# A held authority tick still owes every remote observer its state, so
		# the frame flows either way. It acknowledges nothing when the consume
		# did not advance: the body has coasted past the ack, and re-stamping it
		# would frame the coast as divergence the owner did not cause.
		_state_binding.authored_tick = server_tick
		_state_binding.reconcile_ack = _ack if _ack_advanced else -1
		_handle.ack_age_ticks = maxi(0, _timeline.newest_input_tick() - _ack)
		# Authoritative state is recorded by the recorder after the tick, so the
		# acknowledgement run sent here covers through the previous tick's close.
		_send_ack_frame()


	# Runs the one transition this physics frame is worth: the owner's if it has
	# arrived, a declared substitution if it has not.
	func _consume_frame_step(timing: NetwPredict.Timing) -> void:
		var previous_ack := _ack
		_last_replayed_fresh = false
		var depth := _replay_depth()
		var drove_before := _handle.stats.drive_seq
		_record_consume_cadence(depth)
		var buffer := maxi(0, _handle.replay_buffer_depth)
		var plan := _kernel._predict_consume(depth, buffer, _replay_warmed)
		_replay_warmed = plan[&"warmed"]
		match plan[&"action"]:
			NetwPredict.ConsumeAction.REPLAY:
				_resync_tape_if_stranded(depth, buffer)
				_replay_tape_entry(timing.delta, timing.tick)
			NetwPredict.ConsumeAction.HOLD:
				_handle.stats.held += 1
				_mark_idle_drive(
					_last_replayed_label,
					NetwPredict.DriveKind.HOLD,
				)
			NetwPredict.ConsumeAction.STARVED:
				_handle.stats.starved += 1
				_mark_idle_drive(
					_last_replayed_label,
					NetwPredict.DriveKind.STARVED,
				)
		_bump_consume_shape(_handle.stats.drive_seq - drove_before)
		_ack_advanced = _ack != previous_ack
		# The frame flows on a held pass too, acknowledging nothing, so a remote
		# observer's stream never gaps while the owner's ack stalls.
		_state_binding.authored_tick = timing.tick
		_state_binding.reconcile_ack = _ack if _ack_advanced else -1
		_refresh_tape_diagnostics()
		_handle.ack_age_ticks = _replay_depth()
		_send_ack_frame()


	# Buckets one authority frame's arrival count and pre-consume standing
	# depth, so the distributions a capture is judged by are engine truth
	# rather than a per-frame stream re-derived offline.
	func _record_consume_cadence(depth: int) -> void:
		var arrivals := mini(_arrivals_this_frame, ARRIVAL_BUCKET_MAX)
		_arrivals_this_frame = 0
		_handle.stats.arrivals[arrivals] += 1
		_handle.stats.replay_depth[mini(depth, REPLAY_DEPTH_BUCKET_MAX)] += 1


	# Buckets the frame under "consumed,quantum_steps". An idle frame reads the
	# previous drive's quantum, deliberately: that is how a held frame shows up
	# beside the two-step drive that follows it.
	func _bump_consume_shape(consumed: int) -> void:
		var key := "%d,%d" % [consumed, _handle.stats.quantum_steps]
		_handle.stats.consume_shape[key] = int(_handle.stats.consume_shape.get(key, 0)) + 1


	# Applies the tape entry at the replay cursor and acknowledges its index.
	func _replay_tape_entry(delta: float, timing_tick: int) -> void:
		var entry := _replay_entry(_replay_cursor)
		if entry.is_empty():
			return
		var label := int(entry.get("label", -1))
		var fresh := bool(entry.get("fresh", false))
		var input := _last_input
		var kind := NetwPredict.DriveKind.REPEAT
		var applied_fresh := false
		if fresh:
			var command := _command_for(entry, label)
			if not command.is_empty():
				input = command
				_last_input = input
				kind = NetwPredict.DriveKind.FRESH
				applied_fresh = true
			else:
				_handle.stats.missing += 1
				kind = NetwPredict.DriveKind.MISSING
		if input.is_empty():
			input = _stall_input
		_record_drive(_replay_cursor, label, kind, input, timing_tick, true)
		_run(input, delta, label, applied_fresh)
		_ack = _replay_cursor
		_replay_cursor += 1
		_last_replayed_label = label
		_last_replayed_fresh = fresh
		_handle.stats.consumed += 1


	# Re-opens a stranded FRAME cursor behind the configured standing buffer.
	func _resync_tape_if_stranded(depth: int, buffer: int) -> void:
		var ceiling := _handle.max_consume_lag_ticks
		if ceiling <= 0 or depth <= buffer + ceiling:
			return
		var target := _replay_cursor + depth - buffer - 1
		if target <= _replay_cursor:
			return
		_declare_skipped(_replay_cursor, target)
		_handle.stats.skipped += target - _replay_cursor
		_handle.stats.resync += 1
		_replay_cursor = target


	# Records the transitions a resync stepped over as ones authority never ran.
	#
	# A skipped transition is the one substitution the honest protocol still
	# admits, so it is journaled and acknowledged rather than left as a silent
	# gap. An owner that sees a gap cannot tell a skip from a lost frame, while
	# a declared substitution names exactly which of its commands never ran.
	func _declare_skipped(from: int, until: int) -> void:
		for transition in range(from, until):
			var queued: Dictionary = _replay_entry(transition)
			_mark_skipped(
				transition,
				int(queued.get("label", -1)),
			)


	# Counts contiguous queued transitions beginning exactly at the replay cursor.
	func _replay_depth() -> int:
		if _replay_cursor < 0:
			return 0
		var depth := 0
		while _command_queue.has(_replay_cursor + depth):
			depth += 1
		return depth


	func _replay_entry(transition: int) -> Dictionary:
		return _command_queue[transition] \
				if _command_queue.has(transition) else { }


	# The command a fresh transition drove with. The owner lane carries it on the
	# transition itself, so there is nothing to look up and nothing to miss.
	func _command_for(entry: Dictionary, label: int) -> Dictionary:
		if entry.has("command"):
			return entry["command"]
		return _timeline.input_at(label) if _timeline.has_input_at(label) else { }


	# Selects at most max_consume_per_tick eligible labels. All older selected
	# labels fold into the newest one, which is the frame's only drive.
	func _fold_and_consume_frame(delta: float) -> void:
		var selected := _next_input_tick
		var allowance := maxi(1, _handle.max_consume_per_tick)
		var fold_budget := maxi(
			0,
			_queued_span() - _handle.consume_buffer_ticks - 1,
		)
		while allowance > 1 and fold_budget > 0 \
				and _input_is_eligible(selected + 1):
			selected += 1
			allowance -= 1
			fold_budget -= 1

		var folded := selected - _next_input_tick
		for tick in range(_next_input_tick, selected):
			if _timeline.has_input_at(tick):
				_handle.stats.consumed += 1
			else:
				_handle.stats.missing += 1
		_handle.stats.folded += folded

		if _timeline.has_input_at(selected):
			var input := _timeline.input_at(selected)
			var kind := NetwPredict.DriveKind.FRESH
			if folded > 0:
				kind = NetwPredict.DriveKind.FOLD_DRIVE
			_record_drive(selected, selected, kind, input, selected, false)
			_run(input, delta, selected, true)
			_last_input = input
			_handle.stats.consumed += 1
		else:
			_handle.stats.missing += 1
			var plan := _iface().native_plan_consume_input(
				NetwPredict.Schedule.FRAME,
				false,
				true,
				not _last_input.is_empty(),
				_handle.missing_policy,
			)
			var input := _last_input if plan.use_last() else _stall_input
			_record_drive(
				selected,
				selected,
				plan.kind() as NetwPredict.DriveKind,
				input,
				selected,
					false,
			)
			if plan.run():
				_run(input, delta, selected, false)
		_ack = selected
		_next_input_tick = selected + 1


	# A tick is eligible once it arrived or a later tick proves its absence.
	func _input_is_eligible(tick: int) -> bool:
		return _timeline.has_input_at(tick) \
				or _timeline.newest_input_tick() > tick


	# Jumps the cursor back to the live edge when it has fallen so far behind that
	# it cannot walk there. The drain never steps over an absent tick, so through a
	# hole the cursor gains one tick per server tick while the controller authors
	# one, and a gap opened by a clock re-anchor or a long stall never closes on
	# its own. Consuming second-old input is worse than skipping it, so past the
	# ceiling the cursor re-opens at the newest arrival behind the standing buffer.
	func _resync_if_stranded() -> void:
		var ceiling := _handle.max_consume_lag_ticks
		if ceiling <= 0 or _queued_span() <= ceiling:
			return
		var target := _timeline.newest_input_tick() - _handle.consume_buffer_ticks
		if target <= _next_input_tick:
			return
		# Skipped ticks are declared substituted like a FRAME resync's entries,
		# floored to the acknowledgement window since older transitions can never
		# ride an ack run anyway.
		for transition in range(
				maxi(_next_input_tick, target - ACK_WINDOW_MAX),
				target,
		):
			_mark_skipped(transition, transition)
		_handle.stats.skipped += maxi(0, target - _next_input_tick)
		_handle.stats.resync += 1
		_next_input_tick = target


	# Queued input ticks from the consume cursor through the newest arrival,
	# counting an unhealed hole as present since the cursor steps over it.
	func _queued_span() -> int:
		return _timeline.newest_input_tick() - _next_input_tick + 1


	func _consume_one(delta: float) -> void:
		var has_input := _timeline.has_input_at(_next_input_tick)
		var plan := _iface().native_plan_consume_input(
			NetwPredict.Schedule.TICK,
			has_input,
			_timeline.newest_input_tick() > _next_input_tick,
			not _last_input.is_empty(),
			_handle.missing_policy,
		)
		if not plan or not plan.eligible():
			return
		var input := _timeline.input_at(_next_input_tick) if has_input \
				else (_last_input if plan.use_last() else _stall_input)
		_record_drive(
			_next_input_tick,
			_next_input_tick,
			plan.kind() as NetwPredict.DriveKind,
			input,
			_next_input_tick,
			false,
		)
		if plan.run():
			_run(input, delta, _next_input_tick, true)
		if plan.missing():
			_handle.stats.missing += 1
		else:
			_last_input = input
			_handle.stats.consumed += 1
		_ack = _next_input_tick
		_next_input_tick += 1

	# --- Shared ---


	# Seals a new skipped row or overlays supersession on retained evidence.
	func _mark_skipped(transition: int, label: int) -> void:
		var iface := _iface()
		if iface:
			iface.native_declare_skipped(_entity, transition, label)


	# The one choke point every real drive passes through, so the journal records
	# exactly the transitions that ran. An idle frame goes through
	# _mark_idle_drive instead and appends no row.
	#
	# [param drive_tick] is the simulated tick of the pass performing this drive,
	# or -1 when the caller has no pass timing of its own. An authoring caller
	# that cannot name it does not feed the native pool, because the pool times
	# its own clamp and fold against that number and a stale one would clamp
	# against the wrong sequence.
	#
	# [param replayed] tells the pool the owner authored this transition and this
	# peer is only re-running it, so the fold, the clamp and the tape entry are
	# already decided and the transition is the caller's to name. A replay is
	# licensed by the entry rather than by a pass tick, which a consuming peer
	# stepped outside its own clock does not have.
	func _record_drive(
			transition: int,
			label: int,
			kind: NetwPredict.DriveKind,
			input: Dictionary,
			drive_tick: int,
			replayed: bool,
	) -> void:
		_handle.stats.drive_seq += 1
		_handle.stats.last_drive_label = label
		_handle.stats.last_drive_kind = kind
		var is_new := not _native_journal_row(transition)
		if is_new:
			_last_quantum = _measure_quantum()
			_handle.stats.quantum_steps = _last_quantum
			_handle.stats.quantum_declared = _declared_quantum_value
			_check_quantum_declaration()
			if _last_quantum != _declared_quantum_value:
				_handle.stats.quantum_faults += 1
		var raw_state := _capture_raw()
		var pre_state := _canonical_state(raw_state)
		var pre_fp := _state_fingerprint(pre_state)
		# The raw column answers whether two peers executed the same transition
		# differently below canonical resolution, so it carries the same causal
		# scope. A raw derived float always differs, and charging EXECUTION for
		# it would indict the solver over a value the solver never read.
		var raw_fp := raw_state_fingerprint(
			compared_state(raw_state, _wiring.causal_fields),
		) if _raw_fp_enabled else 0
		var evidence_mask := NetwPredictJournal.EVIDENCE_RAW \
				if _raw_fp_enabled else 0
		var provenance := _pending_provenance.duplicate()
		var previous := _native_journal_row(transition - 1)
		var c_hash := NetwPredictJournal.fnv1a(_input_bytes(input))
		var pre_families := _state_family_fingerprints(pre_state)
		_open_topology_facts = _topology_facts()
		var iface := _iface()
		if iface and (replayed or drive_tick >= 0):
			iface.configure_native_prediction_axes(
				_entity,
				_handle.schedule,
				_role,
				_correction,
				_handle.snap_restore,
				_handle.max_restore_ticks,
				_pool_island(),
				not _wiring.carry_rules.is_empty(),
				_handle.witness_contacts.is_valid(),
			)
			iface.native_record_input(_entity, label, c_hash)
			var caller_selected_kind := replayed \
					or kind == NetwPredict.DriveKind.SUBSTITUTED
			var pool_transition := iface.native_replay_drive(
				_entity,
				_open_topology_facts,
				transition,
				label,
				kind,
				drive_tick,
				_frame_index,
				_tick_delta,
				_last_quantum,
				pre_fp,
				pre_families,
				raw_fp,
				evidence_mask,
			) if caller_selected_kind else iface.native_open_drive(
				_entity,
				_open_topology_facts,
				drive_tick,
				_frame_index,
				_tick_delta,
				_last_quantum,
				true,
				pre_fp,
				pre_families,
				raw_fp,
				evidence_mask,
			)
			if pool_transition >= 0:
				_pool_transitions[transition] = pool_transition
				if int(provenance.get(&"write_id", 0)) > 0:
					iface.native_mark_provenance(
						_entity,
						pool_transition,
						provenance,
					)
				while _pool_transitions.size() > TAPE_HISTORY_LIMIT:
					var oldest := _pool_transitions.keys()
					oldest.sort()
					_pool_transitions.erase(oldest[0])
		if is_new:
			_pending_provenance = { }
			var previous_flags := previous.flags() if previous else 0
			var comparable := previous != null \
					and bool(previous_flags & NetwPredictJournal.ROW_CLOSED) \
					and not bool(previous_flags & NetwPredictJournal.ROW_SUBSTITUTED)
			if comparable \
					and previous.post_fp() != pre_fp \
					and int(provenance.get(&"write_id", 0)) == 0:
				var native_transition := int(
					_pool_transitions.get(transition, -1),
				)
				if iface and native_transition >= 0:
					iface.native_mark_chain_broken(
						_entity,
						native_transition,
					)
				_handle.stats.chain_breaks += 1
		# The environment is fingerprinted before the drive runs, because what
		# attribution needs to know is the world the transition ran AGAINST. A
		# digest taken afterward would describe the world the transition helped
		# make, which cannot exonerate or convict it.
		_e_digest = _sample_environment()
		_mark_domain(transition, domain_of(
			_handle.island.declared,
			_handle.island.approximate,
			label,
			_out_of_domain_until,
		))


	# Records the entitlement on the row the pool owns.
	func _mark_domain(
			transition: int,
			domain: NetwPredictJournal.Domain,
	) -> void:
		var iface := _iface()
		var pool_transition := int(_pool_transitions.get(transition, -1))
		if iface and pool_transition >= 0:
			iface.native_mark_domain(_entity, pool_transition, domain)


	# Fingerprints the declared world facts this drive runs against. An entity
	# that declared no epoch and no sensors digests to a constant, which is the
	# honest answer: it declared nothing, so it can discover nothing.
	func _sample_environment() -> int:
		var epoch := _handle.epoch
		var sensors := _handle.sensors
		# An entity that declared no world facts digests to the same zero an
		# unwritten row already holds, so the fold is skipped rather than run to
		# reach a foregone answer. Every drive of every predicted entity passes
		# through here, which makes a constant worth not recomputing.
		if epoch == -1 and sensors.is_empty():
			return 0
		if epoch != _island_epoch:
			# A world the game says changed version reopens the window, since the
			# peers cannot both have adopted the change on the same transition.
			if _island_epoch != -1:
				_out_of_domain_until = window_after(
					_latest_input_tick,
					_handle.collision_cooldown_ticks,
					_out_of_domain_until,
				)
			_island_epoch = epoch
			# The recoveries before the world changed were answering divergences
			# of a world that no longer exists, so their streak proves nothing
			# about the one that replaced it.
			_reset_recovery_trackers()
		var samples: Dictionary = { }
		for name: StringName in sensors:
			var sampler := sensors[name] as Callable
			if sampler.is_valid():
				samples[name] = sampler.call()
		_sensor_samples = samples
		return environment_digest(epoch, samples)


	func _mark_idle_drive(label: int, kind: NetwPredict.DriveKind) -> void:
		_handle.stats.last_drive_label = label
		_handle.stats.last_drive_kind = kind


	# Authors exactly one contiguous tape entry for the FRAME drive about to run.
	func _author_tape_entry(label: int, fresh: bool) -> void:
		var entry := {
			"index": _next_tape_entry_index,
			"label": label,
			"fresh": fresh,
		}
		_authored_tape.append(entry)
		while _authored_tape.size() > TAPE_HISTORY_LIMIT:
			_authored_tape.remove_at(0)
		_last_driven_entry_index = _next_tape_entry_index
		_handle.stats.tape_epoch = _tape_epoch
		_handle.stats.tape_index = _next_tape_entry_index
		_next_tape_entry_index += 1


	func _authored_entry_at(index: int) -> Dictionary:
		for entry: Dictionary in _authored_tape:
			if int(entry.get("index", -1)) == index:
				return entry
		return { }


	func _refresh_tape_diagnostics() -> void:
		var indices: Array = _command_queue.keys()
		indices.sort()
		_handle.stats.tape_epoch = _command_epoch
		_handle.stats.tape_queue_depth = _replay_depth()
		_handle.stats.tape_index = (
				int(indices.back()) if not indices.is_empty() else -1
		)


	func _run(input: Dictionary, delta: float, tick: int, is_fresh: bool) -> void:
		var iface := _iface()
		if iface and iface.native_owner_bound(_entity):
			iface.native_run_step(_entity, input, delta, tick, is_fresh)
			return
		_input_binding.apply_payload(input)
		if _handle.simulate.is_valid():
			_handle.simulate.call(delta, tick, is_fresh)


	# Keeps the pool's copy of the step equal to the handle's, which is what
	# lets the pass run the step without asking the shell for it.
	func push_simulate() -> void:
		var iface := _iface()
		if iface:
			iface.native_set_simulate(_entity, _handle.simulate)


	# Round-trips prediction input through its wire quantizers so both peers feed
	# the simulation the same canonical values.
	func _canonical_input(payload: Dictionary) -> Dictionary:
		var iface := _iface()
		return iface.native_canonicalize_input(_entity, payload) if iface \
				else _input_binding.canonicalize_payload(payload)


	# Every recorded slot holds the canonical form, so a compare between a
	# prediction and the authority payload that produced it is an equality
	# question rather than a tolerance question.
	func _capture() -> Dictionary:
		return _canonical_state(_state_binding.snapshot_payload())


	func _canonical_state(payload: Dictionary) -> Dictionary:
		var iface := _iface()
		return iface.native_canonicalize_state(_entity, payload) if iface \
				else _state_binding.canonicalize_payload(payload)


	func _state_bytes(payload: Dictionary) -> PackedByteArray:
		var iface := _iface()
		return iface.native_canonical_state_bytes(_entity, payload) if iface \
				else _state_binding.canonical_bytes(payload)


	func _input_bytes(payload: Dictionary) -> PackedByteArray:
		var iface := _iface()
		return iface.native_canonical_input_bytes(_entity, payload) if iface \
				else _input_binding.canonical_bytes(payload)


	func _capture_raw() -> Dictionary:
		return _state_binding.snapshot_payload()


	# The compared state is the causal state. The recorded payload stays whole,
	# because a recovery restores fields the comparison has no business judging.
	func _state_fingerprint(payload: Dictionary) -> int:
		return NetwPredictJournal.fnv1a(
			_state_bytes(compared_state(payload, _wiring.causal_fields)),
		)


	# Fingerprints pose, momentum, then controller and latch state separately,
	# over the same causal scope the whole-state fingerprint uses. A family names
	# which part of the state forked, so it has to name a part the next
	# transition reads.
	func _state_family_fingerprints(payload: Dictionary) -> PackedInt32Array:
		var family_payloads: Array[Dictionary] = [{ }, { }, { }]
		for key: StringName in compared_state(payload, _wiring.causal_fields):
			if not _wiring.state_family_of.has(key):
				continue
			family_payloads[_wiring.state_family_of[key]][key] = payload[key]
		var out := PackedInt32Array()
		for family: Dictionary in family_payloads:
			out.append(
				0 if family.is_empty() else NetwPredictJournal.fnv1a(
					_state_bytes(family),
				),
			)
		return out


	# Seals the after-solve observation before sealing the produced state.
	func _close_journal_row(transition: int, state: Dictionary) -> void:
		_record_joint_cell(transition, state)
		var row := _native_journal_row(transition)
		if not row or row.flags() & NetwPredictJournal.ROW_CLOSED:
			return
		var solve := _sample_solve_evidence(_open_topology_facts, transition)
		var post_fp := _state_fingerprint(state)
		var post_families := _state_family_fingerprints(state)
		var iface := _iface()
		var pool_transition := int(_pool_transitions.get(transition, -1))
		if iface and pool_transition >= 0:
			iface.native_record_evidence(
				_entity,
				pool_transition,
				_handle.epoch,
				_sensor_samples,
				solve[&"topology_facts"],
				solve[&"contacts"],
				bool(solve[&"sleeping"]),
			)
		if iface and pool_transition >= 0:
			iface.native_close_drive(
				_entity,
				pool_transition,
				post_fp,
				post_families,
			)
		_record_witness_detail(transition, solve[&"detail"])
		_maybe_demote_for_breach(transition, solve)


	# Retains one transition's solve detail, bounded with the tape it indexes
	# into so a long-lived engine cannot grow a row per transition forever.
	func _record_witness_detail(transition: int, detail: Dictionary) -> void:
		_witness_details[transition] = detail.duplicate(true)
		while _witness_details.size() > TAPE_HISTORY_LIMIT:
			var retained := _witness_details.keys()
			retained.sort()
			_witness_details.erase(retained[0])


	# Closes speculation on the same transition that realized the breach.
	func _maybe_demote_for_breach(transition: int, solve: Dictionary) -> void:
		if _role != NetwPredict.Role.PREDICT or _fallback_latched \
				or _handle.breach_response \
						!= NetwPredict.BreachResponse.DEMOTE \
				or not _handle.witness_contacts.is_valid() \
				or not bool(solve.get(&"breach", false)):
			return
		_mark_domain(transition, NetwPredictJournal.Domain.OUT_OF_DOMAIN)
		var breach_iface := _iface()
		if not breach_iface:
			return
		var opened := not _episode_open()
		breach_iface.native_record_breach(
			_entity,
			int(_pool_transitions.get(transition, transition)),
		)
		if opened:
			_sync_episode()
			_handle.episode_opened.emit(
				_handle._episode_report(_pool_episode()),
			)
		_record_episode_write(
			NetwPredictJournal.Operator.DEMOTE,
			transition,
			int(solve.get(&"witness_fp", 0)),
			_entity.entity_id,
			true,
		)
		# TICK closes before its ordinary lane send. Preserve the command that
		# produced the breach before rewire opens the author-only epoch.
		_send_command_frame()
		_enter_fallback(
			transition,
			NetwPredictJournal.Attribution.CONTACT,
			true,
		)


	# The execution facts known before the solve begins, as facts rather than as
	# a fingerprint, because the solve adds the body's own and the two fold
	# together exactly once. A nested fold would make a topology that names a
	# body unequal to one that names the same body a different way round.
	func _topology_facts() -> Dictionary:
		var participants := PackedStringArray()
		for participant in _island_participants():
			participants.append(String(participant.entity_id))
		participants.sort()
		return {
			&"schedule": _handle.schedule,
			&"epoch": _handle.epoch,
			&"participants": participants,
		}


	func _topology_fingerprint(facts: Dictionary, quantum: int) -> int:
		var folded := facts.duplicate()
		folded[&"quantum"] = quantum
		return fact_fingerprint(folded)


	# How much simulated time the transition now opening actually buys, in
	# physics steps since the previous drive.
	#
	# The physics server integrates a solver body on its own cadence, so a drive
	# and a solve are two separate events and only their ratio makes a compared
	# transition mean the same thing on both peers. Recording the ratio is what
	# lets the boundary ladder charge TOPOLOGY instead of walking to CLOSURE and
	# reporting that every antecedent agreed.
	#
	# An unmeasurable step reports the declared quantum rather than zero. A first
	# drive and a rewire are peer-local facts, and a peer-local fact must never
	# manufacture a cross-peer mismatch.
	func _measure_quantum() -> int:
		var previous := _last_drive_frame
		_last_drive_frame = _frame_index
		if previous < 0 or _frame_index <= 0:
			return _declared_quantum_value
		return clampi(_frame_index - previous, 0, QUANTUM_STEPS_MAX)


	# Asks the interface to judge the clock configuration this body drives under.
	# The engine holds no clock, so the rates are the interface's to read.
	func _check_quantum_declaration() -> void:
		if _handle.schedule != NetwPredict.Schedule.FRAME:
			return
		var iface := _iface()
		if iface:
			iface._report_quantum_misconfiguration(_entity)


	# Samples and classifies the realized solve. An absent declaration is unknown.
	func _sample_solve_evidence(
			base_facts: Dictionary,
			transition: int,
	) -> Dictionary:
		var no_contacts: Array[Dictionary] = []
		var empty := {
			&"topo_fp": _topology_fingerprint(base_facts, _last_quantum),
			&"topology_facts": base_facts,
			&"witness_fp": 0,
			&"witness_class_bits": 0,
			&"evidence_mask": 0,
			&"contacts": no_contacts,
			&"sleeping": false,
			&"detail": { },
		}
		var sampler := _handle.witness_contacts
		_realized_contact_entities.clear()
		if not sampler.is_valid():
			return empty
		var sample := _normalize_witness_sample(sampler.call())
		if sample.is_empty():
			return empty
		var support := _declared_support_collider()
		var contact_classes: Array[int] = []
		var collider_classes := PackedStringArray()
		var collider_ids := PackedStringArray()
		var compared_contacts := PackedStringArray()
		var outside_boundary_ids := PackedStringArray()
		var island_participants := _island_participants()
		var witness_class_bits := 0
		var contacts: Array[Dictionary] = []
		for value: Variant in sample[&"colliders"]:
			var collider := value as Object
			var collider_entity := _contact_entity(collider)
			if collider_entity and collider_entity != _entity:
				_realized_contact_entities[collider_entity] = true
			var contact_class := _classify_collider(collider, support)
			if contact_class not in contact_classes:
				contact_classes.append(contact_class)
			var collider_type := collider.get_class()
			if collider_type not in collider_classes:
				collider_classes.append(collider_type)
			var collider_id := _collider_identity(collider)
			if collider_id not in collider_ids:
				collider_ids.append(collider_id)
			if _contact_breaches_boundary(
					collider,
					support,
					island_participants,
			) \
					and collider_id not in outside_boundary_ids:
				outside_boundary_ids.append(collider_id)
			var witness_class := _witness_class(collider, support)
			witness_class_bits |= witness_class
			var compared := "%s:%d" % [collider_id, witness_class]
			if compared not in compared_contacts:
				compared_contacts.append(compared)
			contacts.append({
				&"identity": collider_id,
				&"witness_class": witness_class,
				&"realization": contact_class,
				&"outside_boundary": collider_id in outside_boundary_ids,
			})
		if contact_classes.is_empty():
			contact_classes.append(NetwPredict.ContactClass.NONE)
		contact_classes.sort()
		collider_classes.sort()
		collider_ids.sort()
		compared_contacts.sort()
		outside_boundary_ids.sort()
		var sleeping := bool(sample[&"sleeping"])
		var woke := _has_previous_witness and _previous_witness_sleeping \
				and not sleeping
		_previous_witness_sleeping = sleeping
		_has_previous_witness = true
		var detail := {
			&"contact_classes": contact_classes,
			&"collider_classes": collider_classes,
			&"collider_ids": collider_ids,
			&"contact_bucket": contact_count_bucket(
				(sample[&"colliders"] as Array).size(),
			),
			&"sleeping": sleeping,
			&"woke": woke,
			&"solve_ordinal": transition,
			&"witness_class_bits": witness_class_bits,
			&"outside_boundary_ids": outside_boundary_ids,
			&"breach": not outside_boundary_ids.is_empty(),
			&"continuous": sample.get(&"continuous", { }).duplicate(true),
		}
		var topology_facts := base_facts.duplicate()
		for key in [&"body_mode", &"collision_layer", &"collision_mask"]:
			if sample.has(key):
				topology_facts[key] = sample[key]
		return {
			&"topo_fp": _topology_fingerprint(topology_facts, _last_quantum),
			&"topology_facts": topology_facts,
			&"witness_fp": fact_fingerprint({
				&"contacts": compared_contacts,
			}),
			&"witness_class_bits": witness_class_bits,
			&"breach": not outside_boundary_ids.is_empty(),
			&"evidence_mask": NetwPredictJournal.EVIDENCE_WITNESS,
			&"contacts": contacts,
			&"sleeping": sleeping,
			&"detail": detail,
		}


	# Accepts only the documented witness shape and reports a bad declaration once.
	func _normalize_witness_sample(value: Variant) -> Dictionary:
		if value is Dictionary:
			var sample := value as Dictionary
			var colliders: Variant = sample.get(&"colliders", null)
			var sleeping: Variant = sample.get(&"sleeping", null)
			if colliders is Array and typeof(sleeping) == TYPE_BOOL:
				for collider: Variant in colliders:
					if not collider is Object or not is_instance_valid(collider):
						return _report_invalid_witness()
				var continuous: Variant = sample.get(&"continuous", { })
				if continuous is Dictionary:
					return sample
		return _report_invalid_witness()


	func _report_invalid_witness() -> Dictionary:
		if not _invalid_witness_reported:
			_invalid_witness_reported = true
			push_error(
				"Prediction witness must return {colliders: Array, sleeping: bool}; "
				+ "continuous, when present, must be a Dictionary.",
			)
		return { }


	func _declared_support_collider() -> String:
		var ground: Variant = _sensor_samples.get(&"ground", { })
		if ground is Dictionary:
			var collider: Variant = (ground as Dictionary).get(&"collider", null)
			if collider is String or collider is StringName:
				return String(collider)
		return ""


	func _classify_collider(
			collider: Object,
			support: String,
	) -> NetwPredict.ContactClass:
		if not support.is_empty() and _collider_identity(collider) == support:
			return NetwPredict.ContactClass.DECLARED_SUPPORT
		if collider is AnimatableBody2D or collider is AnimatableBody3D \
				or collider is CharacterBody2D or collider is CharacterBody3D:
			return NetwPredict.ContactClass.KINEMATIC_PROXY
		if _is_static_geometry(collider):
			return NetwPredict.ContactClass.OTHER_STATIC
		if collider is RigidBody2D and (collider as RigidBody2D).freeze \
				or collider is RigidBody3D and (collider as RigidBody3D).freeze:
			return NetwPredict.ContactClass.KINEMATIC_PROXY
		var entity := _contact_entity(collider)
		var iface := _iface()
		if entity and iface and iface.predicts(entity):
			return NetwPredict.ContactClass.PREDICTED_DYNAMIC
		return NetwPredict.ContactClass.UNPREDICTED_DYNAMIC


	# Returns the peer-invariant class compared across the acknowledgement lane.
	func _witness_class(collider: Object, support: String) -> int:
		var iface := _iface()
		if not iface:
			return NetwPredict.WitnessClass.NONE
		return iface.native_witness_class(
			collider,
			not support.is_empty() and _collider_identity(collider) == support,
		)


	# True when the solve consumed a stand-in the closure cannot reproduce.
	# Static world geometry is identical on every peer, so touching it is
	# in-boundary; only an un-stepped dynamic body is a stale stand-in.
	func _contact_breaches_boundary(
			collider: Object,
			support: String,
			island_participants: Array[NetwEntity],
	) -> bool:
		if not support.is_empty() and _collider_identity(collider) == support:
			return false
		if _is_static_geometry(collider):
			return false
		var entity := _contact_entity(collider)
		if not entity:
			return true
		if entity == _entity:
			return false
		if entity not in island_participants:
			return true
		return entity.prediction.sim_mode == NetwPredict.SimMode.DISPLAY


	func _collider_identity(collider: Object) -> String:
		var node := collider as Node
		if _is_static_geometry(collider):
			return "path:%s" % node.get_path()
		var entity := _contact_entity(collider)
		if entity:
			return "entity:%s" % entity.entity_id
		return "path:%s" % node.get_path() if node else "object:%s" % \
				collider.get_class()


	func _is_static_geometry(collider: Object) -> bool:
		var iface := _iface()
		return iface.native_static_geometry(collider) if iface else false


	# Resolves the entity a contact belongs to. A scene wrapper is a container,
	# not a body: its level geometry must never read as contact with the scene
	# entity itself.
	func _contact_entity(collider: Object) -> NetwEntity:
		var node := collider as Node
		var entity := NetwEntity.of(node) if node else null
		if entity and entity.declares_scene:
			return null
		return entity


	func _restore(
			payload: Dictionary,
			operator: NetwPredictJournal.Operator = NetwPredictJournal.Operator.NONE,
			basis: int = -1,
			provenance_owner: _PredictionEngine = null,
			evidence_free: bool = false,
			pool_planned: bool = false,
	) -> void:
		var staged := _StagedRestore.new(
			payload,
			operator,
			basis,
			provenance_owner,
			evidence_free,
			pool_planned,
		)
		_apply_staged_restore(staged)


	func _apply_staged_restore(staged: _StagedRestore) -> void:
		if not staged.is_valid():
			push_error("Prediction restore effect is invalid.")
			return
		var iface := _iface()
		if iface and iface.native_owner_bound(_entity):
			iface.native_apply_state(_entity, staged.payload)
		else:
			_state_binding.apply_payload(staged.payload)
		_record_staged_restore(staged)


	func _record_staged_restore(staged: _StagedRestore) -> void:
		# The next recording carries this write as well as the drive before it, so
		# it is not a transition any rule states and cannot judge one.
		if not _wiring.carry_rules.is_empty():
			_mark_carry_dirty(_ledger_drive_frontier() + 1)
		if staged.operator == NetwPredictJournal.Operator.NONE:
			return
		var episode_owner := staged.provenance_owner \
				if staged.provenance_owner else self
		var stats := episode_owner._episode_stats()
		var episode_id := 0
		if stats[NetwPredictionEngine.STAT_EPISODE_STATE] \
				== NetwPredict.EpisodeState.OPEN:
			episode_id = int(stats[NetwPredictionEngine.STAT_EPISODE_ID])
		var delta_fp := NetwPredictJournal.fnv1a(_state_bytes(staged.payload))
		var write_id := 0
		if staged.pool_planned:
			write_id = int(
				stats[NetwPredictionEngine.STAT_EPISODE_LAST_WRITE_ID],
			)
			episode_owner._stamp_episode_write_delta(delta_fp)
		else:
			write_id = episode_owner._record_episode_write(
				staged.operator,
				staged.basis,
				delta_fp,
				_entity.entity_id if _entity else StringName(),
				staged.evidence_free,
			)
		_pending_provenance = {
			&"episode": episode_id,
			&"write_id": write_id,
			&"operator": staged.operator,
			&"basis": staged.basis,
		}


	func _register_with_loop() -> void:
		var iface := _iface()
		if iface:
			iface._runner.register(self)
			_registered = true


	func _unregister_from_loop() -> void:
		var iface := _iface()
		if iface and _registered:
			iface._runner.unregister(self)
		_registered = false
