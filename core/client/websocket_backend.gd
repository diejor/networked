extends MultiplayerClientBackend
class_name WebSocketClientBackend

@export var port: int = 21253
@export var public_host: String = "ws.diejor.tech"

var ws_peer: WebSocketMultiplayerPeer:
	get:
		return multiplayer_peer as WebSocketMultiplayerPeer

func _init() -> void:
	multiplayer_peer = WebSocketMultiplayerPeer.new()

func build_url(server_address: String) -> String:
	if server_address.is_empty():
		return "wss://" + public_host

	if server_address == "localhost" or server_address == "127.0.0.1":
		return "ws://localhost:" + str(port)

	return "wss://" + server_address

func create_connection(server_address: String, _username: String) -> Error:
	if (multiplayer_api.has_multiplayer_peer()
		and multiplayer_api.multiplayer_peer is WebSocketMultiplayerPeer):
		var previous_peer := multiplayer_api.multiplayer_peer as WebSocketMultiplayerPeer
		if previous_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED:
			previous_peer.close()

	# Create a fresh peer.
	multiplayer_peer = WebSocketMultiplayerPeer.new()
	var url := build_url(server_address)

	var err := (multiplayer_peer as WebSocketMultiplayerPeer).create_client(url)
	if err != OK:
		push_warning("Can't create client (%s) to %s" % [error_string(err), url])
		return err

	print("Client connecting to ", url)
	return OK

func peer_reset_state() -> void:
	if multiplayer_api.has_multiplayer_peer() \
	and multiplayer_api.multiplayer_peer is WebSocketMultiplayerPeer:
		var previous_peer := multiplayer_api.multiplayer_peer as WebSocketMultiplayerPeer
		if previous_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED:
			previous_peer.close()

	multiplayer_api.multiplayer_peer = OfflineMultiplayerPeer.new()
	multiplayer_peer = WebSocketMultiplayerPeer.new()
