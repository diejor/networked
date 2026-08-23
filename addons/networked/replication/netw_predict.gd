## The enum vocabulary of the prediction family, on a leaf nobody has to import
## an engine to read.
##
## These twenty-three names were members of the PredictionHandle inside
## [NetwMultiplayer], so a game, a component or a sibling subsystem
## naming one -- and they are named 407 times across this repository -- pulled an
## 11,488-line file in behind it. The island leaf and the property layer both
## need this vocabulary and neither can afford that dependency, so the vocabulary
## moves out and the engine keeps the behaviour.
##
## Nothing here depends on anything, which is the point: an enum is a fact about
## what a declaration MEANS, and a fact that costs a dependency to state gets
## restated locally instead. That is how one enum ends up with two spellings.
##
## Handle members are named in code spans below rather than linked, because the
## path to an inner class member is longer than a line and a BBCode tag broken
## across one fails silently.
##
## The handle does not re-export these. What used to be spelled on the handle is
## spelled here now, which is a source break taken deliberately, and taken before
## Phase C's collapse rather than beside it.
class_name NetwPredict

## Per-entity role, resolved from authority at spawn and on control transfer.
## The engine leaf owns this enum. A component that exports a role reads it
## from here.
##
## The role is the name for a pair of [code]input_source[/code] and
## [code]sim_mode[/code], derived through [code]role_for_axes[/code] rather than
## chosen on its own, so the name and the two facts behind it cannot
## disagree.
enum Role {
	## A remote client controls the entity. Predicts and reconciles on ack.
	PREDICT,
	## The server consumes a remote peer's received input into authoritative state.
	CONSUME,
	## A listen-server host controls its own entity, simulating authoritatively.
	HOST_LOCAL,
	## A remote display. Never simulates here, the interpolator shows it.
	REMOTE,
	## A replicated remote stepped locally with a predicted command.
	SIMULATE,
}

## Where this peer's copy of the entity gets the command it simulates with.
##
## [constant LOCAL] authors the command here. [constant RECEIVED] reads a
## command another peer authored and sent. [constant PREDICTED] guesses a
## command nobody sent for a simulated island participant.
## [constant NONE] simulates nothing, so it needs no command.
enum InputSource {
	LOCAL,
	RECEIVED,
	PREDICTED,
	NONE,
}

## What this peer's simulation of the entity means.
##
## [constant AUTHORITATIVE] produces the truth other peers reconcile against.
## [constant SPECULATIVE] produces a guess this peer will reconcile.
## [constant DISPLAY] runs no simulation and shows received state.
enum SimMode {
	AUTHORITATIVE,
	SPECULATIVE,
	DISPLAY,
}

## Cadence that applies the entity's simulation drive.
enum Schedule {
	## Apply once for every network tick. This preserves kinematic prediction.
	TICK,
	## Apply once after every physics frame's network tick loop.
	FRAME,
	## Apply once per network tick through a
	## [NetwPhysicsStepper], which can re-run a space step inside one frame.
	STEPPED,
}

## Local collider realizations retained for diagnostics, never compared.
enum ContactClass {
	## The solve reported no collider.
	NONE,
	## The collider is the support reported by the declared ground sensor.
	DECLARED_SUPPORT,
	## Static geometry other than the declared support.
	OTHER_STATIC,
	## A dynamic entity this peer predicts or simulates authoritatively.
	PREDICTED_DYNAMIC,
	## A dynamic entity outside this peer's prediction island.
	UNPREDICTED_DYNAMIC,
	## A kinematic or animated collision proxy.
	KINEMATIC_PROXY,
}

## Peer-invariant realized-witness classes carried by fingerprints and acks.
enum WitnessClass {
	## No contact was realized.
	NONE = 0,
	## The declared support was contacted.
	SUPPORT = 1 << 0,
	## Static geometry other than the declared support was contacted.
	STATIC = 1 << 1,
	## A replicated dynamic entity was contacted.
	DYNAMIC_ENTITY = 1 << 2,
}

