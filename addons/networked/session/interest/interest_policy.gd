## Stateless verdict resolver for [NetwInterestLayer].
##
## Given a [enum NetwInterestLayer.Policy] and a viewer set, returns
## the per-peer visibility verdict. Every gate in the system - layer
## verdicts, per-entity filters, anchor admission - routes through
## this function so disagreement is a single-source bug.
##
## [method explain] returns a human-readable reason and is the first
## tool to reach for when a peer is visible or hidden when it should
## not be.
##
## [codeblock]
##     var k := NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS
##     InterestPolicy.verdict(k, viewers, peer_id)
##     print(InterestPolicy.explain(k, viewers, peer_id))
## [/codeblock]
class_name InterestPolicy
extends RefCounted

## Returns the per-peer visibility verdict.
##
## [param kind] is one of [enum NetwInterestLayer.Policy].
## [param viewers] is the layer's viewer set. Server peer
## ([constant MultiplayerPeer.TARGET_PEER_SERVER]) is always admitted;
## peer id [code]0[/code] is always rejected.
static func verdict(
		kind: NetwInterestLayer.Policy,
		viewers: Dictionary,
		peer_id: int,
) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	if peer_id == 0:
		return false
	match kind:
		NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
			return viewers.has(peer_id)
		NetwInterestLayer.Policy.HIDE_FROM_INSIDERS:
			return not viewers.has(peer_id)
	return true


## Returns a one-line description of why [param peer_id] resolved the
## way it did. Intended for log lines and debugger inspection.
static func explain(
		kind: NetwInterestLayer.Policy,
		viewers: Dictionary,
		peer_id: int,
) -> String:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return "ADMIT peer=SERVER (always admitted)"
	if peer_id == 0:
		return "REJECT peer=0 (no peer context)"
	var in_viewers := viewers.has(peer_id)
	var label := _kind_label(kind)
	match kind:
		NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
			if in_viewers:
				return "ADMIT peer=%d in viewers under %s" \
						% [peer_id, label]
			return "REJECT peer=%d not in viewers under %s" \
					% [peer_id, label]
		NetwInterestLayer.Policy.HIDE_FROM_INSIDERS:
			if in_viewers:
				return "REJECT peer=%d in viewers under %s" \
						% [peer_id, label]
			return "ADMIT peer=%d not in viewers under %s" \
					% [peer_id, label]
	return "ADMIT peer=%d (unknown kind=%d defaults true)" \
			% [peer_id, kind]


static func _kind_label(kind: NetwInterestLayer.Policy) -> String:
	match kind:
		NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
			return "HIDE_FROM_OUTSIDERS"
		NetwInterestLayer.Policy.HIDE_FROM_INSIDERS:
			return "HIDE_FROM_INSIDERS"
	return "kind=%d" % kind
