## Everything a prediction engine counted, on one object with one name per fact.
##
## These thirty-nine facts were public members of the handle, and [code]stats()[/code]
## re-exported twenty-four of them under DIFFERENT names -- [code]consumed_count[/code]
## read back as [code]consumed[/code], [code]fp_mismatch_count[/code] as
## [code]fp_mismatches[/code], [code]arrival_histogram[/code] as [code]arrivals[/code].
## Two hand-maintained spellings per fact, in two places, with nothing keeping
## them in step: a counter added to one and forgotten in the other is invisible
## until somebody reads the wrong name and gets null.
##
## One name now. The shorter spelling won, because it is the one a reader
## already saw.
##
## [br][br]This is the demoted half of the evidence surface. What a game is
## meant to react to -- the six signals, the verdict reason, the per-field
## divergence and recovery rows, [code]reachability()[/code] -- stays on the
## handle and is contract. What is here is diagnostic: counters, histograms,
## tape bookkeeping, fingerprint tallies. Read it, print it, chart it; do not
## build a rule on it, because the freeze table does not protect it.
## [codeblock]
## var stats := NetwEntity.of(self).prediction.stats
## print("%d corrections, %d skipped" % [stats.corrections, stats.skipped])
## [/codeblock]
class_name NetwPredictStats
extends RefCounted

## Every fact on this object, by name, for an instrument that serializes rather
## than reads one thing.
##
## Reflected over the fields rather than listed, which is the whole point: the
## hand-written dictionary this replaces re-exported twenty-four facts under a
## second set of names, and a counter added to the class and forgotten in the
## dictionary was invisible. A field that exists is in here by existing.
## Buckets [member arrivals] holds, so its size is a declared fact rather than
## a shape a writer grew.
const ARRIVAL_BUCKETS := 9

## Buckets [member replay_depth] holds.
const REPLAY_DEPTH_BUCKETS := 33


# The pool's drive columns, supplied by the engine that owns the slot. The
# engine keeps these counters as it drives, so a second copy maintained here
# would be a second ledger that agrees only while both are remembered.
var _drive_columns: Callable = Callable()


# One pool column, or the value this object carries with no engine behind it.
func _column(column: NetwPredictionEngine.DriveStat, fallback: int) -> int:
	if not _drive_columns.is_valid():
		return fallback
	var columns: PackedInt64Array = _drive_columns.call()
	if column >= columns.size():
		return fallback
	return int(columns[column])


func _init() -> void:
	arrivals.resize(ARRIVAL_BUCKETS)
	replay_depth.resize(REPLAY_DEPTH_BUCKETS)


func to_dictionary() -> Dictionary:
	var out: Dictionary = { }
	for entry: Dictionary in get_script().get_script_property_list():
		if not (int(entry[&"usage"]) & PROPERTY_USAGE_SCRIPT_VARIABLE):
			continue
		var name: StringName = entry[&"name"]
		var value: Variant = get(name)
		out[name] = value.duplicate() if value is Dictionary else value
	return out

## Physics-frame callbacks folded into their tick's existing command record.
## A growing value means this peer produces frame callbacks faster than the
## shared network clock advances, but it cannot grow the command queue.
var authoring_clamped: int = 0:
	get:
		return _column(
			NetwPredictionEngine.STAT_AUTHORING_CLAMPED,
			0,
		)

## Prediction passes held because their unacknowledged horizon reached the
## structural ceiling. Input capture continues while the simulated body holds.
var speculation_held: int = 0:
	get:
		return _column(
			NetwPredictionEngine.STAT_SPECULATION_HELD,
			0,
		)

## Physics steps the world advanced across the newest transition.
##
## A transition is a fixed quantum of simulated time, and a solver body is
## integrated by the physics server on its own cadence rather than by the
## drive, so two peers only compare a transition meaningfully when they spend
## the same number of steps on it. This is that number, measured rather than
## assumed, and [member quantum_declared] is what it is supposed to be.
var quantum_steps: int = 0

## Physics steps one transition is declared to advance, from the clock's
## [member ClockCore.physics_factor].
var quantum_declared: int = 1

