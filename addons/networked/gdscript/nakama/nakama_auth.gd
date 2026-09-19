## Admission handler that admits a join only under the username Nakama
## attests for the joining peer.
##
## The host trusts the Nakama server's presence list, which maps each peer to
## their authenticated Nakama user id. That is what makes the username
## spoof-proof even though the listen-server host is itself an untrusted
## browser in the relay topology. Mount it inside the session's branch, where
## it declares itself.
##
## [codeblock]
## var gate := NakamaAuth.new()
## tree.add_child(gate)
## [/codeblock]
class_name NakamaAuth
extends Node

var _tree: MultiplayerTree


func _ready() -> void:
	var session: NetwSessionHandle = Netw.session(self)
	if session != null:
		_tree = session.root as MultiplayerTree
	Netw.configure_admission(admit)


## Binds the [MultiplayerTree] whose relay presence attests a join.
func bind_tree(tree: MultiplayerTree) -> void:
	_tree = tree


## Admits [param peer_id] only under the username Nakama attests for it.
##
## [br][br][b]Server Only.[/b]
func admit(peer_id: int, username: StringName, _args: Array) -> Error:
	var wrapper := _active_wrapper()
	if wrapper == null:
		push_error(
			"NakamaAuth: the relay presence is unavailable, so this join is "
			+ "turned down rather than taken on the name it claimed.",
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
	var dir := Netw.service(_tree, NakamaLobbyDirectory) as NakamaLobbyDirectory
	return dir.wrapper() if dir != null else null
