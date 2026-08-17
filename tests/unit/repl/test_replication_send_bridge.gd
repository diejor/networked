## The shell's handle on the native send pipeline, driven the way the shell will.
##
## The pipeline's own laws live in the native tiers. What only this tier can
## check is that the bridge is reachable from GDScript at all, and that the
## currency crossing it survives the trip: a schema goes over as a resource, a
## row comes back as bytes, and the counts that say why a pass sent nothing are
## readable on this side.
class_name TestReplicationSendBridge
extends NetwTestSuite


func _body_schema() -> SchemaRecord:
	var record := SchemaRecord.new()
	record.name = &"Body"
	SchemaCore.append_column(record, &"x", SchemaCore.I16, 1)
	SchemaCore.fix(record)
	return record


func _offer(route: int, value: int, recipients: PackedInt32Array) -> Dictionary:
	return {
		"route": route,
		"comp": 0,
		"channel": 44,
		"schema": _body_schema(),
		"values": [value],
		"recipients": recipients,
		"masked": true,
		"priority": 1.0,
	}


func test_the_bridge_is_reachable_and_carries_a_pass() -> void:
	assert_bool(ClassDB.class_exists(&"NetwReplicationSend")).is_true()
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)

	var result: Dictionary = send.run([_offer(1, 10, PackedInt32Array([7, 9]))], 10000, 1, 500)

	# Two recipients, neither of which holds anything yet, so both are owed the
	# whole row and the bytes come back as bytes rather than as a handle.
	var sends: Array = result["sends"]
	assert_int(sends.size()).is_equal(2)
	assert_int(result["caught_up"]).is_equal(0)
	assert_int(result["ungathered"]).is_equal(0)
	assert_bool(result["untrackable"]).is_false()
	assert_int(send.lane_count()).is_equal(1)
	assert_object(sends[0]["bytes"]).is_not_null()


func test_an_acked_peer_stops_costing_the_pass() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	assert_int((send.run([_offer(1, 10, PackedInt32Array([7]))], 10000, 1, 500)["sends"] as Array).size()).is_equal(1)
	send.acknowledge(7, 1)

	var result: Dictionary = send.run([_offer(1, 10, PackedInt32Array([7]))], 10000, 2, 501)

	# The counts are what make an empty pass readable. Without them this answer
	# is identical to one where every gather refused.
	assert_int((result["sends"] as Array).size()).is_equal(0)
	assert_int(result["caught_up"]).is_equal(1)
	assert_int(result["ungathered"]).is_equal(0)


func test_a_peer_dropped_from_the_roster_is_owed_the_row_again() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	send.run([_offer(1, 10, PackedInt32Array([7]))], 10000, 1, 500)
	send.acknowledge(7, 1)

	send.retain(PackedInt32Array())

	var result: Dictionary = send.run([_offer(1, 10, PackedInt32Array([7]))], 10000, 2, 501)
	assert_int((result["sends"] as Array).size()).is_equal(1)


func test_the_encode_stage_replaces_the_bytes_that_ride() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	assert_bool(send.has_encode_stage()).is_false()

	var seen: Array = []
	send.set_encode_stage(
		func(peer: int, tick: int, route: int, comp: int, mask: int, bytes: PackedByteArray) -> PackedByteArray:
			seen.append({"peer": peer, "route": route, "comp": comp, "mask": mask, "size": bytes.size()})
			return PackedByteArray([0xAB, 0xCD])
	)
	assert_bool(send.has_encode_stage()).is_true()

	var result: Dictionary = send.run([_offer(1, 10, PackedInt32Array([7]))], 10000, 1, 500)
	var sends: Array = result["sends"]

	# A game that installed a stage keeps it. A native pipeline that encoded
	# directly would drop the override and nothing would say so until the
	# game's wire changed shape under it.
	assert_int(sends.size()).is_equal(1)
	assert_array(sends[0]["bytes"]).is_equal(PackedByteArray([0xAB, 0xCD]))
	assert_int(seen.size()).is_equal(1)
	assert_int(seen[0]["peer"]).is_equal(7)
	assert_int(seen[0]["route"]).is_equal(1)


