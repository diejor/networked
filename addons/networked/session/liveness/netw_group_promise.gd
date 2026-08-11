## The pending results of a one-to-many broadcast request, one entry per responding
## peer.
##
## [method Netw.request_all] returns one of these after fanning a request out to
## every live peer. The awaited set is snapshotted at send time, so a peer that
## joins mid-flight is not waited on, and a peer that disconnects is dropped
## from the set so the group can still complete. [method then] fires once with
## the full [code]peer_id -> value[/code] map when the last response lands,
## while [signal completed_single] reports each response as it arrives.
## [codeblock]
## Netw.request_all(player.ready_check) \
##     .then(func(votes: Dictionary) -> void: 
##         start_when_all_ready(votes)
##     )
## [/codeblock]
## On timeout the group settles through [method catch_error] with
## [constant @GlobalScope.ERR_TIMEOUT], and whatever responses arrived first
## remain readable on [member results]. It shares the settle vocabulary of
## [NetwPromise], which is also the one-to-one form.
class_name NetwGroupPromise
extends RefCounted

## Emitted when all expected peers have resolved. Passes the
## [member results] dictionary.
signal completed(results: Dictionary)

## Emitted when one specific [param peer_id] resolves with [param value].
signal completed_single(peer_id: int, value: Variant)

## Emitted when the group promise is rejected due to a timeout or failure.
## Passes the settle [param code] and its human-readable [param detail].
signal failed(code: Error, detail: String)

## Returns [code]true[/code] if all expected peers resolved.
var is_completed := false

## Returns [code]true[/code] if the request failed or timed out.
var is_failed := false

## Map of [code]peer_id -> result[/code] for all resolved responses.
var results: Dictionary = { }

## List of peer IDs that we are still waiting for responses from.
var expected_peers: Array[int] = []

## The settle code if [member is_failed] is [code]true[/code], otherwise
## [constant @GlobalScope.OK].
var code: Error = OK

## The human-readable reason behind [member code], empty when the code says it
## all. Diagnostics only, never a branch condition.
var detail: String = ""

var _then_callbacks: Array[Callable] = []
var _catch_callbacks: Array[Callable] = []


# Initializes the group promise, waiting on responses from peers.
func _init(peers: Array[int]) -> void:
	expected_peers = peers.duplicate()
	if expected_peers.is_empty():
		call_deferred("resolve_all")


## Chains a callback [param cb] to execute when all expected peers resolve.
##
## The callback receives the [member results] dictionary. If the promise is
## already completed, the callback executes immediately.
func then(cb: Callable) -> NetwGroupPromise:
	if is_completed:
		cb.call(results)
	else:
		_then_callbacks.append(cb)
	return self


## Chains a callback [param cb] to execute if the group promise fails or times out.
##
## The callback receives [member code] and [member detail]. If the promise is
## already failed, the callback executes immediately.
func catch_error(cb: Callable) -> NetwGroupPromise:
	if is_failed:
		cb.call(code, detail)
	else:
		_catch_callbacks.append(cb)
	return self


## Resolves the response for one specific [param peer_id] with value [param val].
##
## If this was the last expected peer, it resolves the entire group promise.
func resolve_peer(peer_id: int, val: Variant) -> void:
	if is_completed or is_failed:
		return
	if not expected_peers.has(peer_id):
		return
	results[peer_id] = val
	completed_single.emit(peer_id, val)
	expected_peers.erase(peer_id)
	if expected_peers.is_empty():
		resolve_all()


## Excludes [param peer_id] from the awaited set.
##
## If this was the last expected peer, it resolves the entire group promise.
func remove_peer(peer_id: int) -> void:
	if is_completed or is_failed:
		return
	if expected_peers.has(peer_id):
		expected_peers.erase(peer_id)
		if expected_peers.is_empty():
			resolve_all()


## Force-completes the group promise immediately.
##
## Triggers [signal completed] and runs all chained [method then] callbacks.
func resolve_all() -> void:
	if is_completed or is_failed:
		return
	is_completed = true
	completed.emit(results)
	for cb in _then_callbacks:
		cb.call(results)


## Rejects the group promise with the settle code [param err_code] and an
## optional human-readable [param err_detail].
##
## Triggers [signal failed] and runs all chained [method catch_error] callbacks.
func reject(err_code: Error, err_detail: String = "") -> void:
	if is_completed or is_failed:
		return
	is_failed = true
	code = err_code
	detail = err_detail
	if _catch_callbacks.is_empty() and failed.get_connections().is_empty():
		Netw.dbg.warn(
			"NetwGroupPromise rejected with no error handler attached: %s %s",
			[error_string(err_code), err_detail],
		)
	failed.emit(err_code, err_detail)
	for cb in _catch_callbacks:
		cb.call(err_code, err_detail)
