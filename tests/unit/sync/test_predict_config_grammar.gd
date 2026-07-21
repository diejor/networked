## Laws for the grammar that declares what a synchronized property does.
##
## A reconciliation has to restore the values the next simulation step reads
## from and leave alone the ones that step recomputes. Nothing in a property's
## name or type says which it is, so the declaration has to, and these laws hold
## the declaration to reaching the set and the binding intact.
class_name TestPredictConfigGrammar
extends NetwTestSuite

const PropertyConfig := NetwScriptModel.PropertyConfig
const PropertyClass := NetwSyncSet.PropertyClass


func _config() -> PropertyConfig:
	return PropertyConfig.new()


func _state_set(configs: Dictionary) -> NetwSyncSet:
	return NetwSyncSet.from_property_configs(
		configs,
		NetwSyncSet.Record.RECORD_STATE,
	)


func test_a_property_is_causal_until_it_says_otherwise() -> void:
	var config := _config().state()

	assert_int(config.property_class).override_failure_message(
		"an undeclared class must be the one whose mistake is a correction, "
		+ "never a silent divergence",
	).is_equal(PropertyClass.CAUSAL)
	assert_float(config.converge_stiffness).is_equal(0.0)
	assert_bool(config.explicit_teleport_only).is_false()


func test_each_class_verb_names_its_own_class() -> void:
	assert_int(_config().state().derived().property_class) \
			.is_equal(PropertyClass.DERIVED)
	assert_int(_config().state().cosmetic().property_class) \
			.is_equal(PropertyClass.COSMETIC)
	assert_int(_config().state().cosmetic().causal().property_class) \
			.override_failure_message(
				"the last class named owns the property, so a chain can correct "
				+ "itself the way every other set-level knob does",
			).is_equal(PropertyClass.CAUSAL)


func test_the_recovery_verbs_chain_and_store() -> void:
	var config := _config().state().causal().converge(0.4).teleport_only()

	assert_int(config.property_class).is_equal(PropertyClass.CAUSAL)
	assert_float(config.converge_stiffness).is_equal(0.4)
	assert_bool(config.explicit_teleport_only).is_true()
	assert_bool(config.in_state_set).override_failure_message(
		"the recovery verbs refine a property, so they must not disturb the "
		+ "kind mark that put it in a set",
	).is_true()


func test_the_declaration_reaches_the_field_spec() -> void:
	var set := _state_set({
		&"position": _config().state().causal(),
		&"speed": _config().state().derived().converge(0.25),
		&"skid": _config().state().cosmetic().teleport_only(),
	})
	assert_object(set).is_not_null()

	var by_key: Dictionary = { }
	for field: NetwSyncSet.Field in set.fields:
		by_key[field.key] = field

	assert_int(by_key[&"position"].property_class).is_equal(PropertyClass.CAUSAL)
	assert_int(by_key[&"speed"].property_class).is_equal(PropertyClass.DERIVED)
	assert_float(by_key[&"speed"].converge_stiffness).is_equal(0.25)
	assert_int(by_key[&"skid"].property_class).is_equal(PropertyClass.COSMETIC)
	assert_bool(by_key[&"skid"].explicit_teleport_only).is_true()


func test_a_class_survives_the_set_that_carries_no_others() -> void:
	# A set whose every member is cosmetic still declares each one, so a later
	# recovery reads the class rather than inferring it from the set's shape.
	var set := _state_set({
		&"skid": _config().state().cosmetic(),
		&"glow": _config().state().cosmetic(),
	})
	assert_object(set).is_not_null()
	for field: NetwSyncSet.Field in set.fields:
		assert_int(field.property_class).is_equal(PropertyClass.COSMETIC)


func test_converge_and_teleport_only_are_independent_of_the_class() -> void:
	# The class says whether a value is restored at all. These two say how, so a
	# derived field can carry them without becoming causal.
	var config := _config().state().derived().converge(0.6).teleport_only()

	assert_int(config.property_class).is_equal(PropertyClass.DERIVED)
	assert_float(config.converge_stiffness).is_equal(0.6)
	assert_bool(config.explicit_teleport_only).is_true()


func test_the_binding_answers_for_a_property_it_never_declared() -> void:
	var set := _state_set({ &"position": _config().state().derived() })
	var node := Node.new()
	auto_free(node)
	var binding := NetwSyncSetBinding.new(set, node)

	assert_int(binding.property_class_of(&"position")) \
			.is_equal(PropertyClass.DERIVED)
	assert_int(binding.property_class_of(&"nothing_declared_this")) \
			.override_failure_message(
				"a property the set never declared is answered causal, because "
				+ "that is the class whose mistake surfaces",
			).is_equal(PropertyClass.CAUSAL)
	assert_float(binding.converge_stiffness_of(&"nothing_declared_this")) \
			.is_equal(0.0)
	assert_bool(binding.teleport_only_of(&"nothing_declared_this")).is_false()
	assert_object(binding.field_of(&"nothing_declared_this")).is_null()
