## Read-only in-world view of one entity's prediction-boundary evidence.
##
## The overlay is attached automatically behind [DebugFeature]'s world debug
## gate. It reads [member NetwEntity.prediction] and interpolation telemetry.
## It never captures a second stream or writes gameplay state.
extends Node

const SETTING := "debug/networked/prediction_boundary_overlay"
const RING_NAMES: PackedStringArray = ["D", "P", "A", "B"]
const RING_COLORS: Array[Color] = [
	Color(0.15, 0.85, 1.0, 0.9),
	Color(0.2, 1.0, 0.35, 0.9),
	Color(1.0, 0.55, 0.15, 0.9),
	Color(0.85, 0.25, 1.0, 0.9),
]
const RING_RADII: PackedFloat32Array = [0.72, 0.86, 1.0, 1.14]
const CIRCLE_SEGMENTS := 40

var _entity_ref: WeakRef
var _rings_3d: Array[MeshInstance3D] = []
var _rings_2d: Array[Line2D] = []
var _label_3d: Label3D
var _label_2d: Label
var _label_anchor_2d: Node2D
var _position_key := StringName()


## Binds the entity whose public prediction evidence this overlay displays.
func bind(entity: NetwEntity) -> void:
	_entity_ref = weakref(entity) if entity else null


func _ready() -> void:
	if not DebugFeature.is_world_debug_enabled() \
			or not bool(ProjectSettings.get_setting(SETTING, true)):
		queue_free()
		return
	var entity := _entity()
	if not entity or not is_instance_valid(entity.owner):
		queue_free()
		return
	if entity.owner is Node3D:
		_build_3d()
	elif entity.owner is Node2D:
		_build_2d()
	else:
		queue_free()


func _process(_delta: float) -> void:
	var entity := _entity()
	if not entity or not is_instance_valid(entity.owner):
		queue_free()
		return
	var handle := entity.prediction
	var relevant := _is_relevant(handle)
	_set_overlay_visible(relevant)
	if not relevant:
		return
	var snapshot := _snapshot(entity)
	if not _rings_3d.is_empty():
		_update_3d(snapshot, handle)
	elif not _rings_2d.is_empty():
		_update_2d(snapshot, handle)


# Only speculative owners, demoted owners, and simulated remotes need rings.
func _is_relevant(handle) -> bool:
	var source: int = handle.input_source
	var mode: int = handle.sim_mode
	var prediction := NetwLagCompensationInterface.PredictionHandle
	if source == prediction.InputSource.PREDICTED:
		return true
	return source == prediction.InputSource.LOCAL \
			and mode != prediction.SimMode.AUTHORITATIVE


# Reads the four present-time layers without retaining another history.
func _snapshot(entity: NetwEntity) -> Dictionary:
	var handle := entity.prediction
	var live := entity.state_binding.snapshot_payload() \
			if entity.state_binding else { }
	var episode: Dictionary = handle.episode()
	var basis_transition := _basis_transition(episode)
	var authority := _authority_sample(entity, live)
	return {
		&"display": _display_state(entity, live),
		&"predicted": live,
		&"authority": authority.get(&"state", { }),
		&"authority_tick": int(authority.get(&"tick", -1)),
		&"basis": handle.transition_state_at(basis_transition) \
				if basis_transition >= 0 else { },
		&"basis_transition": basis_transition,
		&"episode": episode,
	}


# Reads the newest authority pose from interpolation's existing state buffer.
func _authority_sample(entity: NetwEntity, live: Dictionary) -> Dictionary:
	var key := _position_field(live)
	if key.is_empty():
		return { }
	var buffer := entity.interpolation.get_buffer(key)
	if not buffer or buffer.is_empty():
		return { }
	var tick := buffer.newest_tick()
	return {
		&"state": { key: buffer.get_at(tick) },
		&"tick": tick,
	}


