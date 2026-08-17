## Runtime prediction-boundary policy and evidence for one [NetwEntity].
##
## Every entity owns one stable handle through [member NetwEntity.prediction],
## and every entity-level fact about its prediction is a property on it. There
## is no second way to say any of them: what a game declares, a reader reads
## back from the same name.
## [codeblock]
## var pred := NetwEntity.of(self).prediction
## pred.archetype = NetwPredict.Archetype.SOLVER_BODY
## pred.witness_contacts = _sample_contacts
## pred.breach_response = NetwPredict.BreachResponse.DEMOTE
## pred.island.add(opponent)
## [/codeblock]
## Only membership earns a class of its own, because who is in the island and
## how faithfully each is simulated are one fact rather than two: see
## [NetwPredictIsland]. Per-field facts -- a tolerance, a restore restriction, a
## forward model -- are not here at all. They belong to the field, and are
## declared on its property mark through [NetwScriptModel].
##
## [br][br]Each fact has one source. A [PredictionComponent] may supply scene
## policy, in which case it applies its archetype preset first and then writes
## only the exports the scene moved off their defaults, so a scene value
## overrides the preset it refines and an export left alone does not. Between
## the scene and code, the later write wins: a property reports the value that
## was written to it, and refusing an assignment would make it report one
## nobody wrote.
##
## [br][br]The client compares its claim with the authority row named by the
## server acknowledgement. [member input_source] and [member sim_mode] state
## whether this peer authors, receives, predicts, or displays the entity.
## [method stats], [method journal], [method episode], and [method metrics]
## expose the resulting evidence without changing simulation, and
## [method reachability] answers the question a meter cannot: whether a
## declaration can reach the thing it names at all.
class_name NetwPredictionHandle
extends RefCounted

const PredictionCore := preload("res://addons/networked/replication/prediction_core.gd")


## One state field's running account of the recoveries it asked for against
## the ones that answered it, a row of
## [member PredictionHandle.field_recovery].
##
## The counts are kept apart because a field can score any combination of them
## independently, and which combination it scores is the whole diagnosis. A
## field that triggers and is never repaired is asking for recoveries that go
## to other fields. One repaired without contracting is being written with a
## value that does not help.
## [codeblock]
## triggered  ──> repaired ──> contracted    the field is served
## triggered  ──> repaired ──> .             written, and the write is no help
## triggered  ──> .        ──> .             asking, and answered elsewhere
## .          ──> repaired ──> .             written on someone else's behalf
## [/codeblock]
class FieldRecovery:
	extends RefCounted

	## Comparisons that corrected while this field was past its own
	## [method NetwScriptModel.PropertyConfig.epsilon], so a recovery was
	## materially answering it.
	var triggered: int = 0

	## Recoveries that wrote this field, whether or not it asked for one.
	var repaired: int = 0

	## Writes to this field that a later comparison found smaller.
	##
	## A recovery lands on the body now while the comparisons just behind it
	## were driven at the acknowledgement, so a write is only judged once a
	## comparison reaches a transition driven past it. Scoring the very next
	## comparison would read back the error the write was answering, which is
	## the same lag that makes a correction train look like a divergence
	## refusing to close.
	var contracted: int = 0

	## Recoveries whose write this field's declared
	## [method NetwScriptModel.PropertyConfig.carry_step] rule advanced to
	## the present.
	var carried: int = 0

	## Carries the engine refused, each of which wrote the acknowledged value
	## instead, which is what an undeclared field always writes.
	##
	## A rule is refused when the recorded transitions it would fold over are
	## missing or incomplete, when it returns the wrong type or a non-finite
	## value, when it moves the value further than a teleport would, or once it
	## has been retired.
	var declined: int = 0

	## Times the rule failed to reproduce a transition the owner had already
	## recorded, which is the one check that can tell a rule reading the live
	## world, or one that is simply wrong, from a rule that agrees.
	##
	## Enough of these retires the rule, after which it is never called again
	## and every recovery writes the acknowledged value.
	var infidelity: int = 0

## Generator status when the causal fork predates retained journal rows.
const GENERATOR_UNKNOWN_BEYOND_RETENTION := &"UNKNOWN_BEYOND_RETENTION"

## The simulation step, defaulting to the entity root's
## [code]_network_tick(delta, tick, is_fresh)[/code]. Set it to route the step
## through a delegating node. It is a single [Callable], never a fan-out, so
## exactly one authoritative step runs per entity per tick.
var simulate: Callable = Callable():
	set(value):
		simulate = value
		var engine := _engine()
		if engine:
			engine.push_simulate()

## The drive cadence: one drive per network tick, or one per physics frame.
##
## A solver body declares [constant NetwPredict.Schedule.FRAME], because a
## rigid body's transition is a physics step and pretending otherwise makes
## every recorded transition a lie about what the solver did.
var schedule: NetwPredict.Schedule = NetwPredict.Schedule.TICK

## How a correction is applied, a [enum NetwPredict.CorrectionMode] value. See
## [enum PredictionComponent.CorrectionMode] for the scene-facing enum.
var correction_mode: int = NetwPredict.CorrectionMode.AUTO

## How a [constant NetwPredict.CorrectionMode.SNAP] restore lands on the body, a
## [enum NetwPredict.RestoreMode] value. [constant NetwPredict.RestoreMode.EXTRAPOLATED] projects each
## field that declares a [member NetwInterpolate.project_channel] velocity
## sibling forward to the present tick through [NetwProject]. Defaults to
## [constant NetwPredict.RestoreMode.EXACT]. Ignored under [constant NetwPredict.CorrectionMode.REPLAY],
## whose input replay already advances the body to the present.
var snap_restore: int = NetwPredict.RestoreMode.EXACT

## Where this peer's copy of the entity gets its command, resolved when the
## entity wires. Read-only: authority and local control decide it.
##
## This and [member sim_mode] are the two independent facts the flat
## [enum NetwPredict.Role] compresses into one name. Reading them separately is what lets
## a question about one axis be answered without naming a whole role.
var input_source: NetwPredict.InputSource = NetwPredict.InputSource.NONE

## What this peer's simulation of the entity means, resolved when the entity
## wires. Read-only: authority and local control decide it.
var sim_mode: NetwPredict.SimMode = NetwPredict.SimMode.DISPLAY

## The [enum NetwPredict.RecoveryPolicy] this entity recovers under, or
## [code]-1[/code] while none has been declared, in which case
## [method resolved_recovery_policy] derives one from
## [member correction_mode].
var recovery_policy: int = -1:
	set(value):
		recovery_policy = value
		# A policy names a strategy and the mechanism carries it out, so
		# declaring one settles the other. This ran inside recovery().policy()
		# and a direct write skipped it; with the verb gone there is one
		# source for the fact and it cannot be skipped.
		correction_mode = _correction_mode_for_policy(value)
		var engine := _engine()
		if engine:
			engine.rewire()

## Immediate response to an out-of-boundary realized contact.
##
## The response is armed only when [member witness_contacts] declares the
## observation. [constant NetwPredict.BreachResponse.DEMOTE] follows authority through
## the existing delayed fallback path while the command lane stays live.
var breach_response: NetwPredict.BreachResponse = \
		NetwPredict.BreachResponse.PREDICT_THROUGH:
	set(value):
		breach_response = value
		# Who chose it, for the report. DEMOTE is the one recovery fact a
		# player feels directly, so a reader must be able to tell a game that
		# asked for it from a default that arrived. This stamping lived in the
		# deleted on_breach() verb; the property carries it now, and the scene
		# path re-stamps after _push_config.
		_breach_source = &"code"

