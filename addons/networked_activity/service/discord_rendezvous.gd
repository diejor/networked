## Turns a Discord Activity instance id into a [MultiplayerTree] session.
##
## Every participant in one Activity sees the same [code]instance_id[/code]. The
## implementation decides whether that means joining a server room or resolving a
## relay match.
## [codeblock]
## instance_id
## └── connect_session(instance_id, tree, payload)
##     ├── OK
##     └── Error
## [/codeblock]
@abstract
class_name DiscordRendezvous
extends Resource

## Wires backend-specific seams after [param tree] is available.
##
## Default implementation does nothing.
func bind(_tree: MultiplayerTree) -> void:
	pass


## Connects [param tree] into the session keyed by [param instance_id].
##
## Implementations call [method MultiplayerTree.host_player] or
## [method MultiplayerTree.join]. Returns [constant OK] after the tree is online.
@abstract
func connect_session(
		instance_id: String,
		tree: MultiplayerTree,
		payload: JoinPayload,
) -> Error
