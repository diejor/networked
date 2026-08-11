## A stateless factory for one family of [MultiplayerPeer]s.
##
## A [NetwTransport] recognizes a [NetwConnectTarget] or [NetwHostConfig] by its
## scheme, then produces the peer. It holds no per-connection state, so one
## instance is [method register]ed process-wide and shared safely across every
## session, including the debugger's cloned sessions. All live state a
## connection accumulates lives on the [NetwConnectAttempt] that builds it and
## the [NetwPeerView] that owns it afterward.
## [codeblock]
## ┌──────────────┐  _join/_host   ┌───────────────────┐  _make_view  ┌──────────────┐
## │ NetwTransport│ ─────────────▶ │ NetwConnectAttempt│ ───────────▶ │ NetwPeerView │
## │  (stateless) │                │ (owns build state)│              │ (owns peer   │
## └──────────────┘                └───────────────────┘              │  state)      │
## [/codeblock]
@abstract
class_name NetwTransport
extends RefCounted

## Optional capability flags returned by [method _capabilities].
enum Capability {
	## The transport answers probes through [method _make_probe_peer].
	SUPPORTS_PROBE = 1,
	## [method NetwConnector.join_or_host] may host directly instead of
	## probing.
	LISTEN_FALLBACK = 2,
}

## Seconds a probe read waits for the same-port [code]NPRB[/code] reply before
## timing out.
const PROBE_TIMEOUT := 2.0

# Process-global, priority-ordered registry. Transports are stateless, so global
# registration is safe across the debugger's cloned sessions.
static var _registry: Array[NetwTransport] = []


## Registers [param transport] in the process-global registry.
##
## The first transport whose [method _can_join] or [method _can_host] answers
## wins, so [param at_front] gives one priority over earlier registrations. A
## session narrows this list for itself through
## [member NetwConnector.transports].
static func register(transport: NetwTransport, at_front := false) -> void:
	if transport in _registry:
		return
	if at_front:
		_registry.push_front(transport)
	else:
		_registry.push_back(transport)


## Removes [param transport] from the process-global registry.
static func unregister(transport: NetwTransport) -> void:
	_registry.erase(transport)


## Returns the process-global registry in priority order.
static func registered() -> Array[NetwTransport]:
	return _registry


## Returns [param source] typed by the registered transport that claims
## [param scheme], or [code]null[/code] when this build registers none.
##
## The one door from the persistence form back to the authoring form. A saved
## [NetwConnectTarget] and a lobby directory row arrive as dictionaries for
## schemes a given export may not carry, so the transport that recognizes the
## scheme is the only thing that can type the row. See
## [method _params_from_dict].
static func params_from_dict(
		scheme: StringName,
		source: Dictionary,
) -> NetwTransportParams:
	for transport in _registry:
		if transport._scheme() == scheme:
			return transport._params_from_dict(source)
	return null


# Registers the built-in transports once, when the class first loads, so a bare
# session resolves the shipped schemes with no setup. Transports are stateless,
# so a single shared instance of each is correct. A game registers its own on
# top, or narrows the list per session through NetwConnector.transports. The
# web-only WebRTC loopback is registered by the web entry point, not here, since
# it claims the same scheme as the tracker transport. The directory backed
# transports hold no SDK state and resolve a clean error when their lobby
# directory service is absent, so they register unconditionally too.
static func _static_init() -> void:
	register(ENetTransport.new())
	register(WebSocketTransport.new())
	register(LocalTransport.new())
	register(TrackerWebRTCTransport.new())
	register(NakamaTransport.new())
	register(SteamTransport.new())


## Returns the canonical scheme this transport joins and hosts, such as
## [code]&"enet"[/code]. Browse UI reads it to stamp the [member
## NetwConnectTarget.scheme] a picked transport recognizes. Empty on the base.
func _scheme() -> StringName:
	return &""


## Returns [param source] read back into this transport's [NetwTransportParams].
##
## This is the persistence seam. Authoring is typed, but a saved
## [NetwConnectTarget] and a lobby directory row are dictionaries, so the
## transport that recognizes a row is the one that types it. Returning
## [code]null[/code] leaves the row untyped, which is correct for a scheme this
## build did not register.
@warning_ignore("unused_parameter")
func _params_from_dict(source: Dictionary) -> NetwTransportParams:
	return null


## Returns [code]true[/code] when this transport recognizes [param target].
@warning_ignore("unused_parameter")
func _can_join(target: NetwConnectTarget) -> bool:
	return false


## Returns [code]true[/code] when this transport recognizes [param config].
@warning_ignore("unused_parameter")
func _can_host(config: NetwHostConfig) -> bool:
	return false


## Produces a listening [MultiplayerPeer] for [param config]. May [code]await[/code].
##
## Reports progress and failures through [param attempt]. Returns [code]null[/code]
## to signal a build failure.
@warning_ignore("unused_parameter")
func _host(
		attempt: NetwConnectAttempt,
		config: NetwHostConfig,
) -> MultiplayerPeer:
	return null


## Produces a connecting [MultiplayerPeer] for [param target]. May [code]await[/code].
##
## Reports progress and failures through [param attempt]. Returns [code]null[/code]
## to signal a build failure. The peer may still be handshaking on return.
@warning_ignore("unused_parameter")
func _join(
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
) -> MultiplayerPeer:
	return null


## Builds the [NetwPeerView] for [param peer].
##
## [param attempt] carries build context (room ids, sessions) into the view when
## this transport built the peer, and is [code]null[/code] for a registry-resolved
## view. The base returns [code]null[/code] so [NetwConnector] binds its generic
## view.
@warning_ignore("unused_parameter")
func _make_view(
		peer: MultiplayerPeer,
		attempt: NetwConnectAttempt = null,
) -> NetwPeerView:
	return null


## Builds a throwaway probe peer for [param target], or [code]null[/code] when
## probing is unsupported.
@warning_ignore("unused_parameter")
func _make_probe_peer(target: NetwConnectTarget) -> MultiplayerPeer:
	return null


## Queries [param target] for its [NetwServerInfo] without joining.
##
## The base builds a throwaway peer through [method _make_probe_peer] and reads
## it with [NetwProbeClient], so a transport gets same-port probing for free by
## overriding only [method _make_probe_peer]. A transport with no probe peer
## reports [method NetwProbeResult.unsupported].
func _probe(target: NetwConnectTarget) -> NetwProbeResult:
	var peer := _make_probe_peer(target)
	if peer == null:
		return NetwProbeResult.unsupported()
	return await NetwProbeClient.new().query(peer, PROBE_TIMEOUT, _display_name())


## Returns the display name for this transport.
func _display_name() -> String:
	return "Generic"


## Returns the [NetwAddressHint] for a connect dialog's address field.
func _address_hint() -> NetwAddressHint:
	return NetwAddressHint.new()


## Returns [code]true[/code] when this transport can run on the current platform.
func _is_available() -> bool:
	return true


## Returns [code]true[/code] when this transport can host on the current platform.
func _can_host_here() -> bool:
	return true


## Seconds a connect attempt to [param target] should run before timing out.
##
## Return [code]-1[/code] to declare the transport self-managed, leaving the
## terminal outcome to its own signals plus a safety-net ceiling.
@warning_ignore("unused_parameter")
func _timeout_hint(target: NetwConnectTarget) -> float:
	return 5.0


## Returns the [enum Capability] flags this transport advertises.
func _capabilities() -> int:
	return 0
