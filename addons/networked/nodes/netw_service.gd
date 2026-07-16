## Opt-in base for a [Node] that registers itself as a session service on the
## owning [MultiplayerTree].
##
## Registration is bound to tree membership. The service enters the registry on
## [code]_enter_tree[/code] and leaves on [code]_exit_tree[/code], skipping the
## editor, so it is discoverable through [method NetwMultiplayer.get_service]
## exactly while it is mounted under a live tree. The lifecycle is sealed.
## Subclasses never override [code]_enter_tree[/code] or [code]_exit_tree[/code].
## They override [method _service_entered] and [method _service_exiting] instead,
## so forgetting a [code]super[/code] call can never silently drop registration.
## [codeblock]
## class_name MatchClock
## extends NetwService
##
## func _service_type() -> Script:
##     return MatchClock           # register under a family base, optional
##
## func _service_entered(mt: MultiplayerTree) -> void:
##     mt.session_entered.connect(_on_session_entered)
##
## func _service_exiting(mt: MultiplayerTree) -> void:
##     mt.session_entered.disconnect(_on_session_entered)
## [/codeblock]
##
## Nodes that already extend a non-[Node] base (such as
## [MultiplayerSceneManager]) cannot adopt this base under GDScript single
## inheritance. They call [method NetwService.register] and
## [method NetwService.unregister] directly.
@icon("res://addons/networked/assets/NetwService.svg")
@abstract
class_name NetwService
extends Node

## Optional probe an embedding addon sets when the runtime environment allows
## only a WebSocket/HTTP relay, such as a Discord iframe that forbids WebRTC and
## native SDKs. A transport service that needs peer-to-peer or a native client
## consults [method is_transport_restricted] in [method _should_register] (and
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
# Static registration helpers
# ---------------------------------------------------------------------------


## Registers [param service] as a session service on its branch's
## [NetwMultiplayer], reached through [member Node.multiplayer]. If [param type]
## is [code]null[/code], the service's script class is used as the registration
## key. A branch whose API is a plain [SceneMultiplayer] is a silent no-op, so
## the node still works as an ordinary [Node] and features degrade rather than
## error.
## [codeblock]
## func _enter_tree() -> void:
##     var mt := NetwService.register(self)
## [/codeblock]
## Returns the owning [MultiplayerTree] for callers that wire tree signals, or
## [code]null[/code] when [param service] is not under one.
static func register(service: Node, type: Script = null) -> MultiplayerTree:
	var api := NetwMultiplayer.of(service)
	if api:
		api.register_service(service, type)
	return MultiplayerTree.resolve(service)


## Unregisters [param service] from its branch's [NetwMultiplayer].
## [codeblock]
## func _exit_tree() -> void:
##     NetwService.unregister(self)
## [/codeblock]
## Returns the owning [MultiplayerTree], or [code]null[/code].
static func unregister(service: Node, type: Script = null) -> MultiplayerTree:
	var api := NetwMultiplayer.of(service)
	if api:
		api.unregister_service(service, type)
	return MultiplayerTree.resolve(service)

# ---------------------------------------------------------------------------
# Override points
# ---------------------------------------------------------------------------


## Returns the registration key for this service.
##
## Return a family base type so [method NetwMultiplayer.get_service] and
## [method NetwMultiplayer.get_services] resolve subclasses under it. Return
## [code]null[/code] (the default) to register under the concrete script, which
## keeps each instance under a unique key. Several instances sharing one key
## overwrite each other in the registry, so families with many instances (like
## [LobbyDirectory]) keep the concrete default and are collected through
## [method NetwMultiplayer.get_services].
func _service_type() -> Script:
	return null


## Returns [code]true[/code] when this service should register on entering the
## tree. Override to opt out at runtime, for example under a test runner or
## behind a feature flag. The default always registers.
func _should_register() -> bool:
	return true


## Called after the service registers, with the owning [param mt].
##
## Override for per-service setup such as signal wiring or clock binding. It does
## not run in the editor, when [method _should_register] returns
## [code]false[/code], or when the node is not under a [MultiplayerTree].
@warning_ignore("unused_parameter")
func _service_entered(mt: MultiplayerTree) -> void:
	pass


## Called before the service unregisters, with the owning [param mt].
##
## Override to tear down whatever [method _service_entered] set up. Mirrors the
## conditions of [method _service_entered].
@warning_ignore("unused_parameter")
func _service_exiting(mt: MultiplayerTree) -> void:
	pass

# ---------------------------------------------------------------------------
# Sealed lifecycle
# ---------------------------------------------------------------------------


func _enter_tree() -> void:
	if Engine.is_editor_hint() or not _should_register():
		return
	var mt := NetwService.register(self, _service_type())
	if is_instance_valid(mt):
		_service_entered(mt)


func _exit_tree() -> void:
	if Engine.is_editor_hint() or not _should_register():
		return
	var mt := MultiplayerTree.resolve(self)
	if is_instance_valid(mt):
		_service_exiting(mt)
	NetwService.unregister(self, _service_type())
