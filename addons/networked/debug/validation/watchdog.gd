## Watchdog that monitors the Godot log file to intercept C++ engine errors.
class_name ErrorWatchdog
extends Node

## Emitted on the main thread when a C++ error is intercepted.
signal cpp_error_caught(timestamp_usec: int, error_text: String)

var _thread: Thread
var _mutex: Mutex
var _quit: bool = false
var _log_path: String

var _dbg: NetwHandle = Netw.dbg.handle(self)


func _ready() -> void:
	if not ProjectSettings.get_setting(
		"debug/file_logging/enable_file_logging",
		false
	):
		_dbg.error(
			"ErrorWatchdog: 'debug/file_logging/enable_file_logging' is OFF " + \
			"— enable it in Project Settings → Debug → File Logging.",
			func(m): push_error(m)
		)
		return

	_log_path = ProjectSettings.get_setting(
		"debug/file_logging/log_path",
		"user://logs/godot.log"
	)
	if _log_path.is_empty():
		_log_path = "user://logs/godot.log"

	_mutex = Mutex.new()
	_thread = Thread.new()
	_thread.start(_tail_log)


func _exit_tree() -> void:
	if _mutex:
		_mutex.lock()
		_quit = true
		_mutex.unlock()
	
	if _thread and _thread.is_alive():
		_thread.wait_to_finish()


func _emit_cpp_error(timestamp: int, error_text: String) -> void:
	cpp_error_caught.emit(timestamp, error_text)


func _tail_log() -> void:
	while not FileAccess.file_exists(_log_path):
		OS.delay_msec(100)
		if _should_quit():
			return

	var file := FileAccess.open(_log_path, FileAccess.READ)
	if not file:
		_dbg.error(
			"ErrorWatchdog: could not open log file: %s", [_log_path]
		)
		return

	# Seek to current end, only watch for errors that happen after this point.
	file.seek_end()
	var read_pos := file.get_position()

	while not _should_quit():
		var file_len := file.get_length()

		if file_len < read_pos:
			# File was truncated (another instance rewrote it). Reopen and reset.
			file = FileAccess.open(_log_path, FileAccess.READ)
			if not file:
				OS.delay_msec(100)
				continue
			read_pos = 0

		if read_pos < file_len:
			file.seek(read_pos)
			while file.get_position() < file_len:
				var line := file.get_line()
				if ("ERROR:" in line or "USER ERROR:" in line) \
						and "remote_debugger_peer.cpp" not in line \
						and "marshalls.cpp" not in line:
					var timestamp := Time.get_ticks_usec()
					var lines: Array[String] = [line.strip_edges()]
					for _i in range(10):
						if file.get_position() >= file_len:
							break
						var pos_before := file.get_position()
						var next_line := file.get_line()
						if "ERROR:" in next_line or "USER ERROR:" in next_line:
							file.seek(pos_before)
							break
						lines.append(next_line.strip_edges())
					call_deferred("_emit_cpp_error", timestamp, "\n".join(lines))
			read_pos = file.get_position()
		else:
			OS.delay_msec(100)


func _should_quit() -> bool:
	_mutex.lock()
	var q := _quit
	_mutex.unlock()
	return q

