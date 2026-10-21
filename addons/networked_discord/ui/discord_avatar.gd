extends TextureRect

const STATUS_COLORS: Dictionary[DiscordProfile.Status, StringName] = {
	DiscordProfile.Status.OFFLINE: &"offline",
	DiscordProfile.Status.ONLINE: &"online",
	DiscordProfile.Status.IDLE: &"idle",
	DiscordProfile.Status.DO_NOT_DISTURB: &"dnd",
}
const STATUS_HOLES: Dictionary[DiscordProfile.Status, Vector4] = {
	DiscordProfile.Status.OFFLINE: Vector4(0.5, 0.5, 0.25, 0.0),
	DiscordProfile.Status.ONLINE: Vector4.ZERO,
	DiscordProfile.Status.IDLE: Vector4(0.25, 0.25, 0.375, 0.0),
	DiscordProfile.Status.DO_NOT_DISTURB: Vector4(0.5, 0.5, 0.125, 0.25),
}

## In pixels. Half the size or more draws a circle.
@export var corner_radius := 1000.0
## Draws the user's status as a dot in the bottom right corner.
@export var show_status := false
## Draws the game icon from Project Settings until a user is set.
@export var show_project_icon := false

var user: DiscordProfile:
	set(value):
		if user != null:
			user.changed.disconnect(_on_user_changed)
		user = value
		if user != null:
			user.changed.connect(_on_user_changed)
		_on_user_changed()
var open_slot := false:
	set(value):
		open_slot = value
		queue_redraw()
var speaking := false:
	set(value):
		speaking = value
		queue_redraw()
var neighbour_cutout := Vector3.ZERO:
	set(value):
		neighbour_cutout = value
		queue_redraw()
var badge: Texture2D:
	set(value):
		badge = value
		queue_redraw()
var dimmed := false:
	set(value):
		dimmed = value
		modulate.a = 0.4 if dimmed else 1.0

@onready var ring: ColorRect = $Ring
@onready var status_dot: ColorRect = $StatusDot
@onready var voice_badge: ColorRect = $VoiceBadge
@onready var voice_icon: TextureRect = $VoiceBadge/VoiceIcon


func _ready() -> void:
	var project_icon: String = ProjectSettings.get_setting(
		"application/config/icon",
	)
	if show_project_icon and ResourceLoader.exists(project_icon):
		texture = load(project_icon)


func _draw() -> void:
	var ratio := minf(size.x, size.y) / 32.0
	var dot := roundf(
		get_theme_constant(&"status_size", &"DiscordAvatar") * ratio,
	)
	var gap := roundf(
		get_theme_constant(&"cutout_gap", &"DiscordAvatar") * ratio,
	)
	var ring_width := get_theme_constant(&"ring_width", &"DiscordAvatar")
	var holes := PackedVector4Array()
	holes.resize(8)
	var count := 0
	if neighbour_cutout.z > 0.0:
		holes[count] = Vector4(
			neighbour_cutout.x,
			neighbour_cutout.y,
			neighbour_cutout.z,
			0.0,
		)
		count += 1

	status_dot.visible = show_status and user != null
	voice_badge.visible = badge != null and not status_dot.visible
	if voice_badge.visible:
		voice_icon.texture = badge
		var side := roundf(maxf(dot * 1.6, 12.0))
		var corner := size - Vector2(side, side) * 0.85
		holes[count] = Vector4(
			corner.x + side * 0.5,
			corner.y + side * 0.5,
			side * 0.5 + gap * 0.7,
			0.0,
		)
		count += 1
		voice_badge.position = corner
		voice_badge.size = Vector2(side, side)
		voice_badge.color = get_theme_color(&"voice_badge", &"DiscordAvatar")
		voice_badge.material.set_shader_parameter(
			&"size",
			Vector2(side, side),
		)
	if status_dot.visible:
		var corner := size - Vector2(dot, dot)
		holes[count] = Vector4(
			corner.x + dot * 0.5,
			corner.y + dot * 0.5,
			dot * 0.5 + gap,
			0.0,
		)
		count += 1
		status_dot.position = corner
		status_dot.size = Vector2(dot, dot)
		status_dot.color = get_theme_color(
			STATUS_COLORS[user.status],
			&"DiscordAvatar",
		)
		var dot_holes := PackedVector4Array()
		dot_holes.resize(8)
		dot_holes[0] = STATUS_HOLES[user.status] * dot
		status_dot.material.set_shader_parameter(&"size", Vector2(dot, dot))
		status_dot.material.set_shader_parameter(&"holes", dot_holes)
		status_dot.material.set_shader_parameter(
			&"hole_count",
			0 if user.status == DiscordProfile.Status.ONLINE else 1,
		)

	ring.visible = speaking
	if speaking:
		ring.color = get_theme_color(&"speaking", &"DiscordAvatar")
		var ring_holes := PackedVector4Array()
		ring_holes.resize(8)
		ring_holes[0] = Vector4(
			size.x * 0.5,
			size.y * 0.5,
			minf(size.x, size.y) * 0.5 - ring_width,
			0.0,
		)
		ring.material.set_shader_parameter(&"size", size)
		ring.material.set_shader_parameter(&"holes", ring_holes)
		ring.material.set_shader_parameter(&"hole_count", 1)

	if texture == null:
		draw_rect(
			Rect2(Vector2.ZERO, size),
			get_theme_color(
				&"open_slot" if open_slot else &"placeholder",
				&"DiscordAvatar",
			),
		)
	material.set_shader_parameter(&"size", size)
	material.set_shader_parameter(&"corner_radius", corner_radius)
	material.set_shader_parameter(
		&"inset",
		float(ring_width + 1) if speaking else 0.0,
	)
	material.set_shader_parameter(&"holes", holes)
	material.set_shader_parameter(&"hole_count", count)


func _on_user_changed() -> void:
	texture = user.avatar if user != null else null
	queue_redraw()
