class_name NetLogSessionHook
extends GdUnitTestSessionHook

## A GdUnit4 session hook that silences the NetLog during test execution.

func _init() -> void:
	super("NetLogSilencer", "Silences the NetLog to reduce output noise during tests.")


func startup(_session: GdUnitTestSession) -> GdUnitResult:
	NetLog.current_level = NetLog.Level.NONE
	return GdUnitResult.success()


func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	NetLog.current_level = NetLog.Level.INFO
	return GdUnitResult.success()
