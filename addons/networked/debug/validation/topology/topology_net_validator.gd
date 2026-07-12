## Validator that checks a spawned player's synchronizer topology.
##
## Reacts to [constant NetwTreeEvent.Kind.PLAYER_SPAWNED] on server authority and
## delegates the structural checks to [TopologyValidator] (expected sync count,
## cache/live consistency, identity, and authority assignments). A non-empty
## error set fails the active span and emits a [NetwTopologyManifest] through
## the finding pipeline.
class_name TopologyNetValidator
extends NetwValidator

var _topology := TopologyValidator.new()


func interests() -> Array:
	return [NetwTreeEvent.Kind.PLAYER_SPAWNED]


func inspect(event: NetwTreeEvent, report: NetwReport) -> void:
	var mt := event.tree
	if not is_instance_valid(mt) or not mt.is_host:
		return

	var player := event.node
	if not is_instance_valid(player):
		return

	var errors: Array[String] = []
	errors.assign(_topology.validate_node(player).errors)
	if errors.is_empty():
		return

	var m := NetwTopologyManifest.new()
	m.trigger = "TOPOLOGY_VALIDATION_FAILED"
	m.errors = errors
	m.player_name = player.name
	m.in_tree = player.is_inside_tree()

	var probe := report.probe()
	if probe:
		m.node_snapshot = probe.build_crash_snapshot(report.span())
	else:
		m.node_snapshot = NetwNodeSnapshot.from_node(player)

	report.fail(m, "topology_invalid", { "errors": errors })
