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
## [method host_lobby] writes browse metadata to Nakama storage because relay
## matches only expose match ids and member counts. [method list_lobbies] merges
## that storage with [method NakamaWrapper.list_matches].
class_name NakamaLobbyDirectory
extends LobbyDirectory

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
## Empty falls back to [method LobbyDirectory.get_local_member_name].
@export var local_member_name: String = ""

## Seconds to wait for a match to fully join before failing.
@export_range(1.0, 30.0, 0.5, "suffix:s") var connect_timeout: float = 10.0

## Maximum number of simultaneous lobby members advertised on the browse card.
## A [member LobbyDirectory.HostOptions.max_players] of [code]0[/code] falls back
## to this.
@export_range(1, 250, 1, "or_greater", "suffix:players") var max_clients: int = 8

## Tag stored on every browse card and required on received cards, so different
## games sharing a Nakama server do not pollute each other's lobby lists.
@export var browser_filter_uid: String = "networked"


## Browse metadata for one relay match.
##
## [method to_dict] is stored by [method NakamaWrapper.write_lobby_card].
## [method to_lobby_info] creates the browse entry returned by
## [method list_lobbies].
## [codeblock]
## Storage
## └── match_id
##     └── LobbyCard.to_dict()
##
## Browse
## └── LobbyCard.from_dict(match_id, value).to_lobby_info(id, size)
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

	## Advertised [enum LobbyDirectory.Visibility].
	@export var visibility: LobbyDirectory.Visibility = LobbyDirectory.Visibility.PUBLIC


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
				int(data.get("visibility", LobbyDirectory.Visibility.PUBLIC)) as LobbyDirectory.Visibility
		)
		return card


	## Builds the [LobbyDirectory.LobbyInfo] for a browse entry.
	func to_lobby_info(id: int, players: int) -> LobbyDirectory.LobbyInfo:
		return LobbyDirectory.LobbyInfo.make(
			id,
			lobby_name,
			players,
			max_players,
			{ "match_id": match_id, "app_id": app_id },
			host,
			visibility,
		)


var _wrapper: NakamaWrapper
var _peer: MultiplayerPeer
var _hosted_match_id: String = ""
var _id_to_match: Dictionary = { } # synthetic int id -> match id
var _session_bound: bool = false # shared session resolved lazily on first connect


## Initializes the internal [NakamaWrapper].
func service_entered(_mt: MultiplayerTree) -> void:
	_wrapper = NakamaWrapper.new()
	if not NakamaWrapper.is_addon_present():
		Netw.dbg.warn("NakamaLobbyDirectory: Nakama addon not present.")
		provider_unavailable.emit.call_deferred("Nakama addon not present")
		return
	_wrapper.match_join_error.connect(_on_match_join_error)
	_wrapper.socket_closed.connect(_on_socket_closed)


## Cleans up the hosted match and relay socket.
func service_exiting(_mt: MultiplayerTree) -> void:
	if _wrapper != null:
		if not _hosted_match_id.is_empty():
			_wrapper.delete_lobby_card(_hosted_match_id)
			_hosted_match_id = ""
		_wrapper.leave()
	_peer = null


## Creates a relay match and publishes its browse card.
##
## [constant LobbyDirectory.Visibility.PRIVATE] skips the card and stays
## join-by-id only.
func host_lobby(options: LobbyDirectory.HostOptions) -> MultiplayerPeer:
	if not await _ensure_connected():
		return null
	_wrapper.create_match()
	var peer := await _await_match("host_lobby")
	if peer != null:
		await _publish_card(options)
	return peer


# Writes the browse card for a freshly hosted match, downgrading FRIENDS_ONLY to
# PRIVATE because Nakama friend gating is not wired yet. PRIVATE skips the card
# so the match is reachable only by sharing its id.
func _publish_card(options: LobbyDirectory.HostOptions) -> void:
	var visibility := options.visibility
	if visibility == LobbyDirectory.Visibility.FRIENDS_ONLY:
		Netw.dbg.warn(
			"NakamaLobbyDirectory: FRIENDS_ONLY unsupported, hosting PRIVATE.",
		)
		visibility = LobbyDirectory.Visibility.PRIVATE
	if visibility == LobbyDirectory.Visibility.PRIVATE:
		return
	var mid := _wrapper.match_id()
	if mid.is_empty():
		return
	var card := LobbyCard.new()
	card.match_id = mid
	card.lobby_name = options.server_name
	card.host = get_local_member_name()
	card.app_id = _local_app_id()
	card.uid = browser_filter_uid
	card.max_players = options.max_players if options.max_players > 0 else max_clients
	card.visibility = visibility
	_hosted_match_id = mid
	var ok := await _wrapper.write_lobby_card(mid, card.to_dict())
	if not ok:
		Netw.dbg.warn("NakamaLobbyDirectory: failed to publish lobby card.")
		_hosted_match_id = ""


## Joins the relay match named by [param match_id].
func join_match_peer(match_id: String) -> MultiplayerPeer:
	if match_id.is_empty():
		Netw.dbg.warn("NakamaLobbyDirectory: empty match id.")
		return null
	if not await _ensure_connected():
		return null
	_wrapper.join_match(match_id)
	return await _await_match("join_match_peer")


