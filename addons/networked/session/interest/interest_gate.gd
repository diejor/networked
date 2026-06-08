## Replicated client mirror for a [NetwInterestLayer].
##
## A gate binds to [member layer_id] when it enters the tree. The server
## writes [member viewers] and [member policy] through
## [method apply_snapshot]; Godot spawn/on-change replication delivers
## those fields to admitted clients before they query the layer.
##
## [br][br]
## Use a gate at a subtree root when admission state must arrive with
## that subtree, such as [MultiplayerScene]'s scene-level gate. Leave
## generic layers unbound when they only need server-side visibility
## filtering.
##
## [br][br]
## A gate mirrors admission only. Entity membership and transition signals
## still live on [NetwInterestLayer].
## [codeblock]
## var gate := InterestGate.new()
## gate.layer_id = &"arena:1"
## arena_root.add_child(gate)
##
## var layer := Netw.ctx(self).interest.layer(&"arena:1")
## layer.add_viewer(player.peer_id)
## [/codeblock]
class_name InterestGate
extends MultiplayerSynchronizer

## Stable identifier of the [NetwInterestLayer] this gate mirrors.
@export var layer_id: StringName:
	set(value):
		if value == layer_id:
			return
		if _registered:
			_unbind()
		layer_id = value
		if is_inside_tree():
			_bind()

## Spawn-synced viewer ids for the bound layer.
##
## Server writes this through [method apply_snapshot]. Client setters
## mirror the value into the local [NetwInterestLayer].
@export var viewers: PackedInt32Array = []:
	set(value):
		var prev := viewers
		viewers = value
		_on_viewers_replicated(prev, value)

## Spawn-synced policy value for the bound layer.
@export var policy: NetwInterestLayer.Policy = \
		NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
	set(value):
		var changed := value != policy
		policy = value
		if changed:
			_on_policy_replicated()

var _layer: NetwInterestLayer
var _applying_local: bool = false
var _config_built: bool = false
var _registered: bool = false
var _client_entities: Dictionary[NetwEntity, bool] = { }


func _init() -> void:
	unique_name_in_owner = true
	public_visibility = false


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		_build_replication_config()


func _enter_tree() -> void:
	_bind()


func _exit_tree() -> void:
	_revoke_all_client_entities()
	_unbind()


## Enrolls [param entity] in this gate's bound layer. Idempotent.
##
## On the server, delegates to [method NetwInterestLayer.add_entity]. On
## a client, records the local entity and admits it through the layer's
## client tracking path so synchronizer visibility filters are installed.
func track_entity(entity: NetwEntity) -> void:
	if entity == null or _layer == null:
		return
	if _is_server():
		_layer.add_entity(entity)
		return
	if _client_entities.has(entity):
		return
	_client_entities[entity] = true
	_layer._client_track_entity(entity)


## Removes [param entity] from this gate's bound layer. Idempotent.
func untrack_entity(entity: NetwEntity) -> void:
	if entity == null or _layer == null:
		return
	if _is_server():
		_layer.remove_entity(entity)
		return
	if not _client_entities.has(entity):
		return
	_client_entities.erase(entity)
	_layer._client_untrack_entity(entity)


## Returns [code]true[/code] when this gate is tracking [param entity].
func has_entity(entity: NetwEntity) -> bool:
	if entity == null or _layer == null:
		return false
	if _is_server():
		return _layer.has_entity(entity)
	return _client_entities.has(entity)


# Revokes local entities if the gate leaves before tracked subtree nodes
# call [method untrack_entity].
func _revoke_all_client_entities() -> void:
	if _client_entities.is_empty() or _layer == null:
		_client_entities.clear()
		return
	var entities := _client_entities.keys()
	_client_entities.clear()
	for entity: NetwEntity in entities:
		_layer._client_untrack_entity(entity)


## Returns [code]true[/code] when [param peer_id] is in [member viewers].
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Returns this gate's admission verdict for [param peer_id].
func verdict_for(peer_id: int) -> bool:
	return InterestPolicy.verdict(policy, _viewers_as_dict(), peer_id)


## Applies the server's current bound-layer snapshot.
##
## This updates replicated properties and the gate synchronizer's own
## per-peer visibility. Callers should mutate the layer, not this method.
func apply_snapshot(
		new_viewers: PackedInt32Array,
		new_policy: NetwInterestLayer.Policy,
) -> void:
	apply_snapshot_data(new_viewers, new_policy)
	_apply_admission_visibility()


