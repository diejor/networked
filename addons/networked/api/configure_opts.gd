## Options bag for [method NetwSpawn.configure_node].
##
## Carries the configuration knobs an entity-agnostic spawn helper needs.
## Built as a [RefCounted] rather than a method-arg cluster so future
## options (spawn-only property sources, scene synchronizer auto-track,
## etc.) can be added without breaking call sites.
class_name ConfigureOpts
extends RefCounted

## How the spawned node's multiplayer authority is decided.
enum AuthorityPolicy {
	## Authority stays at the server peer ([code]1[/code]).
	SERVER,
	## Authority is set to [member authority_peer]. Used by the player flow
	## via [SpawnerComponent.AuthorityMode.CLIENT].
	FIXED_PEER,
	## Authority is left untouched. The caller (or an [EntityComponent]
	## subclass) is expected to set it elsewhere.
	INHERIT,
}

## Stable string id for this entity class. Used for save-table routing
## and debug labels. Optional.
var class_id: StringName

## How authority is decided at spawn time. Defaults to [code]SERVER[/code]
## so a missed configuration produces server-authoritative behavior, which
## is the safest default.
var authority_policy: AuthorityPolicy = AuthorityPolicy.SERVER

## Peer id used when [member authority_policy] is [code]FIXED_PEER[/code].
var authority_peer: int = 1

## When non-empty, override [member Node.name] before placement.
## Useful for player nodes that encode authority in the name.
var name_override: String = ""


func _init(
	p_class_id: StringName = &"",
	p_authority_policy: AuthorityPolicy = AuthorityPolicy.SERVER,
	p_authority_peer: int = 1,
) -> void:
	class_id = p_class_id
	authority_policy = p_authority_policy
	authority_peer = p_authority_peer
