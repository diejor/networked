## Engine-owned sync progress books keyed only by route, peer, and channel.
class_name NetwSyncProgress
extends RefCounted

var _receive_sequences: Dictionary = { }
var _pending_masked: Dictionary = { }


## Accepts a first or fresher unreliable datagram for one stream.
func accept_unreliable(
		sender: int,
		route: int,
		channel: int,
		sequence: int,
) -> bool:
	var by_sender: Dictionary = _receive_sequences.get_or_add(route, { })
	var by_channel: Dictionary = by_sender.get_or_add(sender, { })
	var accepted := true
	if by_channel.has(channel):
		var last := int(by_channel[channel])
		accepted = sequence != last \
				and ((sequence - last) & 0xFFFF) < 32768
	if accepted:
		by_channel[channel] = sequence
	return accepted


## Stages one masked row until its carrier sequence is assigned.
func stage_masked(
		peer: int,
		route: int,
		key: StringName,
		record: int,
		row: Dictionary,
) -> void:
	var pending: Array = _pending_masked.get_or_add(peer, [])
	pending.append(
		{
			&"route": route,
			&"key": key,
			&"record": record,
			&"row": row,
		},
	)


## Takes every masked row awaiting [param peer]'s next carrier sequence.
func take_masked(peer: int) -> Array:
	var pending: Array = _pending_masked.get(peer, [])
	_pending_masked.erase(peer)
	return pending


## Drops every book owned by one route.
func clear_route(route: int) -> void:
	_receive_sequences.erase(route)
	for peer: int in _pending_masked.keys().duplicate():
		var kept: Array = []
		for entry: Dictionary in _pending_masked[peer]:
			if int(entry[&"route"]) != route:
				kept.append(entry)
		if kept.is_empty():
			_pending_masked.erase(peer)
		else:
			_pending_masked[peer] = kept


## Drops every book owned by one peer.
func clear_peer(peer: int) -> void:
	for route: int in _receive_sequences:
		_receive_sequences[route].erase(peer)
	_pending_masked.erase(peer)


## Drops all progress state.
func clear() -> void:
	_receive_sequences.clear()
	_pending_masked.clear()


## Returns value-only book sizes for laws and diagnostics.
func stats() -> Dictionary:
	var streams := 0
	for by_sender: Dictionary in _receive_sequences.values():
		for by_channel: Dictionary in by_sender.values():
			streams += by_channel.size()
	var pending := 0
	for rows: Array in _pending_masked.values():
		pending += rows.size()
	return { &"streams": streams, &"pending_masked": pending }
