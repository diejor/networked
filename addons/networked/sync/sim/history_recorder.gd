## Records every registered entity's authoritative state snapshot after a tick.
##
## The server holds the truth, so the recorder runs only on server authority and
## reads each entity's [method StampedSynchronizer.snapshot_payload] through its
## [member NetwEntity.state]. This gives non-predicted state-synced entities rewind
## history too, without a [PredictionComponent].
##
## [codeblock]
## # LagCompensationService._on_tick, server only, after simulation:
## recorder.record(registry, tick)
## [/codeblock]
##
## Owned by [LagCompensationService], which guards server authority before calling.
class_name HistoryRecorder
extends RefCounted

## Records the current [method StampedSynchronizer.snapshot_payload] of every
## entity in [param registry] into its timeline at [param tick].
func record(registry: TimelineRegistry, tick: int) -> void:
	var timelines := registry.all()
	for entity in timelines:
		if not is_instance_valid(entity.owner):
			continue
		# A deactivated entity (a lingering despawn) freezes its history at the
		# despawn boundary instead of recording stale frozen copies, so its retained
		# window ages from the moment it died and expires cleanly when it frees.
		if not entity.owner.can_process():
			continue
		var state := entity.state
		if state:
			timelines[entity].record_state(tick, state.snapshot_payload())
