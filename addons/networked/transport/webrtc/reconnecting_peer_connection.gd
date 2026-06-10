## A [WebRTCPeerConnection] that masks a transient drop as a live connection.
##
## WebRTC briefly reports [constant WebRTCPeerConnection.STATE_DISCONNECTED]
## during ICE blips and usually recovers on its own, but [WebRTCMultiplayerPeer]
## treats that state as a teardown and removes the peer, forcing a full re-signal
## through the [WebRTCSession]. This extension reports
## [constant WebRTCPeerConnection.STATE_CONNECTED] while the link is only
## transiently disconnected, so a real loss surfaces only once it escalates to
## [constant WebRTCPeerConnection.STATE_FAILED].
## [codeblock]
## NEW -> CONNECTING -> CONNECTED -> DISCONNECTED -> FAILED
##                                   └─ reported as ─┘
##                                      CONNECTED (masked)
## [/codeblock]
## Every other call forwards untouched to an inner [WebRTCPeerConnection], so the
## peer behaves identically apart from the masked state.
class_name ReconnectingPeerConnection
extends WebRTCPeerConnectionExtension

var _inner := WebRTCPeerConnection.new()


func _init() -> void:
	# Re-emit the inner connection's local SDP and ICE up through this wrapper so
	# a session bound to it sees them as if it owned a plain connection.
	_inner.session_description_created.connect(_on_session_description_created)
	_inner.ice_candidate_created.connect(_on_ice_candidate_created)


func _on_session_description_created(type: String, sdp: String) -> void:
	session_description_created.emit(type, sdp)


func _on_ice_candidate_created(media: String, index: int, name: String) -> void:
	ice_candidate_created.emit(media, index, name)


func _get_connection_state() -> WebRTCPeerConnection.ConnectionState:
	var state := _inner.get_connection_state()
	if state == WebRTCPeerConnection.STATE_DISCONNECTED:
		return WebRTCPeerConnection.STATE_CONNECTED
	return state


func _get_gathering_state() -> WebRTCPeerConnection.GatheringState:
	return _inner.get_gathering_state()


func _get_signaling_state() -> WebRTCPeerConnection.SignalingState:
	return _inner.get_signaling_state()


func _initialize(config: Dictionary) -> Error:
	return _inner.initialize(config)


func _create_data_channel(label: String, config: Dictionary) -> WebRTCDataChannel:
	return _inner.create_data_channel(label, config)


func _create_offer() -> Error:
	return _inner.create_offer()


func _set_local_description(type: String, sdp: String) -> Error:
	return _inner.set_local_description(type, sdp)


func _set_remote_description(type: String, sdp: String) -> Error:
	return _inner.set_remote_description(type, sdp)


func _add_ice_candidate(media: String, index: int, name: String) -> Error:
	return _inner.add_ice_candidate(media, index, name)


func _poll() -> Error:
	return _inner.poll()


func _close() -> void:
	if _inner.session_description_created.is_connected(_on_session_description_created):
		_inner.session_description_created.disconnect(_on_session_description_created)
	if _inner.ice_candidate_created.is_connected(_on_ice_candidate_created):
		_inner.ice_candidate_created.disconnect(_on_ice_candidate_created)
	_inner.close()
