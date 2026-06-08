class_name AuthProbeResponder
extends RefCounted
## Server side of the same-port [code]NPRB[/code] probe.
##
## [AuthCoordinator] dispatches [code]NPRB[/code] auth packets here while it
## handles [code]NHEL[/code] hellos itself. This keeps the coordinator about
## authentication while the probe — its rate limit, active-probe cap, and
## [ServerInfo] reply — lives as one cohesive unit on both ends (see
## [AuthProbeClient] for the client half).
## [br][br]
## [b]auth_timeout dependency:[/b] probe peers are not closed by the server.
## The probing client owns the peer and closes it after receiving the reply.
## Stragglers (crashed or malicious probers) are reaped by
## [code]SceneMultiplayer.auth_timeout[/code] (default 3s). Setting
## [code]auth_timeout = 0[/code] disables this cleanup and lets probe slots
## accumulate up to [constant MAX_ACTIVE_PROBES]. Do not do that on
## production hosts.

## Maximum probe replies per second before further probes are answered
## with [constant AuthProtocol.ProbeStatus.BUSY].
const PROBE_RATE_LIMIT := 10

## Upper bound on concurrent pending probes. Bounds the
## [member _probe_peer_ids] dictionary, and excess probes get BUSY. Pairs with
## [SceneMultiplayer]'s [code]auth_timeout[/code], which reaps probe peers
## the client never closed.
const MAX_ACTIVE_PROBES := 32

var _api: SceneMultiplayer
var _tree: MultiplayerTree
var _server_info_source: ServerInfoSource
var _probe_timestamps_ms: Array[int] = []
var _probe_peer_ids: Dictionary[int, bool] = { }


## Sets the api used to send probe replies. Pass [code]null[/code] to unbind.
func bind_api(api: SceneMultiplayer) -> void:
	_api = api


## Stores the owning tree so probe replies can build a [ServerInfo] from
## live session state.
func set_tree(tree: MultiplayerTree) -> void:
	_tree = tree


## Sets the [ServerInfoSource] used to build probe replies. When
## [code]null[/code], a [DefaultServerInfoSource] is created on first use.
func set_server_info_source(source: ServerInfoSource) -> void:
	_server_info_source = source


## Handles a probe request. Builds a [ServerInfo] via the configured
## [ServerInfoSource], encodes it into an NPRB reply, and lets the client
## close. SceneMultiplayer's auth_timeout reaps stragglers. Excess probes
## (rate or concurrency cap) are answered with BUSY.
##
## Probe peers are tracked in [member _probe_peer_ids] so the matching
## peer_authentication_failed signal (fired when the client closes or
## auth_timeout reaps) does not produce a misleading warning.
func handle(peer_id: int) -> void:
	if not _api:
		return
	Netw.dbg.debug("Auth: peer %d probe request received", [peer_id])
	_probe_peer_ids[peer_id] = true

	if _is_rate_limited() or _probe_peer_ids.size() > MAX_ACTIVE_PROBES:
		Netw.dbg.debug(
			"Auth: peer %d probe deferred (busy)",
			[peer_id],
		)
		var busy := AuthProtocol.encode_probe_reply(
			AuthProtocol.ProbeStatus.BUSY,
		)
		_api.send_auth(peer_id, busy)
		return

	var source := _server_info_source
	if source == null:
		source = DefaultServerInfoSource.new()

	var info := source.build_server_info(_tree)
	var payload := ServerInfo.to_payload(info)
	var reply := AuthProtocol.encode_probe_reply(
		AuthProtocol.ProbeStatus.OK,
		payload,
	)
	_api.send_auth(peer_id, reply)


## Releases a probe peer when its auth fails. Returns [code]true[/code] if
## [param peer_id] was a tracked probe (the caller should then skip the
## "Auth failed" warning), [code]false[/code] otherwise.
func note_auth_failed(peer_id: int) -> bool:
	if _probe_peer_ids.erase(peer_id):
		# Expected for probes: client closed its peer after receiving the
		# reply, or [code]auth_timeout[/code] reaped a straggler.
		Netw.dbg.debug("Auth: probe peer %d released", [peer_id])
		return true
	return false


## Clears tracked probe state.
func clear() -> void:
	_probe_timestamps_ms.clear()
	_probe_peer_ids.clear()


# Records the current probe timestamp and returns whether the latest one
# exceeded the per-second cap. Keeps the ring trimmed to the window.
func _is_rate_limited() -> bool:
	var now_ms := Time.get_ticks_msec()
	var window_start_ms := now_ms - 1000
	while _probe_timestamps_ms.size() > 0 \
			and _probe_timestamps_ms[0] < window_start_ms:
		_probe_timestamps_ms.pop_front()
	_probe_timestamps_ms.push_back(now_ms)
	return _probe_timestamps_ms.size() > PROBE_RATE_LIMIT
