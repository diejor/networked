## Identity registry and existence state machine for every routed entity in one
## [MultiplayerTree].
##
## [InterestCore] decides what a peer [i]should[/i] see. Liveness reports what a
## peer actually [i]has[/i]. The two answers differ whenever packets are in
## flight, because a spawn packet and an unreliable state packet travel on
## uncoupled streams and either can arrive first. Liveness turns that window
## into a normal, queryable condition instead of an engine error. Senders gate
## on [method ReplicationCore.is_live_for], receivers resolve through
## [method entity_of], and anything addressed to an absent entity is dropped or
## deferred, never raised.
##
## [br][br]
## Liveness answers only its own half of that gate, which is
## [method route_state]. Joining it with an admission is
## [ReplicationCore]'s job, so identity never has to know what interest
## decided.
##
## [br][br]
## Owned by [NetwMultiplayer] and reached through its entity and route verbs.
## The sweep of expired [method when_live] callbacks is driven from
## [method NetwMultiplayer._poll], mirroring the engine's own poll-driven
## interfaces rather than a node process.
##
## [br][br]
## Everything keyed on a route lives in [member core], because it outlives the
## entities it names: a [constant State.DEAD] route stays resolvable for the
## whole session while the [NetwEntity] behind it is freed, and a caller parked
## through [method when_live] is waiting on a route rather than on an object.
## This interface keeps what is bound to an entity's lifetime instead, which is
## the [NetwEntity]-typed signals, the wrapper each route resolves to, and the
## node observation that notices an owner leaving the tree.
## [codeblock]
## NetwLivenessCore   routes, states, the waiting room, the frame counter
## LivenessShell      wrappers, nodes, signals, the session
## [/codeblock]
##
## [br][br][b]The route[/b]
## [br]On the wire an entity is named by a route, a session-monotonic integer
## allocated once by [method allocate_route] and never reused. The route rides
## the SPAWN frame header, so a peer learns it inside the same reliable
## message that creates the node. By the time [method entity_of] can resolve
## a route, the node it names already exists.
## [codeblock]
## server                                 client
## allocate_route(entity)   -> 7
## spawn packet { ..., route: 7 }  ────▶  node enters tree
##                                        route bound automatically
##                                        entity_of(7) == entity
## [/codeblock]
## An entity without a route never rides that channel. Bind it with
## [method bind_route] on every peer, or leave it unroutable. This is why
## [ReplicationCore] transports require a [NetwEntity] on the entity
## root.
##
## [br][br]
## [b]The state machine[/b]
## [br]A route moves forward only. [constant State.DEAD] routes stay tombstoned
## for the whole session, so a stale packet resolves to "known but dead" and is
## dropped instead of being mistaken for a packet that arrived early. The one
## sanctioned revival is a spawn frame arriving after the despawn that
## tombstoned the route, which is an interest re-admission: spawn and despawn
## share one reliable ordered channel, so that ordering can never be stale.
## [codeblock]
## UNKNOWN ──spawn──▶ LIVE ──despawn linger──▶ LINGERING ──window──▶ DEAD
##                     │
##                     └───────plain despawn──────────────────────▶ DEAD
## [/codeblock]
## [constant State.LINGERING] keeps the route resolvable while a
## [method NetwEntity.despawn] with [NetwEntity.DespawnOpts]
## linger winds the node down, which is what lets a late lag-compensation query
## still name a dying entity. A reparent through
## [method NetwEntity.reparent_to] never kills a route, because
## [member NetwEntity.reparenting] distinguishes it from a despawn.
##
## [br][br]
## [b]Waiting instead of retrying[/b]
## [br]Reliable events that must not be lost across the spawn window use
## [method when_live] instead of hand-rolled retry loops. [InterestCore]
## defers unbound-layer visibility transitions through it.
## [codeblock]
## liveness.when_live(route, func():
##     apply_event(liveness.entity_of(route))
## )
## [/codeblock]
class_name LivenessShell
extends RefCounted

