## Stable entity-level interest configuration owned by
## [member NetwEntity.interest].
##
## Membership declarations survive tree exits and reapply when the entity
## enters a session. The server owns the real [NetwInterestLayer] membership.
## Clients keep the same labels and callback surface for local visibility and
## observer-awareness events.
## [codeblock]
## var interest := NetwEntity.resolve(self).interest
## interest.join(&"team:red")
## interest.on_enter(&"team:red", _on_visible)
## [/codeblock]
class_name NetwInterestHandle
extends RefCounted

## Wire behavior when a layer stops admitting an entity to a peer. Mirrors
## [enum NetwMultiplayer.LeavePolicy].
const LeavePolicy := NetwMultiplayer.LeavePolicy

## Local presentation behavior when the participant row stops admitting an
## entity that remains present in this process. Mirrors
## [enum NetwMultiplayer.PerceptionPolicy].
const PerceptionPolicy := NetwMultiplayer.PerceptionPolicy

var _entity_ref: WeakRef
var _service_ref: WeakRef
var _decl := NetwInterestDecl.new()
var _enter_callbacks: Dictionary[StringName, Array] = { }
var _leave_callbacks: Dictionary[StringName, Array] = { }
var _observed_callbacks: Array[Callable] = []
var _unobserved_callbacks: Array[Callable] = []


# Binds the handle once and installs its entity lifecycle hooks.
func _bind(entity: NetwEntity) -> void:
	assert(entity != null, "NetwInterestHandle._bind: entity is null")
	var current := _entity()
	assert(
		current == null or current == entity,
		"NetwInterestHandle cannot be rebound to another entity",
	)
	if current == entity:
		return
	_entity_ref = weakref(entity)
	var root := entity.owner
	if not is_instance_valid(root):
		return
	if not root.tree_entered.is_connected(_activate):
		root.tree_entered.connect(_activate)
	if not root.tree_exiting.is_connected(_deactivate):
		root.tree_exiting.connect(_deactivate)
	if not entity.observer_entered.is_connected(_on_observer_entered):
		entity.observer_entered.connect(_on_observer_entered)
	if not entity.observer_left.is_connected(_on_observer_left):
		entity.observer_left.connect(_on_observer_left)
	if root.is_inside_tree():
		_activate()


## Adds the entity to [param layer_id]. Idempotent.
##
## The declaration is safe in [method Object._init] on every peer. Only
## server authority mutates the live [NetwInterestLayer] entity set.
func join(layer_id: StringName) -> NetwInterestHandle:
	if not _decl.join(layer_id):
		return self
	var api := _flat_api()
	var entity := _entity()
	if api and api.is_server() and entity:
		var layer := api.layer_create(layer_id)
		api.layer_add_entity(layer, api.entity_of(entity.owner))
	return self


## Removes the entity from [param layer_id]. Idempotent.
func leave(layer_id: StringName) -> NetwInterestHandle:
	if not _decl.leave(layer_id):
		return self
	var api := _flat_api()
	var entity := _entity()
	if api and api.is_server() and entity:
		var layer := api.layer_find(layer_id)
		api.layer_remove_entity(layer, entity.rid)
	return self


## Returns a copy of the locally known layer labels.
func layer_ids() -> Array[StringName]:
	var out: Array[StringName] = []
	out.assign(_decl.labels())
	return out


## Returns whether [param peer_id] currently sees this entity.
func is_visible_to(peer_id: int) -> bool:
	var api := _flat_api()
	var entity := _entity()
	if not api or not entity:
		return false
	if not api.interest_is_filtered(entity.rid):
		return true
	return api.interest_admits(entity.rid, peer_id)


## Calls [param callback] with [code](layer_id, peer_id)[/code] whenever this
## entity becomes visible through [param layer_id].
func on_enter(
		layer_id: StringName,
		callback: Callable,
) -> NetwInterestHandle:
	_assert_callback("on_enter", callback)
	var callbacks: Array = _enter_callbacks.get_or_add(layer_id, [])
	if callback not in callbacks:
		callbacks.append(callback)
	return self


## Calls [param callback] with [code](layer_id, peer_id)[/code] whenever this
## entity stops being visible through [param layer_id].
func on_leave(
		layer_id: StringName,
		callback: Callable,
) -> NetwInterestHandle:
	_assert_callback("on_leave", callback)
	var callbacks: Array = _leave_callbacks.get_or_add(layer_id, [])
	if callback not in callbacks:
		callbacks.append(callback)
	return self


## Calls [param callback] with [code](peer_id)[/code] when another peer
## starts observing this entity. Registering opts the entity into the
## owner-awareness relay.
func on_observed(callback: Callable) -> NetwInterestHandle:
	_assert_callback("on_observed", callback)
	_decl.reports_observers = true
	if callback not in _observed_callbacks:
		_observed_callbacks.append(callback)
	return self


## Calls [param callback] with [code](peer_id)[/code] when another peer
## stops observing this entity. Registering opts the entity into the
## owner-awareness relay.
func on_unobserved(callback: Callable) -> NetwInterestHandle:
	_assert_callback("on_unobserved", callback)
	_decl.reports_observers = true
	if callback not in _unobserved_callbacks:
		_unobserved_callbacks.append(callback)
	return self


