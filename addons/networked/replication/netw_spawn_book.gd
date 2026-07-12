## Per-session spawn ledger owned by [NetwReplicationInterface]: the armed
## book of nodes a verb has stamped but not yet placed, the ordered spawn book
## the authority replays for late joiners, and the recv book of routes this
## peer materialized from the server.
##
## The spawn book is a [Dictionary] keyed by route on purpose. Insertion order
## is the replay order, and parents always arm before their children under the
## visibility cascade, so iterating the book replays parents before children.
## Do not replace it with a set.
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
