## The pending result of a one-to-one network request, resolved once with the
## responder's return value.
##
## [method Netw.request] and [method Netw.request_id] hand one of these
## back the moment the request goes out. It settles exactly once:
## [method then] fires with the value the remote handler returned, or
## [method catch_error] fires with an [enum @GlobalScope.Error] code if the
## request times out or the target peer disconnects. Chaining before it settles
## is safe, and chaining after a settled promise fires the callback immediately,
## so there is no race between sending and subscribing.
## [codeblock]
## Netw.request(server.buy_item, item_id) \
##     .then(func(receipt: Dictionary) -> void: show_receipt(receipt)) \
##     .catch_error(func(code: Error, detail: String) -> void:
##         show_error(error_string(code) if detail.is_empty() else detail)
##     )
## [/codeblock]
## The settle vocabulary is closed. [constant @GlobalScope.ERR_TIMEOUT] means the
## deadline passed with no reply, [constant @GlobalScope.ERR_UNAVAILABLE] means
## the responder went away before it could answer, and
## [constant @GlobalScope.ERR_UNAUTHORIZED] means the responder refused. The
## [member detail] string carries the human-readable reason and is never the
## thing code branches on.
## A returned [Node] arrives resolved to the live local instance, deferred until it
## spawns if the reply beats its spawn packet. See [NetwGroupPromise] for the
## one-to-many form.
class_name NetwPromise
extends RefCounted

## Emitted when the promise is successfully resolved.
## Passes the resolved value.
signal completed(value: Variant)

## Emitted when the promise is rejected due to a timeout or failure.
## Passes the settle [param code] and its human-readable [param detail].
signal failed(code: Error, detail: String)

## Emitted once when the promise settles, whichever way it went.
##
## Await this when the outcome matters less than the operation being over, and
## read [member code] afterwards. Guard the await with [member is_settled],
## because a signal that already fired never fires again.
## [codeblock]
## if not promise.is_settled:
##     await promise.settled
## if promise.code != OK:
##     show_error(promise.detail)
## [/codeblock]
signal settled()

## Returns [code]true[/code] if the promise resolved successfully.
var is_completed := false

## Returns [code]true[/code] if the promise was rejected or timed out.
var is_failed := false

## Returns [code]true[/code] once the promise has settled, whichever way it
## went.
##
## [member is_completed] alone means "succeeded", so code that only needs to
## know the operation is over asks this instead of testing both flags.
var is_settled: bool:
	get:
		return is_completed or is_failed

## The resolved result value if [member is_completed] is [code]true[/code].
var result: Variant

## The settle code if [member is_failed] is [code]true[/code], otherwise
## [constant @GlobalScope.OK].
var code: Error = OK

## The human-readable reason behind [member code], empty when the code says it
## all. Diagnostics only, never a branch condition.
var detail: String = ""

var _then_callbacks: Array[Callable] = []
var _catch_callbacks: Array[Callable] = []


## Chains a callback [param cb] to execute upon successful resolution
## of the promise.
##
## The callback receives the resolved [member result] value. If the promise
## is already completed, the callback executes immediately.
func then(cb: Callable) -> NetwPromise:
	if is_completed:
		cb.call(result)
	else:
		_then_callbacks.append(cb)
	return self


## Chains a callback [param cb] to execute if the promise is rejected
## or timed out.
##
## The callback receives [member code] and [member detail]. If the promise is
## already failed, the callback executes immediately.
func catch_error(cb: Callable) -> NetwPromise:
	if is_failed:
		cb.call(code, detail)
	else:
		_catch_callbacks.append(cb)
	return self


## Resolves the promise with the result value [param val].
##
## Triggers [signal completed] and runs all chained [method then] callbacks.
func resolve(val: Variant) -> void:
	if is_completed or is_failed:
		return
	is_completed = true
	result = val
	completed.emit(val)
	settled.emit()
	for cb in _then_callbacks:
		cb.call(val)


## Rejects the promise with the settle code [param err_code] and an optional
## human-readable [param err_detail].
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
			"NetwPromise rejected with no error handler attached: %s %s",
			[error_string(err_code), err_detail],
		)
	failed.emit(err_code, err_detail)
	settled.emit()
	for cb in _catch_callbacks:
		cb.call(err_code, err_detail)
