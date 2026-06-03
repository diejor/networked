## Saved or discovered server entry the browser can probe and join.
##
## A [JoinTarget] is one row in the server list. It bundles a [member backend]
## template and an [member address] along with display and metadata fields.
## [br][br]
## [b]Backend-is-template invariant:[/b] [member backend] is a config
## template only. Callers must obtain a fresh instance via
## [method make_backend_instance] before assigning to a tree or running
## a probe. Assigning [member backend] directly is a bug: runtime state
## from one session would leak into the next.
class_name JoinTarget
extends Resource

## Display label shown in the server list.
@export var display_name: String = ""

## Backend template (duplicated on use via [method make_backend_instance]).
@export var backend: BackendPeer

## Address handed to the backend (host:port, room code, etc.).
@export var address: String = ""

## Free-form metadata (motd cache, region tag, etc.).
@export var metadata: Dictionary = { }


## Returns a fresh [BackendPeer] derived from [member backend], or
## [code]null[/code] when no template is set.
##
## Use this instead of assigning [member backend] directly: the field
## is a template, and reusing it would leak runtime state.
func make_backend_instance() -> BackendPeer:
	if backend == null:
		return null
	return backend.clone()
