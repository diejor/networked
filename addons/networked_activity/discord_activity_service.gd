## Puts every player of a Discord activity in one room, served by the first
## player to arrive.
##
## Inside an activity it posts the room through [method NetwConnection.invite]
## when it enters, and outside one it does not register.
## [codeblock]
## # NakamaAccount       host "nakama.example.com", port 443, use_ssl
## # NakamaLobbyDirectory
## # DiscordActivityService
## #     application_id  1234567890
## #     url_mappings    { "/nakama": "nakama.example.com" }
## # ConnectBrowser      joins the room
##
## func _ready() -> void:
##     session.connection.invited.connect(_on_invited)
##
## func _on_invited(endpoint: NetwEndpoint) -> void:
##     await session.connection.join(endpoint).settled
## [/codeblock]
## [member sdk] is Discord's Embedded SDK, for presence, participants, layout
## and the rest.
class_name DiscordActivityService
extends NetwService

const _SCOPES := ["identify"]
const _ISSUER := "https://%d.discordsays.com"
const _URL_PATCH := """
(function (rules) {
	if (window.__netwUrlMappings) {
		return;
	}
	window.__netwUrlMappings = true;
	const remap = (url) => {
		let href;
		try {
			href = new URL(String(url), location.href).href;
		} catch (e) {
			return url;
		}
		for (const [from, to] of rules) {
			if (href === from || href.startsWith(from + "/")
					|| href.startsWith(from + "?")) {
				return to + href.slice(from.length);
			}
		}
		return url;
	};
	const pageFetch = window.fetch;
	window.fetch = (input, init) => input instanceof Request
		? pageFetch(new Request(remap(input.url), input), init)
		: pageFetch(remap(input), init);
	const PageSocket = window.WebSocket;
	window.WebSocket = class extends PageSocket {
		constructor(url, protocols) {
			super(remap(url), protocols);
		}
	};
	const pageOpen = XMLHttpRequest.prototype.open;
	XMLHttpRequest.prototype.open = function (method, url, ...rest) {
		return pageOpen.call(this, method, remap(url), ...rest);
	};
})(%s);
"""

## The Discord application ID of the activity.
@export var application_id: int

## Where the token Worker swaps Discord's authorization code, for
## [method sign_in] and for the token a host checks with
## [method DiscordIdToken.signed_by]. A path starting [code]/[/code] is one of
## [member url_mappings].
@export var token_url := "/token"

## The activity's URL mappings from the Discord Developer Portal, each a
## prefix and the host it reaches. A request or WebSocket the page opens to a
## mapped host goes through Discord's proxy, as [method proxied_url] shows.
@export var url_mappings: Dictionary[String, String]

## The transport the room is joined through. Empty posts no invite, for a game
## that finds its server another way.
@export var room_transport: StringName = &"NakamaRelayPeer"

## The room's address. [code]{application_id}[/code] and
## [code]{instance_id}[/code] are filled in.
@export var room_address := "room:discord:{application_id}:{instance_id}"

## Seconds a join waits for the player to authorize the activity and for
## [member token_url] to answer. Inside an activity it raises
## [member NetwMultiplayer.auth_timeout] to this, on the server and on every
## player.
@export_range(1.0, 300.0, 1.0, "suffix:s") var proof_timeout := 60.0

## The activity instance every player in it shares, empty outside an activity.
## It is read-only.
var instance_id: String

## The player's Discord user ID, [code]0[/code] before [method sign_in]. It is
## read-only.
var user_id: int

## The player's Discord display name, empty before [method sign_in]. It is
## read-only.
var display_name: String

## Discord's Embedded SDK, [code]null[/code] outside an activity. It is
## read-only.
var sdk: DiscordSDK

var _signing_in := NetwPromise.resolved(0)


func _init() -> void:
	if OS.has_feature("web"):
		instance_id = str(
			JavaScriptBridge.eval(
				"new URLSearchParams(location.search).get('instance_id') ?? ''",
				true,
			),
		)


func _should_register() -> bool:
	return not instance_id.is_empty()


func _service_entered(session: NetwSession) -> void:
	if OS.has_feature("web"):
		var rules := []
		for prefix: String in url_mappings:
			for scheme: String in ["https", "wss"]:
				var from := "%s://%s" % [
					scheme,
					url_mappings[prefix].trim_suffix("/"),
				]
				rules.append([from, proxied_url(from)])
		JavaScriptBridge.eval(_URL_PATCH % JSON.stringify(rules), true)
		sdk = DiscordSDK.new()
		sdk.name = &"DiscordSDK"
		add_child.call_deferred(sdk)
		sdk.init.call_deferred(str(application_id))

	NetwIdToken.provide(_ISSUER % application_id, _fetch_token)
	var api := multiplayer as NetwMultiplayer
	api.auth_timeout = maxf(api.auth_timeout, proof_timeout)
	session.connection.invite.call_deferred(
		room_transport,
		room_address.format(
			{ "application_id": application_id, "instance_id": instance_id },
		),
	)


