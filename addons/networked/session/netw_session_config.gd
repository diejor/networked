## Typed registration payload for the [NetwMultiplayer] session machine.
##
## A [MultiplayerTree] snapshots its session exports into one of these and hands
## it to [method NetwMultiplayer.service_install]. The session dispatches on the
## resource type rather than the node class, so kit and code-first callers
## configure the same machine. This resource carries only the facts the wire and
## the session machine own. The connect kit's own knobs live in its kit config.
## [codeblock]
## var config := NetwSessionConfig.new()
## config.app_id = &"bomber-v2"
## config.desired_role = NetwMultiplayer.Role.LISTEN_SERVER
## api.service_install(config)
## [/codeblock]
class_name NetwSessionConfig
extends NetwObjectConfig

## Game-build tag that gates session admission.
##
## A joining peer whose tag differs is rejected during the auth handshake before
## it reaches [method MultiplayerAPI.get_peers]. Leave it empty to disable the
## gate. Read the live value back from [member NetwSessionHandle.app_id].
@export var app_id: StringName = ""

## The [enum NetwMultiplayer.Role] the local peer intends to play once a session
## starts.
##
## The live [member NetwMultiplayer.role] is only assigned when the session
## reaches [constant NetwMultiplayer.SessionState.ONLINE]. This is the intent the
## assignment edge reads to pick the server role.
@export var desired_role: NetwMultiplayer.Role = NetwMultiplayer.Role.LISTEN_SERVER

## The transport this session opens and joins over, authored once at install.
##
## This is what lets [method NetwConnector.host] be called with no config at
## all: [method NetwConnector.default_host_config] reads these params, so a
## session that registered its transport never has to be told again. Leaving it
## [code]null[/code] makes every self-sourcing host path report
## [constant @GlobalScope.ERR_UNCONFIGURED].
@export var transport: NetwTransportParams

## Optional latency and loss simulation applied to this session's peer.
##
## Authored beside [member transport] because it is tuning for the same
## connection. [NetwMultiplayer] wraps the built peer with it, so a session
## applies what it was configured with whether or not a [MultiplayerTree]
## owns it.
@export var link_conditions: NetwLinkConditions
