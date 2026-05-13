## Unit tests for [NetwDbgScope] and [NetwDbgNoop] facade.
class_name TestNetwDebugScope
extends NetworkedTestSuite


func test_enable_debugger_enables_trace_sink() -> void:
	enable_debugger()
	
	assert_that(Netw.dbg.is_enabled()).is_true()
	assert_that(NetTrace.message_delegate.is_valid()).is_true()
	
	var span := Netw.dbg.span(self, "debug_scope_test")
	span.step("opened")
	span.end()


func test_noop_debug_facade_returns_callable_spans() -> void:
	var dbg := NetwDbgNoop.new()
	var span := dbg.span(self, "noop_span")
	var peer_span := dbg.peer_span(self, "noop_peer_span", [2])
	
	assert_that(span).is_not_null()
	assert_that(peer_span).is_not_null()
	assert_that(str(span.id)).is_equal("")
	assert_that(str(peer_span.id)).is_equal("")
