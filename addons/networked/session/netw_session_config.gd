## Typed registration payload for the [NetwSessionInterface] session machine.
##
## A [MultiplayerTree] snapshots its session exports into one of these and hands
## it to [method MultiplayerAPI.object_configuration_add]. The core dispatches on
## the resource type rather than the node class, so the session is configured the
## same way whether a [MultiplayerTree], a test rig, or a future native caller
## supplies the values. This resource carries only the facts the wire and the
## session machine own. The connect kit's own knobs (transport, headless
## auto-host, probe budget) live in a separate kit config.
## [codeblock]
## var config := NetwSessionConfig.new()
## config.app_id = &"bomber-v2"
## config.desired_role = NetwSessionInterface.Role.LISTEN_SERVER
## api.object_configuration_add(tree, config)
## # core routes it to NetwSessionInterface.configure(config)
## [/codeblock]
class_name NetwSessionConfig
extends NetwObjectConfig

## Game-build tag that gates session admission.
##
## A joining peer whose tag differs is rejected during the auth handshake before
## it reaches [method MultiplayerAPI.get_peers]. Leave it empty to disable the
## gate. See [member NetwSessionInterface.app_id].
@export var app_id: StringName = ""

## The [enum NetwSessionInterface.Role] the local peer intends to play once a session
## starts.
##
## The live [member NetwSessionInterface.role] is only assigned when the session
## reaches [constant NetwSessionInterface.State.ONLINE]. This is the intent the
## assignment edge reads to pick the server role. See
## [member NetwSessionInterface.desired_role].
@export var desired_role: NetwSessionInterface.Role = NetwSessionInterface.Role.LISTEN_SERVER
