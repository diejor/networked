## Typed observation event dispatched by a [TreeProbe] for one [MultiplayerTree].
##
## The probe normalizes the tree's domain signals into this single stream so
## detection consumers react to one typed surface instead of subscribing to
## each node directly. The probe is bound to one tree for its whole life, so
## [member tree] is a constant anchor while [member node] is the per-event
## subject the consumer resolves context from.
## [br][br]
## In Phase 0 this is dispatched but not yet routed to validators. The validator
## registry that consumes it by [enum Kind] lands in a later phase.
@tool
class_name NetwTreeEvent
extends RefCounted

## The kinds the probe currently dispatches. Each maps one-to-one onto a domain
## signal the probe already observes.
enum Kind {
	PEER_CONNECTED, ## A remote peer joined; [member peer_id] is set, [member node] is null.
	PEER_DISCONNECTED, ## A remote peer left; [member peer_id] is set, [member node] is null.
	SCENE_SPAWNED, ## A [MultiplayerScene] spawned; [member node] is the scene.
	SCENE_DESPAWNED, ## A [MultiplayerScene] despawned; [member node] is the scene.
	PLAYER_SPAWNED, ## A player spawned; [member node] is the player root.
	LOCAL_PLAYER_CHANGED, ## The local authority player changed; [member node] is its owner or null.
	CLOCK_PONG, ## A clock pong was captured; [member data] carries the pong payload.
	ROLE_CHANGED, ## The tree's [enum MultiplayerTree.Role] changed; [member data] carries old/new.
}

## The kind of observation this event carries.
var kind: Kind

## The probe's tree. Always present, even for an offline tree at
## [constant MultiplayerTree.Role.NONE].
var tree: MultiplayerTree

## The primary subject node (player or scene). Null for peer and clock events,
## and may be freed by the time a consumer reads it (validators must
## null-check before anchoring).
var node: Node = null

## The peer id for peer events. Zero for every other kind.
var peer_id: int = 0

## Event-specific extras (e.g. the scene's [CheckpointToken] under
## [code]"token"[/code], the raw clock pong dictionary).
var data: Dictionary = { }


func _init(p_kind: Kind, p_tree: MultiplayerTree) -> void:
	kind = p_kind
	tree = p_tree


## Resolves a [NetwContext] anchored at [member node] when present, else at the
## tree. The node anchor gives a player event its [member NetwContext.entity]
## for free.
func ctx() -> NetwContext:
	return Netw.ctx(node) if is_instance_valid(node) else Netw.ctx(tree)


## Resolves a [NetwContext] anchored at an explicit node, for when a consumer
## wants a sibling or the scene root rather than the primary subject.
func ctx_for(n: Node) -> NetwContext:
	return Netw.ctx(n)