## Transitions whose measured [member quantum_steps] differed from
## [member quantum_declared].
##
## Any nonzero value means this peer advanced its solver by an amount no
## transition accounts for, so its predictions carry an error the compared
## columns cannot describe and no recovery can repair. It is measurable on one
## peer alone, before any comparison disagrees.
var quantum_faults: int = 0

## Total corrections applied since spawn.
var corrections: int = 0

## Deepest replay window walked by any correction.
var max_replay_depth: int = 0

## Inputs the server consumed into authoritative state.
var consumed: int = 0

## Input ticks the server stepped over as lost.
var missing: int = 0

## Server ticks that consumed nothing because the queue was empty, the input
## stream running dry under the consume cursor. A healthy link holds this at
## zero once [member consume_buffer_ticks] is standing, so a rising count is
## the de-jitter buffer being too shallow for the peers' clock phase drift.
var starved: int = 0

## Consume passes that declined a transition they were holding, because the
## queue had not risen above the tier's standing buffer.
##
## A hold is a frame the world solved without running a transition, so it
## costs one [member quantum_faults] on the next drive and nothing on the
## wire tells the owner it happened. Unlike [member starved] there was a
## transition available, which is why the FRAME tier's
## [member replay_buffer_depth] defaults to zero and never produces one.
var held: int = 0

## Older eligible inputs skipped by a FRAME-scheduled backlog fold. The newest
## eligible input drives once, so this count never represents extra simulation
## applications.
var folded: int = 0

## Monotone count of scheduled drive applications. This fingerprints their
## ordering without changing simulation behavior.
var drive_seq: int = 0:
	get:
		return _column(
			NetwPredictionEngine.STAT_DRIVE_SEQ,
			0,
		)

## Input label applied by the most recent scheduled drive.
var last_drive_label: int = -1:
	get:
		return _column(
			NetwPredictionEngine.STAT_LAST_DRIVE_LABEL,
			-1,
		)

## Selection kind of the most recent scheduled drive, a [enum NetwPredict.DriveKind] value.
var last_drive_kind: int = NetwPredict.DriveKind.NONE:
	get:
		return _column(
			NetwPredictionEngine.STAT_LAST_DRIVE_KIND,
			NetwPredict.DriveKind.NONE,
		)

## Epoch of the active prediction tape, or [code]-1[/code] before one is
## authored or received. Rewiring starts a new client-authored epoch.
var tape_epoch: int = -1

## Index of the newest prediction tape transition authored or received. A
## [constant NetwPredict.Schedule.TICK] entity's transitions are its ticks, so this
## tracks the newest driven tick there.
var tape_index: int = -1

## Contiguous owner-lane transitions currently queued on the consuming
## server at or beyond its replay cursor.
var tape_queue_depth: int = 0

## Times the consume cursor gave up walking and re-opened at the live edge,
## bounded by [member max_consume_lag_ticks]. A healthy session resyncs once at
## most, when the controller's clock first anchors. A rising count means input
## is arriving too far ahead of the cursor to consume in order.
var resync: int = 0

## Input ticks a [member resync] jump passed over without simulating.
## They were already stale by more than [member max_consume_lag_ticks], so the
## authoritative body skips them rather than replaying second-old intent.
var skipped: int = 0

## Owner-lane frames authority dropped whole because they were malformed or
## carried a fresh transition without its command. A frame is never parsed
## into a shorter one, so this counts frames rather than transitions.
var frames_dropped_invalid: int = 0

## Owner-lane frames this peer handed to the transport, counted where the
## bytes were produced rather than where they left.
##
## The owner writes one on every frame it drives and on every frame it holds,
## because a held frame still owes its commands. Read against the consuming
## peer's [member command_frames_received], the pair says how much of the lane
## survives the trip, which is the one lane fact neither peer can state alone.
## [codeblock]
## sent == received      the lane delivers what it is given
## sent >  received      frames are lost or coalesced under the engine, and
##                       the redundancy window is healing it silently
## [/codeblock]
var command_frames_sent: int = 0

## Owner-lane frames authority decoded, whether or not they carried anything
## new. Counts frames rather than transitions, so it pairs directly with the
## owner's [member command_frames_sent].
var command_frames_received: int = 0

## Transitions authority holds with their commands, decoded off the owner
## lane and waiting to be replayed.
var command_queue_depth: int = 0

