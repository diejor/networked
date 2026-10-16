class_name NetwLink
extends RefCounted

enum Profile {
	PERFECT,
	WIFI,
	MOBILE_4G,
	POOR_3G,
	SATELLITE,
}

var session: LocalLoopbackSession
var peer: LocalMultiplayerPeer
var sender_id: int = 0


func _init(
		session: LocalLoopbackSession,
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> void:
	assert(session != null, "NetwLink: local loopback session is required.")
	assert(peer != null, "NetwLink: local loopback peer is required.")
	self.session = session
	self.peer = peer
	self.sender_id = sender_id


func latency_ms(ms: float) -> NetwLink:
	var conditions := read_conditions()
	conditions.latency_ms = ms
	return apply(conditions)


func jitter_ms(ms: float) -> NetwLink:
	var conditions := read_conditions()
	conditions.jitter_ms = ms
	return apply(conditions)


func loss(probability: float) -> NetwLink:
	var conditions := read_conditions()
	conditions.loss_chance = probability
	return apply(conditions)


func reorder(probability: float) -> NetwLink:
	var conditions := read_conditions()
	conditions.reorder_chance = probability
	return apply(conditions)


func duplicate(probability: float) -> NetwLink:
	var conditions := read_conditions()
	conditions.duplicate_chance = probability
	return apply(conditions)


func stall(probability: float, ms: float = 0.0) -> NetwLink:
	var conditions := read_conditions()
	conditions.stall_chance = probability
	conditions.stall_ms = ms
	return apply(conditions)


func seed(n: int) -> NetwLink:
	var conditions := read_conditions()
	conditions.seed = n
	return apply(conditions)


func profile(profile_id: Profile) -> NetwLink:
	var conditions: NetwLinkConditions
	match profile_id:
		Profile.WIFI:
			conditions = NetwLinkConditions.wifi()
		Profile.MOBILE_4G:
			conditions = NetwLinkConditions.mobile_4g()
		Profile.POOR_3G:
			conditions = NetwLinkConditions.poor_3g()
		Profile.SATELLITE:
			conditions = NetwLinkConditions.satellite()
		_:
			conditions = NetwLinkConditions.new()
	return apply(conditions)


func effective_latency_ms() -> float:
	var conditions := session.get_link_conditions(peer, sender_id)
	if not conditions:
		return 0.0
	return conditions.latency_ms


func clear() -> void:
	session.clear_link_conditions(peer, sender_id)


func read_conditions() -> NetwLinkConditions:
	var conditions := session.get_link_conditions(peer, sender_id)
	if conditions:
		return conditions.duplicate()
	return NetwLinkConditions.new()


func apply(
		conditions: NetwLinkConditions,
) -> NetwLink:
	session.set_link_conditions(peer, conditions, sender_id)
	return self


class NetwLinkMulti:
	extends RefCounted

	var inbound_link: NetwLink
	var outbound_link: NetwLink
	var links: Array[NetwLink] = []


	func _init(
			inbound_link: NetwLink,
			outbound_link: NetwLink,
	) -> void:
		self.inbound_link = inbound_link
		self.outbound_link = outbound_link
		links = []
		if inbound_link:
			links.append(inbound_link)
		if outbound_link:
			links.append(outbound_link)


	func inbound() -> NetwLinkMulti:
		return NetwLinkMulti.new(inbound_link, null)


	func outbound() -> NetwLinkMulti:
		return NetwLinkMulti.new(null, outbound_link)


	func latency_ms(ms: float) -> NetwLinkMulti:
		for link in links:
			link.latency_ms(ms)
		return self


	func jitter_ms(ms: float) -> NetwLinkMulti:
		for link in links:
			link.jitter_ms(ms)
		return self


	func loss(probability: float) -> NetwLinkMulti:
		for link in links:
			link.loss(probability)
		return self


	func reorder(probability: float) -> NetwLinkMulti:
		for link in links:
			link.reorder(probability)
		return self


	func duplicate(probability: float) -> NetwLinkMulti:
		for link in links:
			link.duplicate(probability)
		return self


	func stall(probability: float, ms: float = 0.0) -> NetwLinkMulti:
		for link in links:
			link.stall(probability, ms)
		return self


	func seed(n: int) -> NetwLinkMulti:
		for link in links:
			link.seed(n)
		return self


	func profile(profile_id: Profile) -> NetwLinkMulti:
		for link in links:
			link.profile(profile_id)
		return self


	func clear() -> void:
		for link in links:
			link.clear()
