## [LobbyDirectory] backed by Nakama relay matches.
##
## Relay hosting does not open a listening socket. The host is the participant
## that claims peer id [code]1[/code], so web exports can host through this
## directory.
## [codeblock]
## MultiplayerTree
## └── NakamaLobbyDirectory
##     ├── NakamaWrapper
##     ├── realtime socket
##     └── relay match
##         └── peer 1 = host
## [/codeblock]
## [method _host_lobby] writes browse metadata to Nakama storage because relay
## matches only expose match ids and member counts. [method _list_lobbies] merges
## that storage with [method NakamaWrapper.list_matches]. A lobby's address is
## its relay match id, so a player joins one that was never browsed by pasting
## the id its host shared.
class_name NakamaLobbyDirectory
extends LobbyDirectory

const Async := preload("res://addons/networked/gdscript/async.gd")

## Nakama server key, matching the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Relay host name or address, without scheme.
@export var host: String = "127.0.0.1"

## Relay port. Use [code]443[/code] behind a TLS terminating tunnel.
@export var port: int = 7350

## When [code]true[/code], connects over [code]https[/code] and [code]wss[/code].
@export var use_ssl: bool = false

## Device id used for authentication. Empty falls back to
## [method OS.get_unique_id]. Set distinct ids per instance for reliable local
## two-client testing.
@export var device_id: String = ""

## Local Nakama username used for device authentication.
##
## Empty falls back to [method LobbyDirectory._local_member_name].
@export var local_member_name: String = ""

## Appends a per-process suffix to [member device_id] and
## [member local_member_name] in debug desktop runs.
##
## This prevents local multi-instance runs from authenticating with the same
## Nakama username. Override the suffix with [code]--nakama-instance=name[/code],
## [code]--netw-instance=name[/code], [code]--instance=name[/code], or
## [code]NAKAMA_INSTANCE_SUFFIX[/code].
@export var uniquify_debug_identity: bool = true

## Seconds to wait for a match to fully join before failing.
@export_range(1.0, 30.0, 0.5, "suffix:s") var connect_timeout: float = 10.0

## Maximum number of simultaneous lobby members advertised on the browse card.
## An absent or [code]0[/code] [code]max_players[/code] advert key falls back
## to this.
@export_range(1, 250, 1, "or_greater", "suffix:players") var max_clients: int = 8

## Tag stored on every browse card and required on received cards, so different
## games sharing a Nakama server do not pollute each other's lobby lists.
@export var browser_filter_uid: String = "networked"


## Browse metadata for one relay match.
##
## [method to_dict] is stored by [method NakamaWrapper.write_lobby_card].
## [method to_server_info] creates the browse entry
## [method LobbyDirectory.publish_lobbies] carries.
## [codeblock]
## Storage
## └── match_id
##     └── LobbyCard.to_dict()
##
## Browse
## └── LobbyCard.from_dict(match_id, value).to_server_info(size)
## [/codeblock]
class LobbyCard:
	extends Resource

	## Relay match id this card describes. Carried as the storage key, not in
	## the serialized body.
	@export var match_id: String = ""

	## Advertised lobby name.
	@export var lobby_name: String = ""

	## Host display name.
	@export var host: String = ""

	## Hosting tree's [member MultiplayerTree.app_id] build tag, compared by the
	## browser compatibility gate.
	@export var app_id: String = ""

	## Game tag, matching [member NakamaLobbyDirectory.browser_filter_uid].
	@export var uid: String = ""

	## Maximum member count.
	@export var max_players: int = 0

	## Advertised [enum NetwServerInfo.Visibility].
	@export var visibility: NetwServerInfo.Visibility = \
			NetwServerInfo.VISIBILITY_PUBLIC


	## Serializes the card body stored under the match id key.
	func to_dict() -> Dictionary:
		return {
			"name": lobby_name,
			"host": host,
			"app_id": app_id,
			"uid": uid,
			"max": max_players,
			"visibility": int(visibility),
		}


	## Rebuilds a [NakamaLobbyDirectory.LobbyCard] from a browse read.
	static func from_dict(match_id: String, data: Dictionary) -> LobbyCard:
		var card := LobbyCard.new()
		card.match_id = match_id
		card.lobby_name = String(data.get("name", ""))
		card.host = String(data.get("host", ""))
		card.app_id = String(data.get("app_id", ""))
		card.uid = String(data.get("uid", ""))
		card.max_players = int(data.get("max", 0))
		card.visibility = (
				int(data.get("visibility", NetwServerInfo.VISIBILITY_PUBLIC)) as NetwServerInfo.Visibility
		)
		return card


	## Builds the [NetwServerInfo] for a browse entry with [param players]
	## members live in the match right now.
	func to_server_info(players: int) -> NetwServerInfo:
		var info := NetwServerInfo.new()
		info.app_id = StringName(app_id)
		info.players = players
		info.max_players = max_players
		info.visibility = visibility
		info.metadata = { "host": host, "uid": uid }
		return info


