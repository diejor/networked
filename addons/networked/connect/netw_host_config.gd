## A pure-data description of a session to host.
##
## A [NetwHostConfig] names how to open a session by [member scheme] and policy
## fields, never by a transport instance. [NetwConnector] hands it to every
## registered [NetwTransport], and the first whose
## [method NetwTransport._can_host] recognizes the scheme opens the listen peer.
## The recognizing transport validates [member params] for its own scheme, so a
## bare config carries no transport behavior and stays safe to save and reuse.
## [codeblock]
## var config := NetwHostConfig.new()
## config.scheme = &"enet"
## config.server_name = "My Server"
## config.max_players = 8
## config.params = { "port": 7777 }
## var attempt := connector.host(config)
## [/codeblock]
class_name NetwHostConfig
extends Resource

## Transport scheme selecting a [NetwTransport].
@export var scheme: StringName = &""

## Display name advertised for the created session.
@export var server_name: String = ""

## Advertised visibility, mapped to [enum LobbyDirectory.Visibility] by lobby
## transports and ignored by direct transports.
@export var visibility: int = 0

## Maximum admitted players, or [code]0[/code] for the transport default.
@export var max_players: int = 0

## Scheme-specific parameters (port, max_clients) validated by the recognizing
## transport.
@export var params: Dictionary = { }
