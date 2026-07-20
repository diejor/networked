## The address surface for a [WebSocketTransport] connection.
##
## WebSocket accumulates no signaler state, so this view exists for the read
## surface the generic view cannot answer: a hosting peer reports the
## [code]ws://[/code] URL others use to join it, built from
## [method NetwPeerView.lan_address] and the listening port the transport
## carried over on [member NetwConnectAttempt.context]. Client peers answer
## [code]""[/code], since only a listening peer is joinable.
## [codeblock]
## var view := connector.peer_view
## share_label.text = view.join_address()   # "ws://192.168.1.20:21253"
## [/codeblock]
class_name WebSocketPeerView
extends NetwPeerView

var _port: int = 0


func _init(peer: MultiplayerPeer = null, port: int = 0) -> void:
	super(peer)
	_port = port


func display_name() -> String:
	return "WebSocket"


func join_address() -> String:
	if _peer == null or _peer.get_unique_id() != 1 or _port <= 0:
		return ""
	return "ws://%s:%d" % [NetwPeerView.lan_address(), _port]
