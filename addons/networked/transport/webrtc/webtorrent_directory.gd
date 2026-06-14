## WebRTC [LobbyDirectory] that discovers rooms by gossip over WebTorrent
## trackers, with no signaling server.
##
## WebTorrent trackers are content addressed. A peer announces on a 20 char
## [code]info_hash[/code] and the tracker only pairs it with peers announcing
## the same hash. There is no enumerate all rooms call, so this directory
## reserves one well known board hash that every host and browser announces on,
## and tunnels a JSON room card through the announce [code]sdp[/code] field. No
## WebRTC handshake happens on the board. The room connection itself is a
## separate [TrackerWebRTCBackend] keyed by the room hash.
## [codeblock]
## board hash = sha1(uid + ":board")[:20]
##
## HOST    announce(offer.sdp = {"t":"room", hash, name, players, max, uid})
## BROWSER announce(offer.sdp = {"t":"query"})
##
## tracker pairs same-hash peers:
##   browser query -> reaches hosts -> hosts answer with their room card
##   host card     -> reaches browsers -> read straight off the offer
##
## reach per announce = board_fanout peers (one offer pairs one swarm peer)
## list_lobbies collects cards for browse_window then emits clear-then-fill
## [/codeblock]
##
## Browser-hosted rooms depend on the host tab polling trackers and sending ICE
## signalling. Browser throttling can pause that work when the tab is not
## focused, so fully peer-to-peer web rooms may disappear from discovery or
## stall during joins until the host tab is visible again.
## [br][br]
## Add as a child of [MultiplayerTree] and register with
## [method ConnectSession.register_directory]. The directory advertises whatever
## room the tree is hosting whenever it reaches
## [constant MultiplayerTree.ONLINE] as a host over a [WebRTCBackend], so
## the [ConnectSession] host path
## ([code]tree.backend = TrackerWebRTCBackend.new()[/code]) and
## [method host_lobby] both light up the board automatically. Browsers join
## through [method make_join_target], which stamps a [TrackerWebRTCBackend] with
## the room hash.
@tool
class_name WebTorrentDirectory
extends LobbyDirectory

## WebTorrent compatible tracker URLs used for the board, shared with the
## [TrackerWebRTCBackend] the directory stamps onto join targets.
@export var trackers: Array[String] = [
	"wss://tracker.openwebtorrent.com",
	"wss://tracker.webtorrent.dev",
	"wss://tracker.btorrent.xyz",
]

## Tag stamped on every room card and required on received cards, so different
## games do not pollute each other's board. Also seeds the board hash.
@export var browser_filter_uid: String = "networked"

## Maximum room members advertised by [method host_lobby].
@export_range(1, 250, 1, "or_greater", "suffix:players") \
		var max_clients: int = 8

## Seconds [method list_lobbies] collects room cards before emitting
## [signal LobbyDirectory.lobby_list_updated].
@export_range(0.5, 10.0, 0.1, "suffix:s") var browse_window: float = 2.5

## Seconds between room card re-announces while advertising.
@export_range(0.5, 10.0, 0.1, "suffix:s") var advertise_interval: float = 2.0

## Seconds the board stays warm after the last interest (advertising or
## browsing) before its tracker sockets close. Set to 0 to close immediately
## once idle. [method list_lobbies] and [method advertise_room] reopen on demand.
@export_range(0, 120) var board_idle_timeout: float = 30.0

## Distinct offers emitted per board announce. A WebTorrent tracker pairs one
## offer per distinct swarm peer, so reach per announce equals this fanout, not
## [code]numwant[/code]. Higher values converge faster at the cost of bandwidth.
@export_range(1, 50) var board_fanout: int = 16

var _tracker: WebTorrentTrackerClient = null
var _tracker_shared := false
var _board_hash := ""
var _peer_id := ""

var _advertising := false
var _room_hash := ""
var _room_name := ""
var _room_max := 0
var _players := 1
var _pending_room_name := ""
var _advertise_acc := 0.0

var _collecting := false
var _collect_left := 0.0
var _query_acc := 0.0
var _collected: Dictionary = { } # room_hash -> LobbyInfo
var _id_to_hash: Dictionary = { } # synthetic int id -> room_hash
var _next_id := 1

var _reconnect_acc := 0.0
var _idle_acc := 0.0
var _provider_unavailable_latched := false

