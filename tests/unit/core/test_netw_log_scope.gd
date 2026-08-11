## Unit tests for [NetwLogScope] and [method NetwLog.scoped].
class_name TestNetwLogScope
extends NetwTestSuite

const MODULE := "core.test_scope"
const SILENT_PROFILE := preload(
	"res://tests/unit/core/data/netw_log_silent.tres"
)


func test_profile_resource_cannot_silence_error() -> void:
	NetwLog.push_settings(SILENT_PROFILE)

	await assert_error(
		func() -> void:
			Netw.dbg.error(
				"profile error floor",
				[],
				func(message: String) -> void: push_error(message),
			)
	).is_push_error("profile error floor")

	NetwLog.pop_settings()


func test_stack_scan_finds_the_addon_root_after_log_rename() -> void:
	NetwLog._ensure_initialized()

	assert_str(NetwLog._addon_root).is_equal("addons/networked")


func test_scoped_global_level_applies_until_close() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var scope := NetwLog.scoped("trace")

	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE,
	)

	scope.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(_before)


func test_scoped_module_override_applies_until_close() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var scope := NetwLog.scoped("none,%s=debug" % MODULE)

	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.DEBUG,
	)
	assert_that(NetwLog.get_effective_level("core.other")).is_equal(
		NetwLog.Level.NONE,
	)

	scope.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(_before)


func test_double_close_is_harmless() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var scope := NetwLog.scoped("trace")

	scope.close()
	scope.close()

	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(_before)


func test_nested_scopes_restore_previous_layer() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var outer := NetwLog.scoped("debug")
	var inner := NetwLog.scoped("trace")

	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE,
	)

	inner.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.DEBUG,
	)

	outer.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(_before)


func test_out_of_order_close_fails_safely() -> void:
	var _before := NetwLog.get_effective_level(MODULE)
	var outer := NetwLog.scoped("debug")
	var inner := NetwLog.scoped("trace")

	outer.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE,
	)

	inner.close()
	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(_before)


func test_enable_logs_uses_session_hook_for_current_test() -> void:
	enable_logs("trace")

	assert_that(NetwLog.get_effective_level(MODULE)).is_equal(
		NetwLog.Level.TRACE,
	)
