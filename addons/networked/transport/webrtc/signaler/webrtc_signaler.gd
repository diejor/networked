## Transport seam that carries WebRTC SDP and ICE between peers.
##
## A [WebRTCSession] is signaling-agnostic. It hands every outbound offer,
## answer, and ICE candidate to a signaler and receives the inbound ones back,
## so swapping WebTorrent trackers for a dedicated server or a matchmaker is a
## matter of swapping the signaler. The session speaks engine
## [code]multiplayer_id[/code]s. The signaler maps those to whatever transport
## address it uses and keeps that mapping to itself.
## [codeblock]
## session.signal_out.connect(signaler.send)      # multiplayer_id + opaque addr
## signaler.received.connect(session.deliver)
##
## signaler.open(room_id, local_multiplayer_id)    # host passes id 1
## room := signaler.room_id()                      # host: generated room hash
## [/codeblock]
##
## [br][br]
## [b]Signaling Models[/b]
## [br]
## Different signaling implementations can be swapped by subclassing:
## [br]- [b]WebTorrent Tracker[/b]: Zero infrastructure. Good for web demos.
## [br]- [b]Dedicated WebSocket[/b]: Production path via a standalone server.
## [br]- [b]Direct WebSocket[/b]: Used for local testing and dedicated hosts.
@abstract
class_name WebRTCSignaler
extends RefCounted

## Emitted with inbound SDP or ICE. [param kind] is [code]"offer"[/code],
## [code]"answer"[/code], or [code]"candidate"[/code].
signal received(
		from_multiplayer_id: int,
		from_signaler_id: String,
		kind: String,
		payload: Dictionary,
)
## Emitted when at least one signaling route becomes usable.
signal ready
## Emitted when the signaling provider becomes unavailable unexpectedly. An
## intentional wind-down after the native WebRTC link is up does not emit this.
signal lost
## Emitted when no signaling route can be reached during initial open.
signal unreachable


## Opens signaling for [param room_id] as [param local_multiplayer_id]. A host
## passes id 1 and may leave [param room_id] empty for the signaler to generate.
## Read the generated id back with [method room_id].
@abstract
func open(room_id: String, local_multiplayer_id: int) -> Error


## Drives the signaling transport for one frame.
@abstract
func poll(_dt: float) -> void


## Releases the signaling transport.
@abstract
func close() -> void


## Sends [param payload] of [param kind] toward [param to_multiplayer_id]. An
## empty [param to_signaler_id] means the address is not yet known, which a
## discovery-capable signaler treats as room-directed.
@abstract
func send(
		to_multiplayer_id: int,
		to_signaler_id: String,
		kind: String,
		payload: Dictionary,
) -> void


## Returns this peer's own transport address.
@abstract
func local_signaler_id() -> String


## Returns the active room identifier, normalized or generated during
## [method open].
@abstract
func room_id() -> String


## Notifies the signaler that the native WebRTC link to [param _multiplayer_id]
## is up, so it may wind down signaling. Override when relevant.
func on_session_connected(_multiplayer_id: int) -> void:
	pass
