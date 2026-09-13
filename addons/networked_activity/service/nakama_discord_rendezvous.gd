## [DiscordRendezvous] that maps a Discord instance to a Nakama relay match.
##
## [member collection] stores public records keyed by Discord
## [code]instance_id[/code]. The freshest live record wins.
## [codeblock]
## Storage
## └── collection
##     └── instance_id
##         ├── match_id (String)
##         └── ts (float)
##
## connect_session()
## ├── freshest live record -> join relay match.
## └── no live record       -> host, publish, read back, converge.
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


## Installs the Discord iframe proxy seam for Nakama traffic.
func bind(_tree: MultiplayerTree) -> void:
	NakamaWrapper.proxy_base_resolver = _resolve_proxy_base


## Joins or claims the Nakama relay match keyed by [param instance_id].
func connect_session(
		instance_id: String,
		tree: MultiplayerTree,
		username: StringName,
		join_args: Array,
) -> Error:
	if instance_id.is_empty():
		push_warning("NakamaDiscordRendezvous: empty instance_id.")
		return ERR_INVALID_PARAMETER
	var wrapper := await _ready_wrapper(tree)
	if wrapper == null:
		return ERR_UNAVAILABLE

	var mid := await _freshest_match(wrapper, instance_id)
	if not mid.is_empty():
		var join_err := await _join_match(tree, mid, username, join_args)
		if join_err == OK:
			return OK

	return await _host_and_commit(
		instance_id, tree, username, join_args, wrapper
	)


# Resolves the Nakama proxy base for Discord's iframe proxy.
func _resolve_proxy_base(host_node: Node, config_host: String) -> String:
	if config_host.ends_with(".discordsays.com"):
		var id := config_host.split(".")[0]
		return "%s.discordsays.com/.proxy/%s" % [id, proxy_prefix]
	if host_node == null:
		return ""
	# The ancestor walk rather than Netw.of, because a tree that was never
	# mounted installs no api on any SceneTree path and still owns one.
	var api := _owning_session(host_node)
	if api == null:
		return ""
	var svc := api.service_get(DiscordActivityService) as DiscordActivityService
	if svc == null or svc.client_id.is_empty():
		return ""
	return "%s.discordsays.com/.proxy/%s" % [svc.client_id, proxy_prefix]


# The session owning [param node], found through the branch it is installed on
# when there is one, and up the ancestor chain when the tree holding it has not
# been mounted.
static func _owning_session(node: Node) -> NetwMultiplayer:
	var api := Netw.of(node)
	if api != null:
		return api
	var walk := node
	while walk != null:
		var tree := walk as MultiplayerTree
		if tree != null:
			return tree.api
		walk = walk.get_parent()
	return null


# Hosts a private match, publishes the record, and joins the winner on a race.
func _host_and_commit(
		instance_id: String,
		tree: MultiplayerTree,
		username: StringName,
		join_args: Array,
		wrapper: NakamaWrapper,
) -> Error:
	tree.peer_class = &"NakamaRelayPeer"
	var info := NetwServerInfo.new()
	info.motd = "Discord Activity"
	info.visibility = NetwServerInfo.VISIBILITY_PRIVATE
	Netw.session(tree).set_server_info(info)
	var host_err := await bring_up(
		tree,
		NetwMultiplayer.TRANSPORT_MODE_HOST,
		"",
		username,
		join_args,
	)
	if host_err != OK:
		return host_err
	var winner := await _commit_host(instance_id, tree, wrapper)
	if winner.is_empty():
		return OK
	await Netw.session(tree).leave().wait()
	return await _join_match(tree, winner, username, join_args)


# Publishes this host's match id and returns a fresher winner if one exists.
func _commit_host(
		instance_id: String,
		tree: MultiplayerTree,
		wrapper: NakamaWrapper,
) -> String:
	var dir := Netw.service(tree, NakamaLobbyDirectory) as NakamaLobbyDirectory
	if dir == null:
		return ""
	var my_match := dir._join_address()
	if my_match.is_empty():
		return ""

	var wrote := await wrapper.write_public_storage(
		collection,
		instance_id,
		{ "match_id": my_match, "ts": Time.get_unix_time_from_system() },
	)
	if not wrote:
		push_warning(
			"NakamaDiscordRendezvous: instance %s did not record match %s."
			% [instance_id, my_match]
		)
	var winner := await _freshest_match(wrapper, instance_id)
	if winner.is_empty() or winner == my_match:
		return ""
	return winner


# Authenticates the shared Nakama session and returns a wrapper bound to it.
func _ready_wrapper(tree: MultiplayerTree) -> NakamaWrapper:
	if not NakamaWrapper.is_addon_present():
		push_warning("NakamaDiscordRendezvous: Nakama addon not present.")
		return null
	var session := NakamaSessionService.of(tree)
	if session == null:
		return null
	session.configure(
		{
			"auth_mode": "device",
			"server_key": server_key,
			"host": host,
			"port": port,
			"use_ssl": use_ssl,
			"device_id": _normalized_device_id(),
		},
	)
	var auth := await session.connect_async()
	if not auth.ok:
		push_error(
			"NakamaDiscordRendezvous: session auth failed: %s" % [auth.error]
		)
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


func _join_match(
		tree: MultiplayerTree,
		match_id: String,
		username: StringName,
		join_args: Array,
) -> Error:
	tree.peer_class = &"NakamaRelayPeer"
	return await bring_up(
		tree,
		NetwMultiplayer.TRANSPORT_MODE_CLIENT,
		match_id,
		username,
		join_args,
	)
