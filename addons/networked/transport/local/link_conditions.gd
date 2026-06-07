## Deterministic local loopback link conditions.
##
## [LocalLoopbackSession] applies these conditions to one peer's inbound
## queue. To delay server to client traffic, condition the client peer. To
## delay client to server traffic, condition the server peer.
##
## [codeblock]
## var conditions := NetwLinkConditions.new()
## conditions.delay_polls = 6
## session.set_link_conditions(server, conditions)
## [/codeblock]
##
## [member delay_polls] counts session polls. A session poll usually maps to
## one frame, not one network tick.
class_name NetwLinkConditions
extends RefCounted

var delay_polls: int = 0
var jitter_polls: int = 0
var loss_probability: float = 0.0
var reorder_probability: float = 0.0
var drop_reliable: bool = false
var seed: int = 0

var _rng := RandomNumberGenerator.new()


func _init(p_seed: int = 0) -> void:
	seed = p_seed
	reset_rng()


## Resets the deterministic random stream to [member seed].
func reset_rng() -> void:
	_rng.seed = seed


## Returns a copy with the same settings and a reset random stream.
func clone() -> NetwLinkConditions:
	var copy := NetwLinkConditions.new(seed)
	copy.delay_polls = delay_polls
	copy.jitter_polls = jitter_polls
	copy.loss_probability = loss_probability
	copy.reorder_probability = reorder_probability
	copy.drop_reliable = drop_reliable
	return copy


func _roll(probability: float) -> bool:
	if probability <= 0.0:
		return false
	if probability >= 1.0:
		return true
	return _rng.randf() < probability


func _draw_jitter() -> int:
	if jitter_polls <= 0:
		return 0
	return _rng.randi_range(0, jitter_polls)