## The entities this one claims to simulate the way authority does.
##
## Naming members through [member NetwPredictIsland.participants] or a
## producer through [method NetwPredictIsland.from_interest] opts the entity
## into the fingerprint compare; that claim, and nothing else, is what
## [member NetwPredictIsland.declared] reports. An entity that declares
## nothing is out of domain on every transition, the absence of a claim
## rather than a claim of divergence.
##
## Membership producers and explicit entities feed one runtime set. The
## interest producer admits only entities replicated on this peer. Promotion
## selects which members become
## [constant NetwPredict.Fidelity.SIMULATED]; every other member remains a
## witnessed [constant NetwPredict.Fidelity.PROXY]. Changes commit at
## transition boundaries, with distance hysteresis and contact-safe fidelity
## handoffs. A simulated member uses the zero-input COAST command unless
## [method NetwPredictIsland.predict_commands] supplies one, and each
## received authority state independently rebases it through the configured
## projection, so its error is bounded by receive cadence times command
## error. Its display chases the collidable simulated body.
## [codeblock]
## var island := NetwEntity.of(self).prediction.island
## island.from_interest()
## island.simulate_nearest(1)
## [/codeblock]
##
## Never [code]null[/code]. Assigning one installs it and binds it to this
## entity, which is how a scene's own rule is inherited.
var island := NetwPredictIsland.new():
	set(value):
		island = value if value else NetwPredictIsland.new()
		island._bind(entity())

## The world facts this entity's transition reads, by name.
##
## Each sampler is called once before each drive and folded into the
## environment digest, so a divergence can be charged to the environment
## instead of left unattributed; [method sensor] reads the sample back. A
## sampler that cannot be called is skipped at sample time rather than
## refused here, which keeps one spelling for the declaration and leaves the
## digest describing exactly the facts the drive ran against.
##
## Sensors are environment attribution, not a claim of exactness, so
## declaring them alone leaves every transition
## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN].
var sensors: Dictionary[StringName, Callable] = { }

## The version of the static world this entity simulates against, or
## [code]-1[/code] while none is declared.
##
## Bumping it reopens the tolerance window, since two peers cannot have
## adopted a world change on the same transition. Like [member sensors] this
## declares the environment, not an island, so it never opts the entity into
## the fingerprint compare.
var epoch: int = -1

## Samples the contacts this peer realized on the transition just solved.
##
## The sampler is called once per solved transition and returns the colliders
## the body actually touched, which is what lets a divergence be charged to a
## contact rather than left unattributed. An invalid or unset Callable is
## valid and makes the witness boundary unknown, which is the absence of an
## observation rather than an observation of nothing.
var witness_contacts: Callable = Callable():
	set(value):
		# Refusing the write would make the property lie about what it holds,
		# so the mistake is reported and stored: a non-null Callable that
		# cannot be called is a typo, while a null one is how a game clears
		# the declaration.
		if not value.is_null() and not value.is_valid():
			push_error(
				"PredictionHandle.witness_contacts: sampler must be a valid "
				+ "Callable. The witness boundary stays unknown.",
			)
		witness_contacts = value
		# Whether a witness observes at all is a declaration the tables are
		# built from, so a sampler arriving after the wiring re-resolves it.
		var engine := _engine()
		if engine:
			engine.rewire()

## Reports whether the path to a present-time pose is clear of obstruction.
##
## Only a declared corridor arms the conditional transport operator, because
## composing a present-time offset across a path nobody swept is how a body
## arrives inside a wall. An invalid or unset Callable keeps the operator
## disabled.
var transport_corridor: Callable = Callable():
	set(value):
		if not value.is_null() and not value.is_valid():
			push_error(
				"PredictionHandle.transport_corridor: sweep must be a valid "
				+ "Callable. Transport stays disabled.",
			)
		transport_corridor = value

## Ceiling in ticks on the age a [constant NetwPredict.RestoreMode.EXTRAPOLATED] restore
## projects across. The projection advances by the unacked span
## [member ack_age_ticks]. The prediction governor holds before that span can
## exceed its structural ceiling, but a linear projection near the ceiling can
## still land a body far off a curved path. This caps that span the way
## [member NetwDisplayHandle.max_forecast_ticks] caps the
## display forecast, so a stale ack never launches the body. Defaults to the
## same [code]6[/code].
var max_restore_ticks: int = 6

## Pose error, in the pose field's own units, above which a recovery restores
## the whole closure instead of withholding the fields declared
## [method NetwScriptModel.PropertyConfig.teleport_only].
##
## A large error means a genuine desync (a wall bounce, a teleport) rather than
## a contractive field drifting, and past it the predicted body holds nothing
## worth keeping. The error is measured over the fields enrolled in the tier:
## the ones that declared a
## [method NetwScriptModel.PropertyConfig.carry_along] channel and the ones that
## named their own [method NetwScriptModel.PropertyConfig.teleport_at]. An
## entity with neither has no pose to measure and every one of its recoveries
## restores the whole closure.
##
## This is the DEFAULT for an enrolled field that named no distance of its own,
## and it is only meaningful for fields sharing its units. A pose spanning
## metres, radians and radians per second cannot be served by one scalar, so a
## field in any other unit declares
## [method NetwScriptModel.PropertyConfig.teleport_at] instead of inheriting a
## number that means nothing for it. Each enrolled field is compared to its own
## distance, and any one of them reaching it is the tier.
var teleport_threshold: float = 2.0:
	set(value):
		teleport_threshold = maxf(0.0, value)

## Ticks that a [method notify_contact] pauses non-teleport corrections for. A
## collision makes the predicted and authoritative bodies legitimately differ
## for a few ticks while both solvers settle, and correcting through that
## transient fights the solver. A hard desync past [member teleport_threshold]
## still snaps.
var collision_cooldown_ticks: int = 6:
	set(value):
		collision_cooldown_ticks = maxi(0, value)

## True while the authoritative body is asleep.
##
## Corrections pause while asleep so a sleeping body is never nudged awake by
## reconciliation. Set it from the game when the dynamic body sleeps and
## wakes.
var sleeping: bool = false

## Server policy for a missing input tick, a [enum NetwPredict.MissingInput] value. See
## [enum PredictionComponent.MissingInput] for the scene-facing enum.
var missing_policy: int = NetwPredict.MissingInput.STALL

## How many queued input ticks a [constant NetwPredict.Schedule.FRAME] pass may fold into
## the one drive it applies when a backlog has built up.
##
## The client authors one input per tick, so a default of [code]1[/code] holds
## lockstep. A value above it lets a frame skip past inputs that have already
## arrived rather than replaying a backlog one frame at a time. Folding never
## steps over a lost tick, and the folded inputs are counted in
## [member NetwPredictStats.folded] rather than simulated, so a fold is never extra
## simulation.
##
## A [constant NetwPredict.Schedule.TICK] entity ignores this. Its consume advances the
## ack by exactly one per tick, because authority may not run a transition its
## own clock has not reached.
var max_consume_per_tick: int = 1

