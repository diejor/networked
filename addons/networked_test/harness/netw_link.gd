## Fluent local loopback link control for harness tests.
##
## [method NetwGameHarness.degrade] applies both directions for one player.
## [method NetwGameHarness.path] exposes one directional path.
## [method latency_ms], [method loss], and [method profile] update the
## selected path and return this handle for chaining.
## [codeblock]
## game.degrade(client).profile(NetwLink.Profile.POOR_3G)
## game.degrade(client).inbound().latency_ms(120)
## game.path(client, host).loss(0.1)
## [/codeblock]
## [codeblock]
## inbound: server to player. This affects what the player sees.
## outbound: player to server. This affects when player actions arrive.
## degrade(): inbound and outbound.
## path(from_runner, to_runner): packets from one runner to another runner.
## [/codeblock]
##
## [method exact] exposes [LocalLoopbackSession.LinkPlan] poll counts for
## tests. Use [method clear] to restore a perfect path.
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
	assert(session != null, "NetwLink: local loopback session is required.")
	assert(peer != null, "NetwLink: local loopback peer is required.")
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


## Returns the effective latency after millisecond values are quantized.
func effective_latency_ms() -> float:
	var plan := _session.get_link_plan(_peer, _sender_id)
	if not plan:
		return 0.0
	return plan.effective_latency_ms()


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


## Fluent control for one player's inbound, outbound, or both paths.
##
## Created by [method NetwGameHarness.degrade]. Direction filters such as
## [method inbound] and [method outbound] return a new handle so the original
## both direction handle remains reusable.
class NetwLinkMulti:
	extends RefCounted

	var _inbound: NetwLink
	var _outbound: NetwLink
	var _links: Array[NetwLink] = []


	func _init(
			inbound_link: NetwLink,
			outbound_link: NetwLink,
	) -> void:
		_inbound = inbound_link
		_outbound = outbound_link
		_links = []
		if _inbound:
			_links.append(_inbound)
		if _outbound:
			_links.append(_outbound)


	## Selects the server to player path.
	func inbound() -> NetwLinkMulti:
		return _from_links(_inbound, null)


	## Selects the player to server path.
	func outbound() -> NetwLinkMulti:
		return _from_links(null, _outbound)


	## Sets baseline latency in milliseconds.
	func latency_ms(ms: float) -> NetwLinkMulti:
		for link in _links:
			link.latency_ms(ms)
		return self


	## Sets jitter in milliseconds for unreliable traffic.
	func jitter_ms(ms: float) -> NetwLinkMulti:
		for link in _links:
			link.jitter_ms(ms)
		return self


	## Sets packet loss probability.
	func loss(probability: float) -> NetwLinkMulti:
		for link in _links:
			link.loss(probability)
		return self


	## Sets unreliable reorder probability.
	func reorder(probability: float) -> NetwLinkMulti:
		for link in _links:
			link.reorder(probability)
		return self


	## Sets unreliable duplicate probability.
	func duplicate(probability: float) -> NetwLinkMulti:
		for link in _links:
			link.duplicate(probability)
		return self


	## Sets unreliable throttle probability and freeze length.
	func throttle(probability: float, ms: float = 0.0) -> NetwLinkMulti:
		for link in _links:
			link.throttle(probability, ms)
		return self


	## Resets every selected path to [param profile_id].
	func profile(profile_id: Profile) -> NetwLinkMulti:
		for link in _links:
			link.profile(profile_id)
		return self


	## Returns the exact poll-level authoring surface for one path.
	func exact() -> NetwLinkExact:
		assert(
			_links.size() == 1,
			"NetwLinkMulti.exact: select inbound() or outbound() first.",
		)
		return _links[0].exact()


	## Clears every selected path.
	func clear() -> void:
		for link in _links:
			link.clear()


	static func _from_links(
			inbound_link: NetwLink,
			outbound_link: NetwLink,
	) -> NetwLinkMulti:
		return NetwLinkMulti.new(inbound_link, outbound_link)


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