# Chooses the newest settled basis while an episode remains open.
func _basis_transition(episode: Dictionary) -> int:
	if episode.is_empty():
		return -1
	var disposition: Dictionary = episode.get(&"disposition", { })
	var state := int(disposition.get(
		&"state",
		NetwLagCompensationInterface.PredictionHandle.EpisodeState.OPEN,
	))
	if state != NetwLagCompensationInterface.PredictionHandle.EpisodeState.OPEN:
		return -1
	var latest := int(episode.get(&"last_comparison_transition", -1))
	if latest >= 0:
		return latest
	var generator: Dictionary = episode.get(&"generator", { })
	return int(generator.get(&"transition", -1))


# Reconstructs the displayed value from the declared interpolation target.
func _display_state(entity: NetwEntity, live: Dictionary) -> Dictionary:
	var key := _position_field(live)
	if key.is_empty() or not entity.state_binding:
		return { }
	var source := entity.state_binding.node()
	if not is_instance_valid(source):
		return { }
	var spec := NetwScriptModel.get_node_property_interpolator(source, key)
	var target := key
	if spec and not spec.target.is_empty():
		target = spec.target
	var target_node := source
	var visual_path := entity.interpolation.visual_root
	if not visual_path.is_empty() and is_instance_valid(entity.owner):
		var visual := entity.owner.get_node_or_null(visual_path)
		if visual:
			target_node = visual
	if target not in target_node:
		return { }
	return { key: target_node.get(target) }


# Creates four transparent world-space torus layers and one annotation label.
func _build_3d() -> void:
	for index in RING_NAMES.size():
		var ring := MeshInstance3D.new()
		ring.name = "Ring%s" % RING_NAMES[index]
		ring.top_level = true
		ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var torus := TorusMesh.new()
		torus.inner_radius = RING_RADII[index] - 0.025
		torus.outer_radius = RING_RADII[index]
		torus.rings = CIRCLE_SEGMENTS
		torus.ring_segments = 6
		torus.material = _material(RING_COLORS[index])
		ring.mesh = torus
		add_child(ring)
		_rings_3d.append(ring)
	_label_3d = Label3D.new()
	_label_3d.name = "Evidence"
	_label_3d.top_level = true
	_label_3d.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_label_3d.no_depth_test = true
	_label_3d.font_size = 26
	_label_3d.outline_size = 8
	_label_3d.modulate = Color.WHITE
	add_child(_label_3d)


func _material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	material.no_depth_test = true
	return material


# Creates four world-space circle outlines and one annotation label.
func _build_2d() -> void:
	for index in RING_NAMES.size():
		var ring := Line2D.new()
		ring.name = "Ring%s" % RING_NAMES[index]
		ring.top_level = true
		ring.width = 2.0
		ring.default_color = RING_COLORS[index]
		ring.points = _circle_points(RING_RADII[index] * 32.0)
		add_child(ring)
		_rings_2d.append(ring)
	_label_anchor_2d = Node2D.new()
	_label_anchor_2d.name = "EvidenceAnchor"
	_label_anchor_2d.top_level = true
	add_child(_label_anchor_2d)
	_label_2d = Label.new()
	_label_2d.position = Vector2(18.0, -96.0)
	_label_2d.add_theme_color_override(&"font_outline_color", Color.BLACK)
	_label_2d.add_theme_constant_override(&"outline_size", 6)
	_label_anchor_2d.add_child(_label_2d)


func _circle_points(radius: float) -> PackedVector2Array:
	var points := PackedVector2Array()
	for index in CIRCLE_SEGMENTS + 1:
		var angle := TAU * float(index) / float(CIRCLE_SEGMENTS)
		points.append(Vector2(cos(angle), sin(angle)) * radius)
	return points


# Places 3D rings and keeps the annotation above the live collidable body.
func _update_3d(snapshot: Dictionary, handle) -> void:
	var positions: Array = _snapshot_positions(snapshot)
	var anchor := Vector3.ZERO
	var has_anchor := false
	for index in _rings_3d.size():
		var position: Variant = positions[index]
		var visible := position is Vector3
		_rings_3d[index].visible = visible
		if visible:
			_rings_3d[index].global_position = position
			if index == 1 or not has_anchor:
				anchor = position
				has_anchor = true
	_label_3d.visible = has_anchor
	if has_anchor:
		_label_3d.global_position = anchor + Vector3(0.0, 1.5, 0.0)
		_label_3d.text = _annotation(handle, snapshot)
		_label_3d.modulate = _annotation_color(handle, snapshot)