## Queued input ticks the server keeps standing instead of consuming to empty,
## a de-jitter buffer. Input arrival rides two free-running peer clocks, so its
## phase drifts through the server's consume boundary and a zero-slack cursor
## alternates starved ticks (the entity does not step at all) with drained ones.
##
## The depth is a maintained standing target, not a one-time warm-up. A tick
## whose queue has fallen to the target consumes nothing and rebuilds the slack
## instead ([member NetwPredictStats.held]), and the [member max_consume_per_tick] drain
## trims a burst back down to the target rather than to zero. So a single slip
## in either direction is absorbed and repaid rather than spent permanently.
## [codeblock]
## span > buffer   ->  consume (and drain toward buffer + 1)
## 0 < span <= buffer  ->  hold, the slack rebuilds   (held)
## span <= 0       ->  starved, no input exists       (starved)
## [/codeblock]
## Each tick of depth costs its own tick of input latency, and a hold delays one
## input by one solver step. [code]0[/code] consumes as inputs arrive, keeping
## strict lockstep with no slack to absorb drift.
var consume_buffer_ticks: int = 0

## [constant NetwPredict.Schedule.FRAME] tape transitions authority leaves standing in the
## queue instead of replaying, a fixed latency it adds to every command.
##
## It buys nothing back. Authority replays at most one transition per frame,
## so it can never drain faster than the owner fills, and a reserve that
## cannot be spent faster than it is refilled absorbs no jitter. Every tick of
## depth is a tick of [member ack_age_ticks] on every recovery basis, which is
## why the default is zero.
## [codeblock]
## depth > buffer  ──> replay one transition   (consumed)
## 0 < depth <= buffer  ──> hold, spending nothing   (held)
## depth == 0      ──> the queue is dry         (starved)
## [/codeblock]
## At the default of zero the middle row is unreachable, so a frame either
## replays the transition it has or reports that it has none. Raise it only
## to trade acknowledgement latency for a later replay position, never to
## smooth arrival: what covers a dry frame is queue depth an earlier burst
## already built, at any buffer including zero.
var replay_buffer_depth: int = 0:
	set(value):
		replay_buffer_depth = maxi(0, value)

## Ceiling in ticks on how far the server's consume cursor may fall behind the
## freshest input before it gives up walking and re-opens at the live edge.
##
## The drain never steps over an absent tick, so across a gap the cursor gains
## one tick per server tick while the controller keeps authoring one per tick.
## A gap therefore never closes on its own, and the ack stays behind forever,
## which reads as an entity that is simulated but never reconciled. The most
## common way to open one is a client whose clock re-anchors after it has
## already authored input, joining a session that has been running a while.
## [codeblock]
## span <= ceiling  ->  walk the cursor, healing holes one at a time
## span >  ceiling  ->  re-open at newest - consume_buffer_ticks (resync)
## [/codeblock]
## Input skipped by a resync is second-old and no longer worth simulating, and
## the count lands in [member NetwPredictStats.skipped]. Set [code]0[/code] to disable the
## recovery and let the cursor walk however far behind it falls.
var max_consume_lag_ticks: int = 60:
	set(value):
		max_consume_lag_ticks = maxi(0, value)

## Ticks the ack lags behind the freshest input this handle knows, updated each
## tick. On the owning client it is the unacked span the extrapolated restore
## projects across ([member max_restore_ticks] caps it). On the server it is the
## consume backlog ([member max_consume_per_tick] drains it). Read it to observe
## the RC4 ratchet: a healthy link holds it near zero, a growing value means the
## consume cursor is falling behind.
var ack_age_ticks: int = 0

## Divergence above which a state receive triggers a correction. A field that
## needs its own tolerance declares it with
## [method NetwScriptModel.PropertyConfig.epsilon], which overrides this
## default for that field alone.
var divergence_epsilon: float = 0.01:
	set(value):
		divergence_epsilon = maxf(0.0, value)

## Reconcile mode for this handle's island (INDEPENDENT or JOINT).
var reconcile_mode: NetwPredict.Reconcile = NetwPredict.Reconcile.INDEPENDENT


## Each state field's own divergence from the most recent authoritative
## comparison, refreshed on every state receive on the owning client.
##
## [signal state_evaluated] carries only the worst field's error, so a set
## mixing meters, radians, and meters per second cannot identify which field
## moved. Read this to tell a position fork from a velocity fork, in
## particular when a field is declared
## [method NetwScriptModel.PropertyConfig.reconcile_only] or
## [method NetwScriptModel.PropertyConfig.teleport_only] and so diverges
## without ever triggering or being restored. Empty until the first
## comparison.
var last_field_divergence: Dictionary[StringName, float] = { }

## Per state field, how often it demanded a recovery and how often one
## repaired it, accumulated since spawn.
##
## A field may be admitted to the trigger set without being reachable by any
## operator in the ladder. [method NetwScriptModel.PropertyConfig.teleport_only]
## withholds a field from every sub-teleport restore while leaving it free to
## trigger, so it can raise corrections that are forbidden to write it and are
## answered by repairing some other field instead. Each comparison looks
## ordinary on its own and [member last_field_divergence] cannot show it,
## because the evidence is the whole run rather than any one tick. This is the
## count the wiring report names when it warns about that pairing.
## [codeblock]
## var row := entity.prediction.field_recovery[&"sphere_angular_velocity"]
## print(row.triggered, row.repaired, row.contracted)   # 1393  50  0
##
## # triggers nearly every recovery, is withheld from nearly every one, and
## # the few that wrote it did not shrink it: no operator repairs this field
## [/codeblock]
## In domain a correction is decided by fingerprint rather than tolerance, so
## [member NetwPredictionHandle.FieldRecovery.triggered] names the fields that
## were also past their declared tolerance rather than the ones that cast the
## deciding vote. A field declared
## [method NetwScriptModel.PropertyConfig.reconcile_only] never counts a
## trigger, since it is excluded from the decision by declaration.
var field_recovery: Dictionary[StringName, FieldRecovery] = { }

## Ticks by which the prediction compared against the most recent
## authoritative state was older than the tick that state answers for, or
## [code]-1[/code] when nothing was recorded to compare at all.
##
## A comparison is only honest at zero. The predicted state is read
## carry-forward, so when the owner recorded nothing at the acked tick the
## comparison silently falls back to an older prediction and the resulting
## [member last_field_divergence] mixes real divergence with the distance the
## body simply travelled in between. Read this alongside every divergence
## number to tell the two apart. Non-zero on the ticks the owner did not
## simulate, which is the schedule disagreeing rather than the physics.
var last_compare_staleness: int = -1

# Who put the current value in [member breach_response]: the scene's
# component, a code verb, or nobody. An archetype preset is deliberately not
# among the answers -- one used to set it, and the most consequential felt
# behaviour in this campaign came from a default no game file named.
var _breach_source: StringName = &"default"

## Per enrolled pose field, the error the teleport tier last measured, or
## empty when the last comparison measured none.
##
## NOT the same number as [member last_field_divergence], and that is the
## whole reason it is published: a divergence is measured against the
## authoritative value, while the tier is measured against that value
## EXTRAPOLATED to now through the field's
## [method NetwScriptModel.PropertyConfig.carry_along] channel. A field whose
## channel overshoots reaches the tier while its compared divergence stays
## small, so a capture that explains a teleport with the divergence is quoting
## a number that did not cause it. Read this against the distance the field
## declared with [method NetwScriptModel.PropertyConfig.teleport_at].
var last_tier_errors: Dictionary[StringName, float] = { }

