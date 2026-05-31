## The engine behind the server browser: holds the live server list and runs
## host / join.
##
## Most games never touch this directly -- they go through [NetwConnect]
## ([code]Netw.ctx(self).connect[/code]), which wraps the one canonical
## session the [MultiplayerTree] owns. Reach for [ConnectSession] when you are
## building a [b]custom browser UI[/b] and want the raw node: it keeps the list
## of [JoinTarget]s (typed-in addresses plus lobbies discovered through
## directories like Steam), probes them for status, and drives the actual
## host / join handshake.
##
## [br][br]
## Constructing your own is a handful of lines:
## [codeblock]
## var session := ConnectSession.new()
## add_child(session)                   # auto-binds to the enclosing tree
## session.register_directory(&"steam", steam_dir)
## session.load_server_list()           # restore saved direct servers
## session.target_updated.connect(_on_status)
## session.refresh()                    # probe everything
## await session.join(chosen_target, payload)
## [/codeblock]
##
## [br][br]
## [b]Tree binding:[/b] host and join need a [MultiplayerTree]. A session added
## under a tree binds to it automatically; otherwise call [method bind_tree].
##
## [br][br]
## [b]Persistence is opt-in:[/b] targets live in memory until you
## [method load_server_list] / [method save_server_list] (or pass
## [code]persist = true[/code] to [method add_target] /
## [method remove_target]). Directory lobbies are always ephemeral.
class_name ConnectSession
extends Node


## A saved or directory-discovered target was added to the live list.
signal target_added(target: JoinTarget)

## A target was removed from the live list (saved removal or
## directory list refresh).
signal target_removed(target: JoinTarget)

## A new probe result or live lobby snapshot landed for [param target].
signal target_updated(target: JoinTarget, result: ServerInfoResult)

## A directory's lobby list refreshed.
signal directory_list_updated(
	directory_id: StringName, lobbies: Array[LobbyInfo]
)

## A registered directory reported that its transport is unavailable.
signal directory_unavailable(directory_id: StringName, reason: String)

## A join attempt began against [param target].
signal join_started(target: JoinTarget)

## A join attempt failed. [param reason] is a human-readable string.
signal join_failed(target: JoinTarget, reason: String)

## A host attempt began.
signal host_started()

## A host attempt failed. [param reason] is a human-readable string.
signal host_failed(reason: String)

## The bound [MultiplayerTree] reports a successful local entry
## (host or client join). Fires once per session.
signal session_entered()

## The bound [MultiplayerTree] returned to its offline state.
signal session_left()


## Path used for [ServerList] persistence by
## [method load_server_list] / [method save_server_list] when no
## explicit path is supplied.
@export var server_list_path: String = ServerList.DEFAULT_PATH

## True if the active join handshake was explicitly aborted.
var join_aborted_flag: bool = false


## Optional persistence handle. Null until [method load_server_list]
## assigns one (or a caller sets it directly). When set,
## [method save_server_list] writes the current saved targets back
## through it.
var server_list: ServerList = null


var _tree: MultiplayerTree
var _probes: ProbeManager
var _directories: DirectoryRegistry
var _saved_targets: Array[JoinTarget] = []
var _discovered: Dictionary = {}  # StringName -> Array[JoinTarget]
var _directories_order: Array[StringName] = []
var _results: Dictionary = {}            # JoinTarget -> ServerInfoResult
var _tree_signals_bound: bool = false


func _init() -> void:
	_ensure_internals()


func _ready() -> void:
	_ensure_internals()
	_parent_internals()
	if _tree == null:
		var tree := MultiplayerTree.resolve(self)
		if tree:
			bind_tree(tree)


func _exit_tree() -> void:
	_unbind_tree_signals()


# -- Tree binding ------------------------------------------------------------

## Binds the [MultiplayerTree] used by [method host] and [method join].
## The session subscribes to its lifecycle so [signal session_entered]
## and [signal session_left] fire from a single place.
func bind_tree(tree: MultiplayerTree) -> void:
	if _tree == tree:
		return
	_unbind_tree_signals()
	_tree = tree
	Netw.dbg.debug(
		"ConnectSession bound to tree '%s'.", [_tree.name]
	)
	_bind_tree_signals()


## Returns the currently bound [MultiplayerTree], or [code]null[/code].
func get_tree_bound() -> MultiplayerTree:
	return _tree if is_instance_valid(_tree) else null


# -- Directories -------------------------------------------------------------

