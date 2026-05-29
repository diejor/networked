## Headless facade over the connect subsystem.
##
## Owns a [ProbeManager], a [ProviderRegistry], and the live list of
## [JoinTarget]s (direct + provider-discovered). UIs observe via
## signals; the same [ConnectSession] can drive the reference
## [ConnectBrowser] or a custom UI.
##
## Persistence is opt-in. By default targets live in memory only.
## Call [method load_server_list] / [method save_server_list] to
## persist, or pass [code]persist = true[/code] to
## [method add_target] / [method remove_target].
##
## Lifecycle: instance the session, [method add_child] it, then
## [method bind_tree] the [MultiplayerTree] that hosts/joins should
## feed. Optionally [method register_provider] before
## [method refresh]. Use [method host] and [method join] for the
## two entry points.
class_name ConnectSession
extends Node


## A direct or provider-discovered target was added to the live list.
signal target_added(target: JoinTarget)

## A target was removed from the live list (direct removal or
## provider list refresh).
signal target_removed(target: JoinTarget)

## A new probe result or live lobby snapshot landed for [param target].
signal target_updated(target: JoinTarget, result: ServerInfoResult)

## A provider's lobby list refreshed. Raw provider feed for callers
## that want to observe per-provider state; most UIs should consume
## the unified target signals above instead.
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

## The bound [MultiplayerTree] reports a successful local entry
## (host or client join). Fires once per session.
signal session_entered()

## The bound [MultiplayerTree] returned to its offline state.
signal session_left()


## Path used for [ServerList] persistence by
## [method load_server_list] / [method save_server_list] when no
## explicit path is supplied.
@export var server_list_path: String = ServerList.DEFAULT_PATH


## Optional persistence handle. Null until [method load_server_list]
## assigns one (or a caller sets it directly). When set,
## [method save_server_list] writes the current direct targets back
## through it.
var server_list: ServerList = null

## How long [method join] waits for a provider's [signal LobbyProvider.peer_ready]
## (racing [signal LobbyProvider.lobby_join_failed]) before
## emitting [signal join_failed] with a timeout reason.
var provider_join_timeout: float = 10.0

## How long [method host] waits for a provider's [signal LobbyProvider.lobby_created]
## (racing [signal LobbyProvider.lobby_join_failed]) before
## emitting [signal host_failed] with a timeout reason.
var provider_host_timeout: float = 10.0


var _tree: MultiplayerTree
var _probes: ProbeManager
var _registry: ProviderRegistry
var _direct_targets: Array[JoinTarget] = []
var _provider_targets: Dictionary = {}  # StringName -> Array[JoinTarget]
var _provider_order: Array[StringName] = []
var _results: Dictionary = {}            # JoinTarget -> ServerInfoResult
var _tree_signals_bound: bool = false


func _init() -> void:
	_ensure_internals()


func _ready() -> void:
	_ensure_internals()
	_parent_internals()


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
	_bind_tree_signals()


## Returns the currently bound [MultiplayerTree], or [code]null[/code].
func get_tree_bound() -> MultiplayerTree:
	return _tree if is_instance_valid(_tree) else null


# -- Providers --------------------------------------------------------------

## Registers [param provider] under [param id] so its lobbies appear
## in [method get_targets]. Must be called before [method refresh] for
## the provider's lobbies to be polled.
func register_provider(id: StringName, provider: LobbyProvider) -> void:
	_ensure_internals()
	_registry.register(id, provider)
	if not _provider_order.has(id):
		_provider_order.append(id)
	var list_cb := _on_provider_list_updated.bind(id)
	if not provider.lobby_list_updated.is_connected(list_cb):
		provider.lobby_list_updated.connect(list_cb)
	var unavailable_cb := _on_provider_unavailable.bind(id)
	if not provider.provider_unavailable.is_connected(unavailable_cb):
		provider.provider_unavailable.connect(unavailable_cb)


