## Pre-lobby UI: lobby browser + create form.
##
## Bound exclusively to [LobbyProvider]. Has no knowledge of [NetwTree],
## [NetwContext], or any session-level API - those concepts only become
## meaningful after a lobby has been entered.
extends Control

signal status_message(text: String)

@onready var _name_edit: LineEdit = %NameEdit
@onready var _max_spin: SpinBox = %MaxSpin
@onready var _host_btn: Button = %HostButton
@onready var _refresh_btn: Button = %RefreshButton
@onready var _list: ItemList = %LobbyList

var _provider: LobbyProvider
var _pending_lobby_action: bool = false


## Binds the pre-lobby controls to [param provider].
func setup(provider: LobbyProvider) -> void:
	_provider = provider
	if _provider == null:
		_host_btn.disabled = true
		_refresh_btn.disabled = true
		status_message.emit("No LobbyProvider registered.")
		return

	_provider.lobby_list_updated.connect(_on_list_updated)
	_provider.lobby_join_started.connect(_on_join_started)
	_provider.lobby_join_failed.connect(_on_join_failed)
	_provider.provider_unavailable.connect(_on_unavailable)
	_host_btn.pressed.connect(_on_host_pressed)
	_refresh_btn.pressed.connect(_on_refresh_pressed)
	_list.item_activated.connect(_on_list_item_activated)

	if "max_clients" in _provider:
		_max_spin.value = _provider.get("max_clients")

	if _provider.is_join_pending():
		_set_pending_lobby_action(true)
	_provider.list_lobbies()


## Enables browser actions after a failed or cancelled lobby transition.
func reset_buttons() -> void:
	_set_pending_lobby_action(false)


## Re-requests the lobby list. Call when re-entering pre-lobby after a
## disconnect or game-end.
func refresh() -> void:
	if _provider:
		_list.clear()
		_provider.list_lobbies()


func _on_host_pressed() -> void:
	var lobby_name := _name_edit.text.strip_edges()
	if lobby_name.is_empty():
		lobby_name = "Bomber Lobby"
	_set_pending_lobby_action(true)
	status_message.emit("Creating lobby...")
	_provider.create_lobby(lobby_name)


func _on_refresh_pressed() -> void:
	if _pending_lobby_action:
		return
	_list.clear()
	_provider.list_lobbies()


func _on_list_item_activated(idx: int) -> void:
	if _pending_lobby_action:
		return
	var id := int(_list.get_item_metadata(idx))
	_set_pending_lobby_action(true)
	status_message.emit("Joining lobby...")
	_provider.join_lobby(id)


func _on_list_updated(lobbies: Array[LobbyInfo]) -> void:
	_list.clear()
	for info in lobbies:
		var display := info.lobby_name if not info.lobby_name.is_empty() \
			else "(unnamed)"
		var label := "%s  -  %d/%d" % [display, info.players, info.max_players]
		var host := info.metadata.get("host", "") as String
		if not host.is_empty():
			label += "   hosted by %s" % host
		var idx := _list.add_item(label)
		_list.set_item_metadata(idx, info.id)
		_list.set_item_disabled(idx, _pending_lobby_action)


func _on_join_started(_lobby_id: int) -> void:
	_set_pending_lobby_action(true)
	status_message.emit("Joining lobby...")


func _on_join_failed(reason: String) -> void:
	_set_pending_lobby_action(false)
	status_message.emit(reason)


func _on_unavailable(reason: String) -> void:
	_set_pending_lobby_action(true)
	status_message.emit("Steam unavailable: %s" % reason)


# Toggles controls that can create overlapping lobby transitions.
func _set_pending_lobby_action(pending: bool) -> void:
	_pending_lobby_action = pending
	_host_btn.disabled = pending
	_refresh_btn.disabled = pending
	for idx in range(_list.get_item_count()):
		_list.set_item_disabled(idx, pending)