## Seconds to wait before reconnecting a board whose sockets all dropped.
const BOARD_RECONNECT_COOLDOWN := 5.0


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	if Netw.is_test_env():
		set_process(false)
		return
	_board_hash = (browser_filter_uid + ":board").sha1_text().substr(0, 20)
	_peer_id = _generate_peer_id()
	NetwServices.register(self)
	var mt := MultiplayerTree.resolve(self)
	if mt:
		_bind_tree_signals(mt)
	# Keep the board connection warm so browse and advertise are instant.
	_ensure_tracker()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	NetwServices.unregister(self)
	NetwServices.unregister(self, LobbyDirectory)
	if _tracker:
		_release_tracker()


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return

	if _tracker:
		_tracker.poll()
	_maintain_board(dt)

	if _advertising and _tracker:
		_advertise_acc += dt
		if _advertise_acc >= advertise_interval:
			_advertise_acc = 0.0
			_tracker.broadcast(_announce_with_card(_room_card()))

	if _collecting:
		_query_acc += dt
		if _query_acc >= 0.7:
			_query_acc = 0.0
			if _tracker:
				_tracker.broadcast(_announce_with_card({ "t": "query" }))
		_collect_left -= dt
		if _collect_left <= 0.0:
			_collecting = false
			_emit_collected()


# Keeps the board warm while there is interest (advertising or browsing) and a
# short idle grace after, then closes its sockets so an idle directory does not
# squat on trackers. Interest reopens the board on demand.
func _maintain_board(dt: float) -> void:
	if _advertising or _collecting:
		_idle_acc = 0.0
		_keep_board_warm(dt)
		return

	_idle_acc += dt
	if _idle_acc < board_idle_timeout:
		_keep_board_warm(dt)
		return

	# Idle past the grace: release the sockets until interest returns.
	if _tracker != null:
		Netw.dbg.debug("WebTorrentDirectory: board idle, closing trackers.")
		_release_tracker()
	_reconnect_acc = 0.0


# Reconnects a warm board whose sockets all dropped, after a cooldown.
func _keep_board_warm(dt: float) -> void:
	if _tracker == null:
		_ensure_tracker()
		_reconnect_acc = 0.0
		return
	if _tracker.is_active():
		_reconnect_acc = 0.0
		return
	_reconnect_acc += dt
	if _reconnect_acc >= BOARD_RECONNECT_COOLDOWN:
		_reconnect_acc = 0.0
		_release_tracker()
		_ensure_tracker()


func list_lobbies() -> void:
	if Netw.is_test_env():
		return
	# The board stays warm, so browsing only opens a fresh collect window.
	_ensure_tracker()
	_collected.clear()
	_id_to_hash.clear()
	_next_id = 1
	_collecting = true
	_collect_left = browse_window
	_query_acc = 0.7 # query on the next process tick
	Netw.dbg.debug("WebTorrentDirectory: browsing board %s.", [_board_hash])
	Netw.dbg.debug(
		"WebTorrentDirectory: browsing board %s with local app_id='%s'.",
		[_board_hash, _local_app_id()],
	)


func leave_lobby() -> void:
	stop_advertising()
	_collecting = false


func make_join_target(lobby: LobbyInfo) -> JoinTarget:
	var target := JoinTarget.new()
	target.display_name = lobby.lobby_name
	target.address = String(lobby.metadata.get("room_hash", ""))
	target.metadata = lobby.metadata.duplicate()
	target.backend = _make_backend()
	var ns := String(lobby.metadata.get("signaling_namespace", ""))
	if not ns.is_empty():
		target.backend.signaling_namespace = ns
	return target


func host_lobby(server_name: String) -> MultiplayerPeer:
	if Netw.is_test_env():
		return null
	var tree := MultiplayerTree.resolve(self)
	if tree == null:
		Netw.dbg.warn("WebTorrentDirectory: host_lobby found no MultiplayerTree.")
		return null
	_pending_room_name = server_name
	tree.backend = _make_backend()
	var payload := JoinPayload.new()
	payload.username = get_local_member_name()
	var err: Error = await tree.host_player(payload)
	if err != OK:
		_pending_room_name = ""
		Netw.dbg.error(
			"WebTorrentDirectory: host_player failed: %s",
			[error_string(err)],
		)
		return null
	# Advertising is started by the State.ONLINE observer.
	return tree.api.multiplayer_peer


func join_lobby_peer(lobby_id: int) -> MultiplayerPeer:
	if Netw.is_test_env():
		return null
	var room_hash := String(_id_to_hash.get(lobby_id, ""))
	if room_hash.is_empty():
		Netw.dbg.warn(
			"WebTorrentDirectory: join_lobby_peer unknown id %d. " +
			"Call list_lobbies first or join through make_join_target.",
			[lobby_id],
		)
		return null
	var tree := MultiplayerTree.resolve(self)
	if tree == null:
		return null
	var target := JoinTarget.new()
	target.address = room_hash
	target.backend = _make_backend()
	var payload := JoinPayload.new()
	payload.username = get_local_member_name()
	var err: Error = await tree.join(target, payload)
	if err != OK:
		Netw.dbg.error(
			"WebTorrentDirectory: join failed: %s",
			[error_string(err)],
		)
		return null
	return tree.api.multiplayer_peer