## Existence states of a route. Transitions only move forward. See the state
## machine diagram in the class description.
enum State {
	## No binding known on this peer. The packet may simply be early.
	UNKNOWN,
	## The entity's node is bound and inside the tree.
	LIVE,
	## Despawn issued with linger. The route still resolves while the node
	## winds down, but transports should stop sending.
	LINGERING,
	## The route is tombstoned for the rest of the session and never
	## resurrects. Respawn is a new spawn with a new route.
	DEAD,
}

## Emitted when an entity route transitions to [constant State.LIVE].
signal entity_live(route: int, entity: NetwEntity)

## Emitted when an entity route transitions to [constant State.LINGERING].
signal entity_lingering(route: int, entity: NetwEntity)

## Emitted when an entity route transitions to [constant State.DEAD].
signal entity_dead(route: int)

# The tickrate a wait ages against while no clock is configured, so a default
# timeout is still roughly one second on a rig that never registered one.
const _CLOCKLESS_TICKRATE := 30

## The record store this interface reads and writes.
##
## Held rather than mirrored: the records outlive the entities they name,
## because [constant State.DEAD] is a tombstone kept for the whole session
## while the entity behind it is freed. Extensions consume it for
## [method NetwLivenessCore.state_of] and the route bridge.
var core := NetwLivenessCore.new()

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef
# The wrapper cache, and the one place an entity is held. Keyed by handle so a
# tombstoned record can drop its entity without losing its own state.
var _entities: Dictionary[RID, NetwEntity] = { }


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	if api:
		api._connect_once(api.peer_disconnected, _on_peer_disconnected)
		api._connect_once(api.session_ended, _on_session_ended)


## Advances the liveness frame counter and sweeps expired [method when_live]
## callbacks. Driven from [method NetwMultiplayer._poll].
##
## The sweep answers with the routes whose last waiter expired, because the
## record plane knows a deadline passed and this interface is the half that
## knows how to say so.
func poll() -> void:
	var clock := _clock_interface()
	for route in core.poll(clock.tick if clock else 0):
		Netw.dbg.warn("LivenessShell: when_live timed out for route %d", [route])


## Resolves the [LivenessShell] for the session enclosing [param node],
## or [code]null[/code] when there is none. Quiet by design so detached nodes and
## offline rigs degrade to unroutable.
##
## Resolves the session api directly, falling back to the enclosing tree when a
## node's own multiplayer is not yet bound to the api at call time. A root install
## has no tree, so the api-first resolve is what keeps liveness routed there.
static func for_node(node: Node) -> LivenessShell:
	var api := NetwMultiplayer.of(node)
	if api == null:
		var mt := MultiplayerTree.resolve(node)
		api = mt.api if mt else null
	return api._liveness if api else null


# Cleans up peer state when they disconnect.
func _on_peer_disconnected(_peer_id: int) -> void:
	pass


# Defers session state clear to avoid desyncs during teardown.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


# Resets registry state between sessions.
func _clear_session_state() -> void:
	for entity in _entities.values():
		if is_instance_valid(entity):
			entity.rid = RID()
	core.clear()
	_entities.clear()


## Increments the route counter and returns it without binding.
## [br][br][b]Server Only.[/b]
func reserve_route() -> int:
	assert(
		_is_server(),
		"LivenessShell.reserve_route is server-only",
	)
	return core.reserve_route()


## Returns the route for [param entity], allocating a fresh one when none is
## bound yet. Allocation is monotonic and a route is never reused, so calling
## this twice for the same entity returns the same route.
## [br][br][b]Server Only.[/b]
func allocate_route(entity: NetwEntity) -> int:
	assert(
		_is_server(),
		"LivenessShell.allocate_route is server-only; clients learn routes "
		+ "from the spawn packet or bind_route",
	)
	var existing := route_of(entity)
	if existing > 0:
		return existing
	var route := core.reserve_route()
	bind_route(route, entity)
	return route


