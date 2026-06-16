## Per-entity authoritative [NetwTimeline] registry, the server-side rewind substrate.
##
## Each registry entry has exactly one writer (the server), so a timeline is keyed
## by the [RefCounted] [NetwEntity] rather than a node path and outlives nothing it
## does not own.
##
## [codeblock]
## var tl := registry.register(entity)   # idempotent; also publishes entity.timeline
## var past := registry.of(entity)       # null when unregistered
## registry.unregister(entity)           # drops the entry
## [/codeblock]
##
## Owned by [LagCompensationService]. The [HistoryRecorder] iterates [method all]
## to snapshot every registered entity each tick, and [RewindQueries] reads
## [method of] to answer history queries.
class_name TimelineRegistry
extends RefCounted

# Keyed by the RefCounted NetwEntity so there is no node-path coupling.
var _timelines: Dictionary[NetwEntity, NetwTimeline] = { }


## Registers [param entity], returning its [NetwTimeline]. Idempotent: a repeat
## call returns the existing timeline. The created timeline is published to
## [member NetwEntity.timeline].
func register(entity: NetwEntity) -> NetwTimeline:
	if not entity:
		return null
	var existing := _timelines.get(entity) as NetwTimeline
	if existing:
		return existing
	var tl := NetwTimeline.new()
	_timelines[entity] = tl
	entity.timeline = tl
	return tl


## Returns the registered [NetwTimeline] for [param entity], or [code]null[/code].
func of(entity: NetwEntity) -> NetwTimeline:
	return _timelines.get(entity) as NetwTimeline


## Drops [param entity]'s timeline from the registry.
func unregister(entity: NetwEntity) -> void:
	_timelines.erase(entity)


## Returns the live [NetwEntity] to [NetwTimeline] map for iteration.
func all() -> Dictionary[NetwEntity, NetwTimeline]:
	return _timelines
