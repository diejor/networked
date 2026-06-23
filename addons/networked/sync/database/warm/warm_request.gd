## A single table's cache-warming intent, produced by a [WarmPolicy].
##
## A request is an immutable value naming what a write-behind backend should
## pre-load for one table. Synchronous backends ignore it. The [enum Kind]
## decides which of [member id_list] or [member filter_map] carries the scope.
## [codeblock]
## # Warm everything in the table:
## WarmRequest.all()
## # Warm a known set of records:
## WarmRequest.ids([&"valeria", &"jose"])
## # Warm by a column filter:
## WarmRequest.filter({ &"online": true })
## # Warm nothing eagerly (lazy fetch-on-miss still applies):
## WarmRequest.none()
## [/codeblock]
class_name WarmRequest
extends RefCounted

## Selects which scope a [WarmRequest] carries.
enum Kind {
	## Warm nothing. Records load lazily on first miss.
	NONE,
	## Warm every record in the table.
	ALL,
	## Warm the explicit [member id_list].
	IDS,
	## Warm every record matching [member filter_map].
	FILTER,
}

## Which scope this request carries.
var kind: Kind = Kind.NONE
## The record ids to warm when [member kind] is [constant Kind.IDS].
var id_list: Array[StringName] = []
## The column filter to warm when [member kind] is [constant Kind.FILTER].
var filter_map: Dictionary = { }


## Returns a request that warms nothing. Lazy fetch-on-miss still covers reads.
static func none() -> WarmRequest:
	return WarmRequest.new()


## Returns a request that warms every record in the table.
static func all() -> WarmRequest:
	var request := WarmRequest.new()
	request.kind = Kind.ALL
	return request


## Returns a request that warms the records named in [param values].
static func ids(values: Array) -> WarmRequest:
	var request := WarmRequest.new()
	request.kind = Kind.IDS
	request.id_list.assign(values)
	return request


## Returns a request that warms every record matching [param map].
static func filter(map: Dictionary) -> WarmRequest:
	var request := WarmRequest.new()
	request.kind = Kind.FILTER
	request.filter_map = map.duplicate()
	return request