## Why the most recent comparison's verdict did not reach the body, or
## [constant NetwPredict.VerdictReason.NONE] when nothing stood in its way.
##
## Read it beside every [signal state_evaluated] row, the way
## [member last_compare_staleness] is read beside every divergence. The signal
## carries the verdict a comparison reached; this carries what was done about
## it, and the two answer different questions. A telemetry consumer that
## counts corrections off the verdict alone counts comparisons that wrote
## nothing -- a re-quarantined probation, an exhausted episode, a deferred
## operator -- and cannot tell them from repairs.
## [codeblock]
## entity.prediction.state_evaluated.connect(func(_r, ack, error, corrected):
##     if corrected and entity.prediction.last_verdict_reason \
##             != NetwPredictionHandle \
##             .VerdictReason.NONE:
##         print("transition %d disagreed by %.3f and wrote nothing" % [
##             ack, error,
##         ])
## )
## [/codeblock]
var last_verdict_reason: NetwPredict.VerdictReason = NetwPredict.VerdictReason.NONE

## True while a correction is restoring and replaying. Game-feel code reads it.
var is_reconciling: bool = false

## Everything this entity's engine counted, one name per fact.
##
## The demoted half of the evidence surface: counters, histograms, tape
## bookkeeping, fingerprint tallies. Diagnostic rather than contract -- read
## it, print it, chart it, but build a rule on the signals and the named
## readers beside them instead, because those are what the freeze table
## protects. See [NetwPredictStats].
var stats := NetwPredictStats.new()

## Highest transition authority has acknowledged, or [code]-1[/code] before
## the acknowledgement lane reaches this peer.
##
## This is the stable lane frontier. [member NetwPredictStats.ack_confirmed]
## carries the diagnostic counter it reads from.
var acknowledged_tick: int:
	get:
		return stats.ack_confirmed

## Which antecedent of the recurrence the most recent divergence was charged
## to, a [enum NetwPredictJournal.Attribution] value. Meaningful only once
## [member last_attributed_transition] is not [code]-1[/code].
##
## [constant NetwPredictJournal.Attribution.UNKNOWN] means required evidence
## was absent. Every other value names the first observed boundary that
## differed.
var last_attribution: NetwPredictJournal.Attribution = \
		NetwPredictJournal.Attribution.UNKNOWN

## The transition [member last_attribution] describes, or [code]-1[/code]
## while no divergence has been charged.
var last_attributed_transition: int = -1

## Emitted when a transition is found to disagree with authority, before
## anything is done about it, naming the transition and what the disagreement
## is charged to.
##
## A divergence is reported whether or not it is recovered, so a game that
## only observes hears about one exactly as loudly as a game that corrects.
## [codeblock]
## entity.prediction.divergence_detected.connect(func(entry, attribution):
##     if attribution == NetwPredictJournal.Attribution.CLOSURE:
##         push_warning("transition %d diverged under equal antecedents" % entry)
## )
## [/codeblock]
signal divergence_detected(
		entry: int,
		attribution: NetwPredictJournal.Attribution,
)

## Emitted when an actionable settled comparison opens an episode.
##
## [param report] has the same schema as [method episode].
signal episode_opened(report: Dictionary)

## Emitted after the verified agreement run closes an episode.
##
## [param report] has the same schema as [method episode]. It is the final
## detached record and remains valid after later journal rows evict its
## generator.
signal episode_closed(report: Dictionary)

## Emitted when bounded recovery evidence enters delayed fallback.
##
## [param report] has the same schema as [method episode] at quarantine entry.
## Command capture continues while local speculation is closed.
signal episode_fallback(report: Dictionary)

## Emitted after a recovery wrote the body, naming the transition it rebased
## at, the per-field pose change it applied ([code]after - before[/code],
## shortest-arc radians for an angle field), whether it was a teleport-tier
## restore, and what the divergence was charged to.
##
## A recovery is one write, so this fires once per recovery and the deltas are
## the whole of it. A display does not need to absorb them by hand: under
## [constant NetwDisplayHandle.PredictedMode.CHASE] the interpolator
## already turns each one into a decaying render offset, clamped, reset per
## recovery, and snapped through on a teleport, tuned by
## [member NetwDisplayHandle.chase_glide_time].
## [codeblock]
## entity.interpolation.predicted_mode = \
##         NetwDisplayHandle.PredictedMode.CHASE
## entity.interpolation.chase_glide_time = 0.15
## # the visual glides onto each corrected pose; connect here only to
## # observe what a recovery wrote
## [/codeblock]
signal recovered(
		entry: int,
		deltas: Dictionary,
		teleported: bool,
		attribution: NetwPredictJournal.Attribution,
)

## Emitted on every state receive on the owning client, before the trim, with
## the full divergence including sub-[member divergence_epsilon] values and
## the verdict the comparison reached.
##
## [param diverged] is the VERDICT, not the write. A comparison that judged a
## divergence reports it here whether or not anything was written for it, and
## several exits deliberately write nothing -- the evidence ran out, an
## operator is waiting on a witness, a transport or dissipate is pending.
## Those rows used to report false, which made this flag mean "a write
## happened" on some paths and "a divergence was judged" on others, and left
## an observe-only reader unable to see a divergence the engine had decided
## not to act on. [member last_verdict_reason] names why a judged divergence
## wrote nothing. The write has its own signal, [signal recovered].
##
## This is the per-receive companion to [signal divergence_detected], which
## fires only when a transition actually disagrees and names the transition and
## its charge. A divergence that grows tick over tick yet never crosses
## [member divergence_epsilon] is heard here and never there, so telemetry that
## must observe an unacted drift reads this signal. The scalar is the worst
## field alone, and [member last_field_divergence] breaks it out per field.
signal state_evaluated(
		recv_tick: int,
		ack: int,
		divergence: float,
		diverged: bool,
)

var _entity_ref: WeakRef
var _engine_ref: WeakRef
var _episode_snapshot: Dictionary = { }
# Bumps on every evidence mutation, so a reader detects a change without
# detaching or serializing the record.
var _episode_revision: int = 0
# Subject instance ids whose local islands currently step this entity.
var _simulation_subjects: Dictionary[int, Callable] = { }

## The named prediction bundle this entity starts from, or
## [constant NetwPredict.Archetype.NONE].
##
## A preset is a starting point, not an arbiter: writing it applies the
## bundle's schedule and recovery facts outright, and anything written
## afterward refines it. [PredictionComponent] therefore applies the scene's
## archetype first and pushes only the exports the scene actually moved, so
## an export left at its default cannot clobber the preset it was meant to
## refine.
##
## No preset sets [member breach_response]. It is the one recovery fact that
## changes [member sim_mode] -- it stops speculation at a witnessed contact
## and follows authority until a reseed -- and the most consequential felt
## behaviour in this whole campaign came from a bundle setting it where no
## game file named it. A game that wants it says so.
var archetype: NetwPredict.Archetype = NetwPredict.Archetype.NONE:
	set(value):
		archetype = value
		match value:
			NetwPredict.Archetype.KINEMATIC:
				schedule = NetwPredict.Schedule.TICK
				missing_policy = NetwPredict.MissingInput.STALL
				recovery_policy = NetwPredict.RecoveryPolicy.REBASE_REPLAY
			NetwPredict.Archetype.SOLVER_BODY:
				schedule = NetwPredict.Schedule.FRAME
				missing_policy = NetwPredict.MissingInput.REPEAT_LAST
				recovery_policy = NetwPredict.RecoveryPolicy.REBASE_RECOVER
				snap_restore = NetwPredict.RestoreMode.EXTRAPOLATED
				teleport_threshold = 3.0


