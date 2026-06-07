## Inbound-RPC sink for link-condition facade tests.
##
## The host (authority) fires [method apply_value] at the client copy. The test
## measures how many ticks the value takes to arrive, so a delayed inbound link
## pushes [member value] to a later tick. This probes inbound RPC timing, not
## property replication.
class_name InboundRpcProbe
extends Node

var value: int = 0


@rpc("authority", "call_remote", "reliable")
func apply_value(next_value: int) -> void:
	value = next_value
