class_name SteamService
extends Node

## Emitted when [method Steam.createLobby] completes.
signal lobby_created(connect_lobby: int, lobby_id: int)
## Emitted when [method Steam.joinLobby] completes.
signal lobby_joined(
	lobby_id: int, permissions: int, locked: bool, response: int
)
## Emitted when a Steam invite is received.
signal invite_received(lobby_id: int, friend_id: int)


# Singleton guard to prevent multiple SteamService instances.
static var _instance: WeakRef = weakref(null)

## The [SteamBackend] resource used when the [MultiplayerTree] does not
## already have a [SteamBackend] assigned.
@export var fallback_backend: SteamBackend

## If [code]true[/code], the service will automatically call
## [method MultiplayerTree.join] when a Steam invite is received.
@export var auto_join_on_invite: bool = true

var _wrapper: SteamWrapper
var _init_ok: bool = false


func _enter_tree() -> void:
	var existing: SteamService = _instance.get_ref()
	if existing and existing != self:
		push_error(
			"SteamService: Only one instance is allowed. " +
			"Queueing duplicate for deletion."
		)
		queue_free()
		return

	_instance = weakref(self)

	_wrapper = SteamWrapper.new()
	if _wrapper.is_available():
		var init_res: Dictionary = _wrapper.steam_init_ex()
		var status: int = init_res.get("status", 1)
		_init_ok = status == SteamWrapper.InitResult.OK
		if not _init_ok:
			var msg := "Steam init failed: %d" % status
			if status == SteamWrapper.InitResult.NO_STEAM_CLIENT:
				msg += " (No Steam Client)"
			Netw.dbg.error("SteamService: %s", [msg])

	NetwServices.register(self)

	if _wrapper.is_available():
		_wrapper.connect_signal("lobby_created", _on_lobby_created)
		_wrapper.connect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.connect_signal("join_requested", _on_join_requested)


func _exit_tree() -> void:
	var existing: SteamService = _instance.get_ref()
	if existing == self:
		_instance = weakref(null)

	if _wrapper and _wrapper.is_available():
		_wrapper.disconnect_signal("lobby_created", _on_lobby_created)
		_wrapper.disconnect_signal("lobby_joined", _on_lobby_joined)
		_wrapper.disconnect_signal("join_requested", _on_join_requested)

	NetwServices.unregister(self)


func _process(_dt: float) -> void:
	if _wrapper and _wrapper.is_available():
		_wrapper.run_callbacks()


## Returns the shared [SteamWrapper] instance.
func get_wrapper() -> SteamWrapper:
	return _wrapper


## Returns [code]true[/code] if Steam initialized successfully.
func is_ready() -> bool:
	return _init_ok and _wrapper != null and _wrapper.is_available()


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()

	var mt: MultiplayerTree = MultiplayerTree.resolve(self)
	if mt and mt.backend is SteamBackend and fallback_backend:
		warnings.append(
			"The MultiplayerTree already has a SteamBackend assigned. " +
			"The fallback_backend export is ambiguous and will be ignored."
		)

	return warnings


func _on_lobby_created(connect_lobby: int, lobby_id: int) -> void:
	lobby_created.emit(connect_lobby, lobby_id)


func _on_lobby_joined(
	lobby_id: int, permissions: int, locked: bool, response: int
) -> void:
	lobby_joined.emit(lobby_id, permissions, locked, response)


func _on_join_requested(lobby_id: int, friend_id: int) -> void:
	invite_received.emit(lobby_id, friend_id)

	var mt: MultiplayerTree = MultiplayerTree.resolve(self)
	if not mt:
		return

	mt.invite_received.emit(str(lobby_id), friend_id)

	if not auto_join_on_invite:
		return

	_auto_join(mt, lobby_id)


func _auto_join(mt: MultiplayerTree, lobby_id: int) -> void:
	if mt.state == MultiplayerTree.State.ONLINE:
		mt.disconnect_player()
		while mt.state != MultiplayerTree.State.OFFLINE:
			await mt.state_changed

	var backend: SteamBackend = null
	if mt.backend is SteamBackend:
		backend = mt.backend
	elif fallback_backend:
		backend = fallback_backend.duplicate()
		backend._copy_from(fallback_backend)
		mt.backend = backend
	else:
		Netw.dbg.warn(
			"SteamService: No SteamBackend available and no fallback set. " +
			"Cannot auto-join lobby %d.", [lobby_id]
		)
		return

	var username := _wrapper.get_persona_name()
	mt.join(str(lobby_id), username)