## Begins advertising [param room_hash] on the board as [param server_name]
## with capacity [param max_players].
##
## Called automatically when the tree hosts over a [WebRTCBackend]. Call it
## directly only when driving the host path outside [method host_lobby].
func advertise_room(
		room_hash: String,
		server_name: String,
		max_players: int,
) -> void:
	if Netw.is_test_env():
		return
	if room_hash.is_empty():
		Netw.dbg.warn(
			"WebTorrentDirectory: refusing to advertise empty room hash.",
			func(m): push_warning(m),
		)
		return
	_room_hash = room_hash
	_room_name = server_name if not server_name.is_empty() \
	else get_local_member_name()
	_room_max = max_players
	_players = _count_players()
	_advertising = true
	_advertise_acc = advertise_interval # announce on the next process tick
	_ensure_tracker()
	Netw.dbg.debug(
		"WebTorrentDirectory: advertising room %s as '%s' with app_id='%s'.",
		[room_hash, _room_name, _local_app_id()],
	)


## Stops advertising the local room. Idempotent.
func stop_advertising() -> void:
	if not _advertising:
		return
	_advertising = false
	_room_hash = ""
	_players = 1
	Netw.dbg.debug("WebTorrentDirectory: stopped advertising.")

# -- Tree observation -------------------------------------------------------


func _bind_tree_signals(mt: MultiplayerTree) -> void:
	if not mt.state_changed.is_connected(_on_tree_state_changed):
		mt.state_changed.connect(_on_tree_state_changed)
	if not mt.peer_connected.is_connected(_on_tree_peer_changed):
		mt.peer_connected.connect(_on_tree_peer_changed)
	if not mt.peer_disconnected.is_connected(_on_tree_peer_changed):
		mt.peer_disconnected.connect(_on_tree_peer_changed)


func _on_tree_state_changed(
		_old: MultiplayerTree.State,
		new_state: MultiplayerTree.State,
) -> void:
	if new_state == MultiplayerTree.State.ONLINE:
		var mt := MultiplayerTree.resolve(self)
		if mt and mt.is_host and mt.backend is WebRTCBackend:
			var backend := mt.backend as WebRTCBackend
			var room_name := _pending_room_name
			if room_name.is_empty():
				room_name = backend.server_name
			advertise_room(
				backend.get_join_address(),
				room_name,
				max_clients,
			)
			_pending_room_name = ""
	elif new_state == MultiplayerTree.State.OFFLINE:
		stop_advertising()


func _on_tree_peer_changed(_peer_id: int) -> void:
	if _advertising:
		_players = _count_players()
		_advertise_acc = advertise_interval


func _count_players() -> int:
	var mt := MultiplayerTree.resolve(self)
	if mt and mt.api and mt.api.has_multiplayer_peer():
		return mt.api.get_peers().size() + 1
	return 1

# -- Board gossip -----------------------------------------------------------


func _ensure_tracker() -> void:
	if _tracker != null:
		return
	var acquired := WebTorrentTrackerClient.acquire_shared(trackers)
	var err := int(acquired.get("error", OK))
	_tracker = acquired.get("client", null) as WebTorrentTrackerClient
	_tracker_shared = _tracker != null
	if err != OK:
		# Latch so the 5s reconnect loop reports one outage, not one per retry.
		if not _provider_unavailable_latched:
			_provider_unavailable_latched = true
			provider_unavailable.emit("No WebRTC tracker reachable for the board.")
	elif _tracker == null:
		if not _provider_unavailable_latched:
			_provider_unavailable_latched = true
			provider_unavailable.emit("No WebRTC tracker reachable for the board.")
	else:
		_tracker.message_received.connect(_on_message)
		_provider_unavailable_latched = false


func _release_tracker() -> void:
	if _tracker == null:
		return
	if _tracker.message_received.is_connected(_on_message):
		_tracker.message_received.disconnect(_on_message)
	if _tracker_shared:
		WebTorrentTrackerClient.release_shared(trackers, _tracker)
	else:
		_tracker.close()
	_tracker = null
	_tracker_shared = false


