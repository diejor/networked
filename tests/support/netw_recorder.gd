## What happened, in what order, on one object's signals.
##
## A recorder connects to a set of signals and keeps every emission: how many
## times each fired, the arguments each carried, and the order they arrived
## across signals. Order across signals is the thing polled state cannot express
## and the thing a lifecycle assertion almost always means.
##
## [br][br]
## A recorder on a signal that does not exist FAILS WHERE IT IS CONSTRUCTED.
## That is the whole reason it is a type and not a helper function: a recorder
## wired to a misspelled signal is silently empty forever, and an empty recorder
## reads exactly like a correct one that saw nothing.
## [codeblock]
## const Recorder := preload("res://tests/support/netw_recorder.gd")
##
## var r := Recorder.new(liveness, [&"entity_live", &"entity_dead"])
## api._poll()
## assert(r.count(&"entity_live") == 1)
## assert(r.args(&"entity_live")[0] == 7)
## assert(r.order() == [&"entity_live", &"entity_dead"])
## [/codeblock]
extends RefCounted

## The widest signal a recorder can watch. A wider one fails at construction
## rather than recording a truncated argument list.
const MAX_ARITY := 4

var _source: Object
var _emissions: Array[Dictionary] = []
var _connected: Array[Dictionary] = []


func _init(source: Object, signals: Array) -> void:
	assert(source != null, "NetwRecorder: no object to record")
	_source = source
	for name: StringName in signals:
		# The one failure a recorder must never absorb. An unconnected recorder
		# answers zero to every question and looks correct.
		assert(
			source.has_signal(name),
			"NetwRecorder: no such signal on the recorded object: %s" % name,
		)
		var arity := _arity_of(source, name)
		assert(
			arity >= 0 and arity <= MAX_ARITY,
			"NetwRecorder: signal %s takes %d arguments" % [name, arity],
		)
		var sink := _sink_for(arity).bind(name)
		source.connect(name, sink)
		_connected.append({ &"name": name, &"sink": sink })


func _notification(what: int) -> void:
	if what != NOTIFICATION_PREDELETE:
		return
	if not is_instance_valid(_source):
		return
	for entry in _connected:
		var name: StringName = entry[&"name"]
		var sink: Callable = entry[&"sink"]
		if _source.is_connected(name, sink):
			_source.disconnect(name, sink)


## Returns how many times [param name] fired.
func count(name: StringName) -> int:
	var total := 0
	for emission in _emissions:
		total += 1 if emission[&"name"] == name else 0
	return total


## Returns the arguments of the [param nth] emission of [param name].
##
## An out-of-range [param nth] answers an empty array rather than failing, so a
## wrong count fails on the count.
func args(name: StringName, nth: int = 0) -> Array:
	var seen := 0
	for emission in _emissions:
		if emission[&"name"] != name:
			continue
		if seen == nth:
			return emission[&"args"]
		seen += 1
	return []


## Returns emission order ACROSS signals, which is what polled state cannot say.
func order() -> Array[StringName]:
	var names: Array[StringName] = []
	for emission in _emissions:
		names.append(emission[&"name"])
	return names


## Forgets every emission so far and keeps every connection.
func clear() -> void:
	_emissions.clear()


# The declared argument count of a signal, or -1 when it is not declared.
func _arity_of(source: Object, name: StringName) -> int:
	for entry in source.get_signal_list():
		if StringName(entry[&"name"]) == name:
			return (entry[&"args"] as Array).size()
	return -1


# Bound arguments arrive after the emitted ones, so the signal name is always
# the last parameter and each arity gets its own sink.
func _sink_for(arity: int) -> Callable:
	match arity:
		0:
			return _sink0
		1:
			return _sink1
		2:
			return _sink2
		3:
			return _sink3
		_:
			return _sink4


func _push(name: StringName, values: Array) -> void:
	_emissions.append({ &"name": name, &"args": values })


func _sink0(name: StringName) -> void:
	_push(name, [])


func _sink1(a: Variant, name: StringName) -> void:
	_push(name, [a])


func _sink2(a: Variant, b: Variant, name: StringName) -> void:
	_push(name, [a, b])


func _sink3(a: Variant, b: Variant, c: Variant, name: StringName) -> void:
	_push(name, [a, b, c])


func _sink4(
		a: Variant,
		b: Variant,
		c: Variant,
		d: Variant,
		name: StringName,
) -> void:
	_push(name, [a, b, c, d])
