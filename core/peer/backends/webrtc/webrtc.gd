class_name WebRTCBackend
extends BackendPeer


var webrtc_peer: WebRTCMultiplayerPeer:
	get: return api.multiplayer_peer as WebRTCMultiplayerPeer

func host() -> Error:
	var peer := WebRTCMultiplayerPeer.new()
	
	var err := peer.create_server()
	
	if err == OK:
		api.multiplayer_peer = peer
		print("WebRTC server peer initialized. Awaiting signaling connections...")
		
		# TODO: Connect to signaling server here as the host
		# signaling_client.connect_as_host()
	return err

func join(server_address: String, _username: String = "") -> Error:
	var peer := WebRTCMultiplayerPeer.new()
	
	# In WebRTC, Godot requires the client to know its assigned network ID 
	# BEFORE it can fully initialize. Usually, the signaling server hands you this ID.
	
	# TODO: Connect to signaling server FIRST, get unique ID, then initialize:
	# var my_assigned_id = signaling_client.get_my_id()
	# var err := peer.create_client(my_assigned_id)
	
	# Placeholder error until signaling logic is integrated:
	push_warning("WebRTC client requires an ID from a signaling server before joining.")
	return ERR_UNAVAILABLE
	
	# If signaling was complete:
	# api.multiplayer_peer = peer
	# return OK

# TODO: Handle the signaling callbacks, like:
# func _on_signaling_offer_received(peer_id, offer):
# func _on_signaling_ice_candidate_received(peer_id, media, index, name):
