## Clock & Sync panel.
##
## Renders four stacked time-series lanes (RTT, Jitter, Clock Error,
## Display Offset) from a ring buffer of [NetworkClock._pong] samples.
## A stat column on the right shows the latest numeric values.
@tool
class_name PanelClock
extends HBoxContainer

const BUFFER_SIZE := 50
const LANE_COLORS := {
	"rtt_avg":   Color(0.2, 0.8, 1.0),  # cyan
	"rtt_raw":   Color(0.2, 0.8, 1.0, 0.35),
	"jitter":    Color(1.0, 0.85, 0.2),  # amber
	"diff_pos":  Color(0.8, 0.3, 0.8),   # magenta
	"diff_neg":  Color(0.3, 0.3, 0.9),   # blue
	"d_offset":  Color(0.4, 1.0, 0.4),   # green
	"d_recom":   Color(1.0, 0.4, 0.4),   # red
	"unstable":  Color(1.0, 0.2, 0.2, 0.18),
}

var _samples: Array = []

var _graph: _ClockGraph
var _stat_labels: Dictionary[String, Label] = {}


func _ready() -> void:
	add_theme_constant_override("separation", 4)

	_graph = _ClockGraph.new()
	_graph.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_graph.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_graph.samples_ref = _samples
	add_child(_graph)

	var stats_box := VBoxContainer.new()
	stats_box.custom_minimum_size.x = 190
	stats_box.add_theme_constant_override("separation", 2)
	add_child(stats_box)
	_build_stat_column(stats_box)


func _build_stat_column(box: VBoxContainer) -> void:
	var fields := [
		["rtt_raw",   "RTT raw"],
		["rtt_avg",   "RTT avg"],
		["jitter",    "Jitter"],
		["diff",      "Clock error"],
		["tick",      "Tick"],
		["d_offset",  "Display offset"],
		["d_rec",     "Recommended"],
		["stable",    "Stable"],
		["synced",    "Synchronized"],
	]
	for f in fields:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 4)
		box.add_child(row)

		var key_lbl := Label.new()
		key_lbl.text = f[1] + ":"
		key_lbl.custom_minimum_size.x = 110
		key_lbl.add_theme_font_size_override("font_size", 11)
		row.add_child(key_lbl)

		var val_lbl := Label.new()
		val_lbl.text = "—"
		val_lbl.add_theme_font_size_override("font_size", 11)
		row.add_child(val_lbl)
		_stat_labels[f[0]] = val_lbl


func push_sample(d: Dictionary) -> void:
	_samples.append(d)
	if _samples.size() > BUFFER_SIZE:
		_samples.pop_front()
	_update_stats(d)
	_graph.queue_redraw()


func clear() -> void:
	_samples.clear()
	for lbl in _stat_labels.values():
		lbl.text = "—"
		lbl.modulate = Color.WHITE
	_graph.queue_redraw()


## Populates the panel from [param buffer] all at once (called on checkbox toggle-on).
func populate(buffer: Array) -> void:
	clear()
	for d: Dictionary in buffer:
		_samples.append(d)
		if _samples.size() > BUFFER_SIZE:
			_samples.pop_front()
	if not _samples.is_empty():
		_update_stats(_samples[-1] as Dictionary)
	_graph.queue_redraw()


## Pushes a single new entry (called per [signal PanelDataAdapter.data_changed]).
func on_new_entry(entry: Variant) -> void:
	push_sample(entry as Dictionary)


func _update_stats(d: Dictionary) -> void:
	_stat_labels["rtt_raw"].text  = "%.1f ms" % (d.get("rtt_raw",  0.0) * 1000.0)
	_stat_labels["rtt_avg"].text  = "%.1f ms" % (d.get("rtt_avg",  0.0) * 1000.0)
	_stat_labels["jitter"].text   = "%.1f ms" % (d.get("rtt_jitter", 0.0) * 1000.0)
	_stat_labels["diff"].text     = "%+d ticks" % int(d.get("diff", 0))
	_stat_labels["tick"].text     = str(d.get("tick", 0))
	_stat_labels["d_offset"].text = str(d.get("display_offset", 0))
	_stat_labels["d_rec"].text    = str(d.get("recommended_display_offset", 0))

	var stable: bool = d.get("is_stable", false)
	_stat_labels["stable"].text = "Yes" if stable else "No"
	_stat_labels["stable"].modulate = Color.GREEN if stable else Color.RED

	var synced: bool = d.get("is_synchronized", false)
	_stat_labels["synced"].text = "Yes" if synced else "No"
	_stat_labels["synced"].modulate = Color.GREEN if synced else Color.ORANGE


