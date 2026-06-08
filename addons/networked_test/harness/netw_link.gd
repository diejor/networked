## Fluent local loopback link control for harness tests.
##
## [method latency_ms], [method loss], and [method profile] update one
## inbound peer link and return this handle for chaining.
##
## [codeblock]
## game.link(client, host).latency_ms(200).loss(0.05)
## game.link(client).profile(NetwLink.Profile.POOR_3G).jitter_ms(40)
## [/codeblock]
##
## [method exact] exposes [LocalLoopbackSession.LinkPlan] poll counts for
## golden tests. Use [method clear] to restore a perfect link.
class_name NetwLink
extends RefCounted

enum Profile {
	PERFECT,
	WIFI,
	MOBILE_4G,
	POOR_3G,
	SATELLITE,
}

var _session: LocalLoopbackSession
var _peer: LocalMultiplayerPeer
var _sender_id: int = 0


func _init(
		session: LocalLoopbackSession,
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> void:
	_session = session
	_peer = peer
	_sender_id = sender_id


## Sets baseline latency in milliseconds.
func latency_ms(ms: float) -> NetwLink:
	var conditions := _conditions()
	conditions.latency_ms = ms
	return _apply_conditions(conditions)


## Sets jitter in milliseconds for unreliable traffic.
func jitter_ms(ms: float) -> NetwLink:
	var conditions := _conditions()
	conditions.jitter_ms = ms
	return _apply_conditions(conditions)


## Sets packet loss probability.
func loss(probability: float) -> NetwLink:
	var conditions := _conditions()
	conditions.packet_loss = probability
	return _apply_conditions(conditions)


## Sets unreliable reorder probability.
func reorder(probability: float) -> NetwLink:
	var conditions := _conditions()
	conditions.reorder = probability
	return _apply_conditions(conditions)


## Sets unreliable duplicate probability.
func duplicate(probability: float) -> NetwLink:
	var conditions := _conditions()
	conditions.duplicate = probability
	return _apply_conditions(conditions)


## Sets unreliable throttle probability and freeze length.
func throttle(probability: float, ms: float = 0.0) -> NetwLink:
	var conditions := _conditions()
	conditions.throttle = probability
	conditions.throttle_ms = ms
	return _apply_conditions(conditions)


## Resets the link to [param profile].
func profile(profile_id: Profile) -> NetwLink:
	return _apply_conditions(_profile_conditions(profile_id))


## Returns the exact poll-level authoring surface.
func exact() -> NetwLinkExact:
	var plan := _session.get_link_plan(_peer, _sender_id)
	if not plan:
		plan = LocalLoopbackSession.LinkPlan.new()
	return NetwLinkExact.new(_session, _peer, _sender_id, plan)


## Clears this peer and sender link.
func clear() -> void:
	_session.clear_link_conditions(_peer, _sender_id)


func _conditions() -> LocalLoopbackSession.LinkConditions:
	var conditions := _session.get_link_conditions(_peer, _sender_id)
	if conditions:
		return conditions.clone()
	return LocalLoopbackSession.LinkConditions.perfect()


func _apply_conditions(
		conditions: LocalLoopbackSession.LinkConditions,
) -> NetwLink:
	_session.set_link_conditions(_peer, conditions, _sender_id)
	return self


func _profile_conditions(
		profile_id: Profile,
) -> LocalLoopbackSession.LinkConditions:
	match profile_id:
		Profile.WIFI:
			return LocalLoopbackSession.LinkConditions.wifi()
		Profile.MOBILE_4G:
			return LocalLoopbackSession.LinkConditions.mobile_4g()
		Profile.POOR_3G:
			return LocalLoopbackSession.LinkConditions.poor_3g()
		Profile.SATELLITE:
			return LocalLoopbackSession.LinkConditions.satellite()
		_:
			return LocalLoopbackSession.LinkConditions.perfect()


## Poll-level link control for deterministic low-level tests.
##
## Each setter updates the installed [LocalLoopbackSession.LinkPlan] and returns
## this handle for chaining.
class NetwLinkExact:
	extends RefCounted

	var _session: LocalLoopbackSession
	var _peer: LocalMultiplayerPeer
	var _sender_id: int = 0
	var _plan: LocalLoopbackSession.LinkPlan


	func _init(
			session: LocalLoopbackSession,
			peer: LocalMultiplayerPeer,
			sender_id: int,
			plan: LocalLoopbackSession.LinkPlan,
	) -> void:
		_session = session
		_peer = peer
		_sender_id = sender_id
		_plan = plan.clone()


	## Sets baseline delay in session polls.
	func delay_polls(n: int) -> NetwLinkExact:
		_plan.delay_polls = n
		return _apply()


	## Sets unreliable jitter in session polls.
	func jitter_polls(n: int) -> NetwLinkExact:
		_plan.jitter_polls = n
		return _apply()


	## Sets packet loss probability.
	func loss_prob(probability: float) -> NetwLinkExact:
		_plan.loss_probability = probability
		return _apply()


	## Sets unreliable reorder probability.
	func reorder_prob(probability: float) -> NetwLinkExact:
		_plan.reorder_probability = probability
		return _apply()


	## Sets unreliable duplicate probability.
	func duplicate_prob(probability: float) -> NetwLinkExact:
		_plan.duplicate_probability = probability
		return _apply()


	## Sets unreliable throttle probability and freeze polls.
	func throttle_prob(probability: float, polls: int = 0) -> NetwLinkExact:
		_plan.throttle_probability = probability
		_plan.throttle_polls = polls
		return _apply()


	## Sets reliable retransmit delay in session polls.
	func retransmit_polls(n: int) -> NetwLinkExact:
		_plan.retransmit_polls = n
		return _apply()


	## Sets the deterministic random seed.
	func seed(n: int) -> NetwLinkExact:
		_plan.seed = n
		return _apply()


	func _apply() -> NetwLinkExact:
		_session.set_link_plan(_peer, _plan, _sender_id)
		return self
