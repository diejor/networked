## Standalone pre-requirement smoke scene for the Discord Activity path.
##
## Re-run this whenever the export config or the proxy path changes. It is the
## living form of the Phase 0 checklist: it logs SDK reachability (handshake,
## [code]instance_id[/code]) and, if a [NakamaSessionService] is configured,
## whether the Nakama socket opens through whatever host it is pointed at (local
## Docker on Tier 1, the discordsays proxy on Tier 2). It never asserts, only
## logs, so it is safe to ship as the main scene of a diagnostic export.
extends Control

## Discord application (client) id used for the SDK handshake.
@export var client_id: String = ""

## Nakama relay host to probe. Empty skips the Nakama check.
@export var nakama_host: String = "127.0.0.1"
@export var nakama_port: int = 7350
@export var nakama_use_ssl: bool = false

@onready var _out: RichTextLabel = $Output


func _ready() -> void:
	_log("[b]Discord Activity smoke[/b]")
	_log("web feature: %s, OS: %s" % [OS.has_feature("web"), OS.get_name()])
	await _check_sdk()
	await _check_nakama()
	_log("[i]smoke complete[/i]")


func _check_sdk() -> void:
	if client_id.is_empty():
		_log("SDK: client_id unset, skipping handshake")
		return
	if not (OS.has_feature("web") or OS.get_name() == "Web"):
		_log("SDK: not in a browser, handshake unavailable")
		return
	var sdk := DiscordSDK.new()
	add_child(sdk)
	sdk.init(client_id)
	_log("SDK: shaking hands, instance_id=%s" % sdk.instance_id)
	await sdk.ready()
	_log("[color=green]SDK ready[/color] instance_id=%s channel_id=%s guild_id=%s"
			% [sdk.instance_id, sdk.channel_id, sdk.guild_id])


func _check_nakama() -> void:
	if nakama_host.is_empty():
		_log("Nakama: host unset, skipping")
		return
	if not NakamaWrapper.is_addon_present():
		_log("Nakama: addon not present, skipping")
		return
	var wrapper := NakamaWrapper.new()
	var res := await wrapper.connect_async(self, {
		"host": nakama_host,
		"port": nakama_port,
		"use_ssl": nakama_use_ssl,
		"device_id": "smoke-%d" % (Time.get_ticks_msec()),
	})
	if res.ok:
		_log("[color=green]Nakama reachable[/color] at %s:%d (ssl=%s)"
				% [nakama_host, nakama_port, nakama_use_ssl])
	else:
		_log("[color=red]Nakama unreachable[/color]: %s" % res.error)
	wrapper.leave()


func _log(line: String) -> void:
	print(line)
	if is_instance_valid(_out):
		_out.append_text(line + "\n")
