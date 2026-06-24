## [DiscordRendezvous] that maps a Discord instance to a Nakama relay match.
##
## The rendezvous record is one public-read storage object under
## [member collection], keyed by the Discord [code]instance_id[/code], whose body
## carries the relay [code]match_id[/code]. The first participant finds no record
## and hosts a match. Every later participant reads the record, confirms the
## match is still live, and joins it. Claiming is last-write-wins with a
## read-back in [method commit_host], not a compare-and-set, so two simultaneous
## hosts reconcile to one rather than racing forever.
## [codeblock]
## collection = "discord_rendezvous"
## key        = instance_id
## value      = { "match_id": "<relay match id>" }
##
## resolve():  record live  -> JoinTarget(NakamaBackend, address = match_id)
##             no record    -> JoinTarget(NakamaBackend, address = "")  # host
## commit_host(): publish my match_id, read back, defer to the winner if I lost
## [/codeblock]
## The storage object goes through [method NakamaWrapper.write_public_storage]
## and [method NakamaWrapper.read_public_storage] on the shared
## [method MultiplayerTree.get_nakama_session] account. The relay socket is
## reached through the same overridable [member host] / [member port] /
## [member use_ssl] the [NakamaLobbyDirectory] exposes, so the Discord proxy path
## is a config change, not a code change.
##
## This rendezvous owns the whole Nakama side of the Discord integration. It
## installs [member NakamaWrapper.proxy_base_resolver] from [method bind] so the
## relay client and socket route through Discord's iframe proxy, which is why the
## [DiscordActivityService] itself never names Nakama.
class_name NakamaDiscordRendezvous
extends DiscordRendezvous

## Nakama storage collection the instance-to-match records live under. Distinct
## from the [code]"lobbies"[/code] collection so Discord rendezvous never shows
## up in a normal lobby browse.
@export var collection: String = "discord_rendezvous"

## Dev-portal URL-mapping prefix Nakama is reached at through Discord's iframe
## proxy. Discord always serves the activity from
## [code]<client_id>.discordsays.com[/code] and always prepends the fixed
## [code]/.proxy/[/code] namespace, so only this trailing prefix is configurable.
## The default matches a [code]/nakama[/code] dev-portal mapping, making the proxy
## base [code]<client_id>.discordsays.com/.proxy/nakama[/code].
@export var proxy_prefix: String = "nakama"

## Nakama server key, matching the server's [code]socket.server_key[/code].
@export var server_key: String = "defaultkey"

## Relay host name or address, without scheme, for a [b]direct[/b] connection
## (local Docker Nakama, a tunnel, a dedicated server). When embedded in Discord,
## [method bind] rewrites the socket through the iframe proxy via
## [member NakamaWrapper.proxy_base_resolver], so this value is ignored there and
## the proxy path comes from [member proxy_prefix].
@export var host: String = "127.0.0.1"

## Relay port for a direct connection. Use [code]443[/code] behind a TLS
## terminating proxy or tunnel. Ignored when embedded (see [member host]).
@export var port: int = 7350

## When [code]true[/code], connects over [code]https[/code] and [code]wss[/code]
## for a direct connection. Ignored when embedded (see [member host]).
@export var use_ssl: bool = false

## Device id used for Nakama authentication. Empty falls back to
## [method OS.get_unique_id]. Set distinct ids per instance for local
## two-client testing with fake instance ids.
@export var device_id: String = ""


## Installs the Nakama iframe-proxy seam so the relay client and socket route
## through Discord's proxy when embedded.
##
## Sets [member NakamaWrapper.proxy_base_resolver] to a resolver that rewrites the
## Nakama base to [code]<client_id>.discordsays.com/.proxy/<proxy_prefix>[/code].
## The resolver looks the [DiscordActivityService] up lazily through the tree at
## connect time, so [param service] and [param tree] are only the lifecycle
## trigger and may be [code]null[/code] when wiring the seam by hand (the
## standalone smoke scene does this). Off the embed path the resolver returns
## [code]""[/code] and the direct connection is untouched.
func bind(_service: DiscordActivityService, _tree: MultiplayerTree) -> void:
	NakamaWrapper.proxy_base_resolver = _resolve_proxy_base


