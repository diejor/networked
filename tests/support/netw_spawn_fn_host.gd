## Spawn-function host for the [method Netw.spawn] round-trip suite.
##
## Mounted at the same [MultiplayerTree]-relative path on every peer, the way
## a real host object (a weapon, a manager) exists everywhere while the nodes
## it spawns do not. The function hand-builds its node, so the fn recipe is
## exercised with no [member Node.scene_file_path] at all.
class_name NetwSpawnFnHost
extends Node


func _init() -> void:
	Netw.configure_spawn(_spawn_probe)


func _spawn_probe(marker: String, tier: int) -> Node:
	var probe := NetwSpawnProbe.new()
	probe.name = "FnProbe"
	probe.marker = marker
	probe.fn_tier = tier
	return probe
