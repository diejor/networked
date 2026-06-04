## Unit tests for [NetwDbgScope] and [NetwDbgNoop] facade.
class_name TestNetwDebugScope
extends NetwTestSuite

func test_noop_debug_facade_returns_callable_spans() -> void:
	var dbg := NetwDbgNoop.new()
	var span := dbg.span(self, "noop_span")
	var peer_span := dbg.peer_span(self, "noop_peer_span", [2])

	assert_that(span).is_not_null()
	assert_that(peer_span).is_not_null()
	assert_that(str(span.id)).is_equal("")
	assert_that(str(peer_span.id)).is_equal("")
