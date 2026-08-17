## Measures the three registration mechanisms a seam funnel can dispatch on.
##
## The funnel's two load-bearing properties are a cached override bit and the
## refusal of a coroutine crossing the boundary. The second one is not a choice
## the funnel makes freely: it can only refuse what the dispatch mechanism hands
## it, and a virtual with a concrete return type hands it nothing to refuse.
## These cases measure what each mechanism actually delivers, against surfaces
## the tree already ships.
class_name TestSeamRegistration
extends NetwTestSuite


## The Variant-returning virtual. [method Object._get] is the engine's own and
## every [Object] publishes it, so the measurement does not depend on one addon
## class keeping a Variant return.
class AwaitingReader extends RefCounted:
	signal never_settles

	func _get(property: StringName) -> Variant:
		await never_settles
		return {&"table": &"players", &"id": property}


class AnsweringReader extends RefCounted:
	func _get(property: StringName) -> Variant:
		return {&"table": &"players", &"id": property}


## The concrete-returning virtual, which is the shape a typed seam freeze
## would take. [method MultiplayerPeerExtension._get_available_packet_count] is
## the engine's own, instantiable, and reached through a bound reader, so the
## measurement does not depend on one addon class staying instantiable.
class AwaitingPeer extends MultiplayerPeerExtension:
	signal never_settles

	func _get_available_packet_count() -> int:
		await never_settles
		return 7


## Verify a coroutine crossing a Variant-returning virtual arrives whole, as an
## object a funnel can name. This is what makes the refusal implementable.
func test_a_variant_virtual_hands_back_a_coroutine_state() -> void:
	var reader := AwaitingReader.new()

	var answer: Variant = reader.get(&"p1")

	assert_int(typeof(answer)).is_equal(TYPE_OBJECT)
	assert_str((answer as Object).get_class()).is_equal("GDScriptFunctionState")


## Verify the same virtual passes an ordinary answer through untouched, so the
## refusal costs the synchronous path nothing.
func test_a_variant_virtual_passes_a_plain_answer_through() -> void:
	var reader := AnsweringReader.new()

	var answer: Variant = reader.get(&"p1")

	assert_int(typeof(answer)).is_equal(TYPE_DICTIONARY)
	assert_str(str((answer as Dictionary).get(&"id"))).is_equal("p1")


## Verify a concrete-returning virtual coerces the coroutine to an empty value
## and reports success. There is nothing at the boundary for a funnel to refuse,
## which is why a typed freeze cannot carry the coroutine protection.
func test_a_typed_virtual_swallows_the_coroutine() -> void:
	var subject := AwaitingPeer.new()

	var answer := subject.get_available_packet_count()

	assert_int(answer).is_equal(0)
	assert_int(answer).is_not_equal(7)


## Verify a cached script call reaches the same override and hands back the
## same state, which is what makes the second registration candidate a real
## fallback rather than a paper one.
func test_a_cached_script_call_reaches_the_override() -> void:
	var reader := AwaitingReader.new()

	assert_bool(reader.has_method(&"_get")).is_true()
	var answer: Variant = reader.call(&"_get", &"p1")

	assert_int(typeof(answer)).is_equal(TYPE_OBJECT)
	assert_str((answer as Object).get_class()).is_equal("GDScriptFunctionState")


## Verify the override bit is a property of the script rather than of the call,
## so resolving it once per seam is sound: the answer cannot change while the
## session holding that script is alive.
func test_the_override_bit_is_stable_for_a_live_script() -> void:
	var reader := AwaitingReader.new()
	var script := reader.get_script() as Script

	var first := script.get_script_method_list().size()
	reader.get(&"p1")

	assert_int(script.get_script_method_list().size()).is_equal(first)
	assert_bool(reader.has_method(&"_get")).is_true()
