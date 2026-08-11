## Per-session spawn ledger owned by [ReplicationCore]: the armed
## book of nodes a verb has stamped but not yet placed, the ordered spawn book
## the authority replays for late joiners, and the recv book of routes this
## peer materialized from the server.
##
## The spawn book is a [Dictionary] keyed by route on purpose. Insertion order
## is the order the session armed its spawns, which is stable and reproducible.
## Do not replace it with a set. Insertion order is not ancestry order: a
## reparent can move an entity under a parent armed after it, so anything that
## reads a parent's answer before a child's must iterate
## [method ancestry_order] instead.
## [codeblock]
## armed    route -> SpawnRecord   verb ran, node still orphaned
## spawned  route -> SpawnRecord   authority-side, SPAWN issued (replay book)
## recv     route -> WeakRef       receiver-side, enrolled before add_child
## [/codeblock]
## Every record holds its node through a [WeakRef], so a node freed while
## armed simply drops out and orphan ownership stays with the caller.
class_name NetwSpawnBook
extends RefCounted

## Reconstruction recipes carried by a
## [constant NetwFrameEnvelope.Channel.SPAWN] frame. See
## [method Netw.replicate] and [method Netw.spawn].
enum Recipe {
	## The node reconstructs by instantiating its [member Node.scene_file_path].
	SCENE = 0,
	## The node reconstructs by invoking a registered spawn function on a host
	## object that exists on every peer.
	FN = 1,
	## The node reconstructs through a [MultiplayerSpawner], addressed by anchor
	## with the spawner's own scene-index compression.
	SPAWNER = 2,
	## The node is not reconstructed at all: every peer already holds it at the
	## frame's parent anchor and name (static structure both peers built out of
	## band), and the frame only stamps identity onto the existing instance.
	## Issued by [NetwSyncCompat] when a consumed synchronizer's root has no
	## [NetwEntity], so a synced node always has a route.
	ADOPT = 3,
	## The node reconstructs by invoking a spawn function the session registered
	## under a stable id, resolved from the id alone with no host node. This is
	## how a manager-less session spawns scenes, whose constructor lives on the
	## API rather than a node.
	FN_REGISTRY = 4,
}

## Nodes stamped by a verb, waiting for tree entry. Keyed by route.
var armed: Dictionary[int, SpawnRecord] = { }

## Authority-side replay book of issued spawns, in spawn order. Keyed by route.
var spawned: Dictionary[int, SpawnRecord] = { }

## Receiver-side routes materialized from the server, enrolled before
## [method Node.add_child] so a locally applied spawn can never re-arm.
var recv: Dictionary[int, WeakRef] = { }


## One armed or issued spawn: the identity stamped by the verb plus the
## reconstruction recipe a receiver needs.
class SpawnRecord:
	extends RefCounted

	var route: int = 0
	var node_ref: WeakRef
	var entity_id: StringName = &""
	var peer_id: int = 0
	var controller: int = 0
	var recipe: Recipe = Recipe.SCENE
	var scene_path: String = ""
	var fn_host_ref: WeakRef
	var fn_method: StringName = &""
	var fn_args: Array = []
	## Registry id of the spawn function for a [constant Recipe.FN_REGISTRY]
	## record. The session resolves it to a host-less constructor.
	var fn_registry_id: StringName = &""
	## The consumed [MultiplayerSpawner] for a [constant Recipe.SPAWNER] record.
	var spawner_ref: WeakRef
	## Index into the spawner's scene list, or [code]-1[/code] for a custom
	## [method MultiplayerSpawner.spawn] whose data rides [member custom_data].
	var scene_index: int = -1
	## The custom spawn argument captured from a [method MultiplayerSpawner.spawn].
	var custom_data: Variant = null
	## [member Node.name] carried on the frame. Applied, never parsed.
	var node_name: String = ""
	## Peers the [constant NetwFrameEnvelope.Channel.SPAWN] frame was sent to,
	## the [constant NetwFrameEnvelope.Channel.DESPAWN] recipient set.
	var recipients: Array[int] = []
	## Route of the nearest tracked ancestor entity at flush time, or
	## [code]0[/code]. Drives the child-first despawn cascade.
	var parent_route: int = 0


	func node() -> Node:
		var n := node_ref.get_ref() as Node if node_ref else null
		return n if is_instance_valid(n) else null


	func fn_host() -> Node:
		var n := fn_host_ref.get_ref() as Node if fn_host_ref else null
		return n if is_instance_valid(n) else null


	func spawner() -> MultiplayerSpawner:
		var s := spawner_ref.get_ref() as MultiplayerSpawner if spawner_ref else null
		return s if is_instance_valid(s) else null


## Returns every issued route with each one after its
## [member NetwSpawnBook.SpawnRecord.parent_route].
##
## Ancestry is the order a per-peer verdict has to be read in, because a child's
## verdict is clamped by its parent's. [member spawned] answers arm order, and
## the two agree only until a reparent moves an entity under a parent armed
## later, which leaves the mover permanently unspawnable if the clamp reads a
## parent verdict that has not been computed yet.
## [codeblock]
## # Level1 armed first, then the players, then Level2. Move the players
## # into Level2 and arm order stops answering ancestry:
## spawned          1 Level1, 2 valeria, 3 jose, 4 maria, 5 Level2
## ancestry_order   1 Level1, 5 Level2, 2 valeria, 3 jose, 4 maria
## [/codeblock]
func ancestry_order() -> Array[int]:
	var out: Array[int] = []
	var placed: Dictionary[int, bool] = { }
	var pending: Array[int] = []
	pending.assign(spawned.keys())
	while not pending.is_empty():
		var deferred: Array[int] = []
		for route: int in pending:
			var parent_route := spawned[route].parent_route
			if parent_route > 0 and spawned.has(parent_route) \
					and not placed.has(parent_route):
				deferred.append(route)
				continue
			placed[route] = true
			out.append(route)
		if deferred.size() == pending.size():
			# Tree ancestry cannot cycle, so a pass that places nothing means the
			# anchors are mid-move. Arm order is the honest fallback.
			out.append_array(deferred)
			break
		pending = deferred
	return out


## Arms [param record] under its route.
func arm(record: SpawnRecord) -> void:
	armed[record.route] = record


## Removes and returns the armed record for [param route], or [code]null[/code].
func take_armed(route: int) -> SpawnRecord:
	var record: SpawnRecord = armed.get(route)
	armed.erase(route)
	return record


## Enrolls a receiver-side [param node] under [param route]. Must run before
## the node is placed so the route is claimed while the node is still orphaned.
func enroll_recv(route: int, node: Node) -> void:
	recv[route] = weakref(node)


## Returns [code]true[/code] when [param route] was materialized from the
## server on this peer.
func is_recv(route: int) -> bool:
	return recv.has(route)


## Drops all books. Warns for nodes still armed, which usually means a
## replicated node was never placed with [method Node.add_child].
func clear() -> void:
	for route in armed:
		var record: SpawnRecord = armed[route]
		var node := record.node()
		if node:
			Netw.dbg.warn(
				"NetwSpawnBook: node '%s' (%s) was still armed at session "
				+ "end. Netw.replicate was called but the node never entered "
				+ "the tree.",
				[node.name, record.scene_path],
			)
	armed.clear()
	spawned.clear()
	recv.clear()
