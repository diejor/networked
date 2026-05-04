## Typed wrapper for a single network clock pong measurement.
##
## Created from the raw [Dictionary] emitted by [signal NetworkClock.pong_received]
## and serialized via [method to_dict] before crossing the [EngineDebugger]
## boundary.
class_name NetClockSample
extends RefCounted

var tree_name: String
var username: String
var rtt_raw: float
var rtt_avg: float
var rtt_jitter: float
var diff: int
var tick: int
var display_offset: int
var recommended_display_offset: int
var is_stable: bool
var is_synchronized: bool


## Creates a [NetClockSample] from a [Dictionary] and tree name.
static func from_dict(d: Dictionary, p_tree_name: String) -> NetClockSample:
	var s := NetClockSample.new()
	s.tree_name = p_tree_name
	s.username = d.get("username", "")
	s.rtt_raw = d.get("rtt_raw", 0.0)
	s.rtt_avg = d.get("rtt_avg", 0.0)
	s.rtt_jitter = d.get("rtt_jitter", 0.0)
	s.diff = d.get("diff", 0)
	s.tick = d.get("tick", 0)
	s.display_offset = d.get("display_offset", 0)
	s.recommended_display_offset = d.get("recommended_display_offset", 0)
	s.is_stable = d.get("is_stable", false)
	s.is_synchronized = d.get("is_synchronized", false)
	return s


## Serializes this sample into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"tree_name": tree_name,
		"username": username,
		"rtt_raw": rtt_raw,
		"rtt_avg": rtt_avg,
		"rtt_jitter": rtt_jitter,
		"diff": diff,
		"tick": tick,
		"display_offset": display_offset,
		"recommended_display_offset": recommended_display_offset,
		"is_stable": is_stable,
		"is_synchronized": is_synchronized,
	}
