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
		directory_id: StringName,
		lobbies: Array[LobbyInfo],
)

## A registered directory reported that its transport is unavailable.
signal directory_unavailable(directory_id: StringName, reason: String)

## A join attempt began against [param target].
signal join_started(target: JoinTarget)

## A join attempt failed. [param result] is the [ConnectResult] outcome.
signal join_failed(target: JoinTarget, result: ConnectResult)

## A join attempt advanced through transport-specific progress.
signal join_progress(target: JoinTarget, step: StringName, message: String, ratio: float)

## A connection succeeded. [param result] is the [ConnectResult] containing
## happy-path diagnostics.
signal connection_diagnostics(result: ConnectResult)

## A host attempt began.
signal host_started()

## A host attempt failed. [param reason] is a human-readable string.
signal host_failed(reason: String)

## The bound [MultiplayerTree] reports a successful local entry
## (host or client join). Fires once per session.
signal session_entered()

## The bound [MultiplayerTree] returned to its offline state.
signal session_left()

## Ceiling applied when a backend declares its connect path self-managed
## ([method BackendPeer.connect_timeout_hint] returns a negative value), so a
## buggy backend can never wedge a join open forever.
const SELF_MANAGED_TIMEOUT_CEILING := 30.0

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
var _discovered: Dictionary = { } # StringName -> Array[JoinTarget]
var _directories_order: Array[StringName] = []
var _results: Dictionary = { } # JoinTarget -> ServerInfoResult
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
		"ConnectSession bound to tree '%s'.",
		[_tree.name],
	)
	_bind_tree_signals()
	_sync_tree_directories()


## Returns the currently bound [MultiplayerTree], or [code]null[/code].
func get_tree_bound() -> MultiplayerTree:
	return _tree if is_instance_valid(_tree) else null


## Returns [code]true[/code] while the bound tree is in an active session.
##
## A late binder reads this after wiring [signal session_entered] to catch up
## when the tree entered before the binding, e.g. a debug auto-connect.
func is_session_active() -> bool:
	return is_instance_valid(_tree) \
			and _tree.state == MultiplayerTree.State.ONLINE

# -- Directories -------------------------------------------------------------