# ─── Inner Draw Control ───────────────────────────────────────────────────────

class _ClockGraph extends Control:
	const LANE_COUNT := 4
	const LANE_PADDING := 4.0
	const LANE_LABEL_OFFSET := Vector2(4.0, 14.0)
	const BG_COLOR     := Color(0.12, 0.12, 0.12)
	const DIVIDER      := Color(0.25, 0.25, 0.25)
	const LABEL_COLOR  := Color(0.7, 0.7, 0.7)

	## Set by parent to share the ring buffer without copying.
	var samples_ref: Array = []

	func _draw() -> void:
		var w := size.x
		var h := size.y
		draw_rect(Rect2(Vector2.ZERO, size), BG_COLOR)

		if samples_ref.is_empty():
			draw_string(ThemeDB.fallback_font,
				Vector2(w * 0.5 - 60, h * 0.5),
				"Waiting for clock data…",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, LABEL_COLOR)
			return

		var lh := h / float(LANE_COUNT)

		_draw_lane(0, lh, w,  "RTT",          _get_rtt_points(w, lh))
		_draw_lane(1, lh, w,  "Jitter",       _get_jitter_points(w, lh))
		_draw_lane(2, lh, w,  "Clock Error",  null)   # bars drawn separately
		_draw_lane(3, lh, w,  "Disp. Offset", null)   # step graph drawn separately

		_draw_rtt_lanes(0, lh, w)
		_draw_jitter_lane(1, lh, w)
		_draw_diff_lane(2, lh, w)
		_draw_display_offset_lane(3, lh, w)


	func _draw_lane(lane_idx: int, lh: float, w: float, label: String, _pts) -> void:
		var y0 := lane_idx * lh
		# Divider line at top of lane (except first).
		if lane_idx > 0:
			draw_line(Vector2(0, y0), Vector2(w, y0), DIVIDER)
		draw_string(ThemeDB.fallback_font, Vector2(4, y0 + 14), label,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, LABEL_COLOR)


	func _x(sample_idx: int, w: float) -> float:
		var n := samples_ref.size()
		return w * float(sample_idx) / float(maxi(n - 1, 1))


	func _draw_rtt_lanes(lane: int, lh: float, w: float) -> void:
		var y0 := lane * lh
		var raws: Array[float] = []
		var avgs: Array[float] = []
		for s in samples_ref:
			raws.append(s.get("rtt_raw", 0.0) * 1000.0)
			avgs.append(s.get("rtt_avg", 0.0) * 1000.0)

		var max_v := maxf(100.0, _arr_max(raws))
		var stable_bands: Array = []
		for i in samples_ref.size():
			if not samples_ref[i].get("is_stable", true):
				stable_bands.append(i)

		# Instability bands.
		for i in stable_bands:
			var xb := _x(i, w)
			var xw := w / float(maxi(samples_ref.size() - 1, 1))
			draw_rect(Rect2(xb, y0 + LANE_PADDING, xw, lh - LANE_PADDING * 2),
				LANE_COLORS["unstable"])

		# Threshold lines.
		for threshold_ms in [50.0, 100.0]:
			var ty: float = y0 + lh - LANE_PADDING - (threshold_ms / max_v) * (lh - LANE_PADDING * 2)
			draw_dashed_line(Vector2(0, ty), Vector2(w, ty), Color(1, 1, 1, 0.18), 1.0, 4.0)

		# Raw line.
		var raw_pts := PackedVector2Array()
		for i in raws.size():
			var yp: float = y0 + lh - LANE_PADDING - (raws[i] / max_v) * (lh - LANE_PADDING * 2)
			raw_pts.append(Vector2(_x(i, w), yp))
		if raw_pts.size() >= 2:
			draw_polyline(raw_pts, LANE_COLORS["rtt_raw"], 1.0)

		# Avg line.
		var avg_pts := PackedVector2Array()
		for i in avgs.size():
			var yp := y0 + lh - LANE_PADDING - (avgs[i] / max_v) * (lh - LANE_PADDING * 2)
			avg_pts.append(Vector2(_x(i, w), yp))
		if avg_pts.size() >= 2:
			draw_polyline(avg_pts, LANE_COLORS["rtt_avg"], 2.0)


	func _draw_jitter_lane(lane: int, lh: float, w: float) -> void:
		var y0 := lane * lh
		var vals: Array[float] = []
		for s in samples_ref:
			vals.append(s.get("rtt_jitter", 0.0) * 1000.0)
		var max_v := maxf(50.0, _arr_max(vals))

		# Threshold dashed line.
		var thresh_ms := 50.0
		var ty: float = y0 + lh - LANE_PADDING - (thresh_ms / max_v) * (lh - LANE_PADDING * 2)
		draw_dashed_line(Vector2(0, ty), Vector2(w, ty), Color(1, 1, 0.2, 0.3), 1.0, 4.0)

		var pts := PackedVector2Array()
		for i in vals.size():
			var yp := y0 + lh - LANE_PADDING - (vals[i] / max_v) * (lh - LANE_PADDING * 2)
			pts.append(Vector2(_x(i, w), yp))
		if pts.size() >= 2:
			draw_polyline(pts, LANE_COLORS["jitter"], 2.0)


	func _draw_diff_lane(lane: int, lh: float, w: float) -> void:
		var y0 := lane * lh
		var diffs: Array[int] = []
		for s in samples_ref:
			diffs.append(int(s.get("diff", 0)))
		var max_abs := maxi(1, _arr_max_abs_int(diffs))

		var n := diffs.size()
		var bar_w := w / float(maxi(n, 1))
		var mid_y := y0 + lh * 0.5

		for i in n:
			var d := diffs[i]
			var bar_h: float = (abs(d) / float(max_abs)) * (lh * 0.5 - LANE_PADDING)
			var xb := _x(i, w)
			var color := LANE_COLORS["diff_pos"] if d >= 0 else LANE_COLORS["diff_neg"]
			var rect_y: float = mid_y - bar_h if d >= 0 else mid_y
			draw_rect(Rect2(xb, rect_y, bar_w - 1.0, bar_h), color)

		# Zero line.
		draw_line(Vector2(0, mid_y), Vector2(w, mid_y), Color(1, 1, 1, 0.15), 1.0)


	func _draw_display_offset_lane(lane: int, lh: float, w: float) -> void:
		var y0 := lane * lh
		var offsets: Array[int] = []
		var recs: Array[int] = []
		for s in samples_ref:
			offsets.append(int(s.get("display_offset", 0)))
			recs.append(int(s.get("recommended_display_offset", 0)))

		var max_v := float(maxi(1, maxi(_arr_max_int(offsets), _arr_max_int(recs))))

		var n := offsets.size()
		var seg_w := w / float(maxi(n - 1, 1))

		# Fill gap between recommended > offset.
		for i in n - 1:
			if recs[i] > offsets[i]:
				var x0 := _x(i, w)
				draw_rect(Rect2(x0, y0 + LANE_PADDING, seg_w, lh - LANE_PADDING * 2),
					Color(1.0, 0.2, 0.2, 0.15))

		# Configured offset step graph.
		var cfg_pts := PackedVector2Array()
		for i in offsets.size():
			var yp := y0 + lh - LANE_PADDING - (offsets[i] / max_v) * (lh - LANE_PADDING * 2)
			if i > 0 and offsets[i] != offsets[i - 1]:
				cfg_pts.append(Vector2(_x(i, w), cfg_pts[-1].y))
			cfg_pts.append(Vector2(_x(i, w), yp))
		if cfg_pts.size() >= 2:
			draw_polyline(cfg_pts, LANE_COLORS["d_offset"], 2.0)

		# Recommended offset step graph.
		var rec_pts := PackedVector2Array()
		for i in recs.size():
			var yp: float = y0 + lh - LANE_PADDING - (recs[i] / max_v) * (lh - LANE_PADDING * 2)
			if i > 0 and recs[i] != recs[i - 1]:
				rec_pts.append(Vector2(_x(i, w), rec_pts[-1].y))
			rec_pts.append(Vector2(_x(i, w), yp))
		if rec_pts.size() >= 2:
			draw_polyline(rec_pts, LANE_COLORS["d_recom"], 1.5)


	func _get_rtt_points(_w: float, _lh: float) -> PackedVector2Array:
		return PackedVector2Array()  # computed inline in _draw_rtt_lanes

	func _get_jitter_points(_w: float, _lh: float) -> PackedVector2Array:
		return PackedVector2Array()


	static func _arr_max(a: Array) -> float:
		var m := 0.0
		for v in a:
			if v > m: m = v
		return m

	static func _arr_max_int(a: Array) -> int:
		var m := 0
		for v in a:
			if v > m: m = v
		return m

	static func _arr_max_abs_int(a: Array) -> int:
		var m := 0
		for v in a:
			if abs(v) > m: m = abs(v)
		return m