## Reads back the value the engine sampled for the declared sensor
## [param name] before the drive now running, or [param default] when
## nothing has sampled it.
##
## A declared sensor is sampled once per drive and folded into the
## environment digest, and this read is the other half of that bargain: a
## drive that reads the sample instead of re-querying the world runs
## against exactly the facts its digest describes, so an environment
## divergence can be charged to the environment rather than left
## unattributed.
## [codeblock]
## func _init() -> void:
##     entity.prediction.sensors[&"ground"] = _sample_ground
##
## func _network_tick(delta: float, tick: int, fresh: bool) -> void:
##     var ground: Dictionary = entity.prediction.sensor(&"ground", { })
## [/codeblock]
func sensor(name: StringName, default: Variant = null) -> Variant:
	var engine := _engine()
	if not engine:
		return default
	var iface := engine._iface()
	var api := iface._api() if iface else null
	return api.predict_sensor_sample(
		api.entity_of(engine._entity.owner),
		name,
		default,
	) if api and engine._entity else default


## Returns the [enum NetwPredict.RecoveryPolicy] this entity recovers under, resolving
## [constant NetwPredict.CorrectionMode.AUTO] against the body type the way
## [method resolved_correction_mode] does.
func resolved_recovery_policy() -> NetwPredict.RecoveryPolicy:
	if recovery_policy >= 0:
		return recovery_policy as NetwPredict.RecoveryPolicy
	return (
			NetwPredict.RecoveryPolicy.REBASE_REPLAY
			if resolved_correction_mode() == NetwPredict.CorrectionMode.REPLAY
			else NetwPredict.RecoveryPolicy.REBASE_RECOVER
	)


# The correction mechanism a policy runs through. A policy names a strategy
# and the mechanism is how it is carried out, so several policies can share
# one mechanism without sharing a meaning.
func _correction_mode_for_policy(
		policy: NetwPredict.RecoveryPolicy,
) -> NetwPredict.CorrectionMode:
	match policy:
		NetwPredict.RecoveryPolicy.REBASE_REPLAY:
			return NetwPredict.CorrectionMode.REPLAY
		NetwPredict.RecoveryPolicy.REBASE_RECOVER, NetwPredict.RecoveryPolicy.DELAY_CLOSED:
			return NetwPredict.CorrectionMode.SNAP
		NetwPredict.RecoveryPolicy.OBSERVE:
			# Nothing is written under OBSERVE, so the mechanism named here
			# only decides what the recovery would have run had it written.
			return NetwPredict.CorrectionMode.SNAP
	return NetwPredict.CorrectionMode.SNAP


## Returns the [enum NetwPredict.Role] named by [param source] and [param mode], the
## compatibility view over the two axes.
##
## The axes are the decision and the role is its name, so every role is
## reachable from some pair and no pair reaches a role that contradicts it.
## [codeblock]
##             AUTHORITATIVE   SPECULATIVE   DISPLAY
##   LOCAL     HOST_LOCAL      PREDICT       REMOTE
##   RECEIVED  CONSUME         (unreachable) REMOTE
##   PREDICTED (unreachable)   SIMULATE      REMOTE
##   NONE      REMOTE          REMOTE        REMOTE
## [/codeblock]
static func role_for_axes(
		source: NetwPredict.InputSource,
		mode: NetwPredict.SimMode,
) -> NetwPredict.Role:
	# The simulation axis is asked first. A peer running none needs no
	# command, so where it would have read one cannot change what it is.
	if mode == NetwPredict.SimMode.DISPLAY:
		return NetwPredict.Role.REMOTE
	if source == NetwPredict.InputSource.LOCAL:
		return NetwPredict.Role.HOST_LOCAL if mode == NetwPredict.SimMode.AUTHORITATIVE \
		else NetwPredict.Role.PREDICT
	if source == NetwPredict.InputSource.RECEIVED \
			and mode == NetwPredict.SimMode.AUTHORITATIVE:
		return NetwPredict.Role.CONSUME
	if source == NetwPredict.InputSource.PREDICTED \
			and mode == NetwPredict.SimMode.SPECULATIVE:
		return NetwPredict.Role.SIMULATE
	return NetwPredict.Role.REMOTE


## Returns the [NetwEntity] this handle configures.
func entity() -> NetwEntity:
	return _entity_ref.get_ref() as NetwEntity if _entity_ref else null


## True once an engine record has wired to this handle. Display and readiness code
## reads this to tell a live predictor from a bare config handle.
func is_registered() -> bool:
	return _engine() != null


## Steps prediction or consumption for [param tick]. Forwards to the engine, a
## no-op when none is wired.
func simulate_tick(delta: float, tick: int) -> void:
	var engine := _engine()
	if engine:
		engine.simulate_tick(
			NetwPredict.Timing.new(
				tick,
				delta,
				_ticktime(),
			),
		)


## Applies one FRAME-scheduled drive. A no-op for TICK scheduling or when no
## engine is wired.
##
## The transition it authors is labeled from the clock the way
## [method LagCompCore.frame_step] labels one, so driving an
## entity directly and letting the pump drive it produce the same transition.
func simulate_frame(delta: float) -> void:
	var engine := _engine()
	if not engine:
		return
	var timing := _frame_timing()
	timing.delta = delta
	engine.simulate_frame(timing)


# Captures a FRAME pass's timing from the interface, the shell's job. Falls
# back to a clock-free reading so a handle stepped outside a session still
# drives rather than refusing.
func _frame_timing() -> NetwPredict.Timing:
	var engine := _engine()
	var iface = engine.shell() if engine else null
	if iface:
		return iface.frame_timing()
	return NetwPredict.Timing.new(0, 0.0, 0.0)


func _ticktime() -> float:
	return _frame_timing().ticktime


## Returns the [NetwPredictJournal] recording every transition this entity's
## engine drove, or [code]null[/code] before one is wired.
##
## The journal is what a capture, an overlay, or a determinism check reads. It
## is the engine's own record, so reading it never perturbs the simulation, and
## its rows outlive the correction that consumed them.
## [codeblock]
## var journal := entity.prediction.journal()
## if journal and journal.first_unmatched() >= 0:
##     print("unverified from transition ", journal.first_unmatched())
## [/codeblock]
func journal() -> NetwPredictJournal:
	var engine := _engine()
	return engine.journal() if engine else null


## Returns what this entity's declarations actually reach, per state field,
## or an empty [Dictionary] before the entity is wired.
##
## Every question here is answerable from declarations alone, and every one of
## them has cost a measurement round to answer by hand. A declaration can be
## legal, be accepted, and then be inert: [method
## NetwScriptModel.PropertyConfig.carry_step] under
## [constant NetwPredict.Schedule.TICK] is refused on first use and permanently retired, a
## [method NetwScriptModel.PropertyConfig.teleport_only] field free to trigger
## demands recoveries no sub-teleport restore may write, and an
## [method NetwScriptModel.PropertyConfig.epsilon] on a field no comparison
## reads bounds nothing. This is where a game reads that at wire time
## instead of discovering it in a capture.
## [codeblock]
## var report := entity.prediction.reachability()
## for key: StringName in report[&"fields"]:
##     var row: Dictionary = report[&"fields"][key]
##     if not row[&"forward_model"][&"live"] \
##             and row[&"forward_model"][&"kind"] != &"none":
##         print(key, " declares a forward model that is not live: ",
##                 row[&"forward_model"][&"why"])
##
## # what each field can expect, and what the entity does about a breach
## print(report[&"breach"])   # { response: DEMOTE, declared_by: code }
## print(report[&"findings"]) # the same list the wiring report emits
## [/codeblock]
## [code]fields[/code] is keyed by property, and each row carries its
## [code]class[/code], whether it [code]triggers[/code], the
## [code]tolerance[/code] and [code]teleport_at[/code] distance in force with
## whether each was declared or inherited, whether it is [code]in_tier[/code],
## the [code]operators[/code] that can write it, and its
## [code]forward_model[/code]. [code]findings[/code] is what the wiring report
## says out loud, so a test can assert on it rather than on log text.
## The per-field teleport distances in force, as a detached copy.
##
## [method reachability] answers this too, and answers far more besides, which
## is exactly why it is the wrong reader for a recorder: it rebuilds the whole
## report per call and a recorder runs once per state receive. This is the
## cheap read of the one fact, and it exists because the alternative was a
## game reaching into an engine member -- which Phase B's wiring record broke,
## as it was always going to.
##
## A field absent from the returned map declared no distance of its own and
## inherits [member teleport_threshold].
func teleport_distances() -> Dictionary:
	var engine := _engine()
	return engine.teleport_distances() if engine else { }


