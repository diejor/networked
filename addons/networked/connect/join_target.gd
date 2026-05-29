## Saved or discovered server entry the browser can probe and join.
##
## A [JoinTarget] is one row in the server list. It either points at a
## direct [BackendPeer] + [member address] (when [member provider_id] is
## empty), or at an external lobby reached through a [LobbyProvider]
## (when [member provider_id] is non-empty and [member remote_id] holds
## the provider-specific lobby ID).
## [br][br]
## [b]Backend-is-template invariant:[/b] [member backend] is a config
## template only. Callers must obtain a fresh instance via
## [method make_backend_instance] before assigning to a tree or running
## a probe. Assigning [member backend] directly is a bug: runtime state
## from one session would leak into the next.
## [br][br]
## [b]remote_id contract:[/b] providers must use a serializable
## primitive ([code]int[/code] or [code]String[/code]). The browser
## passes [member remote_id] back to [method LobbyProvider.join_lobby]
## unchanged.
@tool
class_name JoinTarget
extends Resource


## Display label shown in the server list.
@export var display_name: String = ""

## Provider id registered in [ProviderRegistry]. Empty means a direct
## target reached through [member backend] + [member address].
@export var provider_id: StringName = &""

## Provider-specific lobby identifier. Ignored for direct targets.
@export var remote_id: Variant = null

## Backend template (duplicated on use via [method make_backend_instance]).
@export var backend: BackendPeer

## Address handed to the backend (host:port, room code, etc.).
@export var address: String = ""

## Free-form metadata (motd cache, region tag, etc.).
@export var metadata: Dictionary = {}


## Returns [code]true[/code] when this target is a direct
## [member backend] + [member address] join (no provider involvement).
func is_direct() -> bool:
	return provider_id == &""


## Returns a fresh [BackendPeer] derived from [member backend], or
## [code]null[/code] when no template is set.
##
## Use this instead of assigning [member backend] directly: the field
## is a template, and reusing it would leak runtime state.
func make_backend_instance() -> BackendPeer:
	if backend == null:
		return null
	return backend.duplicate() as BackendPeer