## Where one cell of the command matrix came from.
##
## A group that replays its members together replays them from this matrix, so
## a cell's origin is what says whether the replay is reproducing authorship or
## repeating a guess.
enum CommandOrigin {
	## This peer's own prediction for an entity it does not own.
	PREDICTED,
	## The authoring peer's own command, relayed through the server.
	RELAYED,
}

## How the most recent scheduled drive selected its input.
enum DriveKind {
	## No scheduled drive has run.
	NONE,
	## A newly authored input label drove the simulation.
	FRESH,
	## The previous input label drove the simulation again.
	REPEAT,
	## The consume cursor held while repeating its acknowledged input.
	HOLD,
	## The consume cursor had no input stream and repeated its acknowledged input.
	STARVED,
	## A fresh input drove after older eligible labels were folded away.
	FOLD_DRIVE,
	## A missing input label drove through [code]missing_policy[/code].
	MISSING,
	## A command this peer predicted for an entity it does not own drove the
	## simulation, so the owner never authored the input the transition ran.
	SUBSTITUTED,
}

## What the fields past their own epsilon looked like when a recovery was
## judged, carried by every write an episode records.
##
## The distinction is between a recovery that had an operator for part of its
## job and one that had none: a write answering both a withheld field and a
## writable one did repair something, while a write whose every trigger is
## withheld repairs nothing and is charged as non-contraction.
enum TriggerShape {
	## No causal field was past its own epsilon. The exact fingerprint verdict
	## can disagree while every field sits inside its tolerance, so a recovery
	## can be staged with nothing asking for one.
	NONE,
	## At least one field past its epsilon is one a sub-teleport restore may
	## write.
	MIXED,
	## Every field past its epsilon is one no sub-teleport restore may write.
	ALL_WITHHELD,
}

## Which of the three things a FRAME consume pass can do this frame.
enum ConsumeAction {
	## A queued transition is available past the standing buffer, so it runs.
	REPLAY,
	## Transitions are queued but not yet past the buffer, so the pass waits.
	HOLD,
	## Nothing is queued at all, so the pass has no command to run.
	STARVED,
}

## Whether an exact comparison reached a verdict for a transition.
enum ExactVerdict {
	## Nobody has compared fingerprints for this transition yet.
	UNJUDGED,
	## The two independently recorded fingerprints are equal.
	EQUAL,
	## The two independently recorded fingerprints differ.
	UNEQUAL,
}

## What the server does for a missing input tick. Mirrors
## [enum PredictionComponent.MissingInput] by value.
enum MissingInput {
	## No input, no movement. The honest cs-style default.
	STALL,
	## Carry the last input forward over the gap.
	REPEAT_LAST,
}

## How a reconciliation correction is applied. Mirrors
## [enum PredictionComponent.CorrectionMode] by value.
enum CorrectionMode {
	## Resolve from the body type: REPLAY for a kinematic body, SNAP for a dynamic one.
	AUTO,
	## Restore authoritative state, then replay every unacked input over it.
	REPLAY,
	## Restore authoritative state and stop, with no replay.
	SNAP,
}

## How a [constant CorrectionMode.SNAP] restore places the authoritative state
## on the body. Mirrors [enum PredictionComponent.RestoreMode] by value.
enum RestoreMode {
	## Restore the authoritative state verbatim at its own tick.
	EXACT,
	## Project each carry-declaring field forward to the present tick by its
	## replicated velocity before restoring, so a dynamic body lands near where
	## it is instead of snapping back to a stale tick.
	EXTRAPOLATED,
}

## What kind of body the entity predicts, the one decision the schedule and
## recovery presets are derived from. Mirrors
## [enum PredictionComponent.Archetype] by value.
##
## An archetype is a floor, not a cage: [code]archetype[/code] applies
## its bundle, and any value declared in the scene or configured afterward
## overrides the bundled one.
##
## No bundle sets a [enum BreachResponse]. That is the one recovery fact an
## entity must name for itself, because it decides whether a body stops
## predicting, and a preset that chose it would change the most visible
## behaviour an entity has without any game file saying so.
enum Archetype {
	## No preset. Every knob keeps its own default until declared.
	NONE,
	## A body whose step is a plain callable, re-runnable within one frame.
	## Bundles the [constant Schedule.TICK] cadence, the
	## [constant MissingInput.STALL] hold, and
	## [constant RecoveryPolicy.REBASE_REPLAY], because a command-pinned
	## velocity re-synchronizes on the next tick.
	KINEMATIC,
	## A solver-integrated body whose step cannot be re-run per input.
	## Bundles the [constant Schedule.FRAME] cadence, the
	## [constant MissingInput.REPEAT_LAST] hold,
	## [constant RecoveryPolicy.REBASE_RECOVER], the
	## [constant RestoreMode.EXTRAPOLATED] projection, and a teleport
	## threshold sized for a body that settles through contacts.
	SOLVER_BODY,
}