var _wrapper: NakamaWrapper
var _peer: MultiplayerPeer
var _hosted_match_id: String = ""
var _session_bound: bool = false # shared session resolved lazily on first connect


## Initializes the internal [NakamaWrapper].
func _service_entered(_api: NetwMultiplayer) -> void:
	_wrapper = NakamaWrapper.new()
	if not NakamaWrapper.is_addon_present():
		push_warning("NakamaLobbyDirectory: Nakama addon not present.")
		provider_unavailable.emit.call_deferred("Nakama addon not present")
		return
	_wrapper.match_join_error.connect(_on_match_join_error)
	_wrapper.socket_closed.connect(_on_socket_closed)


## Cleans up the hosted match and relay socket.
func _service_exiting(_api: NetwMultiplayer) -> void:
	if _wrapper != null:
		if not _hosted_match_id.is_empty():
			_wrapper.delete_lobby_card(_hosted_match_id)
			_hosted_match_id = ""
		_wrapper.leave()
	_peer = null


## Creates a relay match and publishes its browse card.
##
## [constant NetwServerInfo.VISIBILITY_PRIVATE] skips the card and stays
## join-by-id only.
func _host_lobby(settings: Dictionary) -> void:
	if not await _ensure_connected():
		fail(ERR_UNAVAILABLE, "the Nakama relay is unreachable.")
		return
	_wrapper.create_match()
	var peer := await _await_match("_host_lobby")
	if peer == null:
		fail(ERR_CANT_CREATE, "the relay produced no match peer.")
		return
	await _publish_card(settings)
	deliver(peer)


# Writes the browse card for a freshly hosted match, downgrading FRIENDS_ONLY to
# PRIVATE because Nakama friend gating is not wired yet. PRIVATE skips the card
# so the match is reachable only by sharing its id.
func _publish_card(settings: Dictionary) -> void:
	var visibility := int(
		settings.get(
			"visibility",
			NetwServerInfo.VISIBILITY_PUBLIC,
		),
	) as NetwServerInfo.Visibility
	if visibility == NetwServerInfo.VISIBILITY_FRIENDS_ONLY:
		push_warning(
			"NakamaLobbyDirectory: FRIENDS_ONLY unsupported, hosting PRIVATE.",
		)
		visibility = NetwServerInfo.VISIBILITY_PRIVATE
	if visibility == NetwServerInfo.VISIBILITY_PRIVATE:
		return
	var mid := _wrapper.match_id()
	if mid.is_empty():
		return
	var card := LobbyCard.new()
	card.match_id = mid
	card.lobby_name = String(settings.get("name", ""))
	card.host = _local_member_name()
	card.app_id = _local_app_id()
	card.uid = browser_filter_uid
	var wanted := int(settings.get("max_players", 0))
	card.max_players = wanted if wanted > 0 else max_clients
	card.visibility = visibility
	_hosted_match_id = mid
	var ok := await _wrapper.write_lobby_card(mid, card.to_dict())
	if not ok:
		push_warning("NakamaLobbyDirectory: failed to publish lobby card.")
		_hosted_match_id = ""


## Joins the relay match [param address] names and delivers its connected
## peer.
##
## [param address] is a relay match id, whether it came from a browse row or
## from a host sharing it, so joining never depends on having browsed first.
func _join_lobby(address: String) -> void:
	if address.is_empty():
		fail(ERR_INVALID_PARAMETER, "the match id is empty.")
		return
	if not await _ensure_connected():
		fail(ERR_UNAVAILABLE, "the Nakama relay is unreachable.")
		return
	_wrapper.join_match(address)
	var peer := await _await_match("_join_lobby")
	if peer == null:
		fail(ERR_CANT_CONNECT, "the relay produced no match peer.")
		return
	deliver(peer)


## Lists public relay lobbies.
##
## Stored browse cards provide metadata. [method NakamaWrapper.list_matches]
## provides live member counts. Cards whose match has ended are skipped.
func _list_lobbies() -> void:
	var addresses := PackedStringArray()
	var names := PackedStringArray()
	var infos: Array[NetwServerInfo] = []
	if not await _ensure_connected():
		publish_lobbies(addresses, names, infos)
		return
	var cards := await _wrapper.read_lobby_cards()
	var matches := await _wrapper.list_matches()
	var live_sizes: Dictionary = { }
	for m in matches:
		live_sizes[String(m.match_id)] = int(m.size)

	for mid in cards:
		if not live_sizes.has(mid):
			continue
		var card := LobbyCard.from_dict(mid, cards[mid])
		if card.uid != browser_filter_uid:
			continue
		addresses.append(mid)
		names.append(card.lobby_name)
		infos.append(card.to_server_info(int(live_sizes[mid])))
	publish_lobbies(addresses, names, infos)


