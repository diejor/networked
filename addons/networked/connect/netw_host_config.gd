## A pure-data description of a session to host.
##
## A [NetwHostConfig] names how to open a session by [member transport] and
## policy fields, never by a transport instance. [NetwConnector] hands it to
## every registered [NetwTransport], and the first whose
## [method NetwTransport._can_host] recognizes it opens the listen peer. Because
## [member scheme] reads out of the params rather than being authored beside
## them, a config that names one transport and configures another is
## unrepresentable.
## [codeblock]
## var enet := NetwENetParams.new()
## enet.port = 21253
##
## var config := NetwHostConfig.new()
## config.transport = enet          # config.scheme now reads &"enet"
## config.server_name = "My Server"
## config.max_players = 8
## var attempt := connector.host(config)
## [/codeblock]
class_name NetwHostConfig
extends Resource

## The typed inputs the opening [NetwTransport] reads, and the fact that selects
## it.
##
## Authored as a [NetwTransportParams] subclass, so the inspector offers every
## registered params type and GDScript type-checks the fields. Leave it
## [code]null[/code] only when a caller means "no transport configured", which
## every host path reports as [constant @GlobalScope.ERR_UNCONFIGURED].
@export var transport: NetwTransportParams

## Display name advertised for the created session.
@export var server_name: String = ""

## Advertised visibility, mapped to [enum LobbyDirectory.Visibility] by lobby
## transports and ignored by direct transports.
@export var visibility: int = 0

## Maximum admitted players, or [code]0[/code] for the transport default.
@export var max_players: int = 0

## The scheme [member transport] is authored for, or [code]&""[/code] when none
## is set.
##
## A read-only reflection of [method NetwTransportParams._scheme]. Set
## [member transport] to change it.
var scheme: StringName:
	get:
		return transport._scheme() if transport else &""
