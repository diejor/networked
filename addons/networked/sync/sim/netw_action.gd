## Predicted discrete action bound to one authority method.
##
## [NetwAction] pairs a local [member predict] effect with a reliable server
## request. The server receives a [NetwAction.Context], validates at
## [member NetwAction.Context.view_tick], and either binds an authoritative
## spawned [NetwEntity] result with [method NetwAction.Context.bind] or denies
## it with [method NetwAction.Context.deny].
##
## [codeblock]
## @onready var api := Netw.of(self)
## @onready var place_bomb := api.lagcomp_action(_place_bomb)
##
## func _ready() -> void:
##     place_bomb.predict = _predict_bomb
##
## func _network_tick(delta, tick, is_fresh) -> void:
##     if is_fresh and inputs.bombing:
##         place_bomb.request(tick)
##
## func _place_bomb(ctx: NetwAction.Context) -> void:
##     var bomb := BOMB.instantiate()
##     ctx.bind(bomb)
##     $Bombs.add_child(bomb)
## [/codeblock]
##
## [method request] is a player request. The server authority method must be a
## plain [Callable], not an RPC.
class_name NetwAction
extends RefCounted

## Server execution policy for the authoritative action.
enum TimingMode {
	## Execute on the server when [member NetwAction.Context.view_tick] arrives.
	TICK_ALIGNED,
	## Execute after server history records
	## [member NetwAction.Context.view_tick].
	##
	## This mode is owner-anchored. It guarantees only that the action owner's
	## recorded state is ready at the view tick. It does not gate other entities.
	## Cross-entity validation must use [method LagCompCore.sample] or
	## [method LagCompCore.rewind] for those targets.
	##
	## Determinism is a precondition, not a toggle. The placement agrees with the
	## client only when consuming the same input yields the same state. Resolution
	## is deferred until that state exists, so remote peers see the result later.
	## Use [constant IMMEDIATE] when the action must not wait.
	##
	## Under loss, the consume policy may fill a missing input slot. This mode
	## guarantees a recorded state exists, not that it came from the real input.
	## [codeblock]
	## action.timing_mode = NetwAction.TimingMode.TICK_ALIGNED_STATE_READY
	## [/codeblock]
	TICK_ALIGNED_STATE_READY,
	## Execute as soon as the request reaches the server.
	IMMEDIATE,
}

## Emitted when the authoritative result adopts the optimistic effect.
signal confirmed

## Emitted when the server denies the optimistic effect or it times out.
signal denied

## Creates the local optimistic effect. It should return the ghost [Node].
var predict: Callable

## Reverts the optimistic effect. Defaults to [method Node.queue_free].
var revert: Callable = Callable()

## Confirms the optimistic effect. Defaults to [method Node.queue_free].
var confirm: Callable = Callable()

## Number of ticks before an unresolved request reverts. [code]0[/code] derives
## a conservative default from [LagCompensation].
var timeout_ticks: int = 0

## Server execution policy. Defaults to arrival-time execution with no readiness
## assumptions. Opt into stricter modes per action.
var timing_mode := TimingMode.IMMEDIATE

var _lag: LagCompCore
var _authority: Callable
var _entity: NetwEntity
var _target_path := NodePath("")
var _method := &""
var _slot := 0


func _init(
		lag: LagCompCore,
		authority: Callable,
		slot: int,
) -> void:
	_lag = lag
	_authority = authority
	_slot = slot
	_method = authority.get_method()
	var target := authority.get_object() as Node
	if target:
		_entity = NetwEntity.of(target)
		if target.is_inside_tree():
			_target_path = _tree_relative_path(target)


