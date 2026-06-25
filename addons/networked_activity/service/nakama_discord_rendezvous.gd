## [DiscordRendezvous] that maps a Discord instance to a Nakama relay match.
##
## The rendezvous record is one public-read storage object under
## [member collection], keyed by the Discord [code]instance_id[/code]. The body
## carries the relay [code]match_id[/code]. The first participant finds no
## record and hosts a match. Later participants read the record, join it, and
## fall back to hosting when the recorded match is stale.
## [codeblock]
## collection = "discord_rendezvous"
## key        = instance_id
## value      = { "match_id": "<relay match id>", "ts": <unix seconds> }
##
## connect_session():
##     freshest record -> join the relay match
##     no live record  -> host a relay match, publish, read back, converge
## [/codeblock]
class_name NakamaDiscordRendezvous
extends DiscordRendezvous

## Nakama storage collection the instance-to-match records live under.
@export var collection: String = "discord_rendezvous"

## Dev-portal URL-mapping prefix Nakama is reached at through Discord's iframe
## proxy. The default matches a [code]/nakama[/code] dev-portal mapping.
@export var proxy_prefix: String = "nakama"

## Nakama server key, matching the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Relay host name or address, without scheme.
@export var host: String = "127.0.0.1"

## Relay port for a direct connection.
@export var port: int = 7350

## When [code]true[/code], connects over [code]https[/code] and
## [code]wss[/code].
@export var use_ssl: bool = false

## Device id used for Nakama authentication. Empty falls back to
## [method OS.get_unique_id].
@export var device_id: String = ""


## Installs the Nakama iframe-proxy seam so the relay client and socket route
## through Discord's proxy when embedded.
func bind(_tree: MultiplayerTree) -> void:
	NakamaWrapper.proxy_base_resolver = _resolve_proxy_base


## Connects [param tree] to the Nakama match keyed by [param instance_id].
func connect_session(
		instance_id: String, tree: MultiplayerTree, payload: JoinPayload,
) -> Error:
	if instance_id.is_empty():
		Netw.dbg.warn("NakamaDiscordRendezvous: empty instance_id.")
		return ERR_INVALID_PARAMETER
	var wrapper := await _ready_wrapper(tree)
	if wrapper == null:
		return ERR_UNAVAILABLE

	var mid := await _freshest_match(wrapper, instance_id)
	if not mid.is_empty():
		Netw.dbg.info(
			"NakamaDiscordRendezvous: joining match %s for instance %s.",
			[mid, instance_id],
		)
		var join_err := await tree.join(_target_for(mid), payload)
		if join_err == OK:
			return OK
		Netw.dbg.info(
			"NakamaDiscordRendezvous: recorded join failed (%s). Hosting.",
			[error_string(join_err)],
		)

	Netw.dbg.info(
		"NakamaDiscordRendezvous: no live record for instance %s. Hosting.",
		[instance_id],
	)
	return await _host_and_commit(instance_id, tree, payload, wrapper)


# Resolves the Nakama proxy base for Discord's iframe proxy.
func _resolve_proxy_base(host_node: Node, config_host: String) -> String:
	if config_host.ends_with(".discordsays.com"):
		var id := config_host.split(".")[0]
		return "%s.discordsays.com/.proxy/%s" % [id, proxy_prefix]
	if host_node == null:
		return ""
	var mt := MultiplayerTree.resolve(host_node)
	if mt == null:
		return ""
	var svc := mt.get_service(DiscordActivityService) as DiscordActivityService
	if svc == null or svc.client_id.is_empty():
		return ""
	return "%s.discordsays.com/.proxy/%s" % [svc.client_id, proxy_prefix]


# Hosts a private match, publishes the record, and joins the winner on a race.
func _host_and_commit(
		instance_id: String,
		tree: MultiplayerTree,
		payload: JoinPayload,
		wrapper: NakamaWrapper,
) -> Error:
	tree.backend = _target_for("").make_backend_instance()
	var opts := LobbyDirectory.HostOptions.make(
		"Discord Activity", LobbyDirectory.Visibility.PRIVATE,
	)
	var host_err := await tree.host_player(payload, opts)
	if host_err != OK:
		return host_err
	var winner := await _commit_host(instance_id, tree, wrapper)
	if winner.is_empty():
		return OK
	Netw.dbg.info(
		"NakamaDiscordRendezvous: lost host race for instance %s. Joining %s.",
		[instance_id, winner],
	)
	await tree.disconnect_player()
	return await tree.join(_target_for(winner), payload)


# Publishes this host's match id and returns a fresher winner if one exists.
func _commit_host(
		instance_id: String, tree: MultiplayerTree, wrapper: NakamaWrapper,
) -> String:
	var dir := tree.get_service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	if dir == null:
		return ""
	var my_match := dir.get_join_address()
	if my_match.is_empty():
		return ""

	var wrote := await wrapper.write_public_storage(
		collection,
		instance_id,
		{ "match_id": my_match, "ts": Time.get_unix_time_from_system() },
	)
	Netw.dbg.debug(
		"NakamaDiscordRendezvous: commit wrote instance=%s match=%s ok=%s",
		[instance_id, my_match, wrote],
	)
	var winner := await _freshest_match(wrapper, instance_id)
	if winner.is_empty() or winner == my_match:
		return ""
	return winner


# Authenticates the shared Nakama session and returns a wrapper bound to it.
func _ready_wrapper(tree: MultiplayerTree) -> NakamaWrapper:
	if not NakamaWrapper.is_addon_present():
		Netw.dbg.warn("NakamaDiscordRendezvous: Nakama addon not present.")
		return null
	var session := tree.get_nakama_session()
	if session == null:
		return null
	session.configure({
		"auth_mode": "device",
		"server_key": server_key,
		"host": host,
		"port": port,
		"use_ssl": use_ssl,
		"device_id": _normalized_device_id(),
	})
	var auth := await session.connect_async()
	if not auth.ok:
		Netw.dbg.error("NakamaDiscordRendezvous: session auth failed: %s", [
			auth.error,
		])
		return null
	var wrapper := NakamaWrapper.new()
	wrapper.use_session(session)
	return wrapper


# Returns the freshest timestamped record for instance_id across all owners.
func _freshest_match(wrapper: NakamaWrapper, instance_id: String) -> String:
	var best_mid := ""
	var best_ts := 0.0
	for obj in await wrapper.list_public_storage(collection):
		if String(obj.get("key", "")) != instance_id:
			continue
		var value: Variant = obj.get("value")
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var mid := String(value.get("match_id", ""))
		var ts := float(value.get("ts", 0.0))
		if mid.is_empty() or ts <= 0.0:
			continue
		if ts > best_ts:
			best_ts = ts
			best_mid = mid
	return best_mid


# Normalizes device_id to Nakama's 10-128 byte requirement.
func _normalized_device_id() -> String:
	if device_id.is_empty():
		return ""
	if device_id.length() < 10:
		return "netw-discord-" + device_id
	return device_id.left(128)


func _target_for(match_id: String) -> JoinTarget:
	var target := JoinTarget.new()
	target.display_name = "Discord Activity"
	target.address = match_id
	target.backend = NakamaBackend.new()
	target.metadata = { "instance_match_id": match_id }
	return target
