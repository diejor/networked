## The server browser API: list servers, host a game, or join one.
##
## This is your entry point for everything that happens [i]before[/i] a match
## begins, discovering servers (typed-in addresses and directory lobbies like
## Steam), probing them for status, and finally hosting or joining. Once you
## are in a session, use [NetwTree] for in-game operations.
##
## [br][br]
## Most methods here take or return a [JoinTarget]: one
## connectable server in the list, a saved address you typed in, or a lobby
## discovered through a directory. You join one, probe one for status, and
## add or remove your own. See [JoinTarget] for the details.
##
## [br][br]
## Grab one from any node descendant of a [MultiplayerTree] and wire up the 
## signals you care about:
## [codeblock]
## var connect := Netw.ctx(self).connect
##
## # Keep the UI in sync as servers come and go.
## connect.target_added.connect(_on_server_found)
## connect.target_updated.connect(_on_server_status)
## connect.refresh()
##
## # Host a game...
## var config := ConnectHostConfig.new()
## config.backend = ENetBackend.new()
## config.server_name = "My Game"
## await connect.host(config, payload)
##
## # ...or join the one the player picked.
## await connect.join(picked_target, payload)
## [/codeblock]
##
## Host and join report failures two ways: the returned [enum Error] and the
## matching [signal host_failed] / [signal join_failed] signal. 
## [signal session_entered] fires once you are in.
##
## [br][br]
## [b]The live list.[/b] The session holds an in-memory list of targets: the
## [i]saved[/i] ones you add or load from disk (a [member JoinTarget.address]
## reached through a [BackendPeer]), plus lobbies that registered directories
## discover. [method get_targets] returns the whole list; [method get_saved_targets]
## and [method get_discovered_targets] return each half.
##
## [br][br]
## [b]Directories.[/b] A [LobbyDirectory] is a platform lobby integration (Steam,
## etc.) you [method register_directory] under an id like [code]&"steam"[/code].
## On [method refresh] each directory reports its current lobbies, which fold
## into the live list as directory targets.
##
## [br][br]
## [b]Probing.[/b] [method refresh] (all targets) and [method probe] (one) ask
## each target for a [ServerInfoResult] -- status, player count, latency. The
## result is cached per target and pushed to you via [signal target_updated];
## [method get_result] returns the latest, or [code]null[/code] before the
## first one arrives.
##
## [br][br]
## [b]Entering a session.[/b] [method host] and [method join] are where the
## connect layer hands off to the [MultiplayerTree]: a target sets the
## tree's [BackendPeer] and opens transport. [signal session_entered] fires on
## success, and [signal session_left] when the tree later goes offline.
class_name NetwConnect
extends RefCounted

## A saved or directory-discovered target was added to the live list.
signal target_added(target: JoinTarget)
## A target was removed from the live list.
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
## A join attempt failed. [param reason] is a human-readable string.
signal join_failed(target: JoinTarget, reason: String)
## A join attempt advanced through transport-specific progress.
signal join_progress(target: JoinTarget, step: StringName, message: String, ratio: float)
## A host attempt began.
signal host_started()
## A host attempt failed. [param reason] is a human-readable string.
signal host_failed(reason: String)
## A local entry (host or client join) succeeded.
signal session_entered()
## The bound tree returned to its offline state.
signal session_left()

var _ref: WeakRef


func _init(session: ConnectSession) -> void:
	_ref = weakref(session)
	session.target_added.connect(target_added.emit)
	session.target_removed.connect(target_removed.emit)
	session.target_updated.connect(target_updated.emit)
	session.directory_list_updated.connect(directory_list_updated.emit)
	session.directory_unavailable.connect(directory_unavailable.emit)
	session.join_started.connect(join_started.emit)
	session.join_failed.connect(join_failed.emit)
	session.join_progress.connect(join_progress.emit)
	session.host_started.connect(host_started.emit)
	session.host_failed.connect(host_failed.emit)
	session.session_entered.connect(session_entered.emit)
	session.session_left.connect(session_left.emit)


## Returns [code]true[/code] while the underlying [ConnectSession] is alive.
func is_valid() -> bool:
	return is_instance_valid(_ref.get_ref())