## Returns the [enum LobbyDirectory.Capability] flags this directory honors:
## browse and persona resolution.
func _capabilities() -> int:
	return (
			LobbyDirectory.CAPABILITY_BROWSE
			| LobbyDirectory.CAPABILITY_FRIEND_NAMES
	)


## Deletes the host browse card and leaves the relay match.
func _leave_lobby() -> void:
	if _wrapper != null:
		if not _hosted_match_id.is_empty():
			await _wrapper.delete_lobby_card(_hosted_match_id)
			_hosted_match_id = ""
		_wrapper.leave()
	_peer = null


## Nakama lobbies join through [NakamaRelayPeer] by match id.
func _peer_class() -> StringName:
	return &"NakamaRelayPeer"


## Browsers name this provider "Nakama".
func _display_name() -> String:
	return "Nakama"


## The relay needs the Nakama addon, and nothing works without it.
func _is_available() -> bool:
	return NakamaWrapper.is_addon_present()


## A relay match opens no listening socket, so a web export can host one.
func _can_host_here() -> bool:
	return NakamaWrapper.is_addon_present()


## A Nakama address is an opaque relay match id.
func _address_label() -> String:
	return "Match ID"


## Points a player at where a match id comes from.
func _address_help() -> String:
	return "Paste the Nakama match id shared by the host."


## Returns the active relay match id others join this host by.
func _join_address() -> String:
	return _wrapper.match_id() if _wrapper != null else ""


## Returns the active [NakamaWrapper], or [code]null[/code] before connect.
func wrapper() -> NakamaWrapper:
	return _wrapper


## Resolves [param peer_id] to its Nakama username when known.
func _member_name(peer_id: int) -> String:
	if _wrapper == null:
		return LobbyDirectory.member_name_default(peer_id)
	var name := _wrapper.username_for_peer(peer_id)
	return name if not name.is_empty() else LobbyDirectory.member_name_default(peer_id)


## Returns [member local_member_name] when configured.
func _local_member_name() -> String:
	return _effective_local_member_name()


# The hosting tree's build tag, stamped on the card so a browser can flag a
# lobby it would be rejected from before it tries to join.
func _local_app_id() -> String:
	var session: NetwSessionHandle = Netw.session(self)
	return String(session.config.app_id) if session else ""


func _ensure_connected() -> bool:
	if _wrapper == null or not NakamaWrapper.is_addon_present():
		return false
	if _wrapper.is_ready():
		return true
	# Bind the shared account lazily, after tree setup. add_child inside the
	# _service_entered window fails while the tree is still building its children,
	# so the shared account resolves lazily instead.
	if not _session_bound:
		_session_bound = true
		var session := NakamaSessionService.of(self)
		if session != null:
			_wrapper.use_session(session)
	var res := await _wrapper.connect_async(
		self,
		{
			"server_key": server_key,
			"host": host,
			"port": port,
			"use_ssl": use_ssl,
			"device_id": _effective_device_id(),
			"username": _effective_local_member_name(),
		},
	)
	if not res.ok:
		var details := "connect to %s:%d failed: %s" % [host, port, res.error]
		push_warning("NakamaLobbyDirectory: %s" % details)
		provider_unavailable.emit(details)
		return false
	return true


func _effective_device_id() -> String:
	return _with_debug_instance_suffix(device_id)


func _effective_local_member_name() -> String:
	var name := local_member_name
	if name.is_empty():
		name = LobbyDirectory.local_member_name_default()
	return _with_debug_instance_suffix(name)


# Appends the unique process ID suffix if running in debug desktop mode.
func _with_debug_instance_suffix(base: String) -> String:
	if not _should_uniquify_debug_identity():
		return base
	if base.is_empty():
		base = "nakama-debug"
	return (base + "-" + str(OS.get_process_id())).left(128)


# Returns true if running a debug desktop instance.
func _should_uniquify_debug_identity() -> bool:
	return (
			uniquify_debug_identity
			and OS.has_feature("debug")
			and not (OS.has_feature("web") or OS.get_name() == "Web")
	)


func _await_match(op: String) -> MultiplayerPeer:
	var timer := get_tree().create_timer(connect_timeout)
	var outcome := await Async.timeout_or_failure(
		_wrapper.match_joined,
		_wrapper.match_join_error,
		timer,
	)
	if outcome.result != "success":
		push_error(
			"NakamaLobbyDirectory: %s did not join (%s)."
			% [op, outcome.result],
		)
		_wrapper.leave()
		return null
	_peer = _wrapper.peer()
	return _peer


func _on_match_join_error(message: String) -> void:
	push_warning("NakamaLobbyDirectory: match join error: %s" % [message])


func _on_socket_closed() -> void:
	_peer = null
