## GdUnit4 session hook that silences [NetLog] output during the entire test run.
##
## Register this in your [code]GdUnitRunner.cfg[/code] to suppress log noise globally.
class_name NetLogSessionHook
extends GdUnitTestSessionHook

func _init() -> void:
	super("NetLogSilencer", "Silences the NetLog to reduce output noise during tests.")

func startup(_session: GdUnitTestSession) -> GdUnitResult:
	#NetLog.push_setting_str("none")
	return GdUnitResult.success()

func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	NetLog.pop_settings()
	return GdUnitResult.success()