# Places 2D rings and keeps the annotation beside the live collidable body.
func _update_2d(snapshot: Dictionary, handle) -> void:
	var positions: Array = _snapshot_positions(snapshot)
	var anchor := Vector2.ZERO
	var has_anchor := false
	for index in _rings_2d.size():
		var position: Variant = positions[index]
		var visible := position is Vector2
		_rings_2d[index].visible = visible
		if visible:
			_rings_2d[index].global_position = position
			if index == 1 or not has_anchor:
				anchor = position
				has_anchor = true
	_label_anchor_2d.visible = has_anchor
	if has_anchor:
		_label_anchor_2d.global_position = anchor
		_label_2d.text = _annotation(handle, snapshot)
		_label_2d.modulate = _annotation_color(handle, snapshot)


func _snapshot_positions(snapshot: Dictionary) -> Array:
	return [
		_state_position(snapshot.get(&"display", { })),
		_state_position(snapshot.get(&"predicted", { })),
		_state_position(snapshot.get(&"authority", { })),
		_state_position(snapshot.get(&"basis", { })),
	]


# Resolves the first position-bearing state field and lifts it into world space.
func _state_position(state: Dictionary) -> Variant:
	var key := _position_field(state)
	if key.is_empty():
		return null
	var entity := _entity()
	var source := entity.state_binding.node() \
			if entity and entity.state_binding else null
	return _world_position(source, key, state.get(key))


func _position_field(state: Dictionary) -> StringName:
	if not _position_key.is_empty() and state.has(_position_key):
		return _position_key
	for key: Variant in state:
		var name := String(key).to_lower()
		var value: Variant = state[key]
		if (value is Vector2 or value is Vector3) and "position" in name:
			_position_key = StringName(key)
			return _position_key
		if (value is Transform2D or value is Transform3D) \
				and "transform" in name:
			_position_key = StringName(key)
			return _position_key
	return &""


func _world_position(source: Node, key: StringName, value: Variant) -> Variant:
	var global_value := String(key).begins_with("global_")
	if value is Transform3D:
		value = (value as Transform3D).origin
	if value is Transform2D:
		value = (value as Transform2D).origin
	if value is Vector3 and source is Node3D and not global_value:
		if key in [&"position", &"transform"]:
			var parent := (source as Node3D).get_parent_node_3d()
			return parent.to_global(value) if parent else value
		return (source as Node3D).to_global(value)
	if value is Vector2 and source is Node2D and not global_value:
		if key in [&"position", &"transform"]:
			var parent := (source as Node2D).get_parent() as Node2D
			return parent.to_global(value) if parent else value
		return (source as Node2D).to_global(value)
	return value


# Formats the current journal, episode, operator, meter, and display evidence.
func _annotation(handle, snapshot: Dictionary) -> String:
	var entity := _entity()
	var stats: Dictionary = handle.stats()
	var lines := PackedStringArray([
		"%s  D cyan  P green  A orange  B magenta" % entity.entity_id,
		"ack %d/%d  auth %d  shown %d  display %.1f ticks" % [
			int(stats.get(&"ack_age_ticks", 0)),
			int(stats.get(&"ack_age_max", 0)),
			int(snapshot.get(&"authority_tick", -1)),
			entity.interpolation.displayed_authoring_tick(),
			entity.interpolation.display_lag,
		],
	])
	var journal := handle.journal() as NetwPredictJournal
	if journal and not journal.transitions().is_empty():
		var transition := int(journal.transitions()[-1])
		var row := journal.row_at(transition)
		lines.append(_row_annotation(transition, row))
	var episode: Dictionary = snapshot.get(&"episode", { })
	if not episode.is_empty():
		lines.append(_episode_annotation(episode))
		var operator := _last_operator_annotation(episode)
		if not operator.is_empty():
			lines.append(operator)
	return "\n".join(lines)


