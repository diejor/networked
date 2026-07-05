## Opaque cross-peer correlation handle for the Networked debugger.
##
## Links a client-side span to the server-side span it triggers when no shared
## process can hold a live [NetwSpan] reference across the network boundary.
## The production RPC that opts in forwards this handle verbatim and never
## reads it. Only the tier-probe on each side stamps its own span with
## [member id] so the editor can match them post hoc.
## [codeblock]
## var corr := Netw.dbg.correlate(self)
## my_rpc.rpc_id(MultiplayerPeer.TARGET_PEER_SERVER, ..., corr.to_dict())
## [/codeblock]
class_name NetwCorrelation
extends RefCounted

## Empty when the debugger is inactive, so a forwarded correlation is a
## zero-cost empty dict in release builds.
var id: StringName = &""


## Serializes this handle for RPC transport.
func to_dict() -> Dictionary:
	if id.is_empty():
		return { }
	return { "corr_id": str(id) }


## Reconstructs a handle from an RPC-transported [Dictionary]. Returns an
## empty handle for an empty or malformed [param d].
static func from_dict(d: Dictionary) -> NetwCorrelation:
	var corr := NetwCorrelation.new()
	if d.has("corr_id"):
		corr.id = StringName(d["corr_id"])
	return corr