## Overrides the leave behavior for [param layer_id].
##
## [enum LeavePolicy].CUSTOM requires [param custom_callback], called with
## [code](peer_id, layer_id)[/code] on server authority. Other policies
## reject a callback so configuration mistakes fail at declaration time.
func on_leave_policy(
		layer_id: StringName,
		policy: LeavePolicy,
		custom_callback: Callable = Callable(),
) -> NetwInterestHandle:
	_decl.set_leave_policy(layer_id, policy, custom_callback)
	return self


## Overrides local presentation behavior for [param layer_id].
##
## [enum PerceptionPolicy].CUSTOM requires [param custom_callback]. The
## callback receives [code](visible, peer_id, layer_id)[/code] on both local
## participant edges. Other policies reject a callback.
func on_perception_policy(
		layer_id: StringName,
		policy: PerceptionPolicy,
		custom_callback: Callable = Callable(),
) -> NetwInterestHandle:
	if not _decl.set_perception_policy(layer_id, policy, custom_callback):
		return self
	var service := _service()
	var entity := _entity()
	if service and entity:
		service._reapply_local_perception(entity)
	return self


# Enables or disables observer carriage for builder and migration paths.
func _set_report_observers(enabled: bool) -> void:
	_decl.reports_observers = enabled


# Returns whether the server should carry observer-awareness events.
func _reports_observers() -> bool:
	return _decl.reports_observers


# Resolves the entity override before a layer default.
func _leave_policy_for(layer_id: StringName, fallback: LeavePolicy) -> int:
	return _decl.leave_policy_for(layer_id, fallback)


# Returns the callback configured for one CUSTOM layer.
func _custom_leave_for(layer_id: StringName) -> Callable:
	return _decl.custom_leave_for(layer_id)


# Resolves the entity perception override before a layer default.
func _perception_policy_for(
		layer_id: StringName,
		fallback: PerceptionPolicy,
) -> int:
	return _decl.perception_policy_for(layer_id, fallback)


# Returns the callback configured for one CUSTOM perception layer.
func _custom_perception_for(layer_id: StringName) -> Callable:
	return _decl.custom_perception_for(layer_id)


# Adds a server-authored label learned from an unbound-layer relay.
func _client_join_label(layer_id: StringName) -> void:
	_decl.join(layer_id)


# Dispatches a layer-specific enter transition for this entity.
func _dispatch_enter(layer_id: StringName, peer_id: int) -> void:
	_emit_callbacks(_enter_callbacks.get(layer_id, []), layer_id, peer_id)


# Dispatches a layer-specific leave transition for this entity.
func _dispatch_leave(layer_id: StringName, peer_id: int) -> void:
	_emit_callbacks(_leave_callbacks.get(layer_id, []), layer_id, peer_id)


# Activates declared memberships and callback bindings for this session.
func _activate() -> void:
	var entity := _entity()
	if not entity or not is_instance_valid(entity.owner):
		return
	var api: NetwMultiplayer = entity.multiplayer
	if not api:
		api = NetwMultiplayer.of(entity.owner)
	if not api:
		return
	_service_ref = weakref(api._interest)
	for layer_id: StringName in _decl.labels():
		var layer := api.layer_create(layer_id)
		if api.is_server():
			api.layer_add_entity(layer, api.entity_of(entity.owner))


# Removes live memberships and session signal bindings on tree exit.
func _deactivate() -> void:
	var api := _flat_api()
	var entity := _entity()
	if api and api.is_server() and entity:
		for layer_id: StringName in _decl.labels():
			var layer := api.layer_find(layer_id)
			api.layer_remove_entity(layer, entity.rid)
	_service_ref = null


# Dispatches owner-awareness enter callbacks.
func _on_observer_entered(
		_layer_id: StringName,
		peer_id: int,
) -> void:
	for callback in _observed_callbacks:
		callback.call(peer_id)


# Dispatches owner-awareness leave callbacks.
func _on_observer_left(
		_layer_id: StringName,
		peer_id: int,
) -> void:
	for callback in _unobserved_callbacks:
		callback.call(peer_id)


# Returns the bound entity while it is alive.
func _entity() -> NetwEntity:
	return _entity_ref.get_ref() as NetwEntity if _entity_ref else null


# Returns the active session service while it is alive.
func _service() -> InterestCore:
	return _service_ref.get_ref() as InterestCore \
	if _service_ref else null


# Returns the flat session api behind the compatibility service.
func _flat_api() -> NetwMultiplayer:
	var service := _service()
	return service._api() if service else null


# Validates a callback at registration time.
func _assert_callback(source: String, callback: Callable) -> void:
	assert(
		callback.is_valid(),
		"NetwInterestHandle.%s: callback is invalid" % source,
	)


# Calls layer callbacks with their documented argument pair.
func _emit_callbacks(
		callbacks: Array,
		layer_id: StringName,
		peer_id: int,
) -> void:
	for callback: Callable in callbacks:
		callback.call(layer_id, peer_id)
