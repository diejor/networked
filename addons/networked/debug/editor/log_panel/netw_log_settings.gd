## Serializable logging profile resource consumed by [NetwLog].
##
## Create instances of this resource as [code].tres[/code] files and assign one as the
## active profile in [b]Project Settings > networked/logging/active_profile[/b].
## The [code]global_level[/code] field uses [code]int[/code] rather than [enum NetwLog.Level]
## to avoid a circular dependency during resource parsing (INHERIT=-1 … NONE=5).
@tool
extends Resource
class_name NetwLogSettings

## Fallback log level applied when no per-module override matches. Uses [enum NetwLog.Level] integer values.
@export var global_level: int = 2
## Per-module level overrides. Keys are dot-separated module paths; values are [enum NetwLog.Level] integers.
@export var module_overrides: Dictionary
