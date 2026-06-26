## Strategy for deciding how each table pre-loads into a write-behind cache.
##
## Assign it to [member NetwDatabase.warm_policy]. The database walks the
## registered schema at init and asks the policy for one [WarmRequest] per table,
## then hands the batch to the backend. A [code]null[/code] policy warms nothing,
## leaving every table to lazy fetch-on-miss. [EagerWarmPolicy] is the default.
## [codeblock]
## # A policy that warms only the players table and leaves the rest lazy.
## class_name WarmPlayersOnly
## extends WarmPolicy
##
## func plan_table(table: StringName, _columns: Array[StringName]) -> WarmRequest:
##     return WarmRequest.all() if table == &"players" else WarmRequest.none()
## [/codeblock]
@abstract
class_name WarmPolicy
extends Resource

## Returns the [WarmRequest] for [param table] given its declared [param columns].
##
## Called once per registered table at [code]_initialize_backend[/code].
## Synchronous backends ignore the result, so a policy can never break a read.
@abstract
func plan_table(table: StringName, columns: Array[StringName]) -> WarmRequest
