#pragma once

namespace netw_test::gdsrc {

constexpr const char *TRANSPORT_THAT_HOLDS = R"(extends NetwTransport

func _peer_class() -> StringName:
	return &"HeldMultiplayerPeer"

func _display_name() -> String:
	return "Held"

func _timeout_hint() -> float:
	return -1.0

func _make_peer(
	_ticket: RID, _mode: int, _address: String, _settings: Dictionary
) -> void:
	pass
)";

constexpr const char *TRANSPORT_THAT_SETTLES = R"(extends NetwTransport

func _peer_class() -> StringName:
	return &"PromptMultiplayerPeer"

func _display_name() -> String:
	return "Prompt"

func _make_peer(
	ticket: RID, _mode: int, _address: String, _settings: Dictionary
) -> void:
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(9)
	deliver(ticket, peer)
)";

constexpr const char *TRANSPORT_CLAIMING_THE_SAME_CLASS
    = R"(extends NetwTransport

func _peer_class() -> StringName:
	return &"HeldMultiplayerPeer"

func _display_name() -> String:
	return "Rival"

func _make_peer(
	_ticket: RID, _mode: int, _address: String, _settings: Dictionary
) -> void:
	pass
)";

constexpr const char *GATE_THAT_CALLS_THROUGH = R"(extends NetwMultiplayer

var sync_calls := 0

func _sync_admit_frame(
		sender: int,
		route: int,
		comp: int,
		channel: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
) -> Error:
	sync_calls += 1
	return sync_admit_frame_default(
		sender, route, comp, channel, flags, tick, payload,
	)
)";

constexpr const char *GATE_THAT_REFUSES_WHAT_STOCK_ADMITS
    = R"(extends NetwMultiplayer

var sync_calls := 0

func _sync_admit_frame(
		sender: int,
		route: int,
		comp: int,
		channel: int,
		flags: int,
		tick: int,
		payload: PackedByteArray,
) -> Error:
	sync_calls += 1
	var verdict := sync_admit_frame_default(
		sender, route, comp, channel, flags, tick, payload,
	)
	return ERR_UNAUTHORIZED if verdict == OK else verdict
)";

constexpr const char *NATIVE_CLASS_HANDLE = R"(extends RefCounted

static func directory() -> Variant:
	return LobbyDirectory
)";

constexpr const char *SEAM_ANSWER_SHAPES = R"(extends RefCounted

signal never_settles

func awaiting() -> Variant:
	await never_settles
	return ERR_UNAUTHORIZED

func mistyped() -> Variant:
	return "not an error"

func plain() -> Variant:
	return ERR_UNAUTHORIZED
)";

constexpr const char *JOIN_HANDLERS = R"(extends RefCounted

signal released(value: Variant)

var calls := 0

func immediate(_participant: NetwParticipant, scene: Node) -> Node:
	calls += 1
	return scene

func suspended(_participant: NetwParticipant, _scene: Node) -> Node:
	calls += 1
	return await released
)";

} // namespace netw_test::gdsrc