## Removes the provider registered under [param id], if any.
func unregister_provider(id: StringName) -> void:
	_ensure_internals()
	var provider := _registry.get_provider(id)
	if provider != null:
		var list_cb := _on_provider_list_updated.bind(id)
		if provider.lobby_list_updated.is_connected(list_cb):
			provider.lobby_list_updated.disconnect(list_cb)
		var unavailable_cb := _on_provider_unavailable.bind(id)
		if provider.provider_unavailable.is_connected(unavailable_cb):
			provider.provider_unavailable.disconnect(unavailable_cb)
	_registry.unregister(id)
	_provider_order.erase(id)


## Returns the provider registered under [param id], or [code]null[/code].
func get_provider(id: StringName) -> LobbyProvider:
	_ensure_internals()
	return _registry.get_provider(id)


## Returns registered provider ids in registration order.
func get_provider_ids() -> Array[StringName]:
	return _provider_order.duplicate()


# -- Target list ------------------------------------------------------------

## Appends [param target] to the live list. When [param persist] is
## [code]true[/code] and a [member server_list] is loaded, the
## change is written to disk immediately.
##
## Provider-discovered targets arrive via
## [signal LobbyProvider.lobby_list_updated]; call this only for
## direct targets the UI is adding by hand.
func add_target(target: JoinTarget, persist: bool = false) -> void:
	if target == null:
		return
	if _direct_targets.has(target):
		return
	_direct_targets.append(target)
	target_added.emit(target)
	if persist:
		save_server_list()


## Removes [param target] from the live list (direct only). When
## [param persist] is [code]true[/code], the change is written to
## disk immediately.
func remove_target(target: JoinTarget, persist: bool = false) -> void:
	var idx := _direct_targets.find(target)
	if idx < 0:
		return
	_direct_targets.remove_at(idx)
	_results.erase(target)
	target_removed.emit(target)
	if persist:
		save_server_list()


## Returns the union of direct targets and provider-discovered
## lobby targets, in display order (direct first, then per-provider
## groups).
func get_targets() -> Array[JoinTarget]:
	var out: Array[JoinTarget] = []
	out.append_array(_direct_targets)
	for id in _provider_order:
		if not _provider_targets.has(id):
			continue
		var lobbies: Array[JoinTarget] = []
		lobbies.assign(_provider_targets[id])
		out.append_array(lobbies)
	return out


## Returns the live direct targets in their persisted order.
func get_direct_targets() -> Array[JoinTarget]:
	return _direct_targets.duplicate()


## Returns the provider-discovered targets for [param provider_id].
func get_provider_targets(provider_id: StringName) -> Array[JoinTarget]:
	var out: Array[JoinTarget] = []
	if not _provider_targets.has(provider_id):
		return out
	out.assign(_provider_targets[provider_id])
	return out


## Returns the latest [ServerInfoResult] for [param target], or
## [code]null[/code] when nothing has been observed yet.
func get_result(target: JoinTarget) -> ServerInfoResult:
	return _results.get(target, null)


# -- Persistence ------------------------------------------------------------

## Loads the persisted [ServerList] at [param path], replacing the
## current direct target list. Provider-discovered targets are not
## affected. Emits [signal target_removed] for the prior set and
## [signal target_added] for the loaded set.
func load_server_list(path: String = server_list_path) -> void:
	server_list_path = path
	var loaded := ServerList.load_or_new(path)
	server_list = loaded
	_replace_direct_targets(loaded.targets)


## Persists the current direct targets through
## [member server_list], creating a fresh [ServerList] when none is
## loaded. Returns the [enum @GlobalScope.Error] from
## [ResourceSaver.save].
func save_server_list(path: String = server_list_path) -> Error:
	server_list_path = path
	if server_list == null:
		server_list = ServerList.new()
	server_list.targets = _direct_targets.duplicate()
	return ServerList.save(server_list, path)


# -- Probing & refresh ------------------------------------------------------

