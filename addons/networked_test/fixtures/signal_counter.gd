## Counts emissions of a [Signal] and records each payload.
##
## Useful for assertions of the form "this signal fired exactly N times with
## these arguments" without the closure-capture pattern
## [code]var n := [0]; sig.connect(func(): n[0] += 1)[/code].
##
## [codeblock]
## var counter := SignalCounter.watch(clock.clock_synchronized)
## clock._calibrate(1)
## assert_that(counter.count).is_equal(1)
## [/codeblock]
class_name SignalCounter
extends RefCounted

## Number of times the watched signal has fired.
var count: int = 0

## Each entry is an [Array] holding the arguments passed to the matching
## emission, in order.
var calls: Array[Array] = []


## Subscribes to [param sig] and returns the counter.
static func watch(sig: Signal) -> SignalCounter:
	var counter := SignalCounter.new()
	sig.connect(counter._on_emit)
	return counter


# Variadic-style sink. GDScript signals can carry up to a small fixed arity,
# so we accept the common shapes and let the [Array] carry the payload.
func _on_emit(
	a: Variant = null,
	b: Variant = null,
	c: Variant = null,
	d: Variant = null,
) -> void:
	var args: Array = []
	if a != null: args.append(a)
	if b != null: args.append(b)
	if c != null: args.append(c)
	if d != null: args.append(d)
	calls.append(args)
	count += 1