## Registers [param directory] under [param id] so its lobbies appear
## in [method get_targets]. Must be called before [method refresh] for
## the directory's lobbies to be polled.
func register_directory(id: StringName, directory: LobbyDirectory) -> void:
	_ensure_internals()
	_directories.register(id, directory)
	if not _directories_order.has(id):
		_directories_order.append(id)
	var list_cb := _on_directory_list_updated.bind(id)
	if not directory.lobby_list_updated.is_connected(list_cb):
		directory.lobby_list_updated.connect(list_cb)
	var unavailable_cb := _on_directory_unavailable.bind(id)
	if not directory.provider_unavailable.is_connected(unavailable_cb):
		directory.provider_unavailable.connect(unavailable_cb)
	Netw.dbg.debug(
		"ConnectSession directory registered: %s.", [String(id)]
	)


## Removes the directory registered under [param id], if any.
func unregister_directory(id: StringName) -> void:
	_ensure_internals()
	var directory := _directories.get_directory(id)
	if directory != null:
		var list_cb := _on_directory_list_updated.bind(id)
		if directory.lobby_list_updated.is_connected(list_cb):
			directory.lobby_list_updated.disconnect(list_cb)
		var unavailable_cb := _on_directory_unavailable.bind(id)
		if directory.provider_unavailable.is_connected(unavailable_cb):
			directory.provider_unavailable.disconnect(unavailable_cb)
	_directories.unregister(id)
	_directories_order.erase(id)


## Returns the directory registered under [param id], or [code]null[/code].
func get_directory(id: StringName) -> LobbyDirectory:
	_ensure_internals()
	return _directories.get_directory(id)


## Returns registered directory ids in registration order.
func get_directory_ids() -> Array[StringName]:
	return _directories_order.duplicate()


# -- Target list ------------------------------------------------------------

## Appends [param target] to the live list. When [param persist] is
## [code]true[/code] and a [member server_list] is loaded, the
## change is written to disk immediately.
##
## Directory-discovered targets arrive via
## [signal LobbyDirectory.lobby_list_updated]. Call this only for
## saved targets the UI is adding by hand.
func add_target(target: JoinTarget, persist: bool = false) -> void:
	if target == null:
		Netw.dbg.warn("ConnectSession add_target ignored null target.")
		return
	if _saved_targets.has(target):
		Netw.dbg.trace(
			"ConnectSession add_target ignored duplicate: %s.",
			[_target_summary(target)]
		)
		return
	_saved_targets.append(target)
	Netw.dbg.info(
		"ConnectSession added saved target %s (persist=%s, size=%d).",
		[_target_summary(target), str(persist), _saved_targets.size()]
	)
	target_added.emit(target)
	if persist:
		save_server_list()


## Removes [param target] from the live list. When [param persist] is
## [code]true[/code], the change is written to disk immediately.
func remove_target(target: JoinTarget, persist: bool = false) -> void:
	var idx := _saved_targets.find(target)
	if idx < 0:
		Netw.dbg.trace(
			"ConnectSession remove_target ignored missing target: %s.",
			[_target_summary(target)]
		)
		return
	_saved_targets.remove_at(idx)
	_results.erase(target)
	Netw.dbg.info(
		"ConnectSession removed saved target %s (persist=%s, size=%d).",
		[_target_summary(target), str(persist), _saved_targets.size()]
	)
	target_removed.emit(target)
	if persist:
		save_server_list()


## Returns the union of saved targets and directory-discovered
## lobby targets, in display order.
func get_targets() -> Array[JoinTarget]:
	var out: Array[JoinTarget] = []
	out.append_array(_saved_targets)
	for id in _directories_order:
		if not _discovered.has(id):
			continue
		var lobbies: Array[JoinTarget] = []
		lobbies.assign(_discovered[id])
		out.append_array(lobbies)
	return out


## Returns the live saved targets in their persisted order.
func get_saved_targets() -> Array[JoinTarget]:
	return _saved_targets.duplicate()


## Returns the directory-discovered targets for [param directory_id].
func get_discovered_targets(directory_id: StringName) -> Array[JoinTarget]:
	var out: Array[JoinTarget] = []
	if not _discovered.has(directory_id):
		return out
	out.assign(_discovered[directory_id])
	return out


## Returns the latest [ServerInfoResult] for [param target], or
## [code]null[/code] when nothing has been observed yet.
func get_result(target: JoinTarget) -> ServerInfoResult:
	return _results.get(target, null)


