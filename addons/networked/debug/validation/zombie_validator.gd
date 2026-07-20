## Validator that detects player nodes left behind by a disconnected peer.
##
## When a peer disconnects, its owned nodes should be despawned. This schedules a
## delayed scan (giving despawn time to run) of every active scene for nodes
## still owned by the departed peer, and emits a [NetwZombieManifest] finding for
## any survivors.
class_name ZombieValidator
extends NetwValidator

# Grace period before scanning, so an in-flight despawn is not flagged.
const _SCAN_DELAY: float = 2.0


func interests() -> Array:
	return [NetwTreeEvent.Kind.PEER_DISCONNECTED]


func inspect(event: NetwTreeEvent, report: NetwReport) -> void:
	var mt := event.tree
	var probe := report.probe()
	if not is_instance_valid(mt) or probe == null:
		return

	var peer_id := event.peer_id
	probe.after(_SCAN_DELAY, func() -> void: _scan(peer_id, mt, probe))


# Delayed scan: reports through the probe because the originating NetwReport is
# gone by the time this runs.
func _scan(peer_id: int, mt: MultiplayerTree, probe: TreeProbe) -> void:
	if not is_instance_valid(mt):
		return

	var scenes := mt.api.scenes if mt.api else null
	if not scenes:
		return

	var zombies: Array[String] = []
	for scene_name: StringName in scenes.scenes:
		var scene: MultiplayerScene = scenes.scenes[scene_name]
		if not is_instance_valid(scene) or not is_instance_valid(scene.level):
			continue

		for node: Node in scene.level.find_children("*", "Node", true, false):
			if is_instance_valid(node) and \
					node.get_multiplayer_authority() == peer_id:
				zombies.append(str(node.get_path()))

	if zombies.is_empty():
		return

	var m := NetwZombieManifest.new()
	m.trigger = "ZOMBIE_PLAYER_DETECTED"
	m.errors = zombies
	m.network_state["disconnected_peer_id"] = peer_id
	probe.emit_finding(NetwFinding.new(m, "ZOMBIE_PLAYER_DETECTED"))