## How a recovery puts a diverged entity back onto the authoritative timeline,
## the named form of what [enum CorrectionMode] expressed as a body-type
## guess.
##
## A policy names the strategy rather than the mechanism, so an entity
## declares what it wants and the engine picks how. [constant REBASE_REPLAY]
## restores the acknowledged state and re-runs the unacknowledged commands
## over it. [constant REBASE_RECOVER] restores without replaying, for a body
## whose step cannot be re-run cheaply.
## [constant DELAY_CLOSED] never speculates, so no divergence can arise.
## [constant OBSERVE] reports a divergence and repairs nothing. It is outside
## the finite recovery guarantee: persistent divergence can retain one open
## episode, but it cannot create a recovery oscillation because it writes no
## operator.
enum RecoveryPolicy {
	REBASE_REPLAY,
	REBASE_RECOVER,
	DELAY_CLOSED,
	OBSERVE,
}

## How a prediction-island member is represented on this peer.
enum Fidelity {
	## Display received state and classify contact against a proxy.
	PROXY,
	## Step the member locally with substituted commands.
	SIMULATED,
}

## The unacknowledged span, in transitions, past which the prediction governor
## stops speculating and holds.
##
## A structural ceiling rather than a tuning knob: a quarter of the engine's
## 256-transition tape history, so the horizon can never outrun the record a
## recovery would have to replay against. Compare it against
## [code]NetwPredictionHandle.ack_age_ticks[/code].
const ACK_AGE_MAX := 64

## Which produced island members are promoted to local simulation.
##
## Explicit promotion through [code]NetwPredictIsland.simulate[/code] is not a
## policy and is unaffected by this: it names one member. A policy answers the
## question for members nobody named, and the two policies differ in what they
## are budgeted by -- a count is a structural budget the game can afford, a
## radius is a claim about where contact can happen.
enum Promotion {
	## Promote nothing automatically. Explicit promotions still apply.
	NONE,
	## Promote the nearest [code]promotion_count[/code] produced members.
	NEAREST,
	## Promote produced members within [code]promotion_meters[/code].
	WITHIN,
	## Promote every produced member. The honest expensive mode of a joint
	## group, where partial promotion is meaningless.
	ALL,
}

## When this island's members open transitions.
enum Pacing {
	## Open speculative transitions immediately. The joint pass (or the
	## per-entity ladder) reconciles them afterward.
	SPECULATE,
	## Open a transition only when every member's command for it is in hand.
	## Nothing speculates, nothing rolls back, and the cost is the declared
	## input delay plus the slowest peer's transport.
	DELAY_CLOSED,
}

## How divergence is reconciled across an island.
enum Reconcile {
	## Reconcile each predicted entity independently.
	INDEPENDENT,
	## Restore and replay the island together. The engine admits the group at
	## membership commit, where every member's schedule is known.
	JOINT,
}

## Four-way provenance selection for a cell in the joint pass.
enum CellProvenance {
	COAST = 0,
	SUBSTITUTED = 1,
	RELAYED = 2,
	AUTHORED = 3,
}

static func joint_floor(
		bases: Dictionary,
		relay_floors: Dictionary,
		epoch_floor: int,
		history_floor: int,
		present: int,
) -> Dictionary:
	var floor_val := present
	for key in bases:
		var b: int = bases[key]
		if b >= 0:
			floor_val = mini(floor_val, b)
	for key in relay_floors:
		var rf: int = relay_floors[key]
		if rf >= 0:
			floor_val = mini(floor_val, rf)
	if epoch_floor >= 0:
		floor_val = mini(floor_val, epoch_floor)
	var heal := false
	if floor_val < history_floor:
		heal = true
		floor_val = present
	return { &"floor": floor_val, &"heal": heal }

