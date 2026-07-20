## A stateless factory for one family of [MultiplayerPeer]s.
##
## A [NetwTransport] recognizes a [NetwConnectTarget] or [NetwHostConfig] by its
## scheme, then produces the peer. It holds no per-connection state, so one
## instance is registered process-wide on [NetwConnector] and shared safely
## across every session, including the debugger's cloned sessions. All live
## state a connection accumulates lives on the [NetwConnectAttempt] that builds
## it and the [NetwPeerView] that owns it afterward.
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
	## [method NetwConnector.join_or_host] may host directly instead of probing.
	LISTEN_FALLBACK = 2,
}

## Seconds a probe read waits for the same-port [code]NPRB[/code] reply before
## timing out.
const PROBE_TIMEOUT := 2.0


## Returns the canonical scheme this transport joins and hosts, such as
## [code]&"enet"[/code]. Browse UI reads it to stamp the [member
## NetwConnectTarget.scheme] and [member NetwHostConfig.scheme] a picked
## transport recognizes. Empty on the base.
func scheme() -> StringName:
	return &""


## Returns [code]true[/code] when this transport recognizes [param target].
@warning_ignore("unused_parameter")
func _can_join(target: NetwConnectTarget) -> bool:
	return false


## Returns [code]true[/code] when this transport recognizes [param config].
@warning_ignore("unused_parameter")
func _can_host(config: NetwHostConfig) -> bool:
	return false


## Returns [code]true[/code] when [param peer] belongs to this transport.
##
## [NetwConnector] uses this to resolve the [NetwPeerView] for a peer it did not
## build itself, such as one assigned directly by user code.
@warning_ignore("unused_parameter")
func _can_view(peer: MultiplayerPeer) -> bool:
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