func reachability() -> Dictionary:
	var engine := _engine()
	return engine.reachability_report() if engine else { }


## Returns the active or most recently retired disturbance episode report.
##
## The returned [Dictionary] is detached from engine storage. An empty value
## means no actionable divergence has opened. The stable report sections are:
## [codeblock]
## {
##     id: int,
##     generator: {
##         transition: int,
##         boundary: NetwPredictJournal.Attribution,
##         row: Dictionary,
##     },
##     operators: Array[{
##         operator: NetwPredictJournal.Operator,
##         basis: int,
##         eligible: bool,
##         applied: bool,
##         eligibility: Dictionary,
##         outcome: NetwPredict.OperatorOutcome,
##         write: Dictionary,
##     }],
##     contraction: Array[{transition, meter, agrees, write_id}],
##     disposition: {
##         state: NetwPredict.EpisodeState,
##         non_contraction_used: int,
##         closure_used: int,
##         closed_transition: int,
##         fallback_transition: int,
##         demoted: bool,
##         breach_transition: int,
##         breach_witness: Dictionary,
##         resume_ack_age: int,
##         quarantine_target: int,
##         quarantine_clean_run: int,
##         reseed_transition: int,
##         aligned_transition: int,
##         evidence_dropped: int,
##     },
##     reopen_chain: Array[int],
## }
## [/codeblock]
## [code]generator.row[/code] is the retained journal row, whose
## [code]witness_detail[/code] carries the realized contact behind its witness
## fingerprint. If the causal run predates retention, its status is
## [constant GENERATOR_UNKNOWN_BEYOND_RETENTION]. Later divergent rows appear
## in [code]taint[/code], while independent agreeing-pre failures appear in
## [code]secondary_generators[/code]. The original evidence keys remain in the
## detached report for capture compatibility. A refused operator has
## [code]outcome = -1[/code] and an empty [code]write[/code].
## [br][br]Each evidence series is retained to a bounded window, because an
## episode has no bound on how long it may stay open. A trim keeps the newest
## entries and counts what it elided, so a truncated series is legible as one
## rather than passing for a short one. Detaching this report costs the whole
## retained record, so a reader that runs every frame wants
## [method episode_digest] and detaches here only when it reports a change.
func episode() -> Dictionary:
	var engine := _engine()
	if engine:
		return engine.episode()
	return _episode_report(_episode_snapshot)


## Returns the episode's present-time scalars without copying its evidence.
##
## [method episode] detaches the whole record, and an open episode retains one
## entry per settled comparison, so a per-frame reader that only wants the
## disposition pays for every comparison the episode has ever seen. This costs
## the same whatever the episode's age, which is what makes it safe to read
## every frame from an overlay, a recorder, or a change check.
## [codeblock]
## var digest := entity.prediction.episode_digest()
## if digest.is_empty():
##     return                                # no divergence has opened one
## if digest[&"revision"] != _last_seen:
##     _last_seen = digest[&"revision"]
##     _export(entity.prediction.episode())  # the full report, on change only
##
## {
##  ┠╴id (int)                          the open or last retired episode
##  ┠╴revision (int)                    bumps on every evidence mutation
##  ┠╴last_comparison_transition (int)  newest settled aligned comparison
##  ┠╴last_meter (int)                  that comparison's aligned error meter
##  ┠╴generator (Dictionary)            {transition, boundary}: where and how
##  ┃                                   causality first forked
##  ┠╴last_operator (Dictionary)        {operator, basis, outcome}, empty when
##  ┃                                   the episode has attempted none
##  ┠╴disposition (Dictionary)          the same section [method episode] emits
##  ┖╴evidence (Dictionary)             {comparisons, writes, decisions, taint,
##                                      secondary_generators, dropped}
## }
## [/codeblock]
## [code]evidence.dropped[/code] counts entries a trim elided once the episode
## outlived the retained window, so a reader can tell a short contraction
## series from a truncated one. [code]last_operator.outcome[/code] is
## [code]-1[/code] for an attempt that was refused before it wrote.
func episode_digest() -> Dictionary:
	var raw := _episode_snapshot
	if raw.is_empty():
		return { }
	var row: Dictionary = raw.get(&"generator_row_copy", { })
	var comparisons: Array = raw.get(&"comparisons", [])
	var writes: Array = raw.get(&"writes", [])
	var decisions: Array = raw.get(&"decisions", [])
	var attempt: Dictionary = { }
	if not writes.is_empty():
		var write: Dictionary = writes.back()
		attempt = {
			&"operator": int(
				write.get(
					&"operator",
					NetwPredictJournal.Operator.NONE,
				),
			),
			&"basis": int(write.get(&"basis", -1)),
			&"outcome": int(write.get(&"outcome", -1)),
		}
	elif not decisions.is_empty():
		var decision: Dictionary = decisions.back()
		attempt = {
			&"operator": int(
				decision.get(
					&"operator",
					NetwPredictJournal.Operator.NONE,
				),
			),
			&"basis": int(decision.get(&"basis", -1)),
			&"outcome": -1,
		}
	return {
		&"id": int(raw.get(&"id", 0)),
		&"revision": _episode_revision,
		&"last_comparison_transition": int(
			raw.get(&"last_comparison_transition", -1),
		),
		&"last_meter": int(comparisons.back().get(&"meter", 0)) \
		if not comparisons.is_empty() else 0,
		&"generator": {
			&"transition": int(
				row.get(
					&"transition",
					raw.get(&"opened_transition", -1),
				),
			),
			&"boundary": int(
				raw.get(
					&"attribution",
					NetwPredictJournal.Attribution.UNKNOWN,
				),
			),
		},
		&"last_operator": attempt,
		&"disposition": _episode_disposition(raw),
		&"evidence": {
			&"comparisons": comparisons.size(),
			&"writes": writes.size(),
			&"decisions": decisions.size(),
			&"taint": (raw.get(&"taint", []) as Array).size(),
			&"secondary_generators": (
					raw.get(&"secondary_generators", []) as Array
			).size(),
			&"dropped": int(raw.get(&"evidence_dropped", 0)),
		},
	}