func test_a_stage_that_answers_nothing_drops_the_frame() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	send.set_encode_stage(
		func(_p: int, _t: int, _r: int, _c: int, _m: int, _b: PackedByteArray) -> PackedByteArray:
			return PackedByteArray()
	)

	var result: Dictionary = send.run([_offer(1, 10, PackedInt32Array([7]))], 10000, 1, 500)

	# An empty answer means drop, which is what the shell's stage already
	# means. The drop is COUNTED, because a pass that sent nothing because a
	# stage refused everything reads otherwise exactly like a caught-up one.
	assert_int((result["sends"] as Array).size()).is_equal(0)
	assert_int(result["staged_out"]).is_equal(1)
	assert_int(result["caught_up"]).is_equal(0)


func test_the_shell_can_send_and_receive_through_one_bridge() -> void:
	var schema := _body_schema()
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)

	var first: Dictionary = send.run(
		[{
			"route": 1, "comp": 0, "channel": 44, "schema": schema,
			"values": [1234], "recipients": PackedInt32Array([7]),
				"masked": true, "priority": 1.0,
		}],
		10000, 1, 500,
	)
	var sends: Array = first["sends"]
	assert_int(sends.size()).is_equal(1)

	# The receiver holds nothing yet, so it applies the frame against an empty
	# row. Both halves have to cross together: a v9 send read by a v8 receive
	# is broken by construction, which is why the flip is one commit.
	var applied: Dictionary = send.apply(schema, PackedByteArray(), sends[0]["bytes"], true)
	assert_bool(applied["ok"]).is_true()
	assert_int((applied["values"] as Array)[0]).is_equal(1234)
	assert_int(applied["route"]).is_equal(1)


func test_a_deferred_pass_waits_for_the_seq_that_carried_it() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)

	var first: Dictionary = send.run_deferred([_offer(1, 10, PackedInt32Array([7]))])
	assert_int((first["sends"] as Array).size()).is_equal(1)
	# A pass answers what it wants to send. Nothing waits on a seq until the
	# caller says the frame reached the carrier.
	assert_int(send.pending_count(7)).is_equal(0)
	send.confirm(0)
	assert_int(send.pending_count(7)).is_equal(1)

	send.acknowledge(7, 1)
	assert_int((send.run_deferred([_offer(1, 10, PackedInt32Array([7]))])["sends"] as Array).size()).is_equal(1)

	send.commit(7, 1)
	assert_int(send.pending_count(7)).is_equal(0)
	send.acknowledge(7, 1)
	assert_int((send.run_deferred([_offer(1, 10, PackedInt32Array([7]))])["sends"] as Array).size()).is_equal(0)


func test_the_address_reads_off_a_frame_before_any_plan_is_chosen() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	var offer := _offer(1, 10, PackedInt32Array([7]))
	offer["comp"] = 3
	offer["tick"] = 12
	offer["ack"] = 5
	var sends: Array = send.run_deferred([offer])["sends"]

	var header: Dictionary = send.peek(sends[0]["bytes"])
	assert_int(header["route"]).is_equal(1)
	assert_int(header["comp"]).is_equal(3)
	assert_int(header["channel"]).is_equal(44)
	assert_int(header["tick"]).is_equal(12)
	assert_int(header["ack"]).is_equal(5)

	assert_dict(send.peek(PackedByteArray([0x01]))).is_empty()


func test_a_frame_read_against_the_wrong_declaration_is_refused() -> void:
	var schema := _body_schema()
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	var sends: Array = send.run(
		[{
			"route": 1, "comp": 0, "channel": 44, "schema": schema,
			"values": [1234], "recipients": PackedInt32Array([7]),
				"masked": true, "priority": 1.0,
		}],
		10000, 1, 500,
	)["sends"]

	var wider := SchemaRecord.new()
	wider.name = &"Wider"
	SchemaCore.append_column(wider, &"x", SchemaCore.I64, 1)
	SchemaCore.fix(wider)

	# There are no type tags on the wire, so a row read against the wrong
	# declaration produces values rather than an error unless something refuses
	# it. This is that something.
	var applied: Dictionary = send.apply(wider, PackedByteArray(), sends[0]["bytes"], true)
	assert_bool(applied["ok"]).is_false()
	assert_int((applied["values"] as Array).size()).is_equal(0)


