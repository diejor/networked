class_name NetwLinkConditions
extends Resource

const _MSEC_TO_SEC := 0.001
const _PERCENT_TO_RATIO := 0.01

## Enables lag simulation.
@export var simulate_lag := false

## Minimum one-way delay in milliseconds.
@export var one_way_delay_min := 0.0

## Maximum one-way delay in milliseconds.
@export var one_way_delay_max := 100.0

## Simulated packet loss percentage.
@export_range(0.0, 25.0, 0.01, "suffix:%") var lag_packet_loss_percent := 0.0


## Wraps [param base_peer] with [LaggyMultiplayerPeer] when enabled.
##
## Dynamic construction keeps projects without the extension loadable.
func wrap_peer(base_peer: MultiplayerPeer) -> MultiplayerPeer:
	if not base_peer:
		return null
	if not simulate_lag:
		return base_peer

	if not ClassDB.class_exists(&"LaggyMultiplayerPeer"):
		Netw.dbg.warn(
			"Lag simulation is enabled but LaggyMultiplayerPeer is missing.",
			func(m): push_warning(m)
		)
		return base_peer

	var min_delay_ms := maxf(0.0, one_way_delay_min)
	var max_delay_ms := maxf(min_delay_ms, one_way_delay_max)
	var packet_loss_percent := clampf(lag_packet_loss_percent, 0.0, 100.0)

	Netw.dbg.info(
		"Wrapping peer in LaggyMultiplayerPeer "
		+ "(delay: %.1f-%.1f ms, packet loss: %d%%)",
		[
			min_delay_ms,
			max_delay_ms,
			int(packet_loss_percent),
		],
	)

	var laggy_instance: Object = ClassDB.instantiate(&"LaggyMultiplayerPeer")
	var wrapped_peer: MultiplayerPeer = laggy_instance.call(&"create", base_peer)
	if wrapped_peer:
		wrapped_peer.set(&"delay_minimum", min_delay_ms * _MSEC_TO_SEC)
		wrapped_peer.set(&"delay_maximum", max_delay_ms * _MSEC_TO_SEC)
		wrapped_peer.set(
			&"packet_loss",
			packet_loss_percent * _PERCENT_TO_RATIO,
		)
		return wrapped_peer

	return base_peer
