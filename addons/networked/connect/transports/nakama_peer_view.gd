## The state home for a [NakamaTransport] connection.
##
## A relay match opens no listening socket, so the only address a host can share
## is the opaque match id the [NakamaLobbyDirectory] holds. This view surfaces it
## through [method join_address] for the whole life of the assigned peer, reading
## it live from the directory rather than snapshotting it at build time.
## [codeblock]
## NakamaTransport._make_view(peer, attempt)
## └── NakamaPeerView(dir)
##     └── join_address() -> dir.get_join_address()   # the relay match id
## [/codeblock]
class_name NakamaPeerView
extends NetwPeerView

var _dir: NakamaLobbyDirectory


func _init(peer: MultiplayerPeer, dir: NakamaLobbyDirectory) -> void:
	super(peer)
	_dir = dir


func display_name() -> String:
	return "Nakama"


## Returns the relay match id others use to join this host, from
## [method NakamaLobbyDirectory.get_join_address].
func join_address() -> String:
	return _dir.get_join_address() if _dir else ""


func close() -> void:
	_dir = null
	super()
