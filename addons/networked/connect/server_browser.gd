## Reference server browser. Lists direct [JoinTarget]s loaded from a
## persisted [ServerList] alongside provider-supplied lobbies, probes
## direct rows with a [ProbeManager], and dispatches joins through
## [MultiplayerTree].
##
## [member backend_templates] populates the Add-Server popup. Add
## providers via [method register_provider] after instancing the scene;
## the browser refreshes their lobby list on [signal _ready] and on
## Refresh.
@tool
class_name ServerBrowser
extends Control


const _ROW_SCENE := preload(
	"res://addons/networked/connect/server_browser_row.tscn"
)


## Backends offered by the Add-Server popup.
@export var backend_templates: Array[BackendPeer] = []

## Spawner candidates the user picks from in the footer. Each entry is
## stamped onto [member JoinPayload.spawner_component_path] at dispatch
## time so the server knows where to place the joining player. Leave
## empty to dispatch without a spawner (server falls back to its
## default).
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:SpawnerComponent",
)
var spawner_options: Array[SceneNodePath] = []

## Path to the [MultiplayerTree] joined direct/provider rows feed.
@export var tree_path: NodePath

## When [code]true[/code], hides this browser after the local player enters
## an active scene and shows it again when [member tree_path] disconnects or
## returns offline.
@export var hide_when_session_active: bool = true:
	set(value):
		hide_when_session_active = value
		if not is_inside_tree():
			return
		if value:
			_bind_session_visibility_signals(_resolve_tree())
			_sync_session_visibility()
		else:
			_unbind_session_visibility_signals()
			show()

## Path used for [ServerList] persistence.
@export var server_list_path: String = ServerList.DEFAULT_PATH


var _list: ServerList
var _registry: ProviderRegistry
var _probes: ProbeManager
var _popup: ServerBrowserPopup
var _host_popup: ServerBrowserHostPopup
var _join_popup: ServerBrowserJoinPopup
var _selected_row: ServerBrowserRow
var _provider_results: Dictionary = {}
var _direct_rows: Array[ServerBrowserRow] = []
var _tree_cache: MultiplayerTree
var _visibility_tree: MultiplayerTree
var _last_username: String = "Player"
var _pending_join_target: JoinTarget
var _pending_host_kind: int = -1
var _pending_host_choice: Variant
var _pending_host_display_name: String = ""


@onready var _count_label: Label = %CountLabel
@onready var _refresh_button: Button = %RefreshButton
@onready var _add_button: Button = %AddButton
@onready var _host_button: Button = %HostButton
@onready var _banner: HBoxContainer = %Banner
@onready var _banner_label: Label = %BannerLabel
@onready var _list_box: VBoxContainer = %ListBox
@onready var _empty_state: VBoxContainer = %EmptyState
@onready var _details_label: Label = %DetailsLabel
@onready var _edit_button: Button = %EditButton
@onready var _remove_button: Button = %RemoveButton
@onready var _join_button: Button = %JoinButton


func _ready() -> void:
	_list = ServerList.load_or_new(server_list_path)

	_probes = ProbeManager.new()
	add_child(_probes)

	_registry = ProviderRegistry.new()
	add_child(_registry)

	_popup = preload(
		"res://addons/networked/connect/server_browser_popup.tscn"
	).instantiate()
	add_child(_popup)
	_popup.submitted.connect(_on_popup_submitted)

	_host_popup = preload(
		"res://addons/networked/connect/server_browser_host_popup.tscn"
	).instantiate()
	add_child(_host_popup)
	_host_popup.submitted.connect(_on_host_submitted)

	_join_popup = preload(
		"res://addons/networked/connect/server_browser_join_popup.tscn"
	).instantiate()
	add_child(_join_popup)
	_join_popup.set_spawner_options(spawner_options)
	_join_popup.submitted.connect(_on_join_submitted)

	_refresh_button.pressed.connect(refresh)
	_add_button.pressed.connect(_on_add_pressed)
	_host_button.pressed.connect(_on_host_pressed)
	_edit_button.pressed.connect(_on_edit_pressed)
	_remove_button.pressed.connect(_on_remove_pressed)
	_join_button.pressed.connect(_on_join_pressed)

	_clear_selection()
	_bind_session_visibility_signals(_resolve_tree())
	_sync_session_visibility()
	refresh()


# Clears session visibility hooks before the browser leaves the tree.
func _exit_tree() -> void:
	_unbind_session_visibility_signals()