# -- Persistence ------------------------------------------------------------

## Loads the persisted [ServerList] at [param path], replacing the
## current saved target list. Directory-discovered targets are not
## affected. Emits [signal target_removed] for the prior set and
## [signal target_added] for the loaded set.
func load_server_list(path: String = server_list_path) -> void:
	server_list_path = path
	var loaded := ServerList.load_or_new(path)
	server_list = loaded
	Netw.dbg.info(
		"ConnectSession loaded %d saved target(s) from %s.",
		[loaded.targets.size(), path]
	)
	_replace_saved_targets(loaded.targets)


## Persists the current saved targets through
## [member server_list], creating a fresh [ServerList] when none is
## loaded. Returns the [enum @GlobalScope.Error] from
## [ResourceSaver.save].
func save_server_list(path: String = server_list_path) -> Error:
	server_list_path = path
	if server_list == null:
		server_list = ServerList.new()
	server_list.targets = _saved_targets.duplicate()
	var err := ServerList.save(server_list, path)
	if err == OK:
		Netw.dbg.info(
			"ConnectSession saved %d saved target(s) to %s.",
			[_saved_targets.size(), path]
		)
	else:
		Netw.dbg.error(
			"ConnectSession failed saving %d saved target(s) to %s: %s.",
			[_saved_targets.size(), path, error_string(err)],
			func(m): push_error(m)
		)
	return err


# -- Probing & refresh ------------------------------------------------------

## Cancels every in-flight probe, re-probes all saved targets, and
## asks every registered directory to refresh its lobby list.
func refresh() -> void:
	_ensure_internals()
	Netw.dbg.debug(
		"ConnectSession refresh: saved=%d directories=%d.",
		[_saved_targets.size(), _directories_order.size()]
	)
	if _probes:
		_probes.cancel_all()
	for target in _saved_targets:
		Netw.dbg.trace(
			"ConnectSession probing saved target %s.",
			[_target_summary(target)]
		)
		_probes.query(target, _on_probe_result.bind(target))
	for id in _directories_order:
		var directory := _directories.get_directory(id)
		if directory:
			Netw.dbg.trace(
				"ConnectSession refreshing directory %s.", [String(id)]
			)
			directory.list_lobbies()


## Issues a single probe for [param target]. Result lands via
## [signal target_updated].
func probe(target: JoinTarget) -> void:
	_ensure_internals()
	if target == null:
		Netw.dbg.warn("ConnectSession probe ignored null target.")
		return
	Netw.dbg.debug(
		"ConnectSession probing target %s.", [_target_summary(target)]
	)
	_probes.query(target, _on_probe_result.bind(target))


# -- Host & join ------------------------------------------------------------

## Hosts a new session. [param config] supplies the transport plus server name.
## The [param payload] carries player identity. Returns OK on success, or
## an [enum @GlobalScope.Error] otherwise.
func host(config: ConnectHostConfig, payload: JoinPayload) -> Error:
	if config == null:
		Netw.dbg.warn("ConnectSession host failed: host config is null.")
		host_failed.emit("host config is null")
		return ERR_INVALID_PARAMETER
	if payload == null:
		Netw.dbg.warn("ConnectSession host failed: join payload is null.")
		host_failed.emit("join payload is null")
		return ERR_INVALID_PARAMETER
	var tree := get_tree_bound()
	if tree == null:
		Netw.dbg.warn("ConnectSession host failed: no bound tree.")
		host_failed.emit("no MultiplayerTree bound; call bind_tree first")
		return ERR_UNCONFIGURED

	Netw.dbg.info(
		"ConnectSession host requested (user=%s).",
		[String(payload.username)]
	)
	host_started.emit()

	var backend := config.make_backend_instance()
	if backend == null:
		host_failed.emit("host config has no backend template")
		return ERR_INVALID_PARAMETER

	if backend is SteamBackend:
		backend.server_name = config.server_name

	tree.backend = backend
	var err := await tree.host_player(payload)
	if err != OK:
		host_failed.emit(
			"backend host_player failed (%s)" % error_string(err)
		)
		return err

	session_entered.emit()
	return OK