## Binds [param route] to [param entity] on this peer, transitioning the route
## to [constant State.LIVE].
##
## A route is one identity everywhere, so a wrapper arriving for a route that
## [method bind_routes_data] already made live adopts that record instead of
## minting a second one. Without the adoption the two orderings would not
## converge: the row's record would stay live and orphaned while the wrapper
## carried a record of its own for the same route.
## [codeblock]
## bind_routes_data(route) then bind_route(route, entity)  one record, adopted
## bind_route(route, entity) then bind_routes_data(route)  one record, reused
## [/codeblock]
## The SPAWN pipeline calls this automatically when the spawn packet
## delivers the route. Call it manually only for an entity spawned outside
## that pipeline, on every peer, with the same route.
func bind_route(route: int, entity: NetwEntity) -> void:
	if route <= 0 or entity == null:
		return
	var held := core.rid_from_route(route)
	if held.is_valid():
		var bound: NetwEntity = _entities.get(held)
		if bound == entity:
			return
		# A record with no wrapper is the identity a data bind minted for this
		# same route, so the arriving wrapper takes it over.
		if bound == null and not entity.rid.is_valid():
			entity.rid = held

	# A re-admission arrives with its handle cleared by the despawn that
	# tombstoned it, so it mints a new record and the tombstone stays dead.
	if not entity.rid.is_valid():
		entity.rid = core.entity_create()
	core.bind_route(entity.rid, route)
	_entities[entity.rid] = entity
	entity.route = route

	var owner := entity.owner
	if owner:
		var hook := _on_entity_owner_tree_exiting.bind(entity)
		var api := _api()
		if api:
			api._connect_once(owner.tree_exiting, hook)

		var despawn_hook := _on_entity_despawning.bind(entity)
		if api:
			api._connect_once(entity.despawning, despawn_hook)

	entity_live.emit(route, entity)
	core.flush_live(route)


## Binds every route in [param routes] as a live entity with no wrapper and no
## node, the identity a replicated row is.
##
## This is the bulk door [method NetwMultiplayer.claim_routes] mints through.
## It emits no [signal entity_live], because a wave of two thousand rows would
## otherwise be two thousand signal dispatches for state no listener can reach
## through a wrapper anyway. Callbacks parked on
## [method when_live] still run, so a caller that asked to
## hear about one particular route is answered.
## [codeblock]
## bind_route(route, entity)   one row, a wrapper, entity_live fires
## bind_routes_data(routes)    a wave of rows, no wrapper, no signal
## [/codeblock]
func bind_routes_data(routes: PackedInt64Array) -> void:
	core.bind_routes_data(routes)


## Tombstones every route in [param routes] without emitting
## [signal entity_dead], the death edge of [method bind_routes_data].
##
## A tombstoned route can never be reissued, which is what lets a receiver drop
## a row for it rather than resurrect one. Authority is decided above this: the
## server runs it when it releases an identity and every other peer runs it when
## the lifecycle frame lands, so both ends reach the same state through the same
## code.
func tombstone_routes_data(routes: PackedInt64Array) -> void:
	core.tombstone_routes_data(routes)


## Returns the route ID bound to [param entity], or [code]0[/code].
func route_of(entity: NetwEntity) -> int:
	if entity == null:
		return 0
	return core.route_of(entity.rid)


## Returns the [NetwEntity] bound to [param route], or [code]null[/code].
func entity_of(route: int) -> NetwEntity:
	return _entities.get(core.rid_from_route(route))


## Returns the [Node] associated with [param route], or [code]null[/code].
func node_of(route: int) -> Node:
	var entity := entity_of(route)
	return entity.owner if entity else null


## Returns every entity this peer currently holds a [constant State.LIVE] route
## for, ordered by route so two calls in one frame agree.
##
## This is the peer's own visible set and nothing more. A route exists here only
## because this peer was sent the spawn, so reading it tells a caller what it can
## already see rather than anything about what other peers can.
## [codeblock]
## for entity in api._liveness.live_entities():
##     ...   # every entity replicated to me, right now
## [/codeblock]
func live_entities() -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	for route in core.live_routes():
		var entity := entity_of(route)
		if is_instance_valid(entity):
			out.append(entity)
	return out


## Returns the state of [param entity].
func state_of(entity: NetwEntity) -> State:
	if entity == null:
		return State.UNKNOWN
	return core.state_of(entity.rid) as State


## Returns the state of [param route].
func route_state(route: int) -> State:
	return core.route_state(route) as State