## Returns [code]true[/code] while the bound tree is already in a session.
##
## Read this after wiring [signal session_entered] to catch up when the tree
## entered before the binding, e.g. a debug auto-connect.
func is_session_active() -> bool:
	var s := _ref.get_ref() as ConnectSession
	return s.is_session_active() if s else false

# -- Host & join ------------------------------------------------------------


## Hosts a new game on the bound [MultiplayerTree]. [param config] selects the
## transport and the server name; [param payload] is the local player's identity.
## On failure also emits [signal host_failed], on success [signal session_entered].
func host(config: ConnectHostConfig, payload: JoinPayload) -> Error:
	var s := _ref.get_ref() as ConnectSession
	return await s.host(config, payload) if s else ERR_UNCONFIGURED


## Joins [param target] on the bound [MultiplayerTree]. [param payload] is the
## local player's identity. Returns [code]OK[/code] or an [enum Error]; on
## failure also emits [signal join_failed], on success [signal session_entered].
func join(target: JoinTarget, payload: JoinPayload) -> Error:
	var s := _ref.get_ref() as ConnectSession
	return await s.join(target, payload) if s else ERR_UNCONFIGURED


## Aborts the current in-progress join attempt.
func abort_join() -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.abort_join()

# -- Probing & refresh ------------------------------------------------------


## Re-probes every target and asks each registered directory to refresh its
## lobby list. Results arrive asynchronously via [signal target_updated].
func refresh() -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.refresh()


## Probes a single [param target] for its [ServerInfoResult], delivered via
## [signal target_updated]. Useful after adding one target instead of
## re-probing the whole list with [method refresh].
func probe(target: JoinTarget) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.probe(target)

# -- Target list ------------------------------------------------------------


## Adds a saved [param target] to the live list and emits [signal target_added].
## Set [param persist] to also write it to the saved server list.
func add_target(target: JoinTarget, persist: bool = false) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.add_target(target, persist)


## Removes a saved [param target] from the live list and emits [signal target_removed].
## Set [param persist] to also drop it from the saved server list.
func remove_target(target: JoinTarget, persist: bool = false) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.remove_target(target, persist)


## Returns the whole live list -- saved targets first, then each directory's
## discovered lobbies.
func get_targets() -> Array[JoinTarget]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_targets() if s else []


## Returns only the saved (address-based) targets -- the ones you added or
## loaded, not directory lobbies.
func get_saved_targets() -> Array[JoinTarget]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_saved_targets() if s else []


## Returns the lobbies the directory registered under [param directory_id]
## reported on the last [method refresh].
func get_discovered_targets(directory_id: StringName) -> Array[JoinTarget]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_discovered_targets(directory_id) if s else []


## Returns the latest [ServerInfoResult] cached for [param target], or
## [code]null[/code] if it has not been probed yet. Refreshed by
## [method refresh] / [method probe] and pushed via [signal target_updated].
func get_result(target: JoinTarget) -> ServerInfoResult:
	var s := _ref.get_ref() as ConnectSession
	return s.get_result(target) if s else null

# -- Directories -------------------------------------------------------------


## Registers [param directory] under [param id]. Its lobbies then appear
## as targets on [method refresh].
func register_directory(id: StringName, directory: LobbyDirectory) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.register_directory(id, directory)


## Removes the directory registered under [param id], if any.
func unregister_directory(id: StringName) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.unregister_directory(id)


## Returns the directory registered under [param id], or null.
func get_directory(id: StringName) -> LobbyDirectory:
	var s := _ref.get_ref() as ConnectSession
	return s.get_directory(id) if s else null


## Returns the ids of all registered directories, in registration order.
func get_directory_ids() -> Array[StringName]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_directory_ids() if s else []

# -- Persistence ------------------------------------------------------------


## Loads the saved targets from disk into the live list, replacing the
## current saved targets. Omit [param path] to use the session's configured default.
func load_server_list(path: String = "") -> void:
	var s := _ref.get_ref() as ConnectSession
	if not s:
		return
	if path.is_empty():
		s.load_server_list()
	else:
		s.load_server_list(path)


## Writes the current saved targets to disk. Omit [param path] to use the
## session's configured default. Returns the [enum Error] from saving.
func save_server_list(path: String = "") -> Error:
	var s := _ref.get_ref() as ConnectSession
	if not s:
		return ERR_UNCONFIGURED
	if path.is_empty():
		return s.save_server_list()
	return s.save_server_list(path)