func _row_annotation(transition: int, row: Dictionary) -> String:
	var domain := _enum_name(
		NetwPredictJournal.Domain,
		int(row.get(&"domain", NetwPredictJournal.Domain.IN_DOMAIN)),
	)
	var attribution := _enum_name(
		NetwPredictJournal.Attribution,
		int(row.get(&"attribution", NetwPredictJournal.Attribution.UNKNOWN)),
	)
	var witness := _witness_glyph(int(row.get(&"witness_class_bits", 0)))
	var error := float(row.get(&"aligned_error", 0.0))
	var marker := "BREACH " if attribution == "CONTACT" else ""
	return "%st%d  %s  %s  W:%s  error %.3f" % [
		marker,
		transition,
		domain,
		attribution,
		witness,
		error,
	]


func _episode_annotation(episode: Dictionary) -> String:
	var disposition: Dictionary = episode.get(&"disposition", { })
	var state := _enum_name(
		NetwLagCompensationInterface.PredictionHandle.EpisodeState,
		int(disposition.get(&"state", 0)),
	)
	var generator: Dictionary = episode.get(&"generator", { })
	return "episode %d %s  generator %d  fallback %d  close %d" % [
		int(episode.get(&"id", 0)),
		state,
		int(generator.get(&"transition", -1)),
		int(disposition.get(&"fallback_transition", -1)),
		int(disposition.get(&"closed_transition", -1)),
	]


func _last_operator_annotation(episode: Dictionary) -> String:
	var operators: Array = episode.get(&"operators", [])
	if operators.is_empty():
		return ""
	var attempt: Dictionary = operators.back()
	var operation := _enum_name(
		NetwPredictJournal.Operator,
		int(attempt.get(&"operator", NetwPredictJournal.Operator.NONE)),
	)
	var outcome_value := int(attempt.get(&"outcome", -1))
	var outcome := "REFUSED" if outcome_value < 0 else _enum_name(
		NetwLagCompensationInterface.PredictionHandle.OperatorOutcome,
		outcome_value,
	)
	var comparisons: Array = episode.get(&"contraction", [])
	var meter := int(comparisons.back().get(&"meter", 0)) \
			if not comparisons.is_empty() else 0
	return "%s %s  basis %d  meter %d" % [
		operation,
		outcome,
		int(attempt.get(&"basis", -1)),
		meter,
	]


func _witness_glyph(bits: int) -> String:
	var names := PackedStringArray()
	var witness := NetwLagCompensationInterface.PredictionHandle.WitnessClass
	if bits & witness.SUPPORT:
		names.append("SUPPORT")
	if bits & witness.STATIC:
		names.append("STATIC")
	if bits & witness.DYNAMIC_ENTITY:
		names.append("DYNAMIC")
	return "+".join(names) if not names.is_empty() else "NONE"


func _enum_name(values: Dictionary, value: int) -> String:
	var name: Variant = values.find_key(value)
	return String(name) if name != null else str(value)


func _annotation_color(handle, snapshot: Dictionary) -> Color:
	var episode: Dictionary = snapshot.get(&"episode", { })
	var disposition: Dictionary = episode.get(&"disposition", { })
	var demoted: bool = bool(disposition.get(&"demoted", false)) \
			and handle.sim_mode == \
			NetwLagCompensationInterface.PredictionHandle.SimMode.DISPLAY
	return Color(1.0, 0.35, 0.3) if demoted else Color.WHITE


func _set_overlay_visible(value: bool) -> void:
	for ring in _rings_3d:
		ring.visible = value
	for ring in _rings_2d:
		ring.visible = value
	if _label_3d:
		_label_3d.visible = value
	if _label_anchor_2d:
		_label_anchor_2d.visible = value


func _entity() -> NetwEntity:
	return _entity_ref.get_ref() as NetwEntity if _entity_ref else null
