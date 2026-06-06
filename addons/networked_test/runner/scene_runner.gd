## Slot scoped [GdUnitSceneRunner] for one [ParticipantSlot].
##
## All inherited input simulation methods route through [member slot].
## Session wide time and stepping are owned by [NetwGameHarness].
class_name NetwSceneRunner
extends GdUnitSceneRunnerImpl

## Adopted [MultiplayerTree] inside [member scene].
var tree: MultiplayerTree

## Frame anchored waiter used by [method await_player] and [method await_scene].
var waiter: NetwWaiter

## Viewport slot wrapping this runner's scene.
var slot: ParticipantSlot

## Peer id assigned by the active [MultiplayerPeer].
var peer_id: int = 0

## Username used to join the session.
var username: StringName = &""

## Mirrors [member MultiplayerTree.local_player].
var local_player: Node:
	get:
		return tree.local_player if tree else null


func _init(
		p_scene: Node,
		p_slot: ParticipantSlot,
		p_username: StringName,
		p_verbose: bool = false,
) -> void:
	slot = p_slot
	username = p_username
	_verbose = p_verbose
	_saved_iterations_per_second = Engine.get_physics_ticks_per_second()
	_time_factor = 1.0
	_current_scene = p_scene
	assert(_current_scene != null, "NetwSceneRunner: scene must not be null.")

	slot.add_child(_current_scene)
	slot.child_exiting_tree.connect(_on_slot_child_exiting)
	_simulate_start_time = LocalTime.now()


func _notification(what: int) -> void:
	if what != NOTIFICATION_PREDELETE or _is_disposed:
		return

	if is_instance_valid(_current_scene):
		_current_scene.process_mode = Node.PROCESS_MODE_DISABLED
		_mouse_button_on_press.clear()
		_key_on_press.clear()
		_action_on_press.clear()
		_last_input_event = null
		if _current_scene.get_parent():
			_current_scene.get_parent().remove_child(_current_scene)
		if _scene_auto_free:
			_current_scene.free()

	_is_disposed = true
	_current_scene = null


## Returns a node at [param path] inside this peer's scene.
func find(path: NodePath) -> Node:
	return scene().get_node_or_null(path)


## Returns the player named [param player_username] in this peer's scenes.
func find_player(player_username: StringName) -> Node:
	if not tree:
		return null

	var player_name := StringName(str(player_username))
	for player in tree.get_all_players():
		if _player_matches_username(player, player_name):
			return player

	var sm := tree.get_service(MultiplayerSceneManager)
	if not sm:
		return null

	for active_scene: MultiplayerScene in sm.active_scenes.values():
		for player in active_scene.get_players():
			if _player_matches_username(player, player_name):
				return player
	return null


## Awaits [param player_username] in this peer's active scenes.
func await_player(
		player_username: StringName,
		timeout: float = 1.0,
) -> Node:
	await _active_waiter().until(
		func() -> bool:
			return find_player(player_username) != null,
		"player '%s' in '%s'" % [player_username, username],
		timeout,
	)
	return find_player(player_username)


## Awaits an active [MultiplayerScene] named [param scene_name].
func await_scene(
		scene_name: StringName,
		timeout: float = 1.0,
) -> MultiplayerScene:
	await _active_waiter().until(
		func() -> bool:
			return _find_active_scene(scene_name) != null,
		"scene '%s' in '%s'" % [scene_name, username],
		timeout,
	)
	return _find_active_scene(scene_name)


func set_time_factor(_time_factor: float = 1.0) -> GdUnitSceneRunner:
	GdAssertReports.report_error("use NetwGameHarness.set_time_factor", -1)
	return self


func simulate_frames(
		_frames: int,
		_delta_milli: int = -1,
) -> GdUnitSceneRunner:
	GdAssertReports.report_error("use NetwGameHarness.sync_ticks", -1)
	return self


func _handle_input_event(event: InputEvent) -> GdUnitSceneRunner:
	if is_instance_valid(_current_scene) \
			and _current_scene.process_mode != Node.PROCESS_MODE_DISABLED:
		slot.send_input(event)
	_last_input_event = event
	return self


func _reset_input_to_default() -> void:
	for button: int in _mouse_button_on_press.duplicate():
		simulate_mouse_button_release(button)
	_mouse_button_on_press.clear()

	for key_code: int in _key_on_press.duplicate():
		simulate_key_release(key_code)
	_key_on_press.clear()

	for action: String in _action_on_press.duplicate():
		simulate_action_release(action)
	_action_on_press.clear()

	_last_input_event = null


func _find_active_scene(scene_name: StringName) -> MultiplayerScene:
	if not tree:
		return null
	var sm := tree.get_service(MultiplayerSceneManager)
	if not sm:
		return null
	return sm.active_scenes.get(scene_name) as MultiplayerScene


func _active_waiter() -> NetwWaiter:
	if waiter == null:
		waiter = NetwWaiter.new(_scene_tree(), _default_reporter)
	return waiter


func _default_reporter(label: String, timeout: float) -> void:
	push_error("Timed out waiting for '%s' after %.2fs." % [label, timeout])


func _player_matches_username(
		player: Node,
		player_username: StringName,
) -> bool:
	var entity := NetwEntity.of(player)
	if entity and entity.entity_id == player_username:
		return true
	return StringName(NetwEntity.parse_entity(player.name)) == player_username


func _on_slot_child_exiting(child: Node) -> void:
	if child != _current_scene:
		return
	_current_scene.process_mode = Node.PROCESS_MODE_DISABLED
	_mouse_button_on_press.clear()
	_key_on_press.clear()
	_action_on_press.clear()
	_last_input_event = null