## Runs [param cb] once [param route] reaches [constant State.LIVE].
##
## Runs immediately when the route is already live. Otherwise the callback
## waits for the binding and expires after [param timeout_ticks] with a
## warning. [code]0[/code] derives roughly one second from
## [member ClockCore.tickrate]. [param on_timeout] runs when the
## wait expires instead, so a caller holding state keyed on the pending
## route can release it rather than leak it.
func when_live(
		route: int,
		cb: Callable,
		timeout_ticks: int = 0,
		on_timeout: Callable = Callable(),
) -> void:
	var clock := _clock_interface()
	var timeout := timeout_ticks
	if timeout == 0:
		timeout = int(clock.tickrate) if clock else _CLOCKLESS_TICKRATE

	# Which counter the deadline is measured against is settled here, where the
	# clock is reachable, and carried into the record plane as a plain integer.
	var origin := clock.tick if clock else core.frame()
	core.when_live(route, cb, origin + timeout, clock != null, on_timeout)


# Resolves the tick engine, or null while no configurator has registered, so
# callers fall back to their no-clock path against the inert interface.
func _clock_interface() -> ClockCore:
	var api := _api()
	if api and api._clock.is_configured():
		return api._clock
	return null


# Handles entity owner tree exiting to transition to DEAD. Routes tracked by
# the replication spawn ledger resolve at end-of-frame instead, so a
# remove_child + add_child reparent in one frame never kills the route. See
# the reparent grace on ReplicationCore.
func _on_entity_owner_tree_exiting(entity: NetwEntity) -> void:
	if entity.reparenting != null:
		return

	var route := route_of(entity)
	if route <= 0:
		return
	var api := _api()
	if api and api._replication.owns_spawned_route(route):
		_resolve_tracked_exit.call_deferred(route, entity)
		return
	_transition_to_dead(route)


# End-of-frame half of the reparent grace: a tracked owner back inside the
# tree was reparented, anything else is a real despawn. LINGERING routes also
# resolve here so a linger-then-free still reaches DEAD.
func _resolve_tracked_exit(route: int, entity: NetwEntity) -> void:
	if route_state(route) == State.DEAD:
		return
	if is_instance_valid(entity.owner) and entity.owner.is_inside_tree():
		return
	_transition_to_dead(route)


# Handles entity owner despawning to transition to LINGERING.
func _on_entity_despawning(_reason: StringName, entity: NetwEntity) -> void:
	if entity.active_despawn_opts and entity.active_despawn_opts.linger:
		var route := route_of(entity)
		if route > 0:
			_transition_to_lingering(route, entity)


func _transition_to_lingering(route: int, entity: NetwEntity) -> void:
	core.set_state(entity.rid, NetwLivenessCore.STATE_LINGERING)
	entity_lingering.emit(route, entity)


## Returns the count of [method when_live] callbacks still waiting for a
## binding, the liveness backlog surfaced by [InterestMonitor].
func pending_live_count() -> int:
	return core.pending_live_count()


func _transition_to_dead(route: int) -> void:
	var entity := entity_of(route)
	core.set_state(core.rid_from_route(route), NetwLivenessCore.STATE_DEAD)
	core.abandon_live(route)
	if entity:
		# The record keeps the tombstone, the entity gives up its handle. That
		# split is what lets the entity be freed while a stale packet naming
		# the route still resolves to dead.
		_entities.erase(entity.rid)
		entity.rid = RID()

		# Disconnect signals if valid
		if is_instance_valid(entity.owner):
			var hook := _on_entity_owner_tree_exiting.bind(entity)
			if entity.owner.tree_exiting.is_connected(hook):
				entity.owner.tree_exiting.disconnect(hook)

		var despawn_hook := _on_entity_despawning.bind(entity)
		if entity.despawning.is_connected(despawn_hook):
			entity.despawning.disconnect(despawn_hook)

	entity_dead.emit(route)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# Authority comes from the API, never the tree. The API answers server offline
# and in a disconnected window (its _get_unique_id returns the server id), so a
# unit rig without a peer allocates just as a real server does.
func _is_server() -> bool:
	var api := _api()
	return api == null or api.is_server()