func _banner_schema() -> SchemaRecord:
	var record := SchemaRecord.new()
	record.name = &"Banner"
	SchemaCore.append_column(record, &"hp", SchemaCore.I32, 1)
	SchemaCore.append_column(record, &"mp", SchemaCore.I32, 1)
	SchemaCore.fix(record)
	return record


func _retained_offer(hp: int, mp: int, recipients: PackedInt32Array) -> Dictionary:
	return {
		"route": 1,
		"comp": 0,
		"channel": 45,
		"schema": _banner_schema(),
		"values": [hp, mp],
		"recipients": recipients,
		"tick": -1,
		"ack": -1,
		"reliable": true,
		"priority": 1.0,
	}


func test_a_reliable_row_needs_no_seq_and_no_ack() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(45, &"bridge_retained", true)

	var first: Dictionary = send.run_deferred([_retained_offer(10, 20, PackedInt32Array([7]))])
	var sends: Array = first["sends"]
	assert_int(sends.size()).is_equal(1)
	assert_bool(sends[0]["reliable"]).is_true()
	assert_int(send.retained_lane_count()).is_equal(1)

	# Ordered and guaranteed delivery means the send is its own proof, so there
	# is nothing held against a seq the shell has not stamped yet.
	assert_int(send.pending_count(7)).is_equal(0)

	var again: Dictionary = send.run_deferred([_retained_offer(10, 20, PackedInt32Array([7]))])
	assert_int((again["sends"] as Array).size()).is_equal(0)
	assert_int(again["caught_up"]).is_equal(1)


func test_the_two_halves_of_one_set_share_an_address() -> void:
	var send := NetwReplicationSend.new()
	send.declare_channel(44, &"bridge_probe", true)
	send.declare_channel(45, &"bridge_retained", true)

	# One route and one ordinal, two schemas, two channels. The lanes are keyed
	# separately, so neither half diffs against the other's row.
	var result: Dictionary = send.run_deferred([
		_offer(1, 10, PackedInt32Array([7])),
		_retained_offer(30, 40, PackedInt32Array([7])),
	])
	var sends: Array = result["sends"]
	assert_int(sends.size()).is_equal(2)
	assert_int(result["ungathered"]).is_equal(0)
	assert_int(send.lane_count()).is_equal(1)
	assert_int(send.retained_lane_count()).is_equal(1)

	var channels: Array = []
	for row: Dictionary in sends:
		channels.append(int(send.peek(row["bytes"])["channel"]))
	channels.sort()
	assert_array(channels).is_equal([44, 45])


func test_a_reliable_row_applies_its_mask_onto_the_row_held() -> void:
	var schema := _banner_schema()
	var send := NetwReplicationSend.new()
	send.declare_channel(45, &"bridge_retained", true)

	var whole: Array = send.run_deferred(
		[_retained_offer(10, 20, PackedInt32Array([7]))]
	)["sends"]
	var held: Dictionary = send.apply(schema, PackedByteArray(), whole[0]["bytes"], true)
	assert_bool(held["ok"]).is_true()
	assert_array(held["values"]).is_equal([10, 20])

	# Only the column that moved rides the second frame, so the receiver's own
	# held row is what supplies the one that did not.
	var partial: Array = send.run_deferred(
		[_retained_offer(10, 21, PackedInt32Array([7]))]
	)["sends"]
	var merged: Dictionary = send.apply(schema, held["held"], partial[0]["bytes"], true)
	assert_bool(merged["ok"]).is_true()
	assert_array(merged["values"]).is_equal([10, 21])
	assert_bool(merged["whole"]).is_false()