## Joins [param target]. [param payload] carries player identity.
func join(target: JoinTarget, payload: JoinPayload) -> Error:
	if target == null:
		Netw.dbg.warn("ConnectSession join failed: target is null.")
		join_failed.emit(null, "target is null")
		return ERR_INVALID_PARAMETER
	if payload == null:
		Netw.dbg.warn(
			"ConnectSession join failed for %s: payload is null.",
			[_target_summary(target)]
		)
		join_failed.emit(target, "join payload is null")
		return ERR_INVALID_PARAMETER
	var tree := get_tree_bound()
	if tree == null:
		Netw.dbg.warn(
			"ConnectSession join failed for %s: no bound tree.",
			[_target_summary(target)]
		)
		join_failed.emit(
			target, "no MultiplayerTree bound; call bind_tree first"
		)
		return ERR_UNCONFIGURED

	Netw.dbg.info(
		"ConnectSession join requested: %s (user=%s).",
		[_target_summary(target), String(payload.username)]
	)
	join_started.emit(target)
	join_aborted_flag = false

	var err := await tree.join(target, payload)
	if err != OK:
		if join_aborted_flag:
			join_failed.emit(target, "Connection aborted by user")
		else:
			join_failed.emit(
				target, "connect failed (%s)" % error_string(err)
			)
		return err

	session_entered.emit()
	return OK


## Aborts the active connection attempt on the bound MultiplayerTree.
func abort_join() -> void:
	var tree := get_tree_bound()
	if tree != null:
		join_aborted_flag = true
		tree.abort_join()


# -- Internals --------------------------------------------------------------

func _bind_tree_signals() -> void:
	if _tree == null or _tree_signals_bound:
		return
	_tree.state_changed.connect(_on_tree_state_changed)
	_tree_signals_bound = true


func _unbind_tree_signals() -> void:
	if not _tree_signals_bound:
		return
	if is_instance_valid(_tree) and _tree.state_changed.is_connected(
		_on_tree_state_changed
	):
		_tree.state_changed.disconnect(_on_tree_state_changed)
	_tree_signals_bound = false


func _on_tree_state_changed(_old_state: int, new_state: int) -> void:
	if new_state == MultiplayerTree.State.OFFLINE:
		session_left.emit()


func _on_probe_result(result: ServerInfoResult, target: JoinTarget) -> void:
	_results[target] = result
	Netw.dbg.debug(
		"ConnectSession probe result for %s: %s.",
		[_target_summary(target), str(result)]
	)
	target_updated.emit(target, result)


func _on_directory_list_updated(
	lobbies: Array[LobbyInfo], id: StringName
) -> void:
	var prior: Array[JoinTarget] = []
	if _discovered.has(id):
		prior.assign(_discovered[id])
	for target in prior:
		_results.erase(target)
		target_removed.emit(target)

	var fresh: Array[JoinTarget] = []
	var directory := _directories.get_directory(id)
	if directory != null:
		for lobby in lobbies:
			var t := directory.make_join_target(lobby)
			if t == null:
				continue
			fresh.append(t)
			var info := ServerInfo.new()
			info.players = lobby.players
			info.max_players = lobby.max_players
			_results[t] = ServerInfoResult.ok(info)

	_discovered[id] = fresh
	Netw.dbg.debug(
		"ConnectSession directory %s refreshed: %d lobby target(s).",
		[String(id), fresh.size()]
	)
	for target in fresh:
		target_added.emit(target)
		target_updated.emit(target, _results[target])
	directory_list_updated.emit(id, lobbies)


func _on_directory_unavailable(reason: String, id: StringName) -> void:
	Netw.dbg.warn(
		"ConnectSession directory %s unavailable: %s.",
		[String(id), reason]
	)
	directory_unavailable.emit(id, reason)


func _replace_saved_targets(loaded: Array[JoinTarget]) -> void:
	for target in _saved_targets.duplicate():
		_results.erase(target)
		target_removed.emit(target)
	_saved_targets.clear()
	for target in loaded:
		_saved_targets.append(target)
		target_added.emit(target)


func _target_summary(target: JoinTarget) -> String:
	if target == null:
		return "<null>"
	var address := target.address
	if target.backend != null:
		var join_address := target.backend.get_join_address()
		if not join_address.is_empty():
			address = join_address
	return "%s (%s)" % [target.display_name, address]


func _ensure_internals() -> void:
	if _probes == null:
		_probes = ProbeManager.new()
	if _directories == null:
		_directories = DirectoryRegistry.new()
	_parent_internals()


func _parent_internals() -> void:
	if not is_inside_tree():
		return
	if _probes.get_parent() == null:
		add_child(_probes)
	if _directories.get_parent() == null:
		add_child(_directories)
