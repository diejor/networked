class_name NetLog
extends Object

## A simple logger for the networked addon that can be silenced during tests.

enum Level { DEBUG, INFO, WARN, ERROR, NONE }

static var current_level: Level = Level.INFO

static func _can_log(level: Level) -> bool:
	return level >= current_level

static func debug(msg: Variant, args: Variant = null) -> void:
	if _can_log(Level.DEBUG):
		_do_log("[NET-DEBUG]", msg, args)

static func info(msg: Variant, args: Variant = null) -> void:
	if _can_log(Level.INFO):
		_do_log("[NET-INFO]", msg, args)

static func warn(msg: Variant, args: Variant = null) -> void:
	if _can_log(Level.WARN):
		_do_log("[NET-WARN]", msg, args, true)

static func error(msg: Variant, args: Variant = null) -> void:
	if _can_log(Level.ERROR):
		_do_log("[NET-ERROR]", msg, args, false, true)

static func _do_log(prefix: String, msg: Variant, args: Variant, is_warn := false, is_error := false) -> void:
	var final_msg: String
	if args == null:
		final_msg = str(msg)
	elif args is Array:
		final_msg = str(msg) % args
	else:
		final_msg = str(msg) + str(args)
	
	if is_error:
		push_error(prefix + " " + final_msg)
	elif is_warn:
		push_warning(prefix + " " + final_msg)
	else:
		print(prefix + " " + final_msg)