## Registers [param provider] under [param id] so its lobbies show as
## rows. Must be called before [method refresh] for the provider's
## rows to appear.
func register_provider(id: StringName, provider: LobbyProvider) -> void:
	if _registry == null:
		_registry = ProviderRegistry.new()
		add_child(_registry)
	_registry.register(id, provider)
	if not provider.lobby_list_updated.is_connected(
		_on_provider_list_updated
	):
		provider.lobby_list_updated.connect(
			_on_provider_list_updated.bind(id)
		)
	if not provider.provider_unavailable.is_connected(_show_banner):
		provider.provider_unavailable.connect(_show_banner)


## Re-runs probes for all direct rows and refreshes provider lobby
## lists.
func refresh() -> void:
	_probes.cancel_all()
	_provider_results.clear()
	_rebuild_rows()
	for id in _registry.list_providers():
		_provider_results[id] = []
		var provider := _registry.get_provider(id)
		if provider:
			provider.list_lobbies()


func _rebuild_rows() -> void:
	for child in _list_box.get_children():
		child.queue_free()
	_direct_rows.clear()

	var direct := _list.targets.filter(func(t): return t.is_direct())
	var total := direct.size()
	for id in _provider_results.keys():
		total += (_provider_results[id] as Array).size()

	_empty_state.visible = total == 0
	_count_label.text = str(total)
	if total == 0:
		return

	if not direct.is_empty():
		_add_section_header("DIRECT")
		for target in direct:
			var row := _instance_row(target)
			_direct_rows.append(row)
			_probes.query(target, _on_probe_result.bind(row))

	for id in _provider_results.keys():
		var lobbies: Array = _provider_results[id]
		if lobbies.is_empty():
			continue
		_add_section_header(String(id).to_upper())
		for lobby in lobbies:
			var target := _lobby_to_target(id, lobby as LobbyInfo)
			var row := _instance_row(target)
			row.set_result(ServerInfoResult.ok(_lobby_info(lobby)))


func _instance_row(target: JoinTarget) -> ServerBrowserRow:
	var row := _ROW_SCENE.instantiate() as ServerBrowserRow
	_list_box.add_child(row)
	row.bind_target(target)
	row.selected.connect(_on_row_selected.bind(row))
	return row


func _add_section_header(text: String) -> void:
	var label := Label.new()
	label.text = text
	label.add_theme_constant_override("outline_size", 0)
	_list_box.add_child(label)


func _lobby_to_target(provider_id: StringName, lobby: LobbyInfo) -> JoinTarget:
	var t := JoinTarget.new()
	t.provider_id = provider_id
	t.remote_id = lobby.id
	t.display_name = lobby.lobby_name
	t.metadata = lobby.metadata
	return t


func _lobby_info(lobby: LobbyInfo) -> ServerInfo:
	var info := ServerInfo.new()
	info.players = lobby.players
	info.max_players = lobby.max_players
	return info


func _on_provider_list_updated(
	lobbies: Array[LobbyInfo], id: StringName
) -> void:
	_provider_results[id] = lobbies
	_rebuild_rows()


func _on_probe_result(
	result: ServerInfoResult, row: ServerBrowserRow
) -> void:
	if is_instance_valid(row):
		row.set_result(result)


func _on_row_selected(_target: JoinTarget, row: ServerBrowserRow) -> void:
	if _selected_row and is_instance_valid(_selected_row):
		_selected_row.button_pressed = false
	_selected_row = row
	_update_details()
	_update_footer()


func _clear_selection() -> void:
	_selected_row = null
	_update_details()
	_update_footer()


func _update_details() -> void:
	if _selected_row == null or _selected_row.target == null:
		_details_label.text = "Select a server to see details"
		return
	var t := _selected_row.target
	var r := _selected_row.result
	var lines: PackedStringArray = []
	if t.is_direct():
		lines.append("Address: %s" % t.address)
		lines.append("Backend: %s" % _backend_label(t.backend))
		lines.append("Status: %s" % _status_text(r))
		lines.append("Latency: %s" % (
			"%d ms" % r.latency_ms if r and r.is_ok() else "-"
		))
		lines.append("Players: %s" % _players_text(r))
		lines.append("Saved: Yes")
	else:
		lines.append("Provider: %s" % String(t.provider_id))
		lines.append("Lobby ID: %s" % str(t.remote_id))
		lines.append("Status: %s" % _status_text(r))
		lines.append("Players: %s" % _players_text(r))
		lines.append("Saved: No - ephemeral")
	_details_label.text = "\n".join(lines)