## Registers [param directory] under [param id] so its lobbies appear
## in [method get_targets].
##
## A [LobbyDirectory] placed under the bound [MultiplayerTree] is adopted
## automatically (keyed by its node name) on [method refresh], so call this
## only for an off-tree directory or to pin a specific [param id].
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
		"ConnectSession directory registered: %s.",
		[String(id)],
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
			[_target_summary(target)],
		)
		return
	_saved_targets.append(target)
	Netw.dbg.info(
		"ConnectSession added saved target %s (persist=%s, size=%d).",
		[_target_summary(target), str(persist), _saved_targets.size()],
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
			[_target_summary(target)],
		)
		return
	_saved_targets.remove_at(idx)
	_results.erase(target)
	Netw.dbg.info(
		"ConnectSession removed saved target %s (persist=%s, size=%d).",
		[_target_summary(target), str(persist), _saved_targets.size()],
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
	if Netw.is_test_env() and path.begins_with("user://"):
		if not path.contains("_test_"):
			path = "user://servers_test.tres"
	server_list_path = path
	var loaded := ServerList.load_or_new(path)
	server_list = loaded
	Netw.dbg.info(
		"ConnectSession loaded %d saved target(s) from %s.",
		[loaded.targets.size(), path],
	)
	_replace_saved_targets(loaded.targets)


## Persists the current saved targets through
## [member server_list], creating a fresh [ServerList] when none is
## loaded. Returns the [enum @GlobalScope.Error] from
## [ResourceSaver.save].
func save_server_list(path: String = server_list_path) -> Error:
	if Netw.is_test_env() and path.begins_with("user://"):
		if not path.contains("_test_"):
			path = "user://servers_test.tres"
	server_list_path = path
	if server_list == null:
		server_list = ServerList.new()
	server_list.targets = _saved_targets.duplicate()
	var err := ServerList.save(server_list, path)
	if err == OK:
		Netw.dbg.info(
			"ConnectSession saved %d saved target(s) to %s.",
			[_saved_targets.size(), path],
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
	_sync_tree_directories()
	Netw.dbg.debug(
		"ConnectSession refresh: saved=%d directories=%d.",
		[_saved_targets.size(), _directories_order.size()],
	)
	if _probes:
		_probes.cancel_all()
	for target in _saved_targets:
		if not _is_target_available(target):
			Netw.dbg.trace(
				"ConnectSession skipping unavailable target %s.",
				[_target_summary(target)],
			)
			continue
		Netw.dbg.trace(
			"ConnectSession probing saved target %s.",
			[_target_summary(target)],
		)
		_probes.query(target, _on_probe_result.bind(target))
	for id in _directories_order:
		var directory := _directories.get_directory(id)
		if directory:
			Netw.dbg.trace(
				"ConnectSession refreshing directory %s.",
				[String(id)],
			)
			directory.list_lobbies()


## Issues a single probe for [param target]. Result lands via
## [signal target_updated].
func probe(target: JoinTarget) -> void:
	_ensure_internals()
	if target == null:
		Netw.dbg.warn("ConnectSession probe ignored null target.")
		return
	if not _is_target_available(target):
		Netw.dbg.debug(
			"ConnectSession probe skipped, unavailable target %s.",
			[_target_summary(target)],
		)
		return
	Netw.dbg.debug(
		"ConnectSession probing target %s.",
		[_target_summary(target)],
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
		[String(payload.username)],
	)
	host_started.emit()

	var backend := config.make_backend_instance()
	if backend == null:
		host_failed.emit("host config has no backend template")
		return ERR_INVALID_PARAMETER

	_apply_host_config(backend, config)

	tree.backend = backend
	var err := await tree.host_player(payload)
	if err != OK:
		host_failed.emit(
			"backend host_player failed (%s)" % error_string(err),
		)
		return err

	# session_entered fires from _on_tree_state_changed when the tree reaches
	# ONLINE, so every entry path (including debug auto-connect) is covered.
	return OK


## Joins [param target]. [param payload] carries player identity.
func join(target: JoinTarget, payload: JoinPayload) -> Error:
	if target == null:
		Netw.dbg.warn("ConnectSession join failed: target is null.")
		join_failed.emit(null, ConnectResult.error("target is null"))
		return ERR_INVALID_PARAMETER
	if payload == null:
		Netw.dbg.warn(
			"ConnectSession join failed for %s: payload is null.",
			[_target_summary(target)],
		)
		join_failed.emit(target, ConnectResult.error("join payload is null"))
		return ERR_INVALID_PARAMETER
	var tree := get_tree_bound()
	if tree == null:
		Netw.dbg.warn(
			"ConnectSession join failed for %s: no bound tree.",
			[_target_summary(target)],
		)
		join_failed.emit(
			target,
			ConnectResult.error(
				"no MultiplayerTree bound; call bind_tree first",
			),
		)
		return ERR_UNCONFIGURED

	Netw.dbg.info(
		"ConnectSession join requested: %s (user=%s).",
		[_target_summary(target), String(payload.username)],
	)
	join_started.emit(target)
	join_aborted_flag = false

	# Failure is reported via join_failed below, so keep the tree quiet to
	# avoid logging the timeout as a redundant hard error. The backend authors
	# the budget so a retry-aware transport gets a wider window; a self-managed
	# backend (hint < 0) falls back to a safety-net ceiling.
	var hint := target.backend.connect_timeout_hint() if target.backend else 5.0
	var timeout := hint if hint > 0.0 else SELF_MANAGED_TIMEOUT_CEILING
	var progress_cb := _on_backend_connect_progress.bind(target)
	var progress_source: BackendPeer = null
	var backend_ready_cb := func(backend: BackendPeer) -> void:
		if backend == null:
			return
		progress_source = backend
		if not backend.connect_progress.is_connected(progress_cb):
			backend.connect_progress.connect(progress_cb)
	tree.backend_ready_for_join.connect(backend_ready_cb, CONNECT_ONE_SHOT)
	var err := await tree.join(target, payload, timeout, true)
	if tree.backend_ready_for_join.is_connected(backend_ready_cb):
		tree.backend_ready_for_join.disconnect(backend_ready_cb)
	if progress_source != null \
			and progress_source.connect_progress.is_connected(progress_cb):
		progress_source.connect_progress.disconnect(progress_cb)
	if err != OK:
		var result := tree.last_connect_result
		if result == null:
			if join_aborted_flag:
				result = ConnectResult.aborted("Connection aborted by user")
			else:
				result = ConnectResult.error(
					"connect failed (%s)" % error_string(err),
				)
		join_failed.emit(target, result)
		return err

	var result := tree.last_connect_result
	if result == null:
		result = ConnectResult.ok()
	if tree.backend:
		result.diagnostics = tree.backend.get_connection_diagnostics(1)
	connection_diagnostics.emit(result)

	# session_entered fires from _on_tree_state_changed when the tree reaches
	# ONLINE, so every entry path (including debug auto-connect) is covered.
	return OK


## Aborts the active connection attempt on the bound MultiplayerTree.
func abort_join() -> void:
	var tree := get_tree_bound()
	if tree != null:
		join_aborted_flag = true
		tree.abort_join()

# -- Backend templates ------------------------------------------------------


## Returns the [param templates] whose backend can run on this platform.
##
## Add and join offer only transports that [method BackendPeer.is_available]
## here.
static func available_templates(
		templates: Array[BackendPeer],
) -> Array[BackendPeer]:
	var out: Array[BackendPeer] = []
	for backend in templates:
		if backend != null and backend.is_available():
			out.append(backend)
	return out


## Returns the [param templates] whose backend can also host on this platform.
##
## The Host form drops a transport that connects but cannot
## [method BackendPeer.can_host] here, such as WebSocket on the web.
static func hostable_templates(
		templates: Array[BackendPeer],
) -> Array[BackendPeer]:
	var out: Array[BackendPeer] = []
	for backend in available_templates(templates):
		if backend.can_host():
			out.append(backend)
	return out

# -- Internals --------------------------------------------------------------


# Adopts every LobbyDirectory service under the bound tree that is not already
# registered, keyed by node name. Manual register_directory entries are left
# alone, so explicit ids and off-tree directories keep working.
func _sync_tree_directories() -> void:
	if _tree == null:
		return
	for service in _tree.get_services(LobbyDirectory):
		var directory := service as LobbyDirectory
		if directory == null or _is_directory_registered(directory):
			continue
		register_directory(StringName(directory.name), directory)


func _is_directory_registered(directory: LobbyDirectory) -> bool:
	for id in _directories_order:
		if _directories.get_directory(id) == directory:
			return true
	return false


func _bind_tree_signals() -> void:
	if _tree == null or _tree_signals_bound:
		return
	_tree.state_changed.connect(_on_tree_state_changed)
	_tree_signals_bound = true


func _unbind_tree_signals() -> void:
	if not _tree_signals_bound:
		return
	if is_instance_valid(_tree) and _tree.state_changed.is_connected(
		_on_tree_state_changed,
	):
		_tree.state_changed.disconnect(_on_tree_state_changed)
	_tree_signals_bound = false


func _on_tree_state_changed(_old_state: int, new_state: int) -> void:
	if new_state == MultiplayerTree.State.ONLINE:
		session_entered.emit()
	elif new_state == MultiplayerTree.State.OFFLINE:
		session_left.emit()


func _apply_host_config(
		backend: BackendPeer,
		config: ConnectHostConfig,
) -> void:
	if backend is SteamBackend:
		var steam := backend as SteamBackend
		steam.server_name = config.server_name
	elif backend is WebRTCBackend:
		var webrtc := backend as WebRTCBackend
		webrtc.server_name = config.server_name


func _on_probe_result(result: ServerInfoResult, target: JoinTarget) -> void:
	if result != null and result.is_ok() and result.info != null \
			and _local_app_id() != String(result.info.app_id):
		Netw.dbg.debug(
			"ConnectSession probe incompatible for %s: local app_id='%s' "
			+ "remote app_id='%s'.",
			[
				_target_summary(target),
				_local_app_id(),
				String(result.info.app_id),
			],
		)
		result = ServerInfoResult.incompatible(result.info)
	_results[target] = result
	Netw.dbg.debug(
		"ConnectSession probe result for %s: %s.",
		[_target_summary(target), str(result)],
	)
	target_updated.emit(target, result)


# A target a probe should poke: it has a backend that can run on this platform.
func _is_target_available(target: JoinTarget) -> bool:
	return target.backend == null or target.backend.is_available()


# The bound tree's build tag, or "" when no tree or the gate is off.
func _local_app_id() -> String:
	return String(_tree.app_id) if is_instance_valid(_tree) else ""


# Flags a discovered server incompatible when its build tag differs from the
# local one, so the browser can warn before a join the auth handshake would
# reject. An empty tag on either side means the gate is off, so it stays OK.
func _classify_discovered(info: ServerInfo) -> ServerInfoResult:
	if _local_app_id() != String(info.app_id):
		Netw.dbg.debug(
			"ConnectSession discovered incompatible lobby: local app_id='%s' "
			+ "remote app_id='%s'.",
			[_local_app_id(), String(info.app_id)],
		)
		return ServerInfoResult.incompatible(info)
	return ServerInfoResult.ok(info, -1)


func _on_directory_list_updated(
		lobbies: Array[LobbyInfo],
		id: StringName,
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
			info.metadata = lobby.metadata.duplicate()
			info.app_id = StringName(lobby.metadata.get("app_id", ""))
			_results[t] = _classify_discovered(info)

	_discovered[id] = fresh
	Netw.dbg.debug(
		"ConnectSession directory %s refreshed: %d lobby target(s).",
		[String(id), fresh.size()],
	)
	for target in fresh:
		target_added.emit(target)
		target_updated.emit(target, _results[target])
	directory_list_updated.emit(id, lobbies)


func _on_directory_unavailable(reason: String, id: StringName) -> void:
	Netw.dbg.warn(
		"ConnectSession directory %s unavailable: %s.",
		[String(id), reason],
	)
	directory_unavailable.emit(id, reason)


func _on_backend_connect_progress(
		step: StringName,
		message: String,
		ratio: float,
		target: JoinTarget,
) -> void:
	join_progress.emit(target, step, message, ratio)


# Swaps the saved set to [param loaded] while keeping the existing instance for
# any entry that reloads unchanged, so a redundant reload does not churn the
# list or orphan a probe result keyed to the old instance.
func _replace_saved_targets(loaded: Array[JoinTarget]) -> void:
	var existing_by_key := { }
	for target in _saved_targets:
		existing_by_key[_target_key(target)] = target

	var next: Array[JoinTarget] = []
	var reused := { }
	for incoming in loaded:
		var key := _target_key(incoming)
		if existing_by_key.has(key):
			var kept: JoinTarget = existing_by_key[key]
			next.append(kept)
			reused[kept] = true
		else:
			next.append(incoming)
			target_added.emit(incoming)

	for target in _saved_targets:
		if not reused.has(target):
			_results.erase(target)
			target_removed.emit(target)

	_saved_targets = next


# Stable identity for a saved target, so reloads can match unchanged entries.
func _target_key(target: JoinTarget) -> String:
	var backend := target.backend
	var backend_id := "none"
	if backend != null:
		backend_id = backend.get_class()
		var script := backend.get_script() as Script
		if script != null:
			backend_id = script.resource_path
	var port := ""
	if backend != null and "port" in backend:
		port = str(backend.port)
	return "%s|%s|%s" % [target.address, backend_id, port]


func _target_summary(target: JoinTarget) -> String:
	if target == null:
		return "<null>"
	var address := target.address
	if address.is_empty() and target.backend != null:
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