## Writes [param new_viewers] and [param new_policy] without touching
## per-peer visibility. Used by [InterestService] to stage gate data
## ahead of split admit/revoke visibility passes.
func apply_snapshot_data(
		new_viewers: PackedInt32Array,
		new_policy: NetwInterestLayer.Policy,
) -> void:
	_applying_local = true
	policy = new_policy
	viewers = new_viewers
	_applying_local = false


## Applies admission visibility for [param peer_ids] only, using the
## gate's current [member policy] and [member viewers] to compute each
## verdict. Peers not listed are left untouched.
func apply_admission_visibility_to(peer_ids: Array) -> void:
	if not is_inside_tree():
		return
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return
	if not multiplayer.is_server():
		return
	var v_dict := _viewers_as_dict()
	for peer_id: int in peer_ids:
		var verdict := InterestPolicy.verdict(policy, v_dict, peer_id)
		set_visibility_for(peer_id, verdict)


func _apply_admission_visibility() -> void:
	if not is_inside_tree():
		return
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return
	if not multiplayer.is_server():
		return
	var v_dict := _viewers_as_dict()
	for peer_id: int in multiplayer.get_peers():
		var verdict := InterestPolicy.verdict(policy, v_dict, peer_id)
		set_visibility_for(peer_id, verdict)


func _on_viewers_replicated(
		prev: PackedInt32Array,
		curr: PackedInt32Array,
) -> void:
	if _applying_local or not _layer or _is_server():
		return
	var prev_set := _packed_to_dict(prev)
	var curr_set := _packed_to_dict(curr)
	for p: int in prev_set.keys():
		if not curr_set.has(p):
			_layer.remove_viewer(p)
	for p: int in curr_set.keys():
		if not prev_set.has(p):
			_layer.add_viewer(p)


func _on_policy_replicated() -> void:
	if _applying_local or not _layer or _is_server():
		return
	_layer.set_policy(policy)


func _bind() -> void:
	if _registered or layer_id.is_empty():
		return
	var service := _service()
	if not service:
		return
	# Refuse duplicates so exit cleanup cannot unregister the incumbent.
	if is_instance_valid(service.gate_for(layer_id)):
		Netw.dbg.error(
			"InterestGate: layer '%s' already has a bound gate",
			String(layer_id),
			func(m): push_error(m)
		)
		return
	_layer = service.layer_for(layer_id)
	if not _layer:
		return
	_layer.bind_gate(self)
	_registered = true
	service.register_gate(self)
	if not _is_server():
		_mirror_snapshot_to_layer()


func _unbind() -> void:
	if not _registered:
		return
	_registered = false
	if _layer:
		_layer.unbind_gate()
	var service := _service()
	if service:
		service.unregister_gate(self)
	_layer = null


func _build_replication_config() -> void:
	if _config_built:
		return
	var target: Node = owner if owner else get_parent()
	if not target:
		return
	root_path = get_path_to(target)
	var config := SceneReplicationConfig.new()
	_add_spawn_property(config, target, self, "viewers")
	_add_spawn_property(config, target, self, "policy")
	replication_config = config
	_config_built = true


static func _add_spawn_property(
		config: SceneReplicationConfig,
		target: Node,
		gate: MultiplayerSynchronizer,
		property: String,
) -> void:
	var path := NodePath(
		str(target.get_path_to(gate)) + ":" + property,
	)
	config.add_property(path)
	config.property_set_spawn(path, true)
	config.property_set_replication_mode(
		path,
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)


func _packed_to_dict(arr: PackedInt32Array) -> Dictionary:
	var d: Dictionary = { }
	for p: int in arr:
		d[p] = true
	return d


func _viewers_as_dict() -> Dictionary:
	return _packed_to_dict(viewers)


func _mirror_snapshot_to_layer() -> void:
	if not _layer:
		return
	var next := _viewers_as_dict()
	var prev: Array[int] = []
	prev.assign(_layer.viewers.keys())
	for p: int in prev:
		if not next.has(p):
			_layer.remove_viewer(p)
	for p in next:
		if not _layer.viewers.has(p):
			_layer.add_viewer(p)
	_layer.set_policy(policy)


func _is_server() -> bool:
	if not is_inside_tree():
		return true
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return true
	return multiplayer.is_server()


func _service() -> InterestService:
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return null
	return mt.get_service(InterestService) as InterestService
