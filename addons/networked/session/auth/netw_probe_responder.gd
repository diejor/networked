## Server responder for the same-port [code]NPRB[/code] probe protocol.
class_name NetwProbeResponder
extends RefCounted

const PROBE_RATE_LIMIT := 10
const MAX_ACTIVE_PROBES := 32

var _api: SceneMultiplayer
var _owner: NetwMultiplayer
var _server_info_provider: Callable
var _probe_timestamps_ms: Array[int] = []
var _probe_peer_ids: Dictionary[int, bool] = { }


## Sets the API used to send replies.
func bind_api(api: SceneMultiplayer) -> void:
	_api = api


## Sets the owning session the built-in descriptor reads live state from.
func set_owner(api: NetwMultiplayer) -> void:
	_owner = api


## Sets the per-session provider used to build replies. When invalid, the
## project-wide [method Netw.configure_server_info] registration answers, then
## [method NetwServerInfo.from_session].
func set_server_info_provider(provider: Callable) -> void:
	_server_info_provider = provider


## Handles one probe request from [param peer_id].
func handle(peer_id: int) -> void:
	if _api == null:
		return
	_probe_peer_ids[peer_id] = true
	if _is_rate_limited() or _probe_peer_ids.size() > MAX_ACTIVE_PROBES:
		_api.send_auth(
			peer_id,
			AuthProtocol.encode_probe_reply(AuthProtocol.ProbeStatus.BUSY),
		)
		return
	_api.send_auth(
		peer_id,
		AuthProtocol.encode_probe_reply(
			AuthProtocol.ProbeStatus.OK,
			NetwServerInfo.to_payload(_resolve_info()),
		),
	)


# Resolves the reply through the three binding layers: a per-session override,
# then the project-wide registration, then the built-in default.
func _resolve_info() -> NetwServerInfo:
	if _server_info_provider.is_valid():
		return _server_info_provider.call(_owner)
	var registered := Netw.resolve_server_info_provider()
	if registered.is_valid():
		return registered.call(_owner)
	return NetwServerInfo.from_session(_owner)


## Retires a tracked probe after authentication closes.
func note_auth_failed(peer_id: int) -> bool:
	return _probe_peer_ids.erase(peer_id)


## Clears tracked probe state.
func clear() -> void:
	_api = null
	_owner = null
	_server_info_provider = Callable()
	_probe_timestamps_ms.clear()
	_probe_peer_ids.clear()


func _is_rate_limited() -> bool:
	var now := Time.get_ticks_msec()
	var window_start := now - 1000
	while not _probe_timestamps_ms.is_empty() \
			and _probe_timestamps_ms[0] < window_start:
		_probe_timestamps_ms.pop_front()
	_probe_timestamps_ms.push_back(now)
	return _probe_timestamps_ms.size() > PROBE_RATE_LIMIT