## Cancels every in-flight probe, re-probes all direct targets, and
## asks every registered provider to refresh its lobby list.
func refresh() -> void:
	_ensure_internals()
	if _probes:
		_probes.cancel_all()
	for target in _direct_targets:
		_probes.query(target, _on_probe_result.bind(target))
	for id in _provider_order:
		var provider := _registry.get_provider(id)
		if provider:
			provider.list_lobbies()


## Issues a single probe for [param target]. Result lands via
## [signal target_updated].
func probe(target: JoinTarget) -> void:
	_ensure_internals()
	if target == null:
		return
	_probes.query(target, _on_probe_result.bind(target))


# -- Host & join ------------------------------------------------------------

## Hosts a new session. [param config] supplies the transport
## (backend template or provider id) plus server name; [param payload]
## carries player identity (username, spawner path). Returns OK on
## success, an [enum @GlobalScope.Error] otherwise; failure also
## emits [signal host_failed] with a human-readable reason.
func host(config: ConnectHostConfig, payload: JoinPayload) -> Error:
	if config == null:
		host_failed.emit("host config is null")
		return ERR_INVALID_PARAMETER
	if payload == null:
		host_failed.emit("join payload is null")
		return ERR_INVALID_PARAMETER
	var tree := get_tree_bound()
	if tree == null:
		host_failed.emit("no MultiplayerTree bound; call bind_tree first")
		return ERR_UNCONFIGURED
	host_started.emit()
	if config.is_direct():
		var backend := config.make_backend_instance()
		if backend == null:
			host_failed.emit("host config has no backend template")
			return ERR_INVALID_PARAMETER
		tree.backend = backend
		var err := await tree.host_player(payload)
		if err != OK:
			host_failed.emit("backend host_player failed (err %d)" % err)
			return err
		session_entered.emit()
		return OK
	# Provider host.
	var provider := _registry.get_provider(config.provider_id)
	if provider == null:
		host_failed.emit(
			"no provider registered for %s" % String(config.provider_id)
		)
		return ERR_DOES_NOT_EXIST
	provider.create_lobby(config.server_name)
	var race := await _race_provider(
		provider, provider.lobby_created, "lobby_created", provider_host_timeout
	)
	if race.kind != _RaceResult.KIND_READY:
		host_failed.emit(race.reason)
		return race.err
	var bind_err := await provider.bind(NetwTree.new(tree), payload)
	if bind_err != OK:
		host_failed.emit(
			"provider bind failed (err %d)" % bind_err
		)
		return bind_err
	session_entered.emit()
	return OK


## Joins [param target]. [param payload] carries player identity.
## Returns OK on success, an [enum @GlobalScope.Error] otherwise;
## failure also emits [signal join_failed] with a human-readable
## reason.
func join(target: JoinTarget, payload: JoinPayload) -> Error:
	if target == null:
		join_failed.emit(null, "target is null")
		return ERR_INVALID_PARAMETER
	if payload == null:
		join_failed.emit(target, "join payload is null")
		return ERR_INVALID_PARAMETER
	var tree := get_tree_bound()
	if tree == null:
		join_failed.emit(
			target, "no MultiplayerTree bound; call bind_tree first"
		)
		return ERR_UNCONFIGURED
	join_started.emit(target)
	if target.is_direct():
		var backend := target.make_backend_instance()
		if backend == null:
			join_failed.emit(target, "target has no backend template")
			return ERR_INVALID_PARAMETER
		var err := await tree.auto_connect_player(
			backend, target.address, payload
		)
		if err != OK:
			join_failed.emit(target, "direct connect failed (err %d)" % err)
			return err
		session_entered.emit()
		return OK
	var provider := _registry.get_provider(target.provider_id)
	if provider == null:
		join_failed.emit(
			target,
			"no provider registered for %s" % String(target.provider_id),
		)
		return ERR_DOES_NOT_EXIST
	provider.join_lobby(target.remote_id)
	var race := await _race_provider(
		provider, provider.peer_ready, "peer_ready", provider_join_timeout
	)
	if race.kind != _RaceResult.KIND_READY:
		join_failed.emit(target, race.reason)
		return race.err
	var bind_err := await provider.bind(NetwTree.new(tree), payload)
	if bind_err != OK:
		join_failed.emit(
			target, "provider bind failed (err %d)" % bind_err
		)
		return bind_err
	session_entered.emit()
	return OK