## Relayed transitions this peer filed into the command matrix.
##
## Counts cells rather than frames, so the redundancy window's re-sends do not
## inflate it and the value is how much of another peer's authorship this one
## actually learned.
var relayed_recorded: int = 0

## Relayed transitions dropped for naming a transition older than the matrix
## retains.
##
## A steady climb here means the relay is arriving later than the window is
## wide, so the subscriber is reconstructing a past its floor has already
## moved past.
var relayed_dropped_late: int = 0

## The highest transition the owner has received an acknowledgement for, or
## [code]-1[/code] before any. This is the authority lane's confirmation
## frontier, and it is what floors both lanes' redundancy windows, so a value
## stuck at [code]-1[/code] means the lane is not arriving rather than that
## it has nothing to say.
var ack_confirmed: int = -1

## The transition authority's acknowledgement lane is currently able to ship,
## [code]-1[/code] on an owner. It is the lesser of the consumed transition
## and the journal's closed prefix, so a value trailing the consumed one
## names the closed prefix as what is holding the lane back.
var ack_frontier: int = -1

## The newest transition whose journal row is closed with every earlier
## retained row also closed, [code]-1[/code] when none is. One row left open
## caps this for as long as the ring retains it, which caps
## [member ack_frontier] with it.
var journal_closed: int = -1

## Authoritative frames this owner reached a divergence verdict on.
##
## Read it beside [member comparisons_skipped]: a divergence of zero means
## the peers agreed only when this is the counter that moved. A predicted
## entity whose skipped count climbs while this one stands still is
## speculating with no reconciliation at all, however healthy its
## divergence reads.
var comparisons_ran: int = 0

## Authoritative frames this owner acknowledged without reaching a verdict,
## because the masked stream had not reconstructed a whole row to compare
## against. Every one of them reports a zero divergence it did not measure.
var comparisons_skipped: int = 0

## Transitions authority ran with a command the owner never authored, as
## declared on the acknowledgement lane. A silent substitution is the fault
## this counter exists to make impossible.
var substituted: int = 0

## New transitions the owner lane delivered to authority, bucketed by how
## many arrived in one frame. [constant NetwPredict.Schedule.FRAME] consume only.
##
## Arrival shape, not arrival total, sets consume health: a lane delivering
## one transition every frame and a lane delivering six every sixth frame
## have the same total and nothing else in common. Authority replays at most
## one per frame, so the second lane leaves five frames dry for every burst
## and no buffer setting changes that. Read it beside
## [member replay_depth] to tell a lane defect from a rate one.
##
## Indexed by arrival count, saturating at [constant ARRIVAL_BUCKETS] minus one.
var arrivals := PackedInt32Array()

## Standing queued-transition depth at each authority frame's consume
## boundary, bucketed by depth. [constant NetwPredict.Schedule.FRAME] consume only.
##
## Depth is the owner's lead over authority measured in transitions, so this
## reads the clock relationship rather than a queue: a distribution parked
## near zero says the two peers run at the same rate, and one that ratchets
## upward says the owner is authoring faster than authority is solving.
## Neither is answered by [member replay_buffer_depth], which shifts this
## whole distribution and changes its shape not at all.
##
## Indexed by depth, saturating at [constant REPLAY_DEPTH_BUCKETS] minus one.
var replay_depth := PackedInt32Array()

## Authority frames bucketed by [code]"consumed,quantum_steps"[/code]: how
## many transitions the frame ran against how much simulated time the last
## of them measured. [constant NetwPredict.Schedule.FRAME] consume only.
##
## [code]"1,1"[/code] is the healthy shape, one transition per solve. Every
## other key names a frame that spent simulated time no single transition
## accounts for, which is the divergence [member quantum_faults]
## totals. This histogram is that total broken down by how each frame
## misspent.
var consume_shape: Dictionary[String, int] = { }

## Acknowledged transitions the owner reached a fingerprint verdict on, the
## denominator [member fp_mismatches] is unreadable without.
##
## Once the acknowledgement lane is live every acknowledged transition counts
## here, because authority sends the fingerprint of the state its own replay
## produced. While the lane is dark the owner must reassemble the state from
## authority frames instead, so a row merged from several frames describes no
## single moment and is left unverified rather than charged as a divergence.
var fp_verified: int = 0

