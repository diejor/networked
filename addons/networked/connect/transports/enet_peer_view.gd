## The address surface for an [ENetTransport] connection.
##
## ENet accumulates no signaler state, so this view exists for the read surface
## the generic view cannot answer: a hosting peer reports the LAN address and
## bound port others use to join it. Client peers answer [code]""[/code], since
## only a listening peer is joinable.
## [codeblock]
## var view := connector.peer_view
## share_label.text = view.join_address()   # "192.168.1.20:21253" on the host
## [/codeblock]
class_name ENetPeerView
extends NetwPeerView

func display_name() -> String:
	return "ENet"


func join_address() -> String:
	var enet := _peer as ENetMultiplayerPeer
	if enet == null or enet.get_unique_id() != 1:
		return ""
	var port := enet.host.get_local_port() if enet.host else 0
	if port <= 0:
		return ""
	return "%s:%d" % [NetwPeerView.lan_address(), port]
