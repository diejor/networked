## The per-peer, stateful facet of a transport.
##
## A [NetwPeerView] owns whatever live state a connection accumulates and lives
## exactly as long as its peer is assigned. [NetwConnector] resolves one for the
## assigned peer through [method NetwTransport._make_view], and falls back to
## this base class as the generic view when no registered transport recognizes
## the peer. The base degrades gracefully: features vanish one by one, the
## session never does, and nothing errors. The session machine never consults a
## view, which is what keeps the whole view system safely in user space.
## [codeblock]
## var view := connector.peer_view          # never null while a peer is assigned
## var label := view.display_name()
## var where := view.join_address()          # "" on the generic view
## [/codeblock]
class_name NetwPeerView
extends RefCounted

# The assigned peer this view describes. Weakly held is unnecessary because the
# connector releases the view on unassignment through close().
var _peer: MultiplayerPeer


func _init(peer: MultiplayerPeer = null) -> void:
	_peer = peer


## Returns a human-readable name for the assigned peer.
##
## The generic view reports the peer's class.
func display_name() -> String:
	return _peer.get_class() if _peer else "Peer"


## Returns the address others use to join this host, or [code]""[/code] when the
## transport has none.
func join_address() -> String:
	return ""


## The first non-loopback IPv4 address this machine exposes, falling back to
## loopback so a single-machine session still renders a joinable address.
## Address-bearing views compose it into [method join_address].
static func lan_address() -> String:
	for address in IP.get_local_addresses():
		var ip := String(address)
		if ip.begins_with("127.") or ip.contains(":"):
			continue
		return ip
	return "127.0.0.1"


## Returns a diagnostics snapshot for [param peer_id], or [code]{ }[/code] when
## the transport records none.
##
## Keys are transport-owned, so a consumer reads only the ones it recognizes and
## ignores the rest. [NetwConnectResult] carries the snapshot to the browser on
## [member NetwConnectResult.diagnostics]. [WebRTCPeerView] answers the richest
## shape:
## [codeblock]
## ┠╴ "phases": Dictionary        # connect milestones as msec timestamps
## ┃   ┠╴ "offer_ms": int
## ┃   ┠╴ "answer_ms": int
## ┃   ┖╴ "native_ms": int
## ┠╴ "candidates": Dictionary    # ICE candidate counts by type
## ┃   ┠╴ "host": int
## ┃   ┠╴ "srflx": int
## ┃   ┖╴ "relay": int
## ┖╴ "relay_used": bool          # true when only relay candidates connected
## [/codeblock]
@warning_ignore("unused_parameter")
func diagnostics(peer_id: int) -> Dictionary:
	return { }


## Returns the current progress narration for a connect UI.
## [codeblock]
## ┠╴ "step": StringName    # machine-readable phase, stable per transport
## ┖╴ "message": String     # human-readable line shown to the player
## [/codeblock]
## The generic view derives three coarse steps from
## [method MultiplayerPeer.get_connection_status]:
## [code]&"disconnected"[/code], [code]&"connecting"[/code], and
## [code]&"connected"[/code]. Rich transports read their own signaler state.
func describe_progress() -> Dictionary:
	var status := (
			_peer.get_connection_status() if _peer \
			else MultiplayerPeer.CONNECTION_DISCONNECTED
	)
	match status:
		MultiplayerPeer.CONNECTION_CONNECTED:
			return { "step": &"connected", "message": "Connected" }
		MultiplayerPeer.CONNECTION_CONNECTING:
			return { "step": &"connecting", "message": "Connecting..." }
		_:
			return { "step": &"disconnected", "message": "Disconnected" }


## Pumps view state for [param dt] seconds.
##
## Pumped by [method NetwConnector.poll]. The generic view has nothing to pump.
@warning_ignore("unused_parameter")
func poll(dt: float) -> void:
	pass


## Tears down view state when the peer is unassigned.
func close() -> void:
	_peer = null
