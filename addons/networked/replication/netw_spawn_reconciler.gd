## Pure interest-to-materialization planner.
##
## Input rows are in parent-before-child book order and contain only route,
## parent route, recipient arrays, and per-peer desired and leave values.
class_name NetwSpawnReconciler
extends RefCounted

## Plans gains parent-first and losses child-first.
static func reconcile(rows: Array[Dictionary], peers: PackedInt32Array) \
-> NetwSpawnPlan:
	var plan := NetwSpawnPlan.new()
	var desired: Dictionary[int, Dictionary] = { }
	var leave_effects: Dictionary[int, Dictionary] = { }
	for row: Dictionary in rows:
		var route := int(row[&"route"])
		var parent_route := int(row.get(&"parent_route", 0))
		var per_peer: Dictionary = { }
		var effects: Dictionary = { }
		for peer: int in peers:
			var local := bool(row[&"local_desired"].get(peer, false))
			var parent_desired := true
			if parent_route > 0:
				parent_desired = bool(
					desired.get(parent_route, { }).get(peer, false),
				)
			var wants := local and parent_desired
			per_peer[peer] = wants
			var recipients: Array = row[&"recipients"]
			if wants and peer not in recipients:
				plan.append(
					{
						&"action": &"spawn",
						&"route": route,
						&"peer": peer,
					},
				)
			if wants or peer not in recipients:
				continue
			var parent_effect: Dictionary = leave_effects \
					.get(parent_route, { }).get(peer, { }) \
			if parent_route > 0 else { }
			var parent_despawns := not parent_effect.is_empty() and (
					bool(parent_effect[&"forced"])
					or bool(parent_effect[&"decision"][&"despawn"])
			)
			var decision: Dictionary = row[&"leave"].get(
				peer,
				{ &"despawn": true, &"custom": [] },
			).duplicate(true)
			var forced := parent_despawns
			if not parent_effect.is_empty() and local:
				decision[&"despawn"] = parent_despawns
			effects[peer] = {
				&"decision": decision,
				&"forced": forced,
			}
		desired[route] = per_peer
		if not effects.is_empty():
			leave_effects[route] = effects
	for index in range(rows.size() - 1, -1, -1):
		var row: Dictionary = rows[index]
		var route := int(row[&"route"])
		for peer: int in peers:
			if bool(desired.get(route, { }).get(peer, false)):
				continue
			var recipients: Array = row[&"recipients"]
			if peer not in recipients:
				continue
			var effect: Dictionary = leave_effects.get(route, { }).get(
				peer,
				{
					&"decision": { &"despawn": true, &"custom": [] },
					&"forced": false,
				},
			)
			var decision: Dictionary = effect[&"decision"]
			var action := &"despawn" if bool(decision[&"despawn"]) \
					or bool(effect[&"forced"]) else &"retain"
			plan.append(
				{
					&"action": action,
					&"route": route,
					&"peer": peer,
					&"decision": decision,
					&"forced": bool(effect[&"forced"]),
				},
			)
	return plan
