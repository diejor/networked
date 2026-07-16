## Browses, probes, and persists the servers a [NetwConnector] can join.
##
## Discovery is a client of the connector, never a peer of it. It probes saved
## targets through the connector's transport registry, reads lobby lists from
## every [LobbyDirectory] registered on the session, classifies each result
## against the local [member NetwMultiplayer.app_id], and hands the picked
## [NetwConnectTarget] back to [method NetwConnector.join]. It reconstructs no
## progress, result, or abort of its own, because the attempt already carries
## them.
## [codeblock]
## var discovery := NetwDiscovery.new(connector)
## discovery.target_updated.connect(_on_status)
## discovery.load_server_list()          # restore saved direct servers
## discovery.refresh()                    # probe saved + list directories
## var attempt := connector.join(discovery.targets[0], payload)
## [/codeblock]
##
## Directories bind themselves. [NetwDiscovery] reads
## [method NetwMultiplayer.get_services] once at construction and then follows
## [signal NetwMultiplayer.service_registered] and
## [signal NetwMultiplayer.service_unregistered], so dropping a
## [LobbyDirectory] node anywhere under the session's branch makes its lobbies
## appear with no wiring. [method add_directory] stays open for a directory that
## is not a session service, such as a test double.
class_name NetwDiscovery
extends RefCounted

## A saved or directory-discovered target entered the live list.
signal target_added(target: NetwConnectTarget)

## A target left the live list.
signal target_removed(target: NetwConnectTarget)

## A fresh [NetwProbeResult] or lobby snapshot landed for [param target].
signal target_updated(target: NetwConnectTarget, result: NetwProbeResult)

## A directory's lobby list refreshed.
signal directory_list_updated(
		directory_id: StringName,
		lobbies: Array[LobbyDirectory.LobbyInfo],
)

## A bound directory reported its transport unavailable.
signal directory_unavailable(directory_id: StringName, reason: String)

## Maximum probe reads allowed in flight at once. Kept below
## [constant NetwProbeResponder.MAX_ACTIVE_PROBES].
var max_concurrent_probes: int = 6

# The connector supplies transports (probe peers) and the session (app tag).
var _connector: NetwConnector
var _saved: Array[NetwConnectTarget] = []
var _directories: Array[LobbyDirectory] = []
var _discovered: Dictionary = { } # StringName -> Array[NetwConnectTarget]
var _results: Dictionary = { } # NetwConnectTarget -> NetwProbeResult

var _server_list: NetwServerList
var _server_list_path: String = NetwServerList.DEFAULT_PATH

var _probe_active: int = 0
var _probe_queue: Array[NetwConnectTarget] = []


func _init(connector: NetwConnector) -> void:
	_connector = connector
	_bind_service_signals()
	_adopt_registered_directories()

# -- Session ----------------------------------------------------------------


## The session this discovery browses for, while it remains live.
func api() -> NetwMultiplayer:
	return _connector.api() if _connector else null

# -- Target list ------------------------------------------------------------


## Appends [param target] to the saved list. When [param persist] is
## [code]true[/code], the change is written through [method save_server_list].
func add_target(target: NetwConnectTarget, persist: bool = false) -> void:
	if target == null or _saved.has(target):
		return
	_saved.append(target)
	target_added.emit(target)
	if persist:
		save_server_list()


## Removes [param target] from the saved list. When [param persist] is
## [code]true[/code], the change is written to disk.
func remove_target(target: NetwConnectTarget, persist: bool = false) -> void:
	var idx := _saved.find(target)
	if idx < 0:
		return
	_saved.remove_at(idx)
	_results.erase(target)
	target_removed.emit(target)
	if persist:
		save_server_list()


## Every browse row, saved targets first, then each directory's lobbies in
## registration order.
var targets: Array[NetwConnectTarget]:
	get:
		var out: Array[NetwConnectTarget] = []
		out.append_array(_saved)
		for directory in _directories:
			var id := _directory_id(directory)
			if _discovered.has(id):
				out.append_array(_discovered[id])
		return out


