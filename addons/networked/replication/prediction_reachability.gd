## The wire-time lint over one entity's declared property set.
##
## Every question it answers is decidable from declarations alone: whether a
## field can trigger a recovery, which operators may write it, whether its
## forward model is live under the schedule the entity resolved, which domain
## its comparisons run in, and what happens on breach. A declaration that is
## legal, accepted, and then silently inert is the failure mode this exists to
## make visible.
##
## It is a diagnostic rather than a decision, which is why it lives beside the
## engine instead of inside it: nothing here reads a body or a tick.
class_name NetwPredictionReachability
extends RefCounted

# The state families, as PredictionCore numbers them.
const STATE_FAMILY_POSE := 0
const STATE_FAMILY_MOMENTUM := 1
const STATE_FAMILY_CONTROLLER := 2


static func of(
		set: NetwPropertySet,
		wiring: NetwPredict.Wiring,
		handle: NetwPredictionHandle,
		entity_id: StringName,
		retired_carry: Dictionary,
) -> Dictionary:
	var findings: Array[Dictionary] = []
	var fields: Dictionary[StringName, Dictionary] = { }
	var unquantized: Array[String] = []
	var uncompared: Array[String] = []
	var transport_without_epsilon: Array[String] = []
	var unobserved: Array[String] = []
	var unrepairable: Array[String] = []
	var inert_steps: Array[String] = []
	# Fields measured against a threshold they did not name. Collected apart
	# because the question is not whether one field inherited, it is whether
	# two quantities ended up sharing one number.
	var tier_inheritors: Array[String] = []
	var rate_inheritors: Array[String] = []
	var schedule := handle.schedule
	for column: NetwPropertySet.Column in set.columns:
		var key := column.key
		var causal := column.property_class \
				== NetwPropertySet.PropertyClass.CAUSAL
		# Read off what the wire-time build already resolved rather than
		# re-reading the declaration: _build_restore_projection runs first, so
		# wiring.carry_rules is exactly the set of steps this entity accepted, and a
		# report that re-derived it could disagree with the engine it
		# describes.
		var channel := column.carry_channel
		var model := &"none"
		if channel != &"":
			model = &"channel"
		elif wiring.carry_rules.has(key):
			model = &"step"
		# Live means the operator would actually run it, not that it parsed.
		# A step under the tick tier is refused on first use and retired, and
		# a channel this set does not carry cannot be read at the column's own
		# tick, so neither is a forward model the entity has.
		var live := false
		var why := "no forward model is declared"
		match model:
			&"channel":
				live = wiring.projection.has(key)
				why = "" if live else (
						"carry_along(&\"%s\") names a column this set does not "
						+ "carry, so the channel cannot be read at the tick it "
						+ "would advance"
				) % [channel]
			&"step":
				if schedule != NetwPredict.Schedule.FRAME:
					why = "carry_step() needs prediction.schedule = FRAME; " \
							+ "under the tick tier it is refused on first use " \
							+ "and retired"
					inert_steps.append(String(key))
				elif retired_carry.has(key):
					why = "the rule was retired at runtime after failing its " \
							+ "fidelity gate"
				else:
					live = true
					why = ""
		# Two declarations keep a column out of the correction decision, and the
		# report reads both because the kernel does: reconcile_only(), and any
		# class but causal(). The ledger, the trigger shape and the escalation
		# ranking read the class too, so a row that answered differently here
		# would describe a column that could demand a recovery while being
		# invisible to everything that would explain why.
		var triggers := not column.explicit_reconcile_only and causal
		var operators := PackedStringArray()
		if not column.explicit_teleport_only:
			operators.append("sub_teleport_restore")
		if column.converge_stiffness > 0.0:
			operators.append("converge")
		if live:
			operators.append("carry_advance")
		operators.append("full_closure")
		fields[key] = {
			&"class": _class_name_of(column.property_class),
			&"triggers": triggers,
			&"tolerance": (
					column.epsilon_override if column.epsilon_override >= 0.0
					else handle.divergence_epsilon
			),
			&"tolerance_declared": column.epsilon_override >= 0.0,
			&"teleport_at": (
					column.teleport_at_override
					if column.teleport_at_override >= 0.0
					else handle.teleport_threshold
			),
			&"teleport_at_declared": column.teleport_at_override >= 0.0,
			&"in_tier": wiring.pose_fields.has(key),
			&"operators": operators,
			&"forward_model": {
				&"kind": model,
				&"live": live,
				&"why": why,
			},
		}
		if causal and not column.quantizer:
			unquantized.append(String(key))
		if causal and column.explicit_reconcile_only \
				and column.epsilon_override < 0.0:
			unobserved.append(String(key))
		if causal and column.explicit_teleport_only \
				and not column.explicit_reconcile_only \
				and model == &"none":
			unrepairable.append(String(key))
		# A tolerance is a statement about a comparison, and no class but
		# causal() is compared, so an epsilon() on any other class bounds
		# nothing at all.
		if not causal and column.epsilon_override >= 0.0:
			uncompared.append(String(key))
		if handle.transport_corridor.is_valid() and causal \
				and int(wiring.state_family_of.get(
					key,
					STATE_FAMILY_CONTROLLER,
				)) != STATE_FAMILY_POSE \
				and column.epsilon_override < 0.0:
			transport_without_epsilon.append(String(key))
		# Enrolled in the tier and measured against the entity's distance
		# because it named none of its own.
		if wiring.pose_fields.has(key) and column.teleport_at_override < 0.0:
			tier_inheritors.append(String(key))
		# A carry channel is the rate of the column it advances, so it is that
		# column's derivative and cannot share its tolerance.
		if causal and column.epsilon_override < 0.0 \
				and int(wiring.state_family_of.get(
					key,
					STATE_FAMILY_CONTROLLER,
				)) == STATE_FAMILY_MOMENTUM:
			rate_inheritors.append(String(key))
	if not unquantized.is_empty():
		findings.append({
			&"code": &"unquantized",
			&"severity": &"debug",
			&"fields": unquantized,
			&"message": (
					"NetwLagCompensation: causal state properties [%s] have "
					+ "no quantizer, so their canonical form is their raw "
					+ "bits. Legal, but two peers agree only if they produce "
					+ "those bits identically."
			) % [", ".join(unquantized)],
		})
	if not uncompared.is_empty():
		findings.append({
			&"code": &"uncompared",
			&"severity": &"error",
			&"fields": uncompared,
			&"message": (
					"PredictionComponent: non-causal state properties [%s] "
					+ "carry a divergence epsilon. Only a causal() field is "
					+ "compared, so the threshold decides nothing: it cannot "
					+ "raise a correction, and the meter reads its own "
					+ "tolerance row. Mark them causal() if the next step "
					+ "reads them, or drop the epsilon() mark."
			) % [", ".join(uncompared)],
		})
	if not transport_without_epsilon.is_empty():
		findings.append({
			&"code": &"transport_without_epsilon",
			&"severity": &"debug",
			&"fields": transport_without_epsilon,
			&"message": (
					"NetwLagCompensation: transport reads causal non-pose "
					+ "properties [%s] without declared epsilons. Their units "
					+ "fall back to the entity default, so the transport gate "
					+ "has no world-scale tolerance."
			) % [", ".join(transport_without_epsilon)],
		})
	if not unobserved.is_empty():
		findings.append({
			&"code": &"unobserved",
			&"severity": &"error",
			&"fields": unobserved,
			&"message": (
					"PredictionComponent: causal state properties [%s] are "
					+ "reconcile_only() and declare no epsilon(), so nothing "
					+ "observes them. A reconcile-only field cannot trigger a "
					+ "correction, and without a declared scale it does not "
					+ "reach the convergence meter either, while the next "
					+ "transition still reads it. A field like that drifts "
					+ "until some other field crosses on its behalf, which "
					+ "charges the divergence to the wrong place. Give each "
					+ "an epsilon() at its own world scale, or drop "
					+ "reconcile_only() so it can answer for itself."
			) % [", ".join(unobserved)],
		})
	if not unrepairable.is_empty():
		findings.append({
			&"code": &"unrepairable",
			&"severity": &"warning",
			&"fields": unrepairable,
			&"message": (
					"PredictionComponent: causal state properties [%s] are "
					+ "teleport_only() and can still trigger, so they demand "
					+ "recoveries that no sub-teleport restore may write, and "
					+ "each one is answered by writing other fields instead. "
					+ "The promoted full closure is the only operator that "
					+ "reaches them, and with neither carry_along() nor "
					+ "carry_step() it writes the acknowledged value, which a "
					+ "moving field has already left. Declare one of those if "
					+ "the field can be advanced, or reconcile_only() so its "
					+ "drift stops asking for recoveries that answer "
					+ "elsewhere. NetwPredictionHandle.field_recovery counts which "
					+ "happened."
			) % [", ".join(unrepairable)],
		})
	if not inert_steps.is_empty():
		findings.append({
			&"code": &"inert_forward_model",
			&"severity": &"warning",
			&"fields": inert_steps,
			&"message": (
					"PredictionComponent: state properties [%s] declare "
					+ "carry_step() while this entity resolved the tick "
					+ "schedule. "
					+ "The rule is legal, is accepted here, and is then refused "
					+ "on its first use and permanently retired, so every "
					+ "recovery writes the acknowledged value as if none had "
					+ "been declared. Set prediction.schedule to FRAME, or "
					+ "carry_along() a replicated derivative instead."
			) % [", ".join(inert_steps)],
		})
	# One threshold can only be right for one quantity. A single field
	# inheriting the entity distance is unambiguous, because the number means
	# that field and nothing else. Two are not: each carries its own forward
	# model, so they are separate quantities, and whatever the number means it
	# is wrong for at least one of them. Measuring that instead of declaring
	# it cost this campaign a round -- a rotation rate enrolled beside a
	# position tripled the teleports on FEWER triggers, because rad/s were
	# being read against a distance in metres.
	if tier_inheritors.size() >= 2:
		findings.append({
			&"code": &"unit_unsafe_inheritance",
			&"severity": &"warning",
			&"fields": tier_inheritors,
			&"message": (
					"NetwLagCompensation: state properties [%s] are enrolled "
					+ "in the teleport tier and none declares teleport_at(), "
					+ "so all of them are measured against the entity's "
					+ "%.4f. Each advances by its own forward model, so they "
					+ "are different quantities sharing one distance, and a "
					+ "rotation rate read against a length teleports on noise "
					+ "while a length read against a rate never teleports at "
					+ "all. Give each one teleport_at() in its own units."
			) % [", ".join(tier_inheritors), handle.teleport_threshold],
		})
	if not rate_inheritors.is_empty():
		findings.append({
			&"code": &"unit_unsafe_inheritance",
			&"severity": &"warning",
			&"fields": rate_inheritors,
			&"message": (
					"NetwLagCompensation: state properties [%s] are carry "
					+ "channels, so each is the rate of the field it advances, "
					+ "and none declares epsilon(). They fall back to the "
					+ "entity's %.4f, which is the tolerance a position was "
					+ "sized for. A derivative compared at its own quantity's "
					+ "scale reports a divergence in the wrong units. Give "
					+ "each an epsilon() at the scale its rate actually "
					+ "moves."
			) % [", ".join(rate_inheritors), handle.divergence_epsilon],
		})
	# There is deliberately no finding for a field that is neither causal()
	# nor reconcile_only(). The correction decision reads the class, so such a
	# field cannot trigger, and a finding for it would test a condition
	# nothing can reach. `uncompared` above carries what is left of the
	# question: whether the field declares a tolerance that decides nothing.
	return {
		&"entity": entity_id,
		&"schedule": (
				&"FRAME" if schedule == NetwPredict.Schedule.FRAME
				else &"TICK"
		),
		&"breach": {
			&"response": (
					&"DEMOTE"
					if handle.breach_response \
							== NetwPredict.BreachResponse.DEMOTE
					else &"PREDICT_THROUGH"
			),
			&"declared_by": handle._breach_source,
		},
		&"domain": {
			&"island": (
					&"approximate"
					if handle.island.approximate
					else (
							&"exact"
							if handle.island.exact_claim
							else &"none"
					)
			),
			&"contact_window_ticks": handle.collision_cooldown_ticks,
		},
		&"transport": handle.transport_corridor.is_valid(),
		&"fields": fields,
		&"findings": findings,
	}


