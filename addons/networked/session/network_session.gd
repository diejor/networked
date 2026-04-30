## Top-level scene node that orchestrates the full client/server multiplayer
## lifecycle.
##
## Place this node as your main scene root. Assign a [MultiplayerTree] to
## [member client], then call [method connect_player] to connect. On headless
## builds the server is started automatically.
## [codeblock]
## # Minimal usage — assign via the inspector or at runtime:
## var data := MultiplayerClientData.new()
## data.username = "Alice"
## data.spawner_path = spawner_node_path
## data.url = "192.168.1.5"
## await network.connect_player(data)
## [/codeblock]
@tool
class_name NetworkSession
extends Node

## The [MultiplayerTree] representing the local client connection.
##
## On headless (dedicated server) builds the client tree is freed automatically.
@export var client: MultiplayerTree:
	set(peer):
		client = peer
		update_configuration_warnings()
		
		if Engine.is_editor_hint() or not client:
			return
			
		if not client.server_disconnected.is_connected(_close_server):
			client.server_disconnected.connect(_close_server)
		
		if manage_scene and DisplayServer.get_name() == "headless":
			client.queue_free()


## When [code]false[/code], [method connect_player] will not promote this node to
## the scene root and will not auto-start a local server for localhost URLs.
## When running as a dedicated server, it will remove the client and auto-host.
@export var manage_scene: bool = true

@export_group("Debug")
## When set, [method connect_player] is called automatically on [code]_ready[/code]
## with this data.
@export var init_client_data: MultiplayerClientData
@export_group("", "")

## The server-side [MultiplayerTree], created dynamically when hosting a local
## session.
var server: MultiplayerTree

## Disconnects any existing session, then connects using [param client_data].
##
## Automatically spins up a local server when [member MultiplayerClientData.url]
## is empty or localhost. On web builds, falls back to [LocalLoopbackBackend] for
## singleplayer when not using WebRTC.
func connect_player(client_data: MultiplayerClientData) -> void:
	Netw.dbg.trace("NetworkSession: connect_player called.")
	if not client_data:
		Netw.dbg.error(
			"connect_player: client_data is null.",
			func(m): push_error(m)
		)
		return
	if client_data.username.is_empty():
		Netw.dbg.error(
			"connect_player: username is empty.",
			func(m): push_error(m)
		)
		return
	if not client_data.spawner_path or not client_data.spawner_path.is_valid():
		Netw.dbg.error(
			"connect_player: spawner_path is invalid or missing.",
			func(m): push_error(m)
		)
		return
	
	await disconnect_player()

	if manage_scene:
		await _validate_current_scene()

	var url := client_data.url
	Netw.dbg.info("Connecting player %s to %s", [client_data.username, url])
	
	if manage_scene and _is_singleplayer(url):
		# First attempt: Try to join an existing server on localhost.
		# If url is empty, we MUST probe localhost to find existing local
		# sessions.
		var probe_url := url if not url.is_empty() else "localhost"
		
		# We use a short timeout and quiet=true so we can pivot to hosting
		# quickly without error logs.
		var quiet := true
		var probe_err: Error = await client.join(
			probe_url,
			client_data.username,
			1.0,
			quiet
		)
		if probe_err == OK:
			_request_join(client_data)
			return
		
		# Second attempt: If join failed (timeout or refused), try to host the
		# server ourselves.
		url = await _host_server()
	elif OS.has_feature("web"):
		if url.begins_with("ws"):
			client.backend = WebSocketBackend.new()
	
	var client_err: Error = await client.join(url, client_data.username)
	if client_err == OK:
		_request_join(client_data)

func _request_join(client_data: MultiplayerClientData) -> void:
	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)


## Starts this session as a dedicated server without creating a separate server
## node.
##
## Returns the error code from [method MultiplayerTree.host].
func host() -> Error:
	Netw.dbg.trace("NetworkSession: host called.")
	client.is_server = true
	client.name = "Server"
	return client.host()


## Returns the address clients should use to connect after [method host]
## succeeds.
func get_host_address() -> String:
	return _resolve_server_address()


## Saves game state, closes the multiplayer peer, and waits for the server to
## acknowledge disconnection.
func disconnect_player() -> void:
	if not client.is_online():
		return
	
	Netw.dbg.trace("NetworkSession: disconnect_player called.")
	Netw.dbg.info("Disconnecting player.")
	SaveComponent.save_all_in(
		client.get_peer_context(client.multiplayer_api.get_unique_id())
	)
	client.multiplayer_peer.close()
	
	var timer := get_tree().create_timer(3.0)
	if await Async.timeout(client.multiplayer_api.server_disconnected, timer):
		Netw.dbg.error(
			"Couldn't disconnect from server.",
			func(m): push_error(m)
		)


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	if not client:
		warnings.append(
			"A MultiplayerTree must be assigned to the 'client' property " + \
			"for the network to function."
		)
	return warnings


func _ready() -> void:
	if Engine.is_editor_hint():
		return
		
	if init_client_data:
		init_client_data.is_debug = true
		connect_player(init_client_data)
	
	if manage_scene and DisplayServer.get_name() == "headless":
		_host_server()


func _is_current_scene() -> bool:
	return get_tree().current_scene == self


func _close_server() -> void:
	Netw.dbg.info("Closing embedded server.")
	if server:
		server.get_parent().remove_child(server)
		server.queue_free()
		server = null


## Returns [code]true[/code] if the client's current
## [member MultiplayerTree.backend] is WebRTC-based.
func is_webrtc() -> bool:
	var script: Script = client.backend.get_script()
	var n := script.get_global_name().to_lower()
	var is_rtc := "rtc" in n or "tube" in n
	Netw.dbg.debug(
		"Backend check: class=%s is_webrtc=%s", \
		[script.get_global_name(), is_rtc]
	)
	return is_rtc


func _host_server() -> String:
	Netw.dbg.trace("NetworkSession: _host_server called.")
	if OS.has_feature("web") and not is_webrtc():
		client.backend = LocalLoopbackBackend.new()

	var client_lm := client.get_service(MultiplayerLobbyManager) \
		as MultiplayerLobbyManager
	server = client.duplicate()
	server.is_server = true
	server.name = "Server"
	add_child(server)
	if client_lm:
		var server_lm := server.get_service(MultiplayerLobbyManager) \
			as MultiplayerLobbyManager
		for path in client_lm._get_configured_paths():
			server_lm._configure_default(path)

	Netw.dbg.info("Starting embedded server...")
	# quiet=true: ERR_ALREADY_IN_USE is expected in multi-client scenarios.
	var server_err := server.host(true)
	var in_use := (server_err == ERR_ALREADY_IN_USE or server_err == ERR_CANT_CREATE)
	
	if server_err != OK and not in_use:
		Netw.dbg.error(
			"Server failed to start: %s", [error_string(server_err)],
			func(m): push_error(m)
		)
	
	if in_use:
		Netw.dbg.info("Server address already in use, connecting to localhost.")
		server.queue_free.call_deferred()
		return "localhost"
		
	var addr := _resolve_server_address()
	Netw.dbg.info("Embedded server started at: %s", [addr])
	return addr


func _resolve_server_address() -> String:
	var tree := server if server else client
	if not tree or not tree.backend:
		return "localhost"
	return tree.backend.get_join_address()


func _validate_current_scene() -> void:
	if not _is_current_scene():
		var tree := get_tree()
		owner.remove_child.call_deferred(self)
		tree.change_scene_to_node.call_deferred(self)
		await tree.scene_changed


func _is_singleplayer(url: String) -> bool:
	return url.is_empty() or "localhost" in url or "127.0.0.1" in url
