class_name NetComponent
extends Node

## Base class for all networked addon components.
##
## Provides ergonomic instance-method access to the session's [MultiplayerTree],
## [MultiplayerLobbyManager], [TPLayerAPI], and [PeerContext] buckets — replacing
## the old [NetworkedAPI] static helper.
##
## The lookup chain is: [code]node.multiplayer[/code] (session-scoped [SceneMultiplayer])
## → metadata key [code]_multiplayer_tree[/code] → [MultiplayerTree] instance.
## This is path-independent and safe across node renames.


## Returns the [MultiplayerTree] that owns this component's multiplayer session.
## Returns [code]null[/code] if called before [method MultiplayerTree.host] /
## [method MultiplayerTree.join] completes, or in the editor.
func get_multiplayer_tree() -> MultiplayerTree:
	var api := multiplayer as SceneMultiplayer
	if not api:
		return null
	return api.get_meta(&"_multiplayer_tree", null) as MultiplayerTree


## Returns the [MultiplayerLobbyManager] for this session.
func get_lobby_manager() -> MultiplayerLobbyManager:
	var tree := get_multiplayer_tree()
	return tree.lobby_manager if tree else null


## Returns the [TPLayerAPI] for visual teleport transitions on the local client.
## Always returns [code]null[/code] on the server.
func get_tp_layer() -> TPLayerAPI:
	if not is_inside_tree() or multiplayer.is_server():
		return null
	var manager := get_lobby_manager()
	return manager.tp_layer if manager else null


## Returns the [PeerContext] for [param peer_id], defaulting to the local peer.
func get_peer_context(peer_id: int = multiplayer.get_unique_id()) -> PeerContext:
	var tree := get_multiplayer_tree()
	return tree.get_peer_context(peer_id) if tree else null


## Returns the typed bucket for [param bucket_type] from the local peer's context.
## Shorthand for [code]get_peer_context().get_bucket(BucketType)[/code].
func get_bucket(bucket_type) -> RefCounted:
	var ctx := get_peer_context()
	return ctx.get_bucket(bucket_type) if ctx else null
