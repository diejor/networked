## Connects a [MultiplayerTree] into one Discord Activity instance's session.
##
## Every participant in one Activity instance receives the same
## [code]instance_id[/code]. A [DiscordRendezvous] turns that id into a live
## session. The backend owns whether that means joining a dedicated room or
## electing one relay host through storage.
## [codeblock]
## instance_id -> connect_session(instance_id, tree, payload) -> Error
## [/codeblock]
@abstract
class_name DiscordRendezvous
extends Resource


## Wires backend-specific seams after [param tree] is available.
##
## The default does nothing. [NakamaDiscordRendezvous] uses this to install the
## Discord iframe proxy resolver.
func bind(_tree: MultiplayerTree) -> void:
	pass


## Connects [param tree] into the session keyed by [param instance_id].
##
## Implementations drive [method MultiplayerTree.host_player] or
## [method MultiplayerTree.join] directly. Returns [constant OK] once the tree
## is online, or an [enum Error] when no session could be reached.
@abstract
func connect_session(
		instance_id: String, tree: MultiplayerTree, payload: JoinPayload,
) -> Error
