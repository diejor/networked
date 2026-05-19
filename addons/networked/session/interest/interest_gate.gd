## Spawn-sync carrier for a [NetwInterestLayer].
##
## Bind a gate to a layer by setting [member layer_id] and adding the
## gate to the scene tree. On [signal Node.tree_entered] the gate
## resolves the layer via [NetwInterest] (creating it when missing),
## registers with [InterestService], and calls [method
## NetwInterestLayer.bind_gate]. From that point the layer's [code]
## viewers[/code] and [code]policy[/code] state is mirrored onto this
## node's spawn-synced properties, so any peer that later observes the
## gate via spawn-sync receives the layer's current state atomically
## with the surrounding scene spawn.
##
## Gates are optional. A layer without a gate is server-only state used
## for property visibility via [method
## MultiplayerSynchronizer.set_visibility_for]. A gate is needed when
## the layer must gate [MultiplayerSpawner] traffic for a subtree -
## place the gate at the subtree's root so the spawner consults
## [method make_spawner_filter].
##
## [codeblock]
##     var gate := InterestGate.new()
##     gate.layer_id = &"arena:1"
##     add_child(gate)
##     var layer := Netw.ctx(self).interest.layer(&"arena:1")
##     layer.add_viewer(player.peer_id)
## [/codeblock]
class_name InterestGate
extends MultiplayerSynchronizer


## Stable identifier of the [NetwInterestLayer] this gate binds to.
@export var layer_id: StringName

## Spawn-synced viewer ids. Authoritative on the server, written
## through [method apply_snapshot]; on the client this property is
## set by replication and mirrored back into the local layer state.
@export var viewers: PackedInt32Array = []:
	set(value):
		var prev := viewers
		viewers = value
		_on_viewers_replicated(prev, value)

## Spawn-synced policy value. See [enum NetwInterestLayer.Policy].
@export var policy: int = NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
	set(value):
		var changed := value != policy
		policy = value
		if changed:
			_on_policy_replicated()


var _layer: NetwInterestLayer
var _applying_local: bool = false
var _config_built: bool = false
var _registered: bool = false


func _init() -> void:
	unique_name_in_owner = true
	public_visibility = false


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		_build_replication_config()


func _enter_tree() -> void:
	_bind()


func _exit_tree() -> void:
	_unbind()


## Returns [code]true[/code] when [param peer_id] is in [member viewers].
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Verdict for [param peer_id] under [member policy].
func verdict_for(peer_id: int) -> bool:
	return InterestPolicy.verdict(policy, _viewers_as_dict(), peer_id)


# ---------------------------------------------------------------------------
# Service-side write path. [InterestService] calls this once per flush
# with the bound layer's current viewer/policy snapshot. The gate
# writes its replicated properties and updates per-peer admission
# visibility on its own [MultiplayerSynchronizer].
# ---------------------------------------------------------------------------

## Server-side: writes [param new_viewers] and [param new_policy] to
## the gate's replicated properties and applies per-peer admission
## via [method MultiplayerSynchronizer.set_visibility_for]. Called by
## [InterestService] on flush; do not invoke directly.
func apply_snapshot(
		new_viewers: PackedInt32Array, new_policy: int) -> void:
	_applying_local = true
	policy = new_policy
	viewers = new_viewers
	_applying_local = false
	_apply_admission_visibility()


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


# ---------------------------------------------------------------------------
# Setter responses. On the server `_applying_local` is set so we don't
# re-apply our own write to the layer. On the client the setter is
# invoked by replication and propagates into the local layer mirror.
# ---------------------------------------------------------------------------

func _on_viewers_replicated(
		prev: PackedInt32Array, curr: PackedInt32Array) -> void:
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


# ---------------------------------------------------------------------------
# Bind / unbind.
# ---------------------------------------------------------------------------

func _bind() -> void:
	if _registered or layer_id.is_empty():
		return
	var service := _service()
	if not service:
		return
	# Refuse if another gate already holds this layer; abandon early so
	# our `_exit_tree` does not unregister the incumbent.
	if is_instance_valid(service.gate_for(layer_id)):
		Netw.dbg.error("InterestGate: layer '%s' already has a bound gate",
			String(layer_id), func(m): push_error(m))
		return
	_layer = service.layer_for(layer_id)
	if not _layer:
		return
	_layer.bind_gate(self)
	_registered = true
	service.register_gate(self)


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


# ---------------------------------------------------------------------------
# Replication config.
# ---------------------------------------------------------------------------

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
		property: String) -> void:
	var path := NodePath(
			str(target.get_path_to(gate)) + ":" + property)
	config.add_property(path)
	config.property_set_spawn(path, true)
	config.property_set_replication_mode(
			path, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


# ---------------------------------------------------------------------------
# Helpers.
# ---------------------------------------------------------------------------

func _packed_to_dict(arr: PackedInt32Array) -> Dictionary:
	var d: Dictionary = {}
	for p: int in arr:
		d[p] = true
	return d


func _viewers_as_dict() -> Dictionary:
	return _packed_to_dict(viewers)


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
