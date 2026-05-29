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

## Path to the [MultiplayerTree] joined direct/provider rows feed.
@export var tree_path: NodePath

## Path used for [ServerList] persistence.
@export var server_list_path: String = ServerList.DEFAULT_PATH


var _list: ServerList
var _registry: ProviderRegistry
var _probes: ProbeManager
var _popup: ServerBrowserPopup
var _selected_row: ServerBrowserRow
var _provider_results: Dictionary = {}
var _direct_rows: Array[ServerBrowserRow] = []
var _tree_cache: MultiplayerTree


@onready var _count_label: Label = %CountLabel
@onready var _refresh_button: Button = %RefreshButton
@onready var _add_button: Button = %AddButton
@onready var _banner: HBoxContainer = %Banner
@onready var _banner_label: Label = %BannerLabel
@onready var _list_box: VBoxContainer = %ListBox
@onready var _empty_state: VBoxContainer = %EmptyState
@onready var _details_label: Label = %DetailsLabel
@onready var _username_edit: LineEdit = %UsernameEdit
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

	_refresh_button.pressed.connect(refresh)
	_add_button.pressed.connect(_on_add_pressed)
	_edit_button.pressed.connect(_on_edit_pressed)
	_remove_button.pressed.connect(_on_remove_pressed)
	_join_button.pressed.connect(_on_join_pressed)

	_clear_selection()
	refresh()


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
		lines.append("Backend: %s" % (
			t.backend.get_class() if t.backend else "-"
		))
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
	var tree := _resolve_tree()
	if tree == null:
		push_warning(
			"ServerBrowser: tree_path is not set or resolves to null"
		)
		return
	var target := _selected_row.target
	var payload := _build_payload(_username_edit.text)
	if target.is_direct():
		await tree.auto_connect_player(
			target.make_backend_instance(), target.address, payload
		)
		return

	var provider := _registry.get_provider(target.provider_id)
	if provider == null:
		push_warning(
			"ServerBrowser: no provider registered for %s" % target.provider_id
		)
		return
	provider.join_lobby(target.remote_id)
	var peer: MultiplayerPeer = await provider.peer_ready
	await tree.adopt_peer(peer, payload)


func _resolve_tree() -> MultiplayerTree:
	if _tree_cache != null and is_instance_valid(_tree_cache):
		return _tree_cache
	if tree_path.is_empty():
		return null
	_tree_cache = get_node_or_null(tree_path) as MultiplayerTree
	return _tree_cache


func _build_payload(username: String) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	return payload
