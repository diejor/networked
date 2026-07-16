## Default [WarmPolicy] that warms every record of every table.
##
## This is the value [member NetwDatabase.warm_policy] holds when left unset, so
## a write-behind backend pre-loads the whole open slot before the first read.
## Replace it with a scoped policy, or clear it to [code]null[/code], when a slot
## is too large to mirror in full.
class_name EagerWarmPolicy
extends WarmPolicy

## Returns [method WarmRequest.all] for every registered table.
##
## [NetwDatabase] calls this once per table during backend initialization.
## Write-behind backends use the returned request to mirror the whole table
## before regular reads fall back to lazy fetch-on-miss.
## [codeblock]
## _plan_table(&"players", columns) -> WarmRequest.all()
## _plan_table(&"world", columns)   -> WarmRequest.all()
## [/codeblock]
func _plan_table(_table: StringName, _columns: Array[StringName]) -> WarmRequest:
	return WarmRequest.all()