## Joins a browse entry by the synthetic id from [method list_lobbies].
##
## Use [method join_match_peer] when the relay match id is already known.
func join_lobby_peer(lobby_id: int) -> MultiplayerPeer:
	var mid := String(_id_to_match.get(lobby_id, ""))
	if mid.is_empty():
		Netw.dbg.warn(
			"NakamaLobbyDirectory: unknown lobby id %d; browse first or use " +
			"join_match_peer with the string match id.",
			[lobby_id],
		)
		return null
	return await join_match_peer(mid)


## Lists public relay lobbies.
##
## Stored browse cards provide metadata. [method NakamaWrapper.list_matches]
## provides live member counts. Cards whose match has ended are skipped.
func list_lobbies() -> void:
	if not await _ensure_connected():
		lobby_list_updated.emit([] as Array[LobbyDirectory.LobbyInfo])
		return
	var cards := await _wrapper.read_lobby_cards()
	var matches := await _wrapper.list_matches()
	var live_sizes: Dictionary = { }
	for m in matches:
		live_sizes[String(m.match_id)] = int(m.size)

	_id_to_match.clear()
	var out: Array[LobbyDirectory.LobbyInfo] = []
	var next_id := 1
	for mid in cards:
		if not live_sizes.has(mid):
			continue
		var card := LobbyCard.from_dict(mid, cards[mid])
		if card.uid != browser_filter_uid:
			continue
		_id_to_match[next_id] = mid
		out.append(card.to_lobby_info(next_id, int(live_sizes[mid])))
		next_id += 1
	lobby_list_updated.emit(out)


## Returns the [enum LobbyDirectory.Capability] flags this directory honors:
## browse and persona resolution.
func capabilities() -> int:
	return LobbyDirectory.Capability.BROWSE | LobbyDirectory.Capability.FRIEND_NAMES


## Deletes the host browse card and leaves the relay match.
func leave_lobby() -> void:
	if _wrapper != null:
		if not _hosted_match_id.is_empty():
			await _wrapper.delete_lobby_card(_hosted_match_id)
			_hosted_match_id = ""
		_wrapper.leave()
	_peer = null


## Stamps a [JoinTarget] whose address is the relay match id.
func make_join_target(lobby: LobbyDirectory.LobbyInfo) -> JoinTarget:
	var target := JoinTarget.new()
	target.display_name = lobby.lobby_name
	target.address = String(lobby.metadata.get("match_id", str(lobby.id)))
	target.metadata = lobby.metadata.duplicate()
	target.backend = NakamaBackend.new()
	return target


## Returns the active relay match id for [method MultiplayerTree.join].
func get_join_address() -> String:
	return _wrapper.match_id() if _wrapper != null else ""


## Returns the active [NakamaWrapper], or [code]null[/code] before connect.
func wrapper() -> NakamaWrapper:
	return _wrapper


## Resolves [param peer_id] to its Nakama username when known.
func get_member_name(peer_id: int) -> String:
	if _wrapper == null:
		return super.get_member_name(peer_id)
	var name := _wrapper.username_for_peer(peer_id)
	return name if not name.is_empty() else super.get_member_name(peer_id)


## Returns [member local_member_name] when configured.
func get_local_member_name() -> String:
	if not local_member_name.is_empty():
		return local_member_name
	return super.get_local_member_name()


# The hosting tree's build tag, stamped on the card so a browser can flag a
# lobby it would be rejected from before it tries to join.
func _local_app_id() -> String:
	var mt := MultiplayerTree.resolve(self)
	return String(mt.app_id) if mt else ""


func _ensure_connected() -> bool:
	if _wrapper == null or not NakamaWrapper.is_addon_present():
		return false
	if _wrapper.is_ready():
		return true
	# Bind the shared account lazily, after tree setup. add_child inside the
	# service_entered window fails while the tree is still building its children,
	# so this mirrors get_connect_session's lazy resolution.
	if not _session_bound:
		_session_bound = true
		var mt := MultiplayerTree.resolve(self)
		if mt:
			_wrapper.use_session(mt.get_nakama_session())
	var res := await _wrapper.connect_async(
		self,
		{
			"server_key": server_key,
			"host": host,
			"port": port,
			"use_ssl": use_ssl,
			"device_id": device_id,
			"username": get_local_member_name(),
		},
	)
	if not res.ok:
		Netw.dbg.error("NakamaLobbyDirectory: connect failed: %s", [res.error])
		provider_unavailable.emit(String(res.error))
		return false
	return true


func _await_match(op: String) -> MultiplayerPeer:
	var timer := get_tree().create_timer(connect_timeout)
	var outcome := await Async.timeout_or_failure(
		_wrapper.match_joined,
		_wrapper.match_join_error,
		timer,
	)
	if outcome.result != "success":
		Netw.dbg.error(
			"NakamaLobbyDirectory: %s did not join (%s).",
			[op, outcome.result],
		)
		_wrapper.leave()
		return null
	_peer = _wrapper.peer()
	Netw.dbg.info("NakamaLobbyDirectory: %s joined match %s.", [op, _wrapper.match_id()])
	return _peer


func _on_match_join_error(message: String) -> void:
	Netw.dbg.warn("NakamaLobbyDirectory: match join error: %s", [message])


func _on_socket_closed() -> void:
	Netw.dbg.info("NakamaLobbyDirectory: socket closed.")
	_peer = null
