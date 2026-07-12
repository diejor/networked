## Wire format and helpers that carry Networked identity through a stock
## [MultiplayerSpawner] spawn call.
##
## A [MultiplayerSpawner] replicates one [Variant] of spawn data. Networked
## reserves a [code]_netw[/code] key inside it for identity (entity id, peer id,
## route) and leaves the gameplay payload under [code]data[/code], so a spawned
## node is addressable and bound before it enters the tree without the caller
## learning the envelope shape. [method wrap_spawn] and [method spawn_for] are
## the two halves of that contract.
## [codeblock]
## spawner.spawn_function = NetwSpawn.wrap_spawn(_spawn_player)
## NetwSpawn.spawn_for(spawner, participant, payload)
## [/codeblock]
## [method decorate_spawn] and [method spawn_identity] are the low-level path for
## a raw [member MultiplayerSpawner.spawn_function] that owns identity binding
## itself, decoding a [NetwSpawn.SpawnIdentity] to apply onto the node.
class_name NetwSpawn
extends RefCounted

# Reserved keys for Networked spawn identity envelopes.
#
# [method spawn_for] owns the outer Variant as a Dictionary with these keys.
# [method wrap_spawn] unwraps it and passes the user payload through unchanged.
# [method decorate_spawn] remains the low-level Dictionary path.
const _SPAWN_NETW_KEY := "_netw"
const _SPAWN_DATA_KEY := "data"


## Wraps a [MultiplayerSpawner] spawn function so Networked identity is bound
## before the spawned node enters the tree.
##
## The wrapped function receives the exact gameplay payload passed to
## [method spawn_for]. The returned node is passed to
## [method NetwSpawn.SpawnIdentity.bind].
## [codeblock]
## func _ready() -> void:
##     spawn_function = NetwSpawn.wrap_spawn(_spawn_player)
##
## func _spawn_player(data: Dictionary) -> Node:
##     var player := PLAYER.instantiate()
##     player.spawn_index = data.spawn_index
##     return player
## [/codeblock]
##
## [method wrap_spawn] and [method spawn_for] are two halves of one contract.
## Use [method decorate_spawn] and [method spawn_identity] only for manual
## low-level spawn functions.
static func wrap_spawn(fn: Callable) -> Callable:
	return _wrapped_spawn.bind(fn)


static func _wrapped_spawn(envelope: Variant, fn: Callable) -> Node:
	var envelope_error := _spawn_envelope_error(envelope)
	if not envelope_error.is_empty():
		var msg := (
				"spawn data is not a Networked envelope: %s. Spawn through " +
				"NetwSpawn.spawn_for(spawner, participant, payload). " +
				"wrap_spawn and spawn_for are two halves of one contract."
		)
		assert(false, msg % envelope_error)
		return fn.call(envelope) as Node
	var spawn_identity := SpawnIdentity.new(envelope)
	var payload: Variant = (envelope as Dictionary).get(_SPAWN_DATA_KEY)
	var node := fn.call(payload) as Node
	assert(node != null, "NetwSpawn.wrap_spawn function must return a Node")
	spawn_identity.bind(node)
	return node


## Wraps [param payload] for [param participant] and calls
## [method MultiplayerSpawner.spawn].
##
## [param payload] can be any Variant supported by Godot spawn replication,
## including [Dictionary], [Array], [PackedByteArray], scalars, or
## [code]null[/code].
## Use with [method wrap_spawn] on the same [MultiplayerSpawner].
## [br][br][b]Server Only.[/b]
static func spawn_for(
		spawner: MultiplayerSpawner,
		participant: NetwParticipant,
		payload: Variant = null,
) -> Node:
	assert(
		spawner == null or spawner.multiplayer.is_server(),
		"NetwSpawn.spawn_for is server-only",
	)
	if spawner == null or participant == null or participant.join == null:
		return null
	assert(
		_is_wrapped_spawn_callable(spawner.spawn_function),
		(
				"spawn_function is not wrapped. Set spawn_function = " +
				"NetwSpawn.wrap_spawn(your_fn) in _ready; spawn_for " +
				"and wrap_spawn are two halves of one contract."
		),
	)
	var liveness := NetwLivenessInterface.for_node(spawner)
	var route := liveness.reserve_route() if liveness else 0
	return spawner.spawn(_spawn_envelope(participant.join, payload, route))


