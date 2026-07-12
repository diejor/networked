## Identity registry and existence state machine for every routed entity in one
## [MultiplayerTree].
##
## [NetwInterestInterface] decides what a peer [i]should[/i] see. Liveness reports what a
## peer actually [i]has[/i]. The two answers differ whenever packets are in
## flight, because a spawn packet and an unreliable state packet travel on
## uncoupled streams and either can arrive first. Liveness turns that window
## into a normal, queryable condition instead of an engine error. Senders gate
## on [method is_live_for], receivers resolve through [method entity_of], and
## anything addressed to an absent entity is dropped or deferred, never raised.
##
## [br][br]
## Owned by [NetwMultiplayer] and exposed at [member NetwMultiplayer.liveness].
## The sweep of expired [method when_live] callbacks is driven from
## [method NetwMultiplayer._poll], mirroring the engine's own poll-driven
## interfaces rather than a node process.
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
## [NetwReplicationInterface] transports require a [NetwEntity] on the entity
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
## [b]The send gate[/b]
## [br][method is_live_for] and [method live_peers] answer "is sending to this
## peer meaningful right now". The answer combines the entity being locally
## [constant State.LIVE] with the peer's committed [NetwInterestInterface] admission.
## The gate is optimistic by construction. A true verdict means the spawn has
## been issued, not that it has been applied, because an unreliable packet can
## still physically overtake the reliable spawn. Receivers therefore keep the
## other half of the contract: drop what cannot be resolved and let the next
## idempotent snapshot heal the gap.
##
## [br][br]
## [b]Waiting instead of retrying[/b]
## [br]Reliable events that must not be lost across the spawn window use
## [method when_live] instead of hand-rolled retry loops. [NetwInterestInterface]
## defers unbound-layer visibility transitions through it.
## [codeblock]
## liveness.when_live(route, func():
##     apply_event(liveness.entity_of(route))
## )
## [/codeblock]
class_name NetwLivenessInterface
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

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef
var _route_counter: int = 0
var _routes: Dictionary[int, NetwEntity] = {}
var _states: Dictionary[int, State] = {}
var _entity_routes: Dictionary[NetwEntity, int] = {}
var _pending_live: Dictionary[int, Array] = {}
var _frame_counter: int = 0


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	var mt := api.tree if api else null
	if mt:
		mt.peer_disconnected.connect(_on_peer_disconnected)
		mt.session_ended.connect(_on_session_ended)


## Advances the liveness frame counter and sweeps expired [method when_live]
## callbacks. Driven from [method NetwMultiplayer._poll].
func poll() -> void:
	_frame_counter += 1
	_sweep_pending_live()


## Resolves the [NetwLivenessInterface] for the [MultiplayerTree] enclosing
## [param node], or [code]null[/code] when there is none. Quiet by design so
## detached nodes and offline rigs degrade to unroutable.
static func for_node(node: Node) -> NetwLivenessInterface:
	var mt := MultiplayerTree.resolve(node)
	if not mt or not mt.api:
		return null
	return mt.api.liveness


# Cleans up peer state when they disconnect.
func _on_peer_disconnected(_peer_id: int) -> void:
	pass


# Defers session state clear to avoid desyncs during teardown.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


# Resets registry state between sessions.
func _clear_session_state() -> void:
	_route_counter = 0
	_routes.clear()
	_states.clear()
	_entity_routes.clear()
	_pending_live.clear()
	_frame_counter = 0


## Increments the route counter and returns it without binding.
## [br][br][b]Server Only.[/b]
func reserve_route() -> int:
	assert(
		_is_server(),
		"NetwLivenessInterface.reserve_route is server-only",
	)
	_route_counter += 1
	return _route_counter


## Returns the route for [param entity], allocating a fresh one when none is
## bound yet. Allocation is monotonic and a route is never reused, so calling
## this twice for the same entity returns the same route.
## [br][br][b]Server Only.[/b]
func allocate_route(entity: NetwEntity) -> int:
	assert(
		_is_server(),
		"NetwLivenessInterface.allocate_route is server-only; clients learn routes "
		+ "from the spawn packet or bind_route",
	)
	var existing := route_of(entity)
	if existing > 0:
		return existing
	_route_counter += 1
	var route := _route_counter
	bind_route(route, entity)
	return route


## Binds [param route] to [param entity] on this peer, transitioning the route
## to [constant State.LIVE].
##
## The SPAWN pipeline calls this automatically when the spawn packet
## delivers the route. Call it manually only for an entity spawned outside
## that pipeline, on every peer, with the same route.
func bind_route(route: int, entity: NetwEntity) -> void:
	if route <= 0 or entity == null:
		return
	if _routes.get(route) == entity:
		return

	_routes[route] = entity
	_entity_routes[entity] = route
	_states[route] = State.LIVE
	entity.route = route

	var owner := entity.owner
	if owner:
		var hook := _on_entity_owner_tree_exiting.bind(entity)
		if not owner.tree_exiting.is_connected(hook):
			owner.tree_exiting.connect(hook)

		var despawn_hook := _on_entity_despawning.bind(entity)
		if not entity.despawning.is_connected(despawn_hook):
			entity.despawning.connect(despawn_hook)

	entity_live.emit(route, entity)
	_flush_pending_live(route)


## Returns the route ID bound to [param entity], or [code]0[/code].
func route_of(entity: NetwEntity) -> int:
	if entity == null:
		return 0
	return _entity_routes.get(entity, 0)


## Returns the [NetwEntity] bound to [param route], or [code]null[/code].
func entity_of(route: int) -> NetwEntity:
	return _routes.get(route)