static func joint_cell(
		authored: bool,
		relayed: bool,
		predictor_valid: bool,
) -> int:
	if authored:
		return CellProvenance.AUTHORED
	if relayed:
		return CellProvenance.RELAYED
	if predictor_valid:
		return CellProvenance.SUBSTITUTED
	return CellProvenance.COAST


## What prediction does when its realized witness touches a body outside the
## prediction boundary.
##
## Static world geometry is part of the shared world every peer solves
## against, so touching it never breaches. A breach is contact with a
## dynamic body this peer does not step, whose displayed pose is a stale
## stand-in the solve cannot reproduce.
enum BreachResponse {
	## Continue speculation and let ordinary recovery absorb any divergence.
	PREDICT_THROUGH,
	## Follow authority immediately while commands continue to flow.
	DEMOTE,
}

## Lifecycle state of one actionable divergence episode.
enum EpisodeState {
	## The episode is accepting comparisons and operator evidence.
	OPEN,
	## A verified agreement run retired the episode.
	CLOSED,
	## Bounded recovery evidence was exhausted.
	FALLBACK,
}

## Measured outcome of one recovery operator application.
enum OperatorOutcome {
	## No later aligned comparison has judged the write yet.
	PENDING,
	## A later comparison strictly reduced or closed the aligned error.
	CONTRACTED,
	## The verification window held or grew the aligned error.
	FAILED_TO_CONTRACT,
	## The write introduced a realized boundary absent on authority.
	INTRODUCED_BOUNDARY,
	## Every field that demanded this recovery was one the recovery was
	## declared never to write, so the error it held is the declaration's
	## and not the operator's.
	##
	## Recorded beside [constant FAILED_TO_CONTRACT] rather than as a kind of
	## it, because the two look identical from the verification window and
	## mean opposite things: one is an operator that tried and did not shrink
	## the error, the other is an operator that was forbidden to touch it.
	## Spending episode evidence on the second converts a declaration gap
	## into a breach, and a breach into a demotion, which is worse than what
	## the game would have got by declaring nothing at all.
	WITHHELD,
}

## Why a comparison's verdict did not reach the body.
##
## [code]state_evaluated[/code] reports the verdict a comparison reached, and a
## true verdict is not the same as a write: several paths judge a divergence
## and then answer it with nothing, and from the signal alone every one of
## them is indistinguishable from agreement. This names which one ran.
enum VerdictReason {
	## Nothing stood between the verdict and the body: either the comparison
	## found agreement, or the ladder ran and the operator it selected
	## decided the write.
	NONE,
	## No comparison ran. A masked set is reconciled only once the stream has
	## seen its gain-edge full row, and before that edge a receive is
	## recorded having judged nothing.
	AWAITING_RECONSTRUCTION,
	## The reseed's evidence-free horizon alignment is still waiting for its
	## command epoch, so this receive is neither compared nor written.
	REALIGN_PENDING,
	## The acknowledgement is at or before the reseed horizon, so the row
	## answers for transitions the seed has already replaced.
	RESEED_IGNORED,
	## The resumed body's first judged comparison disagreed, so probation
	## ended in a re-quarantine. It writes nothing, spends no evidence, and
	## does not price the next hold as a flap: a resume that was never proven
	## clean says nothing about the body that a longer wait would settle.
	PROBATION_REQUARANTINE,
	## The episode's bounded recovery evidence ran out on this comparison, so
	## speculation closed instead of correcting.
	EVIDENCE_EXHAUSTED,
	## A transport is already staged against this divergence and owns the
	## write that answers it.
	TRANSPORT_PENDING,
	## A dissipation is already staged against this divergence.
	DISSIPATE_PENDING,
	## DISSIPATE answered the comparison, which is a decision to write
	## nothing rather than a failure to write.
	DISSIPATED,
	## The operator is deferred until an authority witness covers the
	## boundary it would rebase across.
	WITNESS_DEFERRED,
	## The ladder ran and the recovery it selected declined to write.
	DECLINED,
}