func _update_footer() -> void:
	var has_selection := _selected_row != null and _selected_row.target != null
	var is_direct := has_selection and _selected_row.target.is_direct()
	_edit_button.disabled = not is_direct
	_remove_button.disabled = not is_direct
	_join_button.disabled = not has_selection
	if has_selection and not _selected_row.target.is_direct():
		_join_button.text = "Join lobby"
	else:
		_join_button.text = "Join"


func _status_text(result: ServerInfoResult) -> String:
	if result == null:
		return "..."
	match result.status:
		ServerInfoResult.Status.OK: return "OK"
		ServerInfoResult.Status.BUSY: return "BUSY"
		ServerInfoResult.Status.UNREACHABLE: return "UNREACHABLE"
		ServerInfoResult.Status.TIMEOUT: return "TIMEOUT"
		ServerInfoResult.Status.UNSUPPORTED: return "UNSUPPORTED"
		_: return "ERROR"


func _players_text(result: ServerInfoResult) -> String:
	if result == null or result.info == null:
		return "-"
	return "%d/%d" % [result.info.players, result.info.max_players]


func _show_banner(reason: String) -> void:
	_banner_label.text = reason
	_banner.visible = true


func _on_add_pressed() -> void:
	_popup.set_templates(backend_templates)
	_popup.open_add()


func _on_host_pressed() -> void:
	_host_popup.set_choices(backend_templates, _registry.list_providers())
	_host_popup.open()


func _on_host_submitted(
	kind: ServerBrowserHostPopup.Kind,
	choice: Variant,
	display_name: String,
) -> void:
	_pending_join_target = null
	_pending_host_kind = kind
	_pending_host_choice = choice
	_pending_host_display_name = display_name
	_join_popup.open(_last_username, "Host server", "Host")


func _host_with_options(
	username: String,
	spawner: SceneNodePath,
) -> void:
	var tree := _resolve_tree()
	if tree == null:
		push_warning(
			"ServerBrowser: tree_path is not set or resolves to null"
		)
		return
	_bind_session_visibility_signals(tree)
	var payload := _build_payload(username, spawner)
	match _pending_host_kind:
		ServerBrowserHostPopup.Kind.DIRECT:
			var template := _pending_host_choice as BackendPeer
			if template == null:
				return
			tree.backend = template
			var err := await tree.host_player(payload)
			_hide_after_successful_session(err)
		ServerBrowserHostPopup.Kind.PROVIDER:
			var provider := _registry.get_provider(
				_pending_host_choice as StringName
			)
			if provider == null:
				push_warning(
					"ServerBrowser: no provider for %s" % _pending_host_choice
				)
				return
			provider.create_lobby(_pending_host_display_name)
			await provider.lobby_created
			var err := await provider.bind(NetwTree.new(tree), payload)
			_hide_after_successful_session(err)
	_pending_host_kind = -1


func _on_edit_pressed() -> void:
	if _selected_row == null or not _selected_row.target.is_direct():
		return
	_popup.set_templates(backend_templates)
	_popup.open_edit(_selected_row.target)


func _on_remove_pressed() -> void:
	if _selected_row == null or not _selected_row.target.is_direct():
		return
	var target := _selected_row.target
	var idx := _list.targets.find(target)
	if idx >= 0:
		_list.targets.remove_at(idx)
		ServerList.save(_list, server_list_path)
	_clear_selection()
	refresh()


func _on_popup_submitted(target: JoinTarget, persist: bool) -> void:
	if _list.targets.find(target) < 0:
		_list.targets.append(target)
	if persist:
		ServerList.save(_list, server_list_path)
	_clear_selection()
	refresh()


func _on_join_pressed() -> void:
	if _selected_row == null or _selected_row.target == null:
		return
	_pending_join_target = _selected_row.target
	_pending_host_kind = -1
	_join_popup.open(_last_username)


