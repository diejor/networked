## Accepts a join only when its username is the peer's Nakama username.
##
## The username is checked against the Nakama server, so a player cannot use
## someone else's name.
class_name NakamaAuth
extends Node

var _tree: MultiplayerTree


func _ready() -> void:
	var session: NetwSession = Netw.session(self)
	if session != null:
		_tree = session.root as MultiplayerTree
	Netw.configure_admission(admit)


## Sets the [MultiplayerTree] whose Nakama match is checked.
func bind_tree(tree: MultiplayerTree) -> void:
	_tree = tree


## Accepts [param peer_id] only with its own Nakama username.
##
## [br][br][b]Server Only.[/b]
func admit(peer_id: int, username: StringName, _args: Array = []) -> Error:
	var wrapper := _active_wrapper()
	if wrapper == null:
		push_error(
			"NakamaAuth refused a join, because it found no "
			+ "NakamaLobbyDirectory. Call NakamaAuth.bind_tree with the tree "
			+ "that hosts the Nakama match.",
		)
		return ERR_UNAVAILABLE
	if wrapper.user_id_for_peer(peer_id).is_empty():
		return ERR_UNAUTHORIZED
	if StringName(wrapper.username_for_peer(peer_id)) != username:
		return ERR_UNAUTHORIZED
	return OK


func _active_wrapper() -> NakamaWrapper:
	if _tree == null:
		return null
	var session: NetwSession = Netw.session(_tree)
	var dir := session.service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	return dir.wrapper() if dir != null else null