## Acknowledged transitions whose prediction fingerprinted unequal to the
## authority state, counted out of [member fp_verified]. On a run where
## every antecedent of the recurrence matched this stays zero, so a nonzero
## count names a broken antecedent rather than a tolerance that wants widening.
##
## The verdict is the correction trigger for a transition labeled
## [constant NetwPredictJournal.Domain.IN_DOMAIN], and a report for one that
## is not. Which label a transition earns is decided by
## [member island], so an entity that declares no island keeps the
## tolerance compare it always had.
## [br][br]It is the trigger only where it arrives in time to be one. The
## verdict rides the acknowledgement lane while the comparison runs when the
## authoritative state arrives, so a transition compared before its
## acknowledgement is judged by tolerance and this counter reports a
## divergence nothing acted on. A set that ships whole rows can be judged on
## arrival. A [method NetwScriptModel.PropertyConfig.masked] set cannot,
## because a masked frame carries only the fields that changed and the
## authority row it belongs to cannot be fingerprinted from it. Read this
## counter as what the peers disagreed about, never as what was corrected.
var fp_mismatches: int = 0

## The first transition [member fp_mismatches] counted, or [code]-1[/code]
## while none has. This is the transition to inspect, since every later
## mismatch may be the propagation of this one.
var first_divergent_transition: int = -1

## Rows whose pre-state did not chain from the preceding post-state and carried
## no operator provenance. Any nonzero value names an out-of-transition write.
var chain_breaks: int = 0:
	get:
		return _column(
			NetwPredictionEngine.STAT_CHAIN_BREAKS,
			0,
		)

## Transitions authority reached a verdict on for the owner's claimed
## post-state, the denominator [member client_mismatches] is unreadable
## without.
##
## The authority-side mirror of [member fp_verified]. Authority judges a
## claim only once its own row for that transition has closed, so a claim that
## arrives before authority has run the transition waits rather than counting.
var client_fp_verified: int = 0

## Transitions where the owner's claimed post-state disagreed with the one
## authority reached, counted out of [member client_fp_verified] and
## reported through
## [signal LagCompCore.peer_divergence].
##
## This counts what authority found, never what it did about it. Authority
## pushes nothing back on the strength of a mismatch, so a run of these is
## evidence for a game to weigh rather than a correction already applied.
var client_mismatches: int = 0

## The island roster this engine last committed, by entity id, sorted.
##
## Sorted because the roster is an antecedent of the fingerprint both peers
## compute, and two peers that agree on the members but not on their order
## would disagree on the digest.
var island_members: PackedStringArray = PackedStringArray()

## The subset of [member island_members] this peer steps locally rather than
## displaying as a received proxy.
var simulated_members: PackedStringArray = PackedStringArray()

## Joint passes this group has run, one per tick whose floor moved.
##
## A group coalesces every basis it received into one pass, so this counts
## ticks that replayed rather than authoritative rows that arrived.
var joint_passes: int = 0

## Transitions replayed per joint pass, as depth -> passes.
var joint_depth: Dictionary[int, int] = { }

## The transition the last joint pass restored its members to, or -1 before a
## pass has run.
##
## Published with [member joint_present] because the two bound the replay a
## consumer is watching, and a consumer that had to infer them would be reading
## the loop's own state to do it.
var joint_floor: int = -1

## The transition the last joint pass replayed forward to, or -1 before a pass
## has run.
var joint_present: int = -1

## Members the last joint pass stepped, lingering ones included.
var joint_members: int = 0

## Cells the joint pass drove from a relayed command.
var cells_relayed: int = 0

## Cells the joint pass drove from a local predictor standing in for an
## author whose command never arrived.
var cells_substituted: int = 0

## What moved the floor, as source -> moves.
##
## [br]- [code]ack[/code]: an acknowledgement lane row
## [br]- [code]state[/code]: an authoritative state row
## [br]- [code]relay[/code]: a relayed command older than the present
## [br]- [code]epoch[/code]: an epoch bump nothing replays across
var floor_moves_by_source: Dictionary[StringName, int] = { }

## Passes that found a basis older than history and snapped to the newest
## recorded state instead of replaying.
var heal_snaps: int = 0

## Members held past their tenure because the group's replay horizon has not
## passed their departure yet.
var linger_held: int = 0
