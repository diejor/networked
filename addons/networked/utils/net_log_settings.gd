@tool
extends Resource
class_name NetLogSettings

# Values: TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4, NONE=5
# Using int instead of NetLog.Level to avoid circular-dependency during resource parsing.
@export var global_level: int = 2  # INFO
@export var module_overrides: Dictionary  # String -> int
