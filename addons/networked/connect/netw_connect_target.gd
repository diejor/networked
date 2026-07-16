## A pure-data description of a session to join.
##
## A [NetwConnectTarget] names a destination by [member scheme] and
## [member address], never by a transport instance. [NetwConnector] hands it to
## every registered [NetwTransport], and the first whose
## [method NetwTransport._can_join] recognizes the scheme builds the peer.
## Because it carries no behavior and no live state, the same target is safe to
## save, share, and reuse across attempts.
## [codeblock]
## var target := NetwConnectTarget.new()
## target.scheme = &"enet"
## target.address = "127.0.0.1:7777"
## var attempt := connector.join(target)
## [/codeblock]
class_name NetwConnectTarget
extends Resource

## Transport scheme selecting a [NetwTransport], such as [code]&"enet"[/code],
## [code]&"ws"[/code], [code]&"webrtc"[/code], [code]&"steam"[/code],
## [code]&"nakama"[/code], or [code]&"local"[/code].
@export var scheme: StringName = &""

## Transport-interpreted address (host:port, room code, or lobby id).
@export var address: String = ""

## Display label shown in browse UI.
@export var display_name: String = ""

## Free-form metadata (region tag, app id, or cached motd).
@export var metadata: Dictionary = { }