## Decodes bindable identity from custom spawn data.
##
## Prefer [method wrap_spawn] for [MultiplayerSpawner.spawn_function].
## [method spawn_identity] remains available for low level spawn functions that
## bind identity directly.
static func spawn_identity(data: Dictionary) -> SpawnIdentity:
	return SpawnIdentity.new(data)


## Returns [param data] with Networked spawn identity attached.
##
## The returned [Dictionary] is a duplicate. The input [param data] is not
## mutated. [code]_netw[/code] is reserved and must not already be present.
static func decorate_spawn(
		data: Dictionary,
		rj: ResolvedJoin,
		spawner: Node = null,
) -> Dictionary:
	assert(
		not data.has(_SPAWN_NETW_KEY),
		"NetwSpawn.decorate_spawn: '_netw' is reserved.",
	)
	var liveness := NetwLivenessInterface.for_node(spawner) if spawner else null
	var route := liveness.reserve_route() if liveness else 0
	var out := data.duplicate(true)
	out[_SPAWN_NETW_KEY] = {
		"entity_id": rj.username,
		"peer_id": rj.peer_id,
		"route": route,
	}
	return out


static func _spawn_envelope(
		rj: ResolvedJoin,
		payload: Variant = null,
		route: int = 0,
) -> Dictionary:
	return {
		_SPAWN_NETW_KEY: {
			"entity_id": rj.username,
			"peer_id": rj.peer_id,
			"route": route,
		},
		_SPAWN_DATA_KEY: payload,
	}


static func _is_spawn_envelope(value: Variant) -> bool:
	return _spawn_envelope_error(value).is_empty()


static func _spawn_envelope_error(value: Variant) -> String:
	if not value is Dictionary:
		return "expected Dictionary"
	var data := value as Dictionary
	if not data.has(_SPAWN_DATA_KEY):
		return "missing 'data'"
	return _spawn_identity_error(data)


static func _spawn_identity_error(data: Dictionary) -> String:
	if not data.has(_SPAWN_NETW_KEY):
		return "missing '_netw'"
	var netw: Variant = data.get(_SPAWN_NETW_KEY)
	if not netw is Dictionary:
		return "'_netw' must be a Dictionary"
	var identity := netw as Dictionary
	if not identity.has("entity_id"):
		return "missing '_netw.entity_id'"
	if StringName(identity.get("entity_id", "")).is_empty():
		return "'_netw.entity_id' must not be empty"
	if not identity.has("peer_id"):
		return "missing '_netw.peer_id'"
	var peer_value: Variant = identity.get("peer_id")
	if not (peer_value is int or peer_value is float):
		return "'_netw.peer_id' must be numeric"
	if int(peer_value) < 0:
		return "'_netw.peer_id' must be >= 0"
	return ""


static func _is_wrapped_spawn_callable(fn: Callable) -> bool:
	return fn.is_valid() and fn.get_method() == &"_wrapped_spawn"


## Bindable identity decoded from custom spawn data.
class SpawnIdentity extends RefCounted:
	## Decoded entity ID for the spawned node, mapped from
	## [member NetwEntity.entity_id].
	var entity_id: StringName = &""
	## Decoded peer ID for the spawned node, mapped from
	## [member NetwEntity.peer_id].
	var peer_id: int = 0
	## Decoded route ID for the spawned node, or [code]0[/code] if missing.
	var route: int = 0


	func _init(spawn_data: Dictionary) -> void:
		var envelope_error := NetwSpawn._spawn_identity_error(spawn_data)
		assert(
			envelope_error.is_empty(),
			"NetwSpawn.spawn_identity: %s." % envelope_error,
		)
		var netw: Dictionary = spawn_data.get(NetwSpawn._SPAWN_NETW_KEY, { })
		entity_id = StringName(netw.get("entity_id", ""))
		peer_id = int(netw.get("peer_id", 0))
		route = int(netw.get("route", 0))


	## Binds this identity onto [param node].
	func bind(node: Node) -> Node:
		var bound := NetwEntity.bind(node, entity_id, peer_id)
		if route > 0:
			var entity := NetwEntity.of(node)
			if entity:
				# bind_route writes entity.route once the tree is resolvable.
				# The direct write covers pre-tree binds so the record can
				# re-bind from it at tree entry.
				entity.route = route
				var liveness := NetwLivenessInterface.for_node(node)
				if liveness:
					liveness.bind_route(route, entity)
		return bound