## The saved targets, in their persisted order.
var saved_targets: Array[NetwConnectTarget]:
	get:
		return _saved.duplicate()


## The lobbies discovered under [param directory_id].
func get_discovered(directory_id: StringName) -> Array[NetwConnectTarget]:
	var out: Array[NetwConnectTarget] = []
	if _discovered.has(directory_id):
		out.assign(_discovered[directory_id])
	return out


## The latest [NetwProbeResult] for [param target], or [code]null[/code] when
## nothing has been observed yet.
func get_result(target: NetwConnectTarget) -> NetwProbeResult:
	return _results.get(target, null)

# -- Directories ------------------------------------------------------------


## Binds [param directory] so its lobbies appear in [member targets].
##
## A [LobbyDirectory] registered as a session service is bound automatically, so
## call this only for a directory that is not a service, such as a test double.
func add_directory(directory: LobbyDirectory) -> void:
	_bind_directory(directory)


## Unbinds [param directory] and drops its discovered lobbies.
func remove_directory(directory: LobbyDirectory) -> void:
	_unbind_directory(directory)


## The bound directories, in bind order.
func get_directories() -> Array[LobbyDirectory]:
	return _directories.duplicate()

# -- Probing & refresh ------------------------------------------------------


## Re-probes every saved target and asks every bound directory to refresh.
func refresh() -> void:
	_probe_queue.clear()
	for target in _saved:
		_enqueue_probe(target)
	for directory in _directories:
		directory._list_lobbies()


## Probes [param target] once. The result lands through [signal target_updated].
func probe(target: NetwConnectTarget) -> void:
	_enqueue_probe(target)

# -- Persistence ------------------------------------------------------------


## Loads the saved target list at [param path], replacing the current saved set.
## Directory lobbies are untouched.
func load_server_list(path: String = _server_list_path) -> void:
	path = _remap_test_path(path)
	_server_list_path = path
	_server_list = NetwServerList.load_or_new(path)
	_replace_saved(_server_list.targets)


## Persists the current saved targets. Returns the storage write
## [enum @GlobalScope.Error].
func save_server_list(path: String = _server_list_path) -> Error:
	path = _remap_test_path(path)
	_server_list_path = path
	if _server_list == null:
		_server_list = NetwServerList.new()
	_server_list.targets = _saved.duplicate()
	return NetwServerList.save(_server_list, path)

# -- Internals --------------------------------------------------------------


func _bind_service_signals() -> void:
	var a := api()
	if a == null:
		return
	a.service_registered.connect(_on_service_registered)
	a.service_unregistered.connect(_on_service_unregistered)


func _adopt_registered_directories() -> void:
	var a := api()
	if a == null:
		return
	for service in a.get_services(LobbyDirectory):
		_bind_directory(service as LobbyDirectory)


func _on_service_registered(service: Node) -> void:
	if service is LobbyDirectory:
		_bind_directory(service)


func _on_service_unregistered(service: Node) -> void:
	if service is LobbyDirectory:
		_unbind_directory(service)


func _bind_directory(directory: LobbyDirectory) -> void:
	if directory == null or _directories.has(directory):
		return
	_directories.append(directory)
	directory.lobby_list_updated.connect(_on_directory_list_updated.bind(directory))
	directory.provider_unavailable.connect(_on_directory_unavailable.bind(directory))


func _unbind_directory(directory: LobbyDirectory) -> void:
	if directory == null or not _directories.has(directory):
		return
	var list_cb := _on_directory_list_updated.bind(directory)
	if directory.lobby_list_updated.is_connected(list_cb):
		directory.lobby_list_updated.disconnect(list_cb)
	var unavailable_cb := _on_directory_unavailable.bind(directory)
	if directory.provider_unavailable.is_connected(unavailable_cb):
		directory.provider_unavailable.disconnect(unavailable_cb)
	var id := _directory_id(directory)
	if _discovered.has(id):
		for target in _discovered[id]:
			_results.erase(target)
			target_removed.emit(target)
		_discovered.erase(id)
	_directories.erase(directory)


