## Blessed test bench for making entities live without a full spawn.
##
## A scenario often needs a matched entity that already exists at the same tree
## location on every peer to share one route, the state the spawn pipeline
## reaches by issuing a frame. The bench mints that route through the same
## [NetwMultiplayerCore] seam the pipeline uses, so a rig expresses its
## scenario instead of copy-pasting route plumbing.
## [codeblock]
## server.add_child(server_root)
## client.add_child(client_root)
## NetwBench.bind_shared_route(server_root, [client_root])
## [/codeblock]
class_name NetwBench
extends Object


## Mints a route for [param authored] on its own session and binds the same route
## to every node in [param mirrors] on theirs, returning the route.
##
## Every node must already be live, its identity bound and its owner in a tree,
## so each resolves its session through [member NetwEntity.multiplayer]. The
## authoring node's peer allocates the route the way the spawn pipeline does, and
## every mirror binds it, the cross-peer make-live the pipeline performs for a
## real spawn.
static func bind_shared_route(authored: Node, mirrors: Array[Node]) -> int:
	var authored_entity := NetwEntity.of(authored)
	assert(authored_entity != null, "NetwBench: the authored node has no NetwEntity.")
	var authored_session: NetwMultiplayer = authored_entity.multiplayer
	assert(
		authored_session != null,
		"NetwBench: the authored entity has no session. Add it to a tree first.",
	)
	var route := authored_session._native_core.liveness_allocate_route(authored_entity)
	authored_session._native_core.liveness_bind_route(route, authored_entity)
	for mirror in mirrors:
		var mirror_entity := NetwEntity.of(mirror)
		assert(mirror_entity != null, "NetwBench: a mirror node has no NetwEntity.")
		assert(
			mirror_entity.multiplayer != null,
			"NetwBench: a mirror entity has no session. Add it to a tree first.",
		)
		mirror_entity.multiplayer._native_core.liveness_bind_route(route, mirror_entity)
	return route