func _service_exiting(_session: NetwSession) -> void:
	NetwIdToken.provide(_ISSUER % application_id, Callable())


## Returns the address the activity page reaches [param url] at through
## [member url_mappings]. A path starting [code]/[/code] is a mapped prefix.
## A URL no mapping covers comes back unchanged.
## [codeblock]
## discord.url_mappings = { "/nakama": "nakama.example.com" }
## discord.proxied_url("wss://nakama.example.com:443/ws")
## # wss://1234567890.discordsays.com/.proxy/nakama/ws
## [/codeblock]
func proxied_url(url: String) -> String:
	var page := "%d.discordsays.com/.proxy" % application_id
	if url.begins_with("/"):
		return "https://%s%s" % [page, url]

	var scheme := url.get_slice("://", 0)
	var rest := url.get_slice("://", 1)
	var authority := rest.get_slice("/", 0).get_slice("?", 0)
	rest = authority.trim_suffix(":443") + rest.trim_prefix(authority)
	for prefix: String in url_mappings:
		var target := url_mappings[prefix].trim_suffix("/")
		var tail := rest.trim_prefix(target)
		if scheme in ["https", "wss"] and rest.begins_with(target) \
				and (tail.is_empty() or tail[0] in ["/", "?"]):
			return "%s://%s/%s%s" % [
				scheme,
				page,
				prefix.trim_prefix("/").trim_suffix("/"),
				tail,
			]
	return url


## Asks the player to authorize the activity, swaps the code at
## [member token_url], and resolves with [member user_id].
func sign_in() -> NetwPromise:
	if sdk == null:
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"DiscordActivityService signs in only inside a Discord activity. "
			+ "Launch the game from Discord.",
		)
	if user_id != 0:
		return NetwPromise.resolved(user_id)
	if not _signing_in.is_settled:
		return _signing_in

	_signing_in = NetwPromise.new()
	var signing_in := _signing_in
	_grant("").then(
		func(grant: Dictionary) -> void:
			var proof := await sdk.command_authenticate(
				String(grant.get("access_token", "")),
			)
			if proof.user == null:
				signing_in.reject(
					ERR_UNAUTHORIZED,
					"Discord refused the access token from token_url.",
				)
				return
			user_id = int(proof.user.id)
			display_name = proof.user.global_name
			if display_name.is_empty():
				display_name = proof.user.username
			signing_in.resolve(user_id)
	).catch_error(signing_in.reject)
	return signing_in


## Opens Discord's dialog for inviting friends to this activity.
func open_invite_dialog() -> void:
	if sdk == null:
		push_warning(
			"DiscordActivityService has no Discord SDK outside an activity, "
			+ "so no invite dialog opened.",
		)
		return
	await sdk.ready()
	sdk.command_open_invite_dialog()


func _grant(nonce: String) -> NetwPromise:
	if sdk == null:
		return NetwPromise.rejected(
			ERR_UNAVAILABLE,
			"DiscordActivityService signs in only inside a Discord activity. "
			+ "Launch the game from Discord.",
		)

	var granting := NetwPromise.new()
	var swap := HTTPRequest.new()
	add_child(swap)
	(func() -> void:
		await sdk.ready()
		var authorized := await sdk.command_authorize("code", _SCOPES, "")
		if authorized.code.is_empty():
			swap.queue_free()
			granting.reject(
				ERR_UNAUTHORIZED,
				"The player did not authorize the activity.",
			)
			return

		var form := "grant_type=authorization_code&code=%s" \
				% authorized.code.uri_encode()
		if not nonce.is_empty():
			form += "&nonce=%s&instance_id=%s" \
					% [nonce.uri_encode(), instance_id.uri_encode()]
		swap.request(
			proxied_url(token_url),
			["Content-Type: application/x-www-form-urlencoded"],
			HTTPClient.METHOD_POST,
			form,
		)
		var answer: Array = await swap.request_completed
		swap.queue_free()
		var grant: Variant = JSON.parse_string(
			answer[3].get_string_from_utf8(),
		)
		if answer[1] != 200 or not grant is Dictionary:
			granting.reject(
				ERR_UNAUTHORIZED,
				"%s answered %d. Check the token Worker and its URL mapping."
				% [token_url, answer[1]],
			)
			return
		granting.resolve(grant)
	).call()
	return granting


func _fetch_token(nonce: String) -> NetwPromise:
	var fetched := NetwPromise.new()
	_grant(nonce).then(
		func(grant: Dictionary) -> void:
			match grant:
				{ "id_token": var token, .. } when token is String:
					fetched.resolve(token)
				_:
					fetched.reject(
						ERR_UNAUTHORIZED,
						(
								"%s answered with no id_token. Set SIGNING_KEY "
								+ "on the token Worker."
						) % token_url,
					)
	).catch_error(fetched.reject)
	return fetched