func _directory_id(directory: LobbyDirectory) -> StringName:
	var name := String(directory.name)
	return StringName(name) if not name.is_empty() \
			else StringName(str(directory.get_instance_id()))


func _on_directory_list_updated(
		lobbies: Array[LobbyDirectory.LobbyInfo],
		directory: LobbyDirectory,
) -> void:
	var id := _directory_id(directory)
	if _discovered.has(id):
		for target in _discovered[id]:
			_results.erase(target)
			target_removed.emit(target)

	var fresh: Array[NetwConnectTarget] = []
	for lobby in lobbies:
		var target := directory._make_connect_target(lobby)
		if target == null:
			continue
		fresh.append(target)
		_results[target] = _classify_discovered(_lobby_info(lobby))

	_discovered[id] = fresh
	for target in fresh:
		target_added.emit(target)
		target_updated.emit(target, _results[target])
	directory_list_updated.emit(id, lobbies)


func _on_directory_unavailable(reason: String, directory: LobbyDirectory) -> void:
	directory_unavailable.emit(_directory_id(directory), reason)


# Reads a lobby's advertised counts and app tag into a NetwServerInfo for
# classification, mirroring what a probe reply would carry.
func _lobby_info(lobby: LobbyDirectory.LobbyInfo) -> NetwServerInfo:
	var info := NetwServerInfo.new()
	info.players = lobby.players
	info.max_players = lobby.max_players
	info.metadata = lobby.metadata.duplicate()
	info.app_id = StringName(lobby.metadata.get("app_id", ""))
	return info


func _enqueue_probe(target: NetwConnectTarget) -> void:
	if target == null:
		return
	_probe_queue.append(target)
	_pump_probes()


func _pump_probes() -> void:
	while not _probe_queue.is_empty() and _probe_active < max_concurrent_probes:
		var target: NetwConnectTarget = _probe_queue.pop_front()
		_probe_active += 1
		_run_probe(target)


func _run_probe(target: NetwConnectTarget) -> void:
	var result: NetwProbeResult = await _connector.probe(target)
	_probe_active -= 1
	_on_probe_result(target, result)
	_pump_probes()


func _on_probe_result(target: NetwConnectTarget, result: NetwProbeResult) -> void:
	if result != null and result.is_ok() and result.info != null \
			and _local_app_id() != String(result.info.app_id):
		result = NetwProbeResult.incompatible(result.info)
	_results[target] = result
	target_updated.emit(target, result)


# Flags a server incompatible when its build tag differs from the local one, so
# the browser can warn before a join the auth handshake would reject. An empty
# tag on either side means the gate is off, so it stays OK.
func _classify_discovered(info: NetwServerInfo) -> NetwProbeResult:
	if _local_app_id() != String(info.app_id):
		return NetwProbeResult.incompatible(info)
	return NetwProbeResult.ok(info, -1)


func _local_app_id() -> String:
	var a := api()
	return String(a.session.app_id) if a and a.session else ""


# Swaps the saved set to [param loaded], announcing the churn so a browser can
# rebind its rows.
func _replace_saved(loaded: Array[NetwConnectTarget]) -> void:
	for target in _saved:
		_results.erase(target)
		target_removed.emit(target)
	_saved = []
	_saved.assign(loaded)
	for target in _saved:
		target_added.emit(target)


# Redirects a default user:// path to a scratch file under the test runner so a
# suite never clobbers a developer's saved list.
func _remap_test_path(path: String) -> String:
	if Netw.is_test_env() and path.begins_with("user://") \
			and not path.contains("_test_"):
		return "user://netw_servers_test.tres"
	return path
