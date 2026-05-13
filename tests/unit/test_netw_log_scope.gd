class_name TestNetwLogScope
extends NetworkedTestSuite

const MODULE := "core.test_scope"


func test_scoped_global_level_applies_until_close() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var scope := NetwLog.scoped("trace")
	
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE
	)
	
	scope.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(before)


func test_scoped_module_override_applies_until_close() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var scope := NetwLog.scoped("none,%s=debug" % MODULE)
	
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.DEBUG
	)
	assert_that(NetwLog.get_effective_level("core.other")).is_equal(
		NetwLog.Level.NONE
	)
	
	scope.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(before)


func test_double_close_is_harmless() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var scope := NetwLog.scoped("trace")
	
	scope.close()
	scope.close()
	
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(before)


func test_nested_scopes_restore_previous_layer() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var outer := NetwLog.scoped("debug")
	var inner := NetwLog.scoped("trace")
	
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE
	)
	
	inner.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.DEBUG
	)
	
	outer.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(before)


func test_out_of_order_close_fails_safely() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var outer := NetwLog.scoped("debug")
	var inner := NetwLog.scoped("trace")
	
	outer.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE
	)
	
	inner.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(before)


func test_enable_logs_uses_session_hook_for_current_test() -> void:
	enable_logs("trace")
	
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE
	)