## Requests the server authority method for [param view_tick].
##
## The local controller gets an immediate [member predict] effect. Non-owning
## peers do nothing, so server consume and remote display passes cannot double
## fire a command.
##
## [br][br][b]Player request.[/b]
func request(view_tick: int, data: Variant = null) -> void:
	if not _entity or not _entity.is_controlled_locally:
		return
	if not _lag or not _lag.is_configured():
		return

	var target := _authority.get_object() as Node
	if target and target.is_inside_tree():
		_target_path = _tree_relative_path(target)
	if _target_path.is_empty():
		return

	var api := _lag._api()
	if not api:
		return

	var key := api.effect_key(_entity.rid, view_tick, _slot)
	var ghost: Node = null
	if predict.is_valid():
		ghost = predict.call() as Node
	var revert_callable := _revert_callable(ghost)
	var confirm_callable := _confirm_callable(ghost)
	api.effect_arm(key, revert_callable, timeout_ticks)
	api.effect_watch(
		key,
		func() -> void:
			confirm_callable.call()
			_emit_confirmed(),
		_emit_denied,
	)
	_lag._send_action_request(
		_target_path,
		_method,
		view_tick,
		data,
		key,
		timing_mode,
	)


func _revert_callable(ghost: Node) -> Callable:
	var ghost_ref := weakref(ghost) if ghost else null
	if revert.is_valid():
		return func() -> void:
			var node := ghost_ref.get_ref() as Node if ghost_ref else null
			if is_instance_valid(node):
				revert.call(node)
	return func() -> void:
		var node := ghost_ref.get_ref() as Node if ghost_ref else null
		if is_instance_valid(node):
			node.queue_free()


func _confirm_callable(ghost: Node) -> Callable:
	var ghost_ref := weakref(ghost) if ghost else null
	if confirm.is_valid():
		return func() -> void:
			var node := ghost_ref.get_ref() as Node if ghost_ref else null
			if is_instance_valid(node):
				confirm.call(node)
	return func() -> void:
		var node := ghost_ref.get_ref() as Node if ghost_ref else null
		if is_instance_valid(node):
			node.queue_free()


func _emit_confirmed() -> void:
	confirmed.emit()


func _emit_denied() -> void:
	denied.emit()


func _tree_relative_path(target: Node) -> NodePath:
	var api := NetwMultiplayer.of(target)
	if api == null or api.root == null:
		return NodePath("")
	return api.root.get_path_to(target)


## Server-side action request context.
##
## The context carries the requester, clamped view tick, and correlation key.
## Authority methods call [method bind] before adding a spawned entity to the
## tree, or [method deny] when validation rejects the request.
##
## [codeblock]
## func _place_bomb(ctx: NetwAction.Context) -> void:
##     var past := lag.sample(entity, ctx.view_tick)
##     if past.is_empty():
##         ctx.deny()
##         return
##     var bomb := BOMB.instantiate()
##     ctx.bind(bomb)
##     bombs.add_child(bomb)
## [/codeblock]
class Context extends RefCounted:
	## Peer id that sent the request.
	var requester: int = 0

	## Tick used for server validation.
	var view_tick: int = 0

	## Tick the requester originally asked the server to evaluate.
	var requested_tick: int = 0

	## Server tick that ran the authority method.
	var execution_tick: int = 0

	var _key: StringName = &""
	var _service_ref: WeakRef
	var _denied := false
	var _bound := false


	func _init(
			service: LagCompCore,
			p_requester: int,
			p_view_tick: int,
			p_requested_tick: int,
			p_execution_tick: int,
			p_key: StringName,
	) -> void:
		_service_ref = weakref(service) if service else null
		requester = p_requester
		view_tick = p_view_tick
		requested_tick = p_requested_tick
		execution_tick = p_execution_tick
		_key = p_key


	## Binds [param node] so its [member NetwEntity.entity_id] confirms this
	## action when the authoritative spawn arrives.
	func bind(node: Node) -> void:
		NetwEntity.bind(node, _key, 0)
		var entity := NetwEntity.ensure(node)
		if entity:
			entity.action_spawn_tick = view_tick
			entity.action_requester = requester
		_bound = true


	## Denies this action and asks the requesting peer to revert it.
	func deny() -> void:
		if _denied:
			return
		_denied = true
		var service := _service()
		if service:
			service._deny_action_to(requester, _key)


	func _service() -> LagCompCore:
		return (
				_service_ref.get_ref() as LagCompCore
				if _service_ref else null
		)
