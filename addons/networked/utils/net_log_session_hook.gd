class_name NetLogSessionHook
extends GdUnitTestSessionHook

## A GdUnit4 session hook that silences the NetLog during test execution.

func _init() -> void:
	super("NetLogSilencer", "Silences the NetLog to reduce output noise during tests.")

func startup(_session: GdUnitTestSession) -> GdUnitResult:
	var silent := NetLogSettings.new()
	silent.global_level = NetLog.Level.NONE
	NetLog.push_settings(silent)
	return GdUnitResult.success()

func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	NetLog.pop_settings()
	return GdUnitResult.success()
