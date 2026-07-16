## The server browser API: list servers, host a game, or join one.
##
## This is your entry point for everything that happens [i]before[/i] a match
## begins, discovering servers (typed-in addresses and directory lobbies like
## Steam), probing them for status, and finally hosting or joining. Once you
## are in a session, use [NetwMultiplayer] for in-game operations.
##
## [br][br]
## The facade is a thin convenience over a [NetwConnector] (which turns a
## [NetwConnectTarget] or [NetwHostConfig] into a live peer) and a
## [NetwDiscovery] (which browses, probes, and persists). Most methods take or
## return a [NetwConnectTarget]: one connectable server, a saved address you
## typed in, or a lobby a directory discovered. You join one, probe one for
## status, and add or remove your own.
##
## [br][br]
## Grab one from any node descendant of a [MultiplayerTree] and wire up the
## signals you care about:
## [codeblock]
## var connect := Netw.of(self).connect
##
## # Keep the UI in sync as servers come and go.
## connect.target_added.connect(_on_server_found)
## connect.target_updated.connect(_on_server_status)
## connect.refresh()
##
## # Host a game...
## var config := NetwHostConfig.new()
## config.scheme = &"enet"
## config.server_name = "My Game"
## await connect.host(config, payload)
##
## # ...or join the one the player picked.
## await connect.join(picked_target, payload)
## [/codeblock]
##
## The owner must pump the facade through [method poll] each frame so a
## view-pumped transport (WebRTC signaling) and eased progress advance. The
## [ConnectBrowser] does this for the common case.
##
## [br][br]
## Host and join report failures two ways: the returned [enum Error] and the
## matching [signal host_failed] / [signal join_failed] signal.
## [signal connected] fires once the session reaches
## [constant NetwSessionInterface.State.ONLINE].
class_name NetwConnect
extends RefCounted

## A saved or directory-discovered target was added to the live list.
signal target_added(target: NetwConnectTarget)
## A target was removed from the live list.
signal target_removed(target: NetwConnectTarget)
## A new probe result or live lobby snapshot landed for [param target].
signal target_updated(target: NetwConnectTarget, result: NetwProbeResult)
## A directory's lobby list refreshed.
signal directory_list_updated(
		directory_id: StringName,
		lobbies: Array[LobbyDirectory.LobbyInfo],
)
## A registered directory reported that its transport is unavailable.
signal directory_unavailable(directory_id: StringName, reason: String)
## A join attempt began against [param target].
signal join_started(target: NetwConnectTarget)
## A join attempt failed. [param result] is the terminal [NetwConnectResult].
signal join_failed(target: NetwConnectTarget, result: NetwConnectResult)
## A join attempt advanced through transport-specific progress.
signal join_progress(target: NetwConnectTarget, step: StringName, message: String, ratio: float)
## A host attempt began.
signal host_started()
## A host attempt failed. [param reason] is a human-readable string.
signal host_failed(reason: String)
## Emitted when the session reaches [constant NetwSessionInterface.State.ONLINE].
signal connected()
## Emitted when the session returns to [constant NetwSessionInterface.State.OFFLINE].
signal disconnected()

var _api_ref: WeakRef
var _connector: NetwConnector
var _discovery: NetwDiscovery
var _server_list_path: String = NetwServerList.DEFAULT_PATH


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api)
	_connector = NetwConnector.new(api)
	_discovery = NetwDiscovery.new(_connector)
	_discovery.target_added.connect(target_added.emit)
	_discovery.target_removed.connect(target_removed.emit)
	_discovery.target_updated.connect(target_updated.emit)
	_discovery.directory_list_updated.connect(directory_list_updated.emit)
	_discovery.directory_unavailable.connect(directory_unavailable.emit)
	if api:
		api.session_entered.connect(connected.emit)
		api.session_ended.connect(disconnected.emit)


## The session this facade drives, while it remains live.
func api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## Returns [code]true[/code] while the underlying session is alive.
func is_valid() -> bool:
	return api() != null


## Returns [code]true[/code] while the session is already online.
##
## Read this after wiring [signal connected] to catch up when the session
## entered before the binding, e.g. a debug auto-connect.
func is_session_active() -> bool:
	var a := api()
	return a != null and a.session != null \
			and a.session.state == NetwSessionInterface.State.ONLINE


## Pumps the connector for [param dt] seconds. The owner calls this each frame.
func poll(dt: float) -> void:
	_connector.poll(dt)


## The [NetwConnector] this facade drives. Advanced callers reach it for a raw
## [NetwConnectAttempt] or the resolved [member NetwConnector.peer_view].
func connector() -> NetwConnector:
	return _connector


## The resolved [NetwPeerView] for the active connection, or [code]null[/code]
## before a host or join resolves. Proxies [member NetwConnector.peer_view].
var peer_view: NetwPeerView:
	get:
		return _connector.peer_view


## The [NetwDiscovery] this facade drives.
func discovery() -> NetwDiscovery:
	return _discovery

# -- Host & join ------------------------------------------------------------