func _on_message(data: Dictionary) -> void:
	if data.get("info_hash", "") != _board_hash:
		return
	var sender := String(data.get("peer_id", ""))
	if sender == _peer_id or sender.length() != 20:
		return

	var payload: Variant = data.get("offer", data.get("answer", null))
	if typeof(payload) != TYPE_DICTIONARY:
		return
	var card_raw: Variant = JSON.parse_string(
		String((payload as Dictionary).get("sdp", "")),
	)
	if typeof(card_raw) != TYPE_DICTIONARY:
		return
	var card: Dictionary = card_raw

	match String(card.get("t", "")):
		"query":
			if _advertising:
				_answer_card(sender, String(data.get("offer_id", "")))
		"room":
			if _collecting:
				_collect_room(card)


func _answer_card(to_peer: String, offer_id: String) -> void:
	var msg := {
		"action": "announce",
		"info_hash": _board_hash,
		"peer_id": _peer_id,
		"to_peer_id": to_peer,
		"answer": { "type": "answer", "sdp": JSON.stringify(_room_card()) },
	}
	if not offer_id.is_empty():
		msg["offer_id"] = offer_id
	_tracker.broadcast(msg)


func _collect_room(card: Dictionary) -> void:
	if String(card.get("uid", "")) != browser_filter_uid:
		return
	var room_hash := String(card.get("hash", ""))
	if room_hash.is_empty():
		return
	var players := int(card.get("players", 0))
	var max_players := int(card.get("max", 0))
	var room_name := String(card.get("name", ""))

	if _collected.has(room_hash):
		var existing: LobbyInfo = _collected[room_hash]
		var changed := existing.players != players \
				or existing.max_players != max_players \
				or existing.lobby_name != room_name
		existing.players = players
		existing.max_players = max_players
		existing.lobby_name = room_name
		existing.metadata = _room_metadata(card)
		if changed:
			Netw.dbg.trace(
				"WebTorrentDirectory: updated room %s (%d/%d) app_id='%s'.",
				[
					room_hash,
					players,
					max_players,
					String(card.get("app_id", "")),
				],
			)
		return

	var id := _next_id
	_next_id += 1
	_id_to_hash[id] = room_hash
	Netw.dbg.debug(
		"WebTorrentDirectory: discovered room %s (%d/%d) app_id='%s'.",
		[room_hash, players, max_players, String(card.get("app_id", ""))],
	)
	_collected[room_hash] = LobbyInfo.make(
		id,
		room_name,
		players,
		max_players,
		_room_metadata(card),
	)


func _emit_collected() -> void:
	var out: Array[LobbyInfo] = []
	for room_hash in _collected:
		out.append(_collected[room_hash])
	Netw.dbg.debug(
		"WebTorrentDirectory: browse found %d room(s).",
		[out.size()],
	)
	lobby_list_updated.emit(out)


func _room_card() -> Dictionary:
	var ns := ""
	var mt := MultiplayerTree.resolve(self)
	if mt and mt.backend is WebRTCBackend:
		ns = (mt.backend as WebRTCBackend).signaling_namespace
	return {
		"t": "room",
		"hash": _room_hash,
		"name": _room_name,
		"players": _players,
		"max": _room_max,
		"uid": browser_filter_uid,
		"app_id": _local_app_id(),
		"signaling_namespace": ns,
	}


func _room_metadata(card: Dictionary) -> Dictionary:
	return {
		"room_hash": String(card.get("hash", "")),
		"host": String(card.get("name", "")),
		"browser_filter_uid": String(card.get("uid", "")),
		"app_id": String(card.get("app_id", "")),
		"signaling_namespace": String(card.get("signaling_namespace", "")),
	}


# The hosting tree's build tag, advertised so browsers can flag a lobby they
# would be rejected from before they try to join.
func _local_app_id() -> String:
	var mt := MultiplayerTree.resolve(self)
	return String(mt.app_id) if mt else ""


func _announce_with_card(card: Dictionary) -> Dictionary:
	# One offer reaches one swarm peer, so fan out board_fanout distinct offers
	# (same sdp, distinct offer_id) to reach that many peers per announce.
	var sdp := JSON.stringify(card)
	var offers: Array = []
	for i in board_fanout:
		offers.append(
			{
				"offer_id": _generate_hash(),
				"offer": { "type": "offer", "sdp": sdp },
			},
		)
	return {
		"action": "announce",
		"info_hash": _board_hash,
		"peer_id": _peer_id,
		"numwant": board_fanout,
		"offers": offers,
	}


func _make_backend() -> WebRTCBackend:
	var template := TrackerWebRTCBackend.new()
	template.trackers = trackers
	# clone() runs copy_from, so trackers/server_name/ice_servers all ride along.
	return template.clone()


func _generate_hash() -> String:
	var chars := "0123456789abcdef"
	var out := ""
	for i in 20:
		out += chars[randi() % chars.length()]
	return out


func _generate_peer_id() -> String:
	return _generate_hash()
