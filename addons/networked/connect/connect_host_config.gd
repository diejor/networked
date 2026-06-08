## Transport-layer config passed to [method ConnectSession.host].
##
## Holds the backend template (duplicated on use) and the user-facing server
## name. Player identity (username, spawner path) lives on the [JoinPayload]
## that [method ConnectSession.host] takes alongside this config.
##
## [b]Backend-is-template invariant:[/b] [member backend] is a
## template only. Use [method make_backend_instance] to obtain a
## fresh instance before assigning to a tree, mirroring
## [method JoinTarget.make_backend_instance].
class_name ConnectHostConfig
extends Resource

## Backend template (duplicated on use via [method make_backend_instance]).
@export var backend: BackendPeer

## Display name for the created session.
@export var server_name: String = ""


## Returns a fresh [BackendPeer] derived from [member backend], or
## [code]null[/code] when no template is set.
##
## Use this instead of assigning [member backend] directly: the field
## is a template, and reusing it would leak runtime state.
func make_backend_instance() -> BackendPeer:
	if backend == null:
		return null
	return backend.clone()