## Hosts a new game. [param config] selects the transport and server name.
## [param payload] is the local player's identity. On failure also emits
## [signal host_failed], on success [signal connected].
func host(config: NetwHostConfig, payload: JoinPayload) -> Error:
	host_started.emit()
	var attempt := _connector.host(config, payload)
	if not attempt.is_done():
		await attempt.finished
	var result := attempt.result
	if result == null or not result.is_ok():
		host_failed.emit(_reason(result))
		return ERR_CANT_CONNECT
	return OK


## Joins [param target]. [param payload] is the local player's identity.
## Returns [code]OK[/code] or an [enum Error]. On failure also emits
## [signal join_failed], on success [signal connected].
func join(target: NetwConnectTarget, payload: JoinPayload) -> Error:
	join_started.emit(target)
	var attempt := _connector.join(target, payload)
	var progress_cb := func(step: StringName, message: String, ratio: float) -> void:
		join_progress.emit(target, step, message, ratio)
	attempt.progress.connect(progress_cb)
	if not attempt.is_done():
		await attempt.finished
	if attempt.progress.is_connected(progress_cb):
		attempt.progress.disconnect(progress_cb)
	var result := attempt.result
	if result == null or not result.is_ok():
		join_failed.emit(target, result if result else NetwConnectResult.error(""))
		return ERR_CANT_CONNECT
	return OK


## Aborts the current in-progress attempt.
func abort_join() -> void:
	if _connector.current_attempt and not _connector.current_attempt.is_done():
		_connector.current_attempt.abort()

# -- Probing & refresh ------------------------------------------------------


## Re-probes every target and asks each bound directory to refresh its lobby
## list. Results arrive asynchronously via [signal target_updated].
func refresh() -> void:
	_discovery.refresh()


## Probes a single [param target], delivered via [signal target_updated].
func probe(target: NetwConnectTarget) -> void:
	_discovery.probe(target)

# -- Target list ------------------------------------------------------------


## Adds a saved [param target] and emits [signal target_added]. Set
## [param persist] to also write it to the saved server list.
func add_target(target: NetwConnectTarget, persist: bool = false) -> void:
	_discovery.add_target(target, persist)


## Removes a saved [param target] and emits [signal target_removed]. Set
## [param persist] to also drop it from the saved server list.
func remove_target(target: NetwConnectTarget, persist: bool = false) -> void:
	_discovery.remove_target(target, persist)

## The whole live list, with saved targets first, then discovered lobbies.
var targets: Array[NetwConnectTarget]:
	get:
		return _discovery.targets

## Only the saved (address-based) targets you added or loaded.
var saved_targets: Array[NetwConnectTarget]:
	get:
		return _discovery.saved_targets


## Returns the lobbies discovered under [param directory_id].
func get_discovered_targets(directory_id: StringName) -> Array[NetwConnectTarget]:
	return _discovery.get_discovered(directory_id)


## Returns the latest [NetwProbeResult] cached for [param target], or
## [code]null[/code] if it has not been probed yet.
func get_result(target: NetwConnectTarget) -> NetwProbeResult:
	return _discovery.get_result(target)

# -- Transports -------------------------------------------------------------


## The registered [NetwTransport]s available on this platform, offered by the
## Add and Host forms.
func available_transports() -> Array[NetwTransport]:
	var out: Array[NetwTransport] = []
	for transport in _connector.get_transports():
		if transport._is_available():
			out.append(transport)
	return out


## The registered [NetwTransport]s that can also host here, offered by the Host
## form (which drops a transport that connects but cannot listen locally).
func hostable_transports() -> Array[NetwTransport]:
	var out: Array[NetwTransport] = []
	for transport in available_transports():
		if transport._can_host_here():
			out.append(transport)
	return out


## Returns [code]true[/code] when a registered transport recognizes and can run
## [param target] on this platform.
func is_target_available(target: NetwConnectTarget) -> bool:
	if target == null:
		return false
	for transport in _connector.get_transports():
		if transport._can_join(target):
			return transport._is_available()
	return false

# -- Directories ------------------------------------------------------------


## Binds [param directory] so its lobbies appear as targets. A [LobbyDirectory]
## registered as a session service binds automatically, so call this only for a
## directory that is not a service.
func register_directory(directory: LobbyDirectory) -> void:
	_discovery.add_directory(directory)


## Unbinds [param directory].
func unregister_directory(directory: LobbyDirectory) -> void:
	_discovery.remove_directory(directory)


## The bound directories, in bind order.
func directories() -> Array[LobbyDirectory]:
	return _discovery.get_directories()

# -- Persistence ------------------------------------------------------------


## Loads the saved targets from disk into the live list. Omit [param path] to
## use the default server list path.
func load_server_list(path: String = "") -> void:
	if not path.is_empty():
		_server_list_path = path
	_discovery.load_server_list(_server_list_path)


## Writes the current saved targets to disk. Omit [param path] to use the
## default. Returns the [enum Error] from saving.
func save_server_list(path: String = "") -> Error:
	if not path.is_empty():
		_server_list_path = path
	return _discovery.save_server_list(_server_list_path)


# The message of a failed [NetwConnectResult], or a generic fallback.
func _reason(result: NetwConnectResult) -> String:
	if result == null:
		return "Connection failed."
	if not result.message.is_empty():
		return result.message
	return "Connection failed."
