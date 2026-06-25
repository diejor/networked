## Opt-in base for a [Node] that registers itself as a session service on the
## owning [MultiplayerTree].
##
## Registration is bound to tree membership. The service enters the registry on
## [code]_enter_tree[/code] and leaves on [code]_exit_tree[/code], skipping the
## editor, so it is discoverable through [method NetwServices.get_service]
## exactly while it is mounted under a live tree. The lifecycle is sealed.
## Subclasses never override [code]_enter_tree[/code] or [code]_exit_tree[/code].
## They override [method service_entered] and [method service_exiting] instead,
## so forgetting a [code]super[/code] call can never silently drop registration.
## [codeblock]
## class_name MatchClock
## extends NetwService
##
## func service_type() -> Script:
##     return MatchClock           # register under a family base, optional
##
## func service_entered(mt: MultiplayerTree) -> void:
##     mt.session_entered.connect(_on_session_entered)
##
## func service_exiting(mt: MultiplayerTree) -> void:
##     mt.session_entered.disconnect(_on_session_entered)
## [/codeblock]
##
## Nodes that already extend a non-[Node] base (such as
## [MultiplayerSceneManager]) cannot adopt this base under GDScript single
## inheritance. They call [method NetwServices.register] and
## [method NetwServices.unregister] directly.
@abstract
class_name NetwService
extends Node

## Optional probe an embedding addon sets when the runtime environment allows
## only a WebSocket/HTTP relay, such as a Discord iframe that forbids WebRTC and
## native SDKs. A transport service that needs peer-to-peer or a native client
## consults [method is_transport_restricted] in [method should_register] (and
## gates its own polling) to stay dormant there. Unset in a normal build, so
## nothing pays for it. [code]networked_activity[/code] wires this to its embed
## detection.
static var transport_restricted_probe: Callable


## Returns [code]true[/code] when [member transport_restricted_probe] reports the
## environment forbids peer-to-peer and native transports. Returns
## [code]false[/code] when no probe is set, which is the normal case.
static func is_transport_restricted() -> bool:
	return transport_restricted_probe.is_valid() and bool(transport_restricted_probe.call())

# ---------------------------------------------------------------------------
# Override points
# ---------------------------------------------------------------------------


## Returns the registration key for this service.
##
## Return a family base type so [method NetwServices.get_service] and
## [method NetwServices.get_services] resolve subclasses under it. Return
## [code]null[/code] (the default) to register under the concrete script, which
## keeps each instance under a unique key. Several instances sharing one key
## overwrite each other in the registry, so families with many instances (like
## [LobbyDirectory]) keep the concrete default and are collected through
## [method NetwServices.get_services].
func service_type() -> Script:
	return null


## Returns [code]true[/code] when this service should register on entering the
## tree. Override to opt out at runtime, for example under a test runner or
## behind a feature flag. The default always registers.
func should_register() -> bool:
	return true


## Called after the service registers, with the owning [param mt].
##
## Override for per-service setup such as signal wiring or clock binding. It does
## not run in the editor, when [method should_register] returns
## [code]false[/code], or when the node is not under a [MultiplayerTree].
func service_entered(_mt: MultiplayerTree) -> void:
	pass


## Called before the service unregisters, with the owning [param mt].
##
## Override to tear down whatever [method service_entered] set up. Mirrors the
## conditions of [method service_entered].
func service_exiting(_mt: MultiplayerTree) -> void:
	pass

# ---------------------------------------------------------------------------
# Sealed lifecycle
# ---------------------------------------------------------------------------


func _enter_tree() -> void:
	if Engine.is_editor_hint() or not should_register():
		return
	var mt := NetwServices.register(self, service_type())
	if is_instance_valid(mt):
		service_entered(mt)


func _exit_tree() -> void:
	if Engine.is_editor_hint() or not should_register():
		return
	var mt := MultiplayerTree.resolve(self)
	if is_instance_valid(mt):
		service_exiting(mt)
	NetwServices.unregister(self, service_type())