# -- Internals --------------------------------------------------------------


class _RaceResult extends RefCounted:
	const KIND_READY := 0
	const KIND_FAILED := 1
	const KIND_TIMEOUT := 2
	var kind: int = KIND_READY
	var reason: String = ""
	var err: Error = OK


# Races [param ready_signal] against [signal LobbyProvider.lobby_join_failed]
# and a [param timeout_s] timeout. Used for both
# [signal LobbyProvider.peer_ready] (join) and
# [signal LobbyProvider.lobby_created] (host). [param ready_label] is
# the human-readable name used in the timeout reason string.
func _race_provider(
	provider: LobbyProvider,
	ready_signal: Signal,
	ready_label: String,
	timeout_s: float,
) -> _RaceResult:
	var result := _RaceResult.new()
	result.kind = -1
	var failed_signal: Signal = provider.lobby_join_failed
	var st := get_tree()
	var on_ready := func(_arg = null) -> void:
		if result.kind != -1:
			return
		result.kind = _RaceResult.KIND_READY
	var on_failed := func(reason: String) -> void:
		if result.kind != -1:
			return
		result.kind = _RaceResult.KIND_FAILED
		result.reason = reason
		result.err = FAILED
	ready_signal.connect(on_ready, CONNECT_ONE_SHOT)
	failed_signal.connect(on_failed, CONNECT_ONE_SHOT)
	var timer := st.create_timer(timeout_s)
	var on_timeout := func() -> void:
		if result.kind != -1:
			return
		result.kind = _RaceResult.KIND_TIMEOUT
		result.reason = "timeout waiting for provider %s" % ready_label
		result.err = ERR_TIMEOUT
	timer.timeout.connect(on_timeout)
	while result.kind == -1:
		await st.process_frame
	if ready_signal.is_connected(on_ready):
		ready_signal.disconnect(on_ready)
	if failed_signal.is_connected(on_failed):
		failed_signal.disconnect(on_failed)
	return result


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
	target_updated.emit(target, result)


func _on_provider_list_updated(
	lobbies: Array[LobbyInfo], id: StringName
) -> void:
	var prior: Array[JoinTarget] = []
	if _provider_targets.has(id):
		prior.assign(_provider_targets[id])
	for target in prior:
		_results.erase(target)
		target_removed.emit(target)
	var fresh: Array[JoinTarget] = []
	for lobby in lobbies:
		var t := JoinTarget.new()
		t.provider_id = id
		t.remote_id = lobby.id
		t.display_name = lobby.lobby_name
		t.metadata = lobby.metadata
		fresh.append(t)
		var info := ServerInfo.new()
		info.players = lobby.players
		info.max_players = lobby.max_players
		_results[t] = ServerInfoResult.ok(info)
	_provider_targets[id] = fresh
	for target in fresh:
		target_added.emit(target)
		target_updated.emit(target, _results[target])
	provider_list_updated.emit(id, lobbies)


func _on_provider_unavailable(reason: String, id: StringName) -> void:
	provider_unavailable.emit(id, reason)


func _replace_direct_targets(loaded: Array[JoinTarget]) -> void:
	for target in _direct_targets.duplicate():
		_results.erase(target)
		target_removed.emit(target)
	_direct_targets.clear()
	for target in loaded:
		_direct_targets.append(target)
		target_added.emit(target)


func _ensure_internals() -> void:
	if _probes == null:
		_probes = ProbeManager.new()
	if _registry == null:
		_registry = ProviderRegistry.new()
	_parent_internals()


func _parent_internals() -> void:
	if not is_inside_tree():
		return
	if _probes.get_parent() == null:
		add_child(_probes)
	if _registry.get_parent() == null:
		add_child(_registry)
