## Accepts a join only when its username is the peer's Nakama username.
##
## The username is checked against the Nakama server, so a player cannot use
## someone else's name.
class_name NakamaAuth
extends Node

@onready var session: NetwSession = Netw.session(self)


func _ready() -> void:
	Netw.configure_admission(admit)


## Accepts [param peer_id] only with its own Nakama username.
##
## [br][br][b]Server Only.[/b]
func admit(peer_id: int, username: StringName, _args: Array = []) -> Error:
	var directory := session.service(NakamaLobbyDirectory) \
			as NakamaLobbyDirectory
	if directory == null:
		push_error(
			"NakamaAuth refused a join, because its session has no "
			+ "NakamaLobbyDirectory. Add a NakamaLobbyDirectory to the "
			+ "session.",
		)
		return ERR_UNAVAILABLE
	var wrapper := directory.wrapper()
	if wrapper.user_id_for_peer(peer_id).is_empty():
		return ERR_UNAUTHORIZED
	if StringName(wrapper.username_for_peer(peer_id)) != username:
		return ERR_UNAUTHORIZED
	return OK
