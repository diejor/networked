## The [NetwPeerView] for the in-process [LocalLoopbackSession].
##
## Loopback delivery has no sockets, so the shared session must be pumped every
## frame for peers to exchange packets. This view owns that pump through
## [method poll], which is the loopback half of the connect kit's
## "own a connector, pump it" contract.
class_name LocalPeerView
extends NetwPeerView

var _session: LocalLoopbackSession


func _init(peer: MultiplayerPeer = null, session: LocalLoopbackSession = null) -> void:
	super(peer)
	_session = session


func display_name() -> String:
	return "Local"


func poll(_dt: float) -> void:
	if _session:
		_session.poll_frame_scoped()


func close() -> void:
	_session = null
	super()
