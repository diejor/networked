## The pending result of a one-to-one network request, resolved once with the
## responder's return value.
##
## [method Netw.request] and [method Netw.request_id] hand one of these
## back the moment the request goes out. It settles exactly once:
## [method then] fires with the value the remote handler returned, or
## [method catch_error] fires if the request times out or the target
## peer disconnects. Chaining before it settles is safe, and chaining
## after a settled promise fires the callback immediately, so there is
## no race between sending and subscribing.
## [codeblock]
## Netw.request(server.buy_item, item_id) \
##     .then(func(receipt: Dictionary) -> void: show_receipt(receipt)) \
##     .catch_error(func(reason: String) -> void: show_error(reason))
## [/codeblock]
## A returned [Node] arrives resolved to the live local instance, deferred until it
## spawns if the reply beats its spawn packet. See [NetwGroupPromise] for the
## one-to-many form.
class_name NetwPromise
extends RefCounted

## Emitted when the promise is successfully resolved.
## Passes the resolved value.
signal completed(value: Variant)

## Emitted when the promise is rejected due to a timeout or failure.
## Passes the error details.
signal failed(error: String)

## Returns [code]true[/code] if the promise resolved successfully.
var is_completed := false

## Returns [code]true[/code] if the promise was rejected or timed out.
var is_failed := false

## The resolved result value if [member is_completed] is [code]true[/code].
var result: Variant

## The failure message description if [member is_failed] is [code]true[/code].
var error: String = ""

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
## The callback receives the error message [String]. If the promise is
## already failed, the callback executes immediately.
func catch_error(cb: Callable) -> NetwPromise:
	if is_failed:
		cb.call(error)
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
	for cb in _then_callbacks:
		cb.call(val)


## Rejects the promise with the failure details [param err].
##
## Triggers [signal failed] and runs all chained [method catch_error] callbacks.
func reject(err: String) -> void:
	if is_completed or is_failed:
		return
	is_failed = true
	error = err
	if _catch_callbacks.is_empty() and failed.get_connections().is_empty():
		Netw.dbg.warn(
			"NetwPromise rejected with no error handler attached: %s", [err]
		)
	failed.emit(err)
	for cb in _catch_callbacks:
		cb.call(err)