# Resolves the Nakama proxy base for a Discord Activity, the NakamaWrapper seam
# that keeps every discordsays.com string out of the core addon. Returns a
# scheme-less "<client_id>.discordsays.com/.proxy/<proxy_prefix>" so the wrapper
# rewrites the client and socket URIs, or "" to leave a direct connection alone.
# It fires for a config_host already pointed at .discordsays.com (the smoke path,
# no service), or for a registered DiscordActivityService that carries a client_id
# (an embedded session) reachable from host_node. A service with no client_id
# (a headless or test context) keeps the direct connection.
func _resolve_proxy_base(host_node: Node, config_host: String) -> String:
	if config_host.ends_with(".discordsays.com"):
		return "%s.discordsays.com/.proxy/%s" % [config_host.split(".")[0], proxy_prefix]
	if host_node == null:
		return ""
	var mt := MultiplayerTree.resolve(host_node)
	if mt == null:
		return ""
	var svc := mt.get_service(DiscordActivityService) as DiscordActivityService
	if svc == null or svc.client_id.is_empty():
		return ""
	return "%s.discordsays.com/.proxy/%s" % [svc.client_id, proxy_prefix]


func resolve(instance_id: String, tree: MultiplayerTree) -> JoinTarget:
	if instance_id.is_empty():
		Netw.dbg.warn("NakamaDiscordRendezvous: empty instance_id.")
		return null
	var wrapper := await _ready_wrapper(tree)
	if wrapper == null:
		return null

	# Pick the freshest record across all owners. Nakama scopes storage per
	# (collection, key, owner), so several hosts can hold the same key. The
	# freshest timestamped one is the most recently claimed match. A dead record
	# is handled by a failed join falling back to host (see
	# DiscordActivityService.connect_activity), not by a liveness gate, because
	# list_matches does not reliably list relay matches.
	var mid := await _freshest_match(wrapper, instance_id)
	if not mid.is_empty():
		Netw.dbg.info(
			"NakamaDiscordRendezvous: joining recorded match %s for instance %s.",
			[mid, instance_id],
		)
		return _target_for(mid)

	Netw.dbg.info(
		"NakamaDiscordRendezvous: no record for instance %s; hosting.",
		[instance_id],
	)
	return _target_for("")


func commit_host(instance_id: String, tree: MultiplayerTree) -> JoinTarget:
	var dir := tree.get_service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	if dir == null:
		return null
	var my_match := dir.get_join_address()
	if my_match.is_empty():
		return null
	var wrapper := await _ready_wrapper(tree)
	if wrapper == null:
		return null

	var wrote := await wrapper.write_public_storage(
		collection,
		instance_id,
		{ "match_id": my_match, "ts": Time.get_unix_time_from_system() },
	)
	Netw.dbg.debug(
		"NakamaDiscordRendezvous: commit_host wrote instance=%s match=%s ok=%s",
		[instance_id, my_match, wrote],
	)
	# Our write is now the freshest record, so a clean host keeps hosting. Only a
	# host that committed after us (a near-simultaneous launch) is fresher, in
	# which case we defer to it so both ends converge on one match.
	var winner := await _freshest_match(wrapper, instance_id)
	if winner.is_empty() or winner == my_match:
		return null
	Netw.dbg.info(
		"NakamaDiscordRendezvous: lost host race for instance %s; joining %s.",
		[instance_id, winner],
	)
	return _target_for(winner)


# Authenticates the shared Nakama session with this rendezvous's relay config
# and returns a wrapper bound to it, or null when Nakama is unavailable. The
# session is single-flight, so the first connect (here or in the directory) wins
# the host config; the service keeps both in sync.
func _ready_wrapper(tree: MultiplayerTree) -> NakamaWrapper:
	if not NakamaWrapper.is_addon_present():
		Netw.dbg.warn("NakamaDiscordRendezvous: Nakama addon not present.")
		return null
	var session := tree.get_nakama_session()
	if session == null:
		return null
	session.configure({
		"server_key": server_key,
		"host": host,
		"port": port,
		"use_ssl": use_ssl,
		"device_id": _normalized_device_id(),
	})
	var auth := await session.connect_async()
	if not auth.ok:
		Netw.dbg.error(
			"NakamaDiscordRendezvous: session auth failed: %s", [auth.error],
		)
		return null
	var wrapper := NakamaWrapper.new()
	wrapper.use_session(session)
	return wrapper


# Returns the match id of the freshest timestamped record for instance_id across
# all owners, or an empty string when none exist. Records without a positive
# timestamp are ignored, which drops legacy and partially written entries.
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


# Normalizes device_id to Nakama's 10-128 byte requirement. An empty id is left
# empty so the session falls back to OS.get_unique_id(). Discord user ids
# (~18-digit snowflakes) pass through untouched; short ids like a fake "alice" are
# given a stable prefix so they stay distinct and reconnect as the same user.
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