## The feed control one role needs on one binding, applied as a unit.
##
## These fields are the shell's wiring rather than the engine's state: the sync
## pipeline reads them when it builds a frame, and more than one subsystem
## depends on what they say. A role used to set them by poking five fields in
## five places, which made "what does PREDICT install?" a question you answered
## by reading the whole rewire. A role now names a plan and the binding applies
## it, so the answer is one record.
##
## The defaults are the cleared state, so building a plan and applying it is
## also how a rewire un-installs the previous role's feed.
## [codeblock]
## var feed := NetwPredict.Feed.new()
## feed.state_write_gate = false          # reconcile, never snap the body
## feed.state_on_applied = _on_state_frame
## feed.input_volatile_external = true    # the command lane carries it
## [/codeblock]
class Feed:
	extends RefCounted

	## Fires with each decoded state row, or unset to leave the set to display.
	var state_on_applied: Callable = Callable()

	## When false a received state row decodes without snapping the node, so a
	## predicting body reconciles instead of being overwritten.
	var state_write_gate: bool = true

	## Fires with each decoded input row, opening the consume cursor.
	var input_on_applied: Callable = Callable()

	## When false a received input row decodes without snapping the node.
	var input_write_gate: bool = true

	## When true the command lane carries this set's volatile bytes and the pump
	## sends none of its own.
	var input_volatile_external: bool = false


## One drive pass's timing, handed to the kernel by the shell that owns the clock.
##
## The shell is the only clock reader in the design, so an engine never resolves
## a tick, a step or a quantum for itself. It is given one of these and reads it.
## The record lives on this leaf rather than on the shell so the kernel can name
## the type without depending on the file that steps it.
class Timing:
	extends RefCounted

	## The transition label this pass authors under. A tick pass carries the tick
	## the clock reported; a frame pass carries the tick whose solve it completes.
	var tick: int

	## The step handed to
	## [member NetwPredictionHandle.simulate] for this
	## one drive.
	var delta: float

	## The fixed duration of one network tick, the step every replayed transition
	## re-runs at and the unit
	## [member NetwPredictionHandle.ack_age_ticks]
	## measures. Zero means the pass could not resolve one, so a reader keeps
	## whatever step it already had rather than adopting a meaningless one.
	var ticktime: float

	## The physics frame this pass runs on, counted by the interface. The
	## difference between the frames two consecutive drives ran on is how much
	## simulated time the transition between them actually bought.
	var frame: int

	## Physics steps one transition is declared to advance, from the clock's
	## [member NetwClockHandle.physics_factor]. A pass that measures a
	## different number advanced its solver by an amount no transition accounts
	## for, which no compared column can describe and no recovery can repair.
	var quantum: int

	## Whether the world advances on this frame. A pass that answers
	## [code]false[/code] opens no transition, because a transition that bought
	## no simulated time is one the peers cannot compare.
	var simulating: bool


	func _init(
			p_tick: int = 0,
			p_delta: float = 0.0,
			p_ticktime: float = 0.0,
			p_frame: int = 0,
			p_quantum: int = 1,
			p_simulating: bool = true,
	) -> void:
		tick = p_tick
		delta = p_delta
		ticktime = p_ticktime
		frame = p_frame
		quantum = p_quantum
		simulating = p_simulating