# The scalar half of the report, shared by the detached report and the digest
# so one projection serves both and they can never drift apart.
func _episode_disposition(raw: Dictionary) -> Dictionary:
	return {
		&"state": int(raw.get(&"state", NetwPredict.EpisodeState.OPEN)),
		&"non_contraction_used": int(raw.get(&"non_contraction_used", 0)),
		# Non-contractions the budget did NOT charge, because every field
		# that triggered them was one the recovery may not write. Reported
		# beside the spend rather than folded into it: the count is how a
		# game sees that its declarations, not the ladder, are what the
		# episode kept meeting.
		&"withheld_non_contractions": int(
			raw.get(&"withheld_non_contractions", 0),
		),
		# Exempt for a different reason: the write was never evidence, so it
		# had nothing to spend. Kept apart from the withheld count so that
		# count means only what K1 claims for it.
		&"evidence_free_non_contractions": int(
			raw.get(&"evidence_free_non_contractions", 0),
		),
		# The charged breakdown. These two sum to non_contraction_used.
		&"nc_no_trigger": int(raw.get(&"nc_no_trigger", 0)),
		&"nc_mixed_trigger": int(raw.get(&"nc_mixed_trigger", 0)),
		&"closure_used": int(raw.get(&"closure_used", 0)),
		&"closed_transition": int(raw.get(&"closed_transition", -1)),
		&"fallback_transition": int(raw.get(&"fallback_transition", -1)),
		&"demoted": bool(raw.get(&"demoted", false)),
		&"breach_transition": int(raw.get(&"breach_transition", -1)),
		&"breach_witness": (
				raw.get(&"breach_witness", { }) as Dictionary
		).duplicate(true),
		&"resume_ack_age": int(raw.get(&"resume_ack_age", 0)),
		&"quarantine_target": int(raw.get(&"quarantine_target", 0)),
		&"quarantine_clean_run": int(raw.get(&"quarantine_clean_run", 0)),
		&"reseed_transition": int(raw.get(&"reseed_transition", -1)),
		&"aligned_transition": int(raw.get(&"aligned_transition", -1)),
		&"evidence_dropped": int(raw.get(&"evidence_dropped", 0)),
	}


# Projects raw resumable episode state into the stable public report.
func _episode_report(raw: Dictionary) -> Dictionary:
	if raw.is_empty():
		return { }
	var report := raw.duplicate(true)
	var row: Dictionary = report.get(&"generator_row_copy", { })
	report[&"generator"] = {
		&"transition": int(
			row.get(
				&"transition",
				report.get(&"opened_transition", -1),
			),
		),
		&"boundary": int(
			report.get(
				&"attribution",
				NetwPredictJournal.Attribution.UNKNOWN,
			),
		),
		&"row": row,
	}
	report[&"operators"] = _episode_operator_attempts(report)
	report[&"contraction"] = (
			report.get(&"comparisons", []) as Array
	).duplicate(true)
	report[&"disposition"] = _episode_disposition(report)
	var chain: Array = report.get(&"reopen_chain", [])
	if chain.is_empty():
		var reopened_from := int(report.get(&"reopened_from", 0))
		if reopened_from > 0:
			chain.append(reopened_from)
		chain.append(int(report.get(&"id", 0)))
	report[&"reopen_chain"] = chain.duplicate()
	return report


# Joins eligibility decisions to applied writes without losing refusals.
func _episode_operator_attempts(report: Dictionary) -> Array[Dictionary]:
	var attempts: Array[Dictionary] = []
	var writes: Array = report.get(&"writes", [])
	var used_writes := { }
	for value: Variant in report.get(&"decisions", []):
		var decision := (value as Dictionary).duplicate(true)
		var matched_write := { }
		for index in writes.size():
			if used_writes.has(index):
				continue
			var candidate := writes[index] as Dictionary
			if int(candidate.get(&"operator", -1)) \
					== int(decision.get(&"operator", -2)) \
					and int(candidate.get(&"basis", -1)) \
							== int(decision.get(&"basis", -2)):
				matched_write = candidate.duplicate(true)
				used_writes[index] = true
				break
		decision[&"outcome"] = int(matched_write.get(&"outcome", -1))
		decision[&"write"] = matched_write
		attempts.append(decision)
	for index in writes.size():
		if used_writes.has(index):
			continue
		var write := (writes[index] as Dictionary).duplicate(true)
		attempts.append(
			{
				&"operator": int(write.get(&"operator", -1)),
				&"basis": int(write.get(&"basis", -1)),
				&"eligible": true,
				&"applied": true,
				&"eligibility": { },
				&"outcome": int(write.get(&"outcome", -1)),
				&"write": write,
			},
		)
	return attempts


# Keeps episode evidence available when the engine rewires or unregisters.
#
# The handle adopts the engine's record rather than copying it. A copy per
# evidence mutation costs the whole record once per entry added to it, which
# is quadratic in the age of an open episode, and every public read detaches
# anyway.
func _store_episode(report: Dictionary) -> void:
	_episode_snapshot = report
	_episode_revision += 1


## Returns the prediction tape in ascending transition order. The owning
## client returns authored transitions. The consuming server returns decoded
## transitions. A [constant NetwPredict.Schedule.TICK] entity's entries are degenerate:
## index, label, and tick are the same number and every entry is fresh.
func tape_transitions() -> Array[Dictionary]:
	var engine := _engine()
	return engine.tape_transitions() if engine else [] as Array[Dictionary]


## Returns the exact predicted state produced by [param transition], or an
## empty [Dictionary] before its post-solve capture is available.
func transition_state_at(transition: int) -> Dictionary:
	var engine := _engine()
	return engine.transition_state_at(transition) if engine else { }


## Records one server-consumed input at [param tick] into the server timeline, the
## direct-feed counterpart of a received input frame. Consume role only.
func record_server_input(tick: int, input: Dictionary) -> void:
	var engine := _engine()
	if engine:
		engine.record_server_input(tick, input)


## Returns true when the engine has consumed through [param state_tick]. A handle
## with no engine, or one not in a consume role, reports ready.
func has_consumed_state_tick(state_tick: int) -> bool:
	var engine := _engine()
	return engine.has_consumed_state_tick(state_tick) if engine else true


## Opens a [member collision_cooldown_ticks] window during which a recovery
## below [member teleport_threshold] is paused, so a contact transient is not
## corrected through. Call it from the
## controlling client when the predicted body registers a collision. A no-op with
## no engine wired.
func notify_contact() -> void:
	var engine := _engine()
	var iface := engine._iface() if engine else null
	var api := iface._api() if iface else null
	if api and engine._entity:
		api.predict_notify_contact(api.entity_of(engine._entity.owner))


## Returns the [NetwTimeline] state key for the latest authoritative snapshot at
## [param fallback_tick], the input-backed tick on a consuming engine, or
## [code]-1[/code] when this tick consumed no input and so authored no state
## worth keying. A caller recording history skips a negative key, leaving the
## slot the last real consume wrote.
func history_record_tick(fallback_tick: int) -> int:
	var engine := _engine()
	return engine.history_record_tick(fallback_tick) if engine else fallback_tick


## Returns the resolved [enum NetwPredict.CorrectionMode]. [constant NetwPredict.CorrectionMode.AUTO] never
## leaks out: this is the concrete mode the reconcile path uses.
func resolved_correction_mode() -> NetwPredict.CorrectionMode:
	var engine := _engine()
	if engine:
		return engine.resolved_correction_mode()
	var body := entity().owner if entity() else null
	return resolve_correction_mode_for(body, correction_mode)


