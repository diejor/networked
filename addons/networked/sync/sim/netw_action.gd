## Predicted discrete action bound to one authority method.
##
## [NetwAction] pairs a local [member predict] effect with a reliable server
## request. The server receives a [NetwAction.Context], validates at
## [member NetwAction.Context.view_tick], and either binds an authoritative
## [MultiplayerEntity] result with [method NetwAction.Context.bind] or denies it
## with [method NetwAction.Context.deny].
##
## [codeblock]
## @onready var lag := Netw.ctx(self).lag_compensation
## @onready var place_bomb := lag.action(_place_bomb)
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

## Emitted when the authoritative result adopts the optimistic effect.
signal confirmed

## Emitted when the server denies the optimistic effect or it times out.
signal denied

## Creates the local optimistic effect. It should return the ghost [Node].
var predict: Callable

## Reverts the optimistic effect. Defaults to [method Node.queue_free].
var revert: Callable = Callable()

## Number of ticks before an unresolved request reverts. [code]0[/code] derives
## a conservative default from [LagCompensationService].
var timeout_ticks: int = 0

var _lag: NetwLagCompensation
var _authority: Callable
var _entity: NetwEntity
var _target_path := NodePath("")
var _method := &""
var _slot := 0


func _init(
		lag: NetwLagCompensation,
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
	var service := _service()
	if not service:
		return

	var target := _authority.get_object() as Node
	if target and target.is_inside_tree():
		_target_path = _tree_relative_path(target)
	if _target_path.is_empty():
		return

	var key := _lag.effects.key_for(_entity, view_tick, _slot)
	var ghost: Node = null
	if predict.is_valid():
		ghost = predict.call() as Node
	var revert_callable := _revert_callable(ghost)
	_lag.effects.arm(key, revert_callable, timeout_ticks)
	service._watch_action(
		key,
		func() -> void:
			if is_instance_valid(ghost):
				ghost.queue_free()
			_emit_confirmed(),
		_emit_denied,
	)
	service._send_action_request(_target_path, _method, view_tick, data, key)


func _revert_callable(ghost: Node) -> Callable:
	if revert.is_valid():
		return revert.bind(ghost)
	return func() -> void:
		if is_instance_valid(ghost):
			ghost.queue_free()


func _service() -> LagCompensationService:
	return _lag._service() if _lag else null


func _emit_confirmed() -> void:
	confirmed.emit()


func _emit_denied() -> void:
	denied.emit()


func _tree_relative_path(target: Node) -> NodePath:
	var mt := MultiplayerTree.resolve(target)
	if not mt:
		return NodePath("")
	return mt.get_path_to(target)


## Server-side action request context.
##
## The context carries the requester, clamped view tick, and correlation key.
## Authority methods call [method bind] before adding a spawned
## [MultiplayerEntity] to the tree, or [method deny] when validation rejects the
## request.
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

	var _key: StringName = &""
	var _service_ref: WeakRef
	var _denied := false
	var _bound := false


	func _init(
			service: LagCompensationService,
			p_requester: int,
			p_view_tick: int,
			p_key: StringName,
	) -> void:
		_service_ref = weakref(service) if service else null
		requester = p_requester
		view_tick = p_view_tick
		_key = p_key


	## Binds [param node] so its [member NetwEntity.entity_id] confirms this
	## action when the authoritative spawn arrives.
	func bind(node: Node) -> void:
		NetwEntity.bind(node, _key, 0)
		_bound = true


	## Denies this action and asks the requesting peer to revert it.
	func deny() -> void:
		if _denied:
			return
		_denied = true
		var service := _service()
		if service:
			service._deny_action_to(requester, _key)


	func _service() -> LagCompensationService:
		return (
				_service_ref.get_ref() as LagCompensationService
				if _service_ref else null
		)
