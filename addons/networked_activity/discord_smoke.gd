## Standalone pre-requirement smoke scene for the Discord Activity path.
##
## Re-run this whenever the export config or the proxy path changes. It logs SDK
## reachability (handshake, [code]instance_id[/code]) and, if a
## [NakamaSessionService] is configured, whether the Nakama socket opens through
## whatever host it is pointed at (a local Docker server during development, or the
## [code]discordsays.com[/code] iframe proxy when embedded). It never asserts, only
## logs, so it is safe to ship as the main scene of a diagnostic export.
extends Control

## Discord application (client) id used for the SDK handshake.
@export var client_id: String = ""

## OAuth token endpoint. Empty skips the OAuth step.
@export var token_endpoint: String = "/.proxy/token"

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

	if token_endpoint.is_empty():
		_log("SDK: token_endpoint unset, skipping OAuth")
		return

	_log("SDK: commanding authorize...")
	var authorize = await sdk.command_authorize("code", ["identify", "guilds"], "")
	if authorize == null or authorize.code.is_empty():
		_log("[color=red]OAuth failed[/color]: no code returned")
		return

	_log("SDK: fetching token from %s..." % token_endpoint)
	var url := token_endpoint
	if not url.begins_with("http"):
		var origin := String(JavaScriptBridge.eval("window.location.origin", true))
		url = origin + url

	var http := HTTPRequest.new()
	add_child(http)
	var err := http.request(url, ["Content-Type: application/json"], HTTPClient.METHOD_POST, JSON.stringify({"code": authorize.code}))
	if err != OK:
		_log("[color=red]Token exchange request failed to start[/color]")
		http.queue_free()
		return
		
	var http_res: Array = await http.request_completed
	http.queue_free()

	if typeof(http_res[3]) != TYPE_PACKED_BYTE_ARRAY:
		_log("[color=red]Token failed[/color]: invalid response body type")
		return

	var parsed: Variant = JSON.parse_string(http_res[3].get_string_from_utf8())
	if typeof(parsed) != TYPE_DICTIONARY or parsed.get("access_token", "").is_empty():
		_log("[color=red]Token failed[/color]: no access_token in response")
		return

	_log("SDK: commanding authenticate...")
	var auth = await sdk.command_authenticate(parsed.access_token)
	if auth != null and auth.user != null:
		var u = auth.user
		_log("[color=green]OAuth identity ready[/color]: %s (id: %s)" % [u.global_name, u.id])
	else:
		_log("[color=red]OAuth identity failed[/color]")


func _check_nakama() -> void:
	if nakama_host.is_empty():
		_log("Nakama: host unset, skipping")
		return
	if not NakamaWrapper.is_addon_present():
		_log("Nakama: addon not present, skipping")
		return
	# This scene has no DiscordActivityService, so register the proxy seam by hand
	# through a rendezvous to route a .discordsays.com host through the iframe proxy.
	# The Callable keeps the rendezvous alive, so no member ref is needed.
	var rendezvous := NakamaDiscordRendezvous.new()
	rendezvous.bind(null, null)
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