## Everything a game's declarations reached, as one record the kernels read.
##
## These eleven tables and three scalars used to cross into
## [code]recover()[/code], [code]evaluate()[/code] and
## [code]guard_projection()[/code] as loose arguments -- twenty of them on
## [code]recover()[/code] alone -- which made every kernel signature a list only
## the caller could get right, and made the reshape that this record IS a
## twenty-way edit at every call site.
##
## The tables are what [code]_build_restore_projection[/code] resolves from the
## declaration at rewire, so they change when the wiring changes and not
## otherwise. The scalars are entity-wide defaults a game may move at any time,
## so they are refreshed from the handle at the head of every comparison: a
## record that cached them behind a dirty flag would go stale for any writer that
## did not route through a setter, and the handle's scalars are plain properties
## with nineteen live-mutation sites in this repository.
class Wiring:
	extends RefCounted

	## field -> the channel a restore projects it along, from carry_along().
	var projection: Dictionary[StringName, StringName] = { }

	## Fields enrolled in the teleport-tier measurement.
	var pose_fields: Dictionary[StringName, bool] = { }

	## field -> the rate converge() declared for it.
	var converge_rules: Dictionary[StringName, float] = { }

	## Fields a sub-teleport recovery may not write, from teleport_only().
	var withheld: Dictionary[StringName, bool] = { }

	## Fields reconcile_only() marked, which is the narrower of the two exclusions
	## and NOT the one the correction vote reads: see [member vote_excludes]. The
	## meter's tolerance rows and dissipate eligibility read this one, because
	## they ask which fields a recovery must answer for rather than which may
	## demand one.
	var trigger_excludes: Dictionary[StringName, bool] = { }

	## Fields the correction vote does not read: reconcile_only() marked, plus
	## every field whose class is not CAUSAL.
	##
	## A separate set rather than a wider [member trigger_excludes] because the
	## two answer different questions and share only this one. A derived() value
	## is recomputed from causal fields every step, so a disagreement in it is a
	## restatement of one already counted, and letting it vote charges a recovery
	## to a field the next transition overwrites anyway.
	var vote_excludes: Dictionary[StringName, bool] = { }

	## field -> its own tolerance, from epsilon().
	var epsilon_overrides: Dictionary[StringName, float] = { }

	## field -> its own tier distance, from teleport_at().
	var teleport_thresholds: Dictionary[StringName, float] = { }

	## Fields compared as angles rather than as numbers.
	var angle_fields: Dictionary[StringName, bool] = { }

	## Fields the next step reads, so a divergence in one is a real fork.
	var causal_fields: Dictionary[StringName, bool] = { }

	## field -> [enum NetwPredictJournal.StateFamily], as an int.
	var state_family_of: Dictionary[StringName, int] = { }

	## field -> the rule carry_step() declared, bound to one body.
	var carry_rules: Dictionary[StringName, Callable] = { }

	## The entity-wide tolerance a field with no epsilon() of its own inherits.
	var epsilon: float = 0.0

	## The entity-wide tier distance a field with no teleport_at() inherits.
	var teleport_threshold: float = 0.0

	## Ceiling in ticks on the span a projected restore may extrapolate across.
	var max_restore_ticks: int = 0


## What one comparison concluded about itself, before a recovery is staged.
##
## Six facts the recovery reads and none of them a table: which domain the
## transition was judged in, what the divergence was attributed to, whether a
## contact window was still open, whether corrections are suppressed, whether the
## pose could be measured at all, and how far behind the acknowledgement the
## owner has driven.
##
## Pooled and refilled per comparison rather than allocated: this runs on every
## authoritative frame of every predicted entity, where a fresh record would be
## an allocation to deliver six scalars.
class Verdict:
	extends RefCounted

	## [enum NetwPredictJournal.Domain], as an int so the record stays on this leaf.
	var domain: int = 0

	## [enum NetwPredictJournal.Attribution], as an int, for the same reason.
	var attribution: int = 0

	## Whether a contact was still disturbing both bodies at this transition.
	var contact_window: bool = false

	## Whether a settling transient is currently holding corrections back.
	var suppressed: bool = false

	## Whether the entity had no measurable pose, so it may not claim to sit
	## below the tier.
	var pose_unmeasured: bool = false

	## How far past the acknowledged transition the owner has driven.
	var ack_age_ticks: int = 0


	## Refills every field, so a pooled record can never carry one comparison's
	## fact into the next.
	func fill(
			p_domain: int,
			p_attribution: int,
			p_contact_window: bool,
			p_suppressed: bool,
			p_pose_unmeasured: bool,
			p_ack_age_ticks: int,
	) -> Verdict:
		domain = p_domain
		attribution = p_attribution
		contact_window = p_contact_window
		suppressed = p_suppressed
		pose_unmeasured = p_pose_unmeasured
		ack_age_ticks = p_ack_age_ticks
		return self