## Resolves [param mode] against [param body]'s type.
##
## [constant NetwPredict.CorrectionMode.AUTO] picks [constant NetwPredict.CorrectionMode.SNAP] for a
## dynamic body (a [RigidBody2D] or [RigidBody3D], whose solver cannot be stepped
## per input) and [constant NetwPredict.CorrectionMode.REPLAY] otherwise. An explicit mode
## passes through. Static so tooling and tests resolve without a wired entity.
static func resolve_correction_mode_for(
		body: Node,
		mode: int,
		solves: bool = false,
) -> NetwPredict.CorrectionMode:
	if mode != NetwPredict.CorrectionMode.AUTO:
		return mode as NetwPredict.CorrectionMode
	if solves or body is RigidBody2D or body is RigidBody3D:
		return NetwPredict.CorrectionMode.SNAP
	return NetwPredict.CorrectionMode.REPLAY


## Returns the largest per-property error between a predicted and an authoritative
## snapshot, or [code]INF[/code] on a missing key. Reported on the divergence
## signals; the correction decision is [method diverged]. A key in
## [param angles] compares as a wrapped angle, so a heading crossing
## [code]+/- PI[/code] reads
## as its true arc instead of a full turn.
static func divergence(
		predicted: Dictionary,
		authoritative: Dictionary,
		angles: Dictionary = { },
) -> float:
	var worst := 0.0
	for key: StringName in authoritative:
		if not predicted.has(key):
			return INF
		worst = maxf(
			worst,
			_error(predicted[key], authoritative[key], angles.has(key)),
		)
	return worst


## Fills [param out] with each field's own divergence and returns the worst of
## them, the same value [method divergence] reports.
##
## The scalar alone cannot say which field diverged, and a state set mixing
## Meters, radians, and meters per second diverge differently per field. A
## caller
## diagnosing why corrections fire (or do not) reads the breakdown.
## [param out] is cleared and refilled, so one dictionary can be reused across
## receives instead of allocating per comparison.
static func divergence_by_field(
		predicted: Dictionary,
		authoritative: Dictionary,
		angles: Dictionary,
		out: Dictionary,
) -> float:
	out.clear()
	var worst := 0.0
	for key: StringName in authoritative:
		var error := INF
		if predicted.has(key):
			error = _error(predicted[key], authoritative[key], angles.has(key))
		out[key] = error
		worst = maxf(worst, error)
	return worst


# Whether [param error] on one field demands a recovery at [param tolerance].
#
# THE one predicate for that question. Six places used to ask it inline and one
# of them answered differently: [method PredictionCore._PredictionEngine._escalation_field]
# read a declared tolerance of 0.0 as "skip this field" where every other
# reader reads it as "any error triggers", so a field could raise a
# correction and then be absent
# from the ranking that decides which divergence the recovery answers. Two
# functions written together for one purpose, reading one declaration in
# opposite ways.
#
# Strictly greater, so a tolerance is the largest error a field is allowed to
# hold rather than the smallest it is corrected for, and a declared 0.0 means
# any error at all -- which is the only reading that makes an exact field
# expressible.
static func _triggers(error: float, tolerance: float) -> bool:
	return error > tolerance


## Returns true when any property exceeds its own threshold, reading [param epsilon]
## refined by the per-property [param overrides]. Per-property because a 3D body
## mixes units (meters, radians, meters per second) that no epsilon can serve.
##
## A [param overrides] entry of [code]0.0[/code] means any error triggers, which
## is how [method NetwScriptModel.PropertyConfig.epsilon] documents it.
##
## A key in [param excludes] never triggers on its own: it is skipped here so a
## field declared [method NetwScriptModel.PropertyConfig.reconcile_only], or
## one whose class is not [constant NetwPropertySet.PropertyClass.CAUSAL], does
## not force a correction, though a correction some other field triggers
## still restores it. The engine passes
## [member NetwPredict.Wiring.vote_excludes], which carries both.
## A key in [param angles] compares as a wrapped angle
## ([method @GlobalScope.angle_difference]), so a heading crossing +/- PI never
## fakes a near-full-turn divergence.
static func diverged(
		predicted: Dictionary,
		authoritative: Dictionary,
		epsilon: float,
		overrides: Dictionary,
		excludes: Dictionary = { },
		angles: Dictionary = { },
) -> bool:
	for key: StringName in authoritative:
		if excludes.has(key):
			continue
		if not predicted.has(key):
			return true
		var error := _error(predicted[key], authoritative[key], angles.has(key))
		if _triggers(error, float(overrides.get(key, epsilon))):
			return true
	return false


# value_error refined by the field's angle flag: a wrapped-angle float compares
# by its shortest arc, everything else falls through to value_error.
static func _error(a: Variant, b: Variant, is_angle: bool) -> float:
	if is_angle and (a is float or a is int) and (b is float or b is int):
		return absf(angle_difference(float(a), float(b)))
	return value_error(a, b)


## Returns the per-property error between two values. Rotations return radians
## ([Quaternion] / [Basis] angle), so a rotation property wants its own threshold.
static func value_error(a: Variant, b: Variant) -> float:
	if a is Vector2 and b is Vector2:
		return (a - b).length()
	if a is Vector3 and b is Vector3:
		return (a - b).length()
	if (a is float or a is int) and (b is float or b is int):
		return absf(float(a) - float(b))
	if a is Quaternion and b is Quaternion:
		return absf((a as Quaternion).angle_to(b))
	if a is Basis and b is Basis:
		return absf(
			(a as Basis).get_rotation_quaternion().angle_to(
				(b as Basis).get_rotation_quaternion(),
			),
		)
	# A whole transform combines position and rotation error; thresholds live per
	# property, so this heuristic only feeds the correction decision.
	if a is Transform3D and b is Transform3D:
		return (a.origin - b.origin).length() + absf(
			a.basis.get_rotation_quaternion().angle_to(b.basis.get_rotation_quaternion()),
		)
	if a is Transform2D and b is Transform2D:
		return (a.origin - b.origin).length() + absf(
			angle_difference(a.get_rotation(), b.get_rotation()),
		)
	return 0.0 if a == b else INF


func _bind(bound_entity: NetwEntity) -> void:
	_entity_ref = weakref(bound_entity)
	island._bind(bound_entity)


# Adds or removes one island's local simulation claim and refreshes the axes.
func _set_simulated_by(
		subject: NetwEntity,
		enabled: bool,
		predictor: Callable = Callable(),
) -> void:
	if subject == null:
		return
	var subject_id := subject.get_instance_id()
	var changed := false
	if enabled:
		changed = not _simulation_subjects.has(subject_id) \
				or _simulation_subjects[subject_id] != predictor
		_simulation_subjects[subject_id] = predictor
	else:
		changed = _simulation_subjects.erase(subject_id)
		# A member no group promotes owes no group evidence, and leaving the
		# mode behind would keep it recording for a replay nobody will run.
		if _simulation_subjects.is_empty() \
				and reconcile_mode != NetwPredict.Reconcile.INDEPENDENT:
			reconcile_mode = NetwPredict.Reconcile.INDEPENDENT
			var engine := _engine()
			if engine:
				engine._follow_relay_subscription(reconcile_mode)
	if changed:
		var current := _engine()
		if current:
			current.rewire()


# Returns the stable first predictor declared by a promoting subject.
func _predicted_command_callable() -> Callable:
	var ids: Array = _simulation_subjects.keys()
	ids.sort()
	for subject_id: int in ids:
		var predictor := _simulation_subjects[subject_id] as Callable
		if predictor.is_valid():
			return predictor
	return Callable()


func _engine() -> PredictionCore._PredictionEngine:
	return _engine_ref.get_ref() as PredictionCore._PredictionEngine if _engine_ref else null
