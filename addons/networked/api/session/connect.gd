## The server browser API: list servers, host a game, or join one.
##
## This is your entry point for everything that happens [i]before[/i] a match
## begins, discovering servers (typed-in addresses and provider lobbies like
## Steam), probing them for status, and finally hosting or joining. Once you
## are in a session, use to [NetwTree] for in-game
## operations.
##
## [br][br]
## Most methods here take or return a [JoinTarget]: one
## connectable server in the list a direct address you typed in, or a lobby
## discovered through a provider. You join one, probe one for status, and
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
## [i]direct[/i] ones you add or load from disk (an [member JoinTarget.address]
## reached through a [BackendPeer]), plus lobbies that registered [i]providers[/i]
## discover. [method get_targets] returns the whole list; [method get_direct_targets]
## and [method get_provider_targets] return each half.
##
## [br][br]
## [b]Providers.[/b] A [LobbyProvider] is a platform lobby integration (Steam,
## etc.) you [method register_provider] under an id like [code]&"steam"[/code].
## On [method refresh] each provider reports its current lobbies, which fold
## into the live list as provider targets; a target carrying that id later
## hosts / joins through that provider.
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
## connect layer hands off to the [MultiplayerTree]: a direct target sets the
## tree's [BackendPeer] and opens transport (host, or probe-then-auto-connect),
## while a provider target negotiates a lobby and binds the resulting peer into
## the tree. Either way [signal session_entered] fires on success, and
## [signal session_left] when the tree later goes offline.
class_name NetwConnect
extends RefCounted


## A direct or provider-discovered target was added to the live list.
signal target_added(target: JoinTarget)
## A target was removed from the live list.
signal target_removed(target: JoinTarget)
## A new probe result or live lobby snapshot landed for [param target].
signal target_updated(target: JoinTarget, result: ServerInfoResult)
## A provider's lobby list refreshed.
signal provider_list_updated(
	provider_id: StringName, lobbies: Array[LobbyInfo]
)
## A registered provider reported that its transport is unavailable.
signal provider_unavailable(provider_id: StringName, reason: String)
## A join attempt began against [param target].
signal join_started(target: JoinTarget)
## A join attempt failed. [param reason] is a human-readable string.
signal join_failed(target: JoinTarget, reason: String)
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
	session.provider_list_updated.connect(provider_list_updated.emit)
	session.provider_unavailable.connect(provider_unavailable.emit)
	session.join_started.connect(join_started.emit)
	session.join_failed.connect(join_failed.emit)
	session.host_started.connect(host_started.emit)
	session.host_failed.connect(host_failed.emit)
	session.session_entered.connect(session_entered.emit)
	session.session_left.connect(session_left.emit)


## Returns [code]true[/code] while the underlying [ConnectSession] is alive.
func is_valid() -> bool:
	return is_instance_valid(_ref.get_ref())


# -- Host & join ------------------------------------------------------------

## Hosts a new game on the bound [MultiplayerTree]. [param config] selects the
## transport (a direct [BackendPeer], or a provider id) and the server name;
## [param payload] is the local player's identity. On failure also emits [signal host_failed], on success
## [signal session_entered].
func host(config: ConnectHostConfig, payload: JoinPayload) -> Error:
	var s := _ref.get_ref() as ConnectSession
	return await s.host(config, payload) if s else ERR_UNCONFIGURED


## Joins [param target] on the bound [MultiplayerTree]. A direct target builds
## its backend and auto-connects to the address. A provider target negotiates
## the lobby and binds the resulting peer into the tree. [param payload] is the
## local player's identity. Returns [code]OK[/code] or an [enum Error]; on
## failure also emits [signal join_failed], on success [signal session_entered].
func join(target: JoinTarget, payload: JoinPayload) -> Error:
	var s := _ref.get_ref() as ConnectSession
	return await s.join(target, payload) if s else ERR_UNCONFIGURED


# -- Probing & refresh ------------------------------------------------------

## Re-probes every target and asks each registered provider to refresh its
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

## Adds a direct [param target] to the live list and emits
## [signal target_added]. Set [param persist] to also write it to the saved
## server list. (Provider lobbies are discovered, not added this way.)
func add_target(target: JoinTarget, persist: bool = false) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.add_target(target, persist)


## Removes a direct [param target] from the live list and emits
## [signal target_removed]. Set [param persist] to also drop it from the saved
## server list.
func remove_target(target: JoinTarget, persist: bool = false) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.remove_target(target, persist)


## Returns the whole live list -- direct targets first, then each provider's
## discovered lobbies.
func get_targets() -> Array[JoinTarget]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_targets() if s else []


## Returns only the direct (address-based) targets -- the ones you added or
## loaded, not provider lobbies.
func get_direct_targets() -> Array[JoinTarget]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_direct_targets() if s else []


## Returns the lobbies the provider registered under [param provider_id]
## reported on the last [method refresh].
func get_provider_targets(provider_id: StringName) -> Array[JoinTarget]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_provider_targets(provider_id) if s else []


## Returns the latest [ServerInfoResult] cached for [param target], or
## [code]null[/code] if it has not been probed yet. Refreshed by
## [method refresh] / [method probe] and pushed via [signal target_updated].
func get_result(target: JoinTarget) -> ServerInfoResult:
	var s := _ref.get_ref() as ConnectSession
	return s.get_result(target) if s else null


# -- Providers --------------------------------------------------------------

## Registers [param provider] (a platform lobby integration such as [SteamLobbyProvider])
## under [param id]. Its lobbies then appear as targets on [method refresh].
func register_provider(id: StringName, provider: LobbyProvider) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.register_provider(id, provider)


## Removes the provider registered under [param id], if any.
func unregister_provider(id: StringName) -> void:
	var s := _ref.get_ref() as ConnectSession
	if s:
		s.unregister_provider(id)


## Returns the provider registered under [param id], or null.
func get_provider(id: StringName) -> LobbyProvider:
	var s := _ref.get_ref() as ConnectSession
	return s.get_provider(id) if s else null


## Returns the ids of all registered providers, in registration order.
func get_provider_ids() -> Array[StringName]:
	var s := _ref.get_ref() as ConnectSession
	return s.get_provider_ids() if s else []


# -- Persistence ------------------------------------------------------------

## Loads the saved direct targets from disk into the live list, replacing the
## current direct targets (provider lobbies are untouched). Omit [param path]
## to use the session's configured default.
func load_server_list(path: String = "") -> void:
	var s := _ref.get_ref() as ConnectSession
	if not s:
		return
	if path.is_empty():
		s.load_server_list()
	else:
		s.load_server_list(path)


## Writes the current direct targets to disk. Omit [param path] to use the
## session's configured default. Returns the [enum Error] from saving.
func save_server_list(path: String = "") -> Error:
	var s := _ref.get_ref() as ConnectSession
	if not s:
		return ERR_UNCONFIGURED
	if path.is_empty():
		return s.save_server_list()
	return s.save_server_list(path)