static func _class_name_of(value: NetwPropertySet.PropertyClass) -> StringName:
	match value:
		NetwPropertySet.PropertyClass.DERIVED:
			return &"DERIVED"
		NetwPropertySet.PropertyClass.COSMETIC:
			return &"COSMETIC"
	return &"CAUSAL"


# The two provable authority-model contradictions an entity can wire with a
# prediction engine attached. Each is a declaration that cannot mean what it
# says, never a heuristic: a broadcast field is trusted to its owner, so
# predicting the entity that owns it contradicts the field's own kind, and
# an input set with no controller has no author, so nothing will ever drive
# the prediction it exists to feed.
static func report_authority_model(
		entity: NetwEntity,
		input_binding: NetwPropertySetBinding,
		command_frame: NetwPredictCommandFrame,
) -> void:
	var node := entity.owner if entity else null
	var script := node.get_script() as Script if is_instance_valid(node) \
			else null
	if script:
		var broadcast_set := NetwPropertySet.from_script(
			script,
			NetwPropertySet.Record.RECORD_BROADCAST,
		)
		if broadcast_set and not broadcast_set.columns.is_empty():
			var keys := PackedStringArray()
			for column: NetwPropertySet.Column in broadcast_set.columns:
				keys.append(String(column.key))
			push_warning(
				(
						"Prediction: %s declares broadcast() fields [%s] and "
						+ "carries a PredictionComponent. A broadcast field "
						+ "trusts its owner outright, so predicting the "
						+ "entity that owns it is a contradiction. Mark the "
						+ "fields state(), or drop the component."
				) % [entity.entity_id, ", ".join(keys)],
			)
	if input_binding and input_binding.set \
			and not input_binding.set.columns.is_empty() \
			and entity.controller <= 0:
		push_warning(
			(
					"Prediction: %s declares input() fields but no peer "
					+ "controls it. Nothing will author the command stream, "
					+ "so the entity will never predict or consume. Assign "
					+ "a controller or drop the input marks."
			) % [entity.entity_id],
		)
	if input_binding and input_binding.set \
			and not input_binding.set.columns.is_empty() \
			and command_frame == null:
		push_warning(
			(
					"Prediction: %s declares input() fields whose types the "
					+ "command lane cannot plan a row from. A self-describing "
					+ "column has no fixed width, so nothing will cross. Type "
					+ "the properties, or quantize them."
			) % [entity.entity_id],
		)