## Returns the [Node] associated with [param route], or [code]null[/code].
func node_of(route: int) -> Node:
	var entity := entity_of(route)
	return entity.owner if entity else null


## Returns the state of [param entity].
func state_of(entity: NetwEntity) -> State:
	if entity == null:
		return State.UNKNOWN
	var route := route_of(entity)
	if route == 0:
		return State.UNKNOWN
	return _states.get(route, State.UNKNOWN)


## Returns the state of [param route].
func route_state(route: int) -> State:
	return _states.get(route, State.UNKNOWN)


## Returns [code]true[/code] when sending [param entity] traffic to
## [param peer_id] is meaningful: the entity is locally [constant State.LIVE]
## and the peer's committed [NetwInterestInterface] admission allows it.
##
## The verdict is a send gate, not a delivery guarantee. See the class
## description for the optimism contract.
func is_live_for(peer_id: int, entity: NetwEntity) -> bool:
	if state_of(entity) != State.LIVE:
		return false
	if peer_id == 1 or peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true

	var api := _api()
	if not api:
		return true

	var interest := api.interest
	if not interest.has_filter(entity):
		return true

	var admits := interest.committed_admits(entity)
	return admits.get(peer_id, 0) > 0


## Returns every peer currently passing [method is_live_for] for
## [param entity]. On a client this is always the server. This is the
## recipient list [NetwReplicationInterface] fans carriers out to.
func live_peers(entity: NetwEntity) -> Array[int]:
	var mt := _tree()
	if not mt:
		return []
	if not mt.is_server:
		return [1]

	var out: Array[int] = []
	for p in _api().get_peers():
		if is_live_for(p, entity):
			out.append(p)
	return out


## Runs [param cb] once [param route] reaches [constant State.LIVE].
##
## Runs immediately when the route is already live. Otherwise the callback
## waits for the binding and expires after [param timeout_ticks] with a
## warning. [code]0[/code] derives roughly one second from
## [member NetwClockInterface.tickrate]. [param on_timeout] runs when the
## wait expires instead, so a caller holding state keyed on the pending
## route can release it rather than leak it.
func when_live(
		route: int,
		cb: Callable,
		timeout_ticks: int = 0,
		on_timeout: Callable = Callable(),
) -> void:
	if route_state(route) == State.LIVE:
		cb.call()
		return

	var clock := _clock_interface()
	var tickrate := int(clock.tickrate) if clock else 30
	var timeout := timeout_ticks
	if timeout == 0:
		timeout = tickrate

	var deadline: int
	if clock:
		deadline = clock.tick + timeout
	else:
		deadline = _frame_counter + timeout

	var list := _pending_live.get_or_add(route, [])
	list.append({
		&"cb": cb,
		&"deadline": deadline,
		&"use_clock": clock != null,
		&"on_timeout": on_timeout,
	})


# Resolves the tick engine, or null while no configurator has registered, so
# callers fall back to their no-clock path against the inert interface.
func _clock_interface() -> NetwClockInterface:
	var api := _api()
	if api and api.clock.is_configured():
		return api.clock
	return null


# Executes pending callbacks for a live route.
func _flush_pending_live(route: int) -> void:
	var list: Array = _pending_live.get(route, [])
	_pending_live.erase(route)
	for item in list:
		var cb: Callable = item.get(&"cb")
		if cb.is_valid():
			cb.call()


# Sweeps expired pending callbacks.
func _sweep_pending_live() -> void:
	var clock := _clock_interface()
	var current_tick := clock.tick if clock else 0

	var expired_routes: Array[int] = []
	for route in _pending_live:
		var list: Array = _pending_live[route]
		var remaining: Array = []
		for item in list:
			var deadline: int = item.get(&"deadline")
			var use_clock: bool = item.get(&"use_clock")
			var current := current_tick if use_clock else _frame_counter
			if current >= deadline:
				var on_timeout: Callable = item.get(&"on_timeout", Callable())
				if on_timeout.is_valid():
					on_timeout.call()
				continue
			remaining.append(item)

		if remaining.is_empty():
			expired_routes.append(route)
		else:
			_pending_live[route] = remaining

	for route in expired_routes:
		_pending_live.erase(route)
		Netw.dbg.warn("NetwLivenessInterface: when_live timed out for route %d", [route])


# Handles entity owner tree exiting to transition to DEAD. Routes tracked by
# the replication spawn ledger resolve at end-of-frame instead, so a
# remove_child + add_child reparent in one frame never kills the route. See
# the reparent grace on NetwReplicationInterface.
func _on_entity_owner_tree_exiting(entity: NetwEntity) -> void:
	if entity.reparenting != null:
		return

	var route := route_of(entity)
	if route <= 0:
		return
	var api := _api()
	if api and api.replication.owns_spawned_route(route):
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
	_states[route] = State.LINGERING
	entity_lingering.emit(route, entity)


## Returns the count of [method when_live] callbacks still waiting for a
## binding, the liveness backlog surfaced by [InterestMonitor].
func pending_live_count() -> int:
	var total := 0
	for route in _pending_live:
		total += (_pending_live[route] as Array).size()
	return total


func _transition_to_dead(route: int) -> void:
	var entity := entity_of(route)
	_states[route] = State.DEAD
	_pending_live.erase(route)
	if entity:
		_entity_routes.erase(entity)
		_routes.erase(route)

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


func _tree() -> MultiplayerTree:
	var api := _api()
	return api.tree if api else null


# Authority comes from the API, never the tree. The API answers server offline
# and in a disconnected window (its _get_unique_id returns the server id), so a
# unit rig without a peer allocates just as a real server does.
func _is_server() -> bool:
	var api := _api()
	return api == null or api.is_server()