func _on_join_submitted(
	username: String,
	spawner: SceneNodePath,
) -> void:
	_last_username = username
	if _pending_host_kind != -1:
		await _host_with_options(username, spawner)
		return
	if _pending_join_target == null:
		return
	var tree := _resolve_tree()
	if tree == null:
		push_warning(
			"ServerBrowser: tree_path is not set or resolves to null"
		)
		return
	_bind_session_visibility_signals(tree)
	var target := _pending_join_target
	_pending_join_target = null
	var payload := _build_payload(username, spawner)
	if target.is_direct():
		var err := await tree.auto_connect_player(
			target.make_backend_instance(), target.address, payload
		)
		_hide_after_successful_session(err)
		return

	var provider := _registry.get_provider(target.provider_id)
	if provider == null:
		push_warning(
			"ServerBrowser: no provider registered for %s" % target.provider_id
		)
		return
	provider.join_lobby(target.remote_id)
	var peer: MultiplayerPeer = await provider.peer_ready
	var err := await tree.adopt_peer(peer, payload)
	_hide_after_successful_session(err)


func _resolve_tree() -> MultiplayerTree:
	if _tree_cache != null and is_instance_valid(_tree_cache):
		return _tree_cache
	if tree_path.is_empty():
		return null
	_tree_cache = get_node_or_null(tree_path) as MultiplayerTree
	return _tree_cache


# Subscribes to the target session's in-game/offline transitions.
func _bind_session_visibility_signals(tree: MultiplayerTree) -> void:
	if not hide_when_session_active or tree == null:
		return
	if _visibility_tree == tree:
		return
	_unbind_session_visibility_signals()
	_visibility_tree = tree
	tree.local_player_changed.connect(_on_tree_local_player_changed)
	tree.player_scene_ready.connect(_on_tree_player_scene_ready)
	tree.server_disconnected.connect(_show_after_session_closed)
	tree.state_changed.connect(_on_tree_state_changed)


# Removes visibility signal hooks from the last resolved session tree.
func _unbind_session_visibility_signals() -> void:
	if _visibility_tree == null or not is_instance_valid(_visibility_tree):
		_visibility_tree = null
		return
	if _visibility_tree.local_player_changed.is_connected(
		_on_tree_local_player_changed
	):
		_visibility_tree.local_player_changed.disconnect(
			_on_tree_local_player_changed
		)
	if _visibility_tree.player_scene_ready.is_connected(
		_on_tree_player_scene_ready
	):
		_visibility_tree.player_scene_ready.disconnect(
			_on_tree_player_scene_ready
		)
	if _visibility_tree.server_disconnected.is_connected(
		_show_after_session_closed
	):
		_visibility_tree.server_disconnected.disconnect(
			_show_after_session_closed
		)
	if _visibility_tree.state_changed.is_connected(_on_tree_state_changed):
		_visibility_tree.state_changed.disconnect(_on_tree_state_changed)
	_visibility_tree = null


# Hides when this client gets its represented player node.
func _on_tree_local_player_changed(player: Node) -> void:
	if player != null:
		hide()
	elif (
		_visibility_tree != null
		and _visibility_tree.state == MultiplayerTree.State.OFFLINE
	):
		_show_after_session_closed()


# Hides the browser once the local player reaches an active scene.
func _on_tree_player_scene_ready(
	_rj: ResolvedJoin,
	_scene: MultiplayerScene,
) -> void:
	hide()


# Restores the browser when the session returns to its offline state.
func _on_tree_state_changed(_old_state: int, new_state: int) -> void:
	if new_state == MultiplayerTree.State.OFFLINE:
		_show_after_session_closed()


# Restores the browser after a server disconnect signal.
func _show_after_session_closed() -> void:
	show()


# Applies the exported auto-hide behavior to the current session state.
func _sync_session_visibility() -> void:
	if not hide_when_session_active:
		return
	var tree := _resolve_tree()
	if tree != null and tree.local_player != null:
		hide()
	else:
		show()


# Hides after this browser successfully opens a session.
func _hide_after_successful_session(err: Error) -> void:
	if err == OK and hide_when_session_active:
		hide()


func _build_payload(username: String, spawner: SceneNodePath) -> JoinPayload:
	var payload := JoinPayload.new()
	var clean_username := username.strip_edges()
	payload.username = clean_username if not clean_username.is_empty() \
		else "Player"
	if spawner == null:
		spawner = _default_spawner()
	if spawner != null:
		payload.spawner_component_path = spawner
	return payload


func _default_spawner() -> SceneNodePath:
	if spawner_options.is_empty():
		return null
	return spawner_options[0]


func _backend_label(backend: BackendPeer) -> String:
	if backend == null:
		return "-"
	var script := backend.get_script()
	if script and not script.get_global_name().is_empty():
		return script.get_global_name()
	if backend.resource_path.is_empty() or "::" in backend.resource_path:
		return backend.get_class()
	return backend.resource_path.get_file()
