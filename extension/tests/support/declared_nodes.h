#pragma once

namespace netw_test::gdsrc {

constexpr const char *STATE_AND_INPUT = R"(extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"rotation").input()
)";

constexpr const char *STATE_ONLY = R"(extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state()
)";

constexpr const char *STATE_MASKED = R"(extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state().masked()
)";

constexpr const char *STATE_VOLATILE_AND_RETAINED = R"(extends Node2D

var stunned: bool = false

func _init() -> void:
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"stunned").state().retained()
)";

constexpr const char *BROADCAST_MASKED_AIM = R"(extends Node2D

var aim_dir: Vector2 = Vector2.ZERO

func _init() -> void:
	Netw.configure_property(self, &"aim_dir").broadcast().masked()
)";

constexpr const char *INTERPOLATED_RPC_AND_SIGNAL = R"(extends Node2D

signal nudged(offset: Vector2)

var rpc_target := Vector2.ZERO
var signal_target := Vector2.ZERO
var last_rpc_arg := Vector2.ZERO

@rpc("any_peer", "call_remote", "reliable") func apply_rpc(offset: Vector2) -> void:
	last_rpc_arg = offset
)";

constexpr const char *NODE_ARG_RPC_SINK = R"(extends Node

var last_node: Node = null
var last_value: int = -1

@rpc("any_peer", "call_remote", "reliable")
func receive_node(node: Node, value: int) -> void:
	last_node = node
	last_value = value
)";

constexpr const char *PROPERTY_PRESENT = R"(extends Node

var replicated_value: int = 7
)";

constexpr const char *PROPERTY_ABSENT = R"(extends Node

var unrelated_value: int = 0
)";

constexpr const char *AUTHORITY_RPC_SINK = R"(extends Node

var value: int = 0

@rpc("authority", "call_remote", "reliable")
func apply_value(next_value: int) -> void:
	value = next_value
)";

constexpr const char *A_PLAIN_SCRIPT = R"(extends Node

var value: int = 0

signal changed(next: int)
)";

} // namespace netw_test::gdsrc
