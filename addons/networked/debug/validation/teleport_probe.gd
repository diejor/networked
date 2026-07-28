## Tier-probe that reconstructs a teleport's causal span from [TPComponent]'s
## domain signals instead of span code living in the component.
##
## Reacts to [constant NetwTreeEvent.Kind.PLAYER_SPAWNED] to find and hook a
## newly spawned player's [code]%TPComponent[/code]. The client-side span is
## kept on this instance (one field, since a [TPComponent] only ever runs one
## local teleport at a time), so it survives the client's delete+respawn cycle
## across a cross-scene teleport. The server-side span is keyed by the
## component's instance id, which is stable for the lifetime of one request.
## [codeblock]
## TPComponent.teleport_initiated  --> open client span, tag corr.id
## TPComponent.stalled             --> step_warn on the tracked span
## TPComponent.teleport_committed  --> step "committed"
## TPComponent.teleport_finished   --> end the client span
## TPComponent.teleport_failed     --> fail the tracked span
##
## TPComponent.teleport_request_received  --> open server span, tag corr.id
## TPComponent.teleport_request_completed --> end the server span
## [/codeblock]
class_name TeleportProbe
extends NetwValidator

var _client_span: NetwSpan = null

# [code]TPComponent[/code] instance id -> [NetwSpan], for concurrent
# server-side requests from different players.
var _server_spans: Dictionary[int, NetwSpan] = { }


func interests() -> Array:
	return [NetwTreeEvent.Kind.PLAYER_SPAWNED]


func inspect(event: NetwTreeEvent, _report: NetwReport) -> void:
	var player := event.node
	if not is_instance_valid(player):
		return
	var tp := player.get_node_or_null("%TPComponent") as TPComponent
	if not tp:
		return
	_hook(tp, event.tree)


func _hook(tp: TPComponent, mt: MultiplayerTree) -> void:
	if tp.teleport_initiated.is_connected(_on_teleport_initiated):
		return
	tp.teleport_initiated.connect(_on_teleport_initiated.bind(mt))
	tp.stalled.connect(_on_stalled)
	tp.teleport_committed.connect(_on_teleport_committed)
	tp.teleport_finished.connect(_on_teleport_finished)
	tp.teleport_failed.connect(_on_teleport_failed.bind(tp))
	tp.teleport_request_received.connect(_on_teleport_request_received.bind(tp, mt))
	tp.teleport_request_completed.connect(_on_teleport_request_completed.bind(tp))

# --- Client side ----------------------------------------------------------


func _on_teleport_initiated(
		target_tp: SceneNodePath,
		corr: NetwCorrelation,
		mt: MultiplayerTree,
) -> void:
	_client_span = Netw.dbg.span(
		mt,
		"tp",
		{ "scene": target_tp.scene_path, "corr_id": str(corr.id) },
	)
	_client_span.step("initiate")


func _on_stalled(reason: StringName) -> void:
	if _client_span:
		_client_span.step_warn("stalled", "", { "reason": reason })


func _on_teleport_committed() -> void:
	if _client_span:
		_client_span.step("committed")


func _on_teleport_finished() -> void:
	if _client_span:
		_client_span.end()
	_client_span = null

# --- Server side ------------------------------------------------------------


func _on_teleport_request_received(
		sender_id: int,
		_from_scene_name: String,
		to_scene_path: String,
		corr: NetwCorrelation,
		tp: TPComponent,
		mt: MultiplayerTree,
) -> void:
	var span := Netw.dbg.peer_span(
		mt,
		"tp_server",
		[sender_id],
		{ "scene": to_scene_path, "corr_id": str(corr.id) },
	)
	_server_spans[tp.get_instance_id()] = span


func _on_teleport_request_completed(tp: TPComponent) -> void:
	var span: NetwSpan = _server_spans.get(tp.get_instance_id())
	if span:
		span.end()
	_server_spans.erase(tp.get_instance_id())

# --- Shared -------------------------------------------------------------


func _on_teleport_failed(
		reason: StringName,
		data: Dictionary,
		tp: TPComponent,
) -> void:
	# A failure can happen on either side; the request-scoped span (if any)
	# takes priority since it is more specific than the client's whole-op span.
	var span: NetwSpan = _server_spans.get(tp.get_instance_id())
	if span:
		span.fail(str(reason), data)
		_server_spans.erase(tp.get_instance_id())
		return
	if _client_span:
		_client_span.fail(str(reason), data)
		_client_span = null
