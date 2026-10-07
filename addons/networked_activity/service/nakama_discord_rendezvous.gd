## A rendezvous that meets through Nakama storage and plays on a relay match.
##
## The freshest record of an instance is its server. A host writes its own
## record after it starts, and the freshest one wins when two hosts race.
## [codeblock lang=text]
## collection
## ┖╴instance_id
##   ┠╴match_id  String
##   ┖╴ts        float
## [/codeblock]
class_name NakamaDiscordRendezvous
extends DiscordRendezvous

## Nakama storage collection that holds the records.
@export var collection: String = "discord_rendezvous"

## URL-mapping prefix Nakama is reached at through Discord's iframe proxy.
@export var proxy_prefix: String = "nakama"

## Matches the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Nakama host, without scheme.
@export var host: String = "127.0.0.1"

@export var port: int = 7350

## Connects over [code]https[/code] and [code]wss[/code].
@export var use_ssl: bool = false

## Empty falls back to [method OS.get_unique_id].
@export var device_id: String = ""

var session: NetwSession


func host_transport() -> StringName:
	return &"NakamaRelayPeer"


func host_settings() -> Dictionary:
	return { visibility = NetwServerInfo.VISIBILITY_PRIVATE }


func bind(bound: NetwSession) -> void:
	session = bound
	NakamaWrapper.proxy_base_resolver = resolve_proxy_base


func find(instance_id: String, connection: NetwConnection) -> NetwEndpoint:
	var wrapper := await authenticated_wrapper()
	if wrapper == null:
		return null
	var match_id := await freshest_match(wrapper, instance_id)
	if match_id.is_empty():
		return null
	return connection.add(&"NakamaRelayPeer", match_id)


func claim(instance_id: String, connection: NetwConnection) -> NetwEndpoint:
	var wrapper := await authenticated_wrapper()
	if wrapper == null:
		return null
	var wrote := await wrapper.write_public_storage(
		collection,
		instance_id,
		{
			"match_id": connection.join_address,
			"ts": Time.get_unix_time_from_system(),
		},
	)
	if not wrote:
		push_warning(
			"The Nakama server did not record match %s for instance %s."
			% [connection.join_address, instance_id],
		)
	var match_id := await freshest_match(wrapper, instance_id)
	if match_id.is_empty() or match_id == connection.join_address:
		return null
	return connection.add(&"NakamaRelayPeer", match_id)


func resolve_proxy_base(host_node: Node, config_host: String) -> String:
	if config_host.ends_with(".discordsays.com"):
		var id := config_host.split(".")[0]
		return "%s.discordsays.com/.proxy/%s" % [id, proxy_prefix]
	if host_node == null:
		return ""
	var activity := Netw.session(host_node).service(DiscordActivityService) \
			as DiscordActivityService
	if activity == null or activity.client_id.is_empty():
		return ""
	return "%s.discordsays.com/.proxy/%s" % [activity.client_id, proxy_prefix]


func authenticated_wrapper() -> NakamaWrapper:
	if not NakamaWrapper.is_addon_present():
		push_warning(
			"The Nakama addon is not installed, so NakamaDiscordRendezvous "
			+ "cannot find a server. Install the Nakama addon.",
		)
		return null
	var nakama := NakamaSessionService.of(session.root)
	nakama.configure(
		{
			"auth_mode": "device",
			"server_key": server_key,
			"host": host,
			"port": port,
			"use_ssl": use_ssl,
			"device_id": nakama_device_id(),
		},
	)
	var auth := await nakama.connect_async()
	if not auth.ok:
		push_error(
			(
				"The Nakama login failed with '%s'. Check host, port and "
				+ "server_key on NakamaDiscordRendezvous."
			) % auth.error,
		)
		return null
	var wrapper := NakamaWrapper.new()
	wrapper.use_session(nakama)
	return wrapper


func freshest_match(wrapper: NakamaWrapper, instance_id: String) -> String:
	var best_match := ""
	var best_ts := 0.0
	for record in await wrapper.list_public_storage(collection):
		if String(record.get("key", "")) != instance_id:
			continue
		var value: Variant = record.get("value")
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var match_id := String(value.get("match_id", ""))
		var ts := float(value.get("ts", 0.0))
		if not match_id.is_empty() and ts > best_ts:
			best_ts = ts
			best_match = match_id
	return best_match


## Pads or trims [member device_id] to Nakama's 10 to 128 byte rule.
func nakama_device_id() -> String:
	if device_id.is_empty():
		return ""
	if device_id.length() < 10:
		return "netw-discord-" + device_id
	return device_id.left(128)
