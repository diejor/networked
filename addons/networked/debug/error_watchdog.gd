class_name ErrorWatchdog
extends Node

## Emitted on the main thread when a C++ error is intercepted.
signal cpp_error_caught(timestamp_usec: int, error_text: String)

var _thread: Thread
var _mutex: Mutex
var _quit: bool = false
var _log_path: String

func _ready() -> void:
	if not ProjectSettings.get_setting("debug/file_logging/enable_file_logging", false):
		push_warning("ErrorWatchdog: File logging is disabled in Project Settings. Tailer will not work.")
		return
	
	_log_path = ProjectSettings.get_setting("debug/file_logging/log_path", "user://logs/godot.log")
	if _log_path.is_empty():
		_log_path = "user://logs/godot.log"
		
	_mutex = Mutex.new()
	_thread = Thread.new()
	_thread.start(_tail_log)

func _exit_tree() -> void:
	# Safely spin down the thread when the node is destroyed
	if _mutex:
		_mutex.lock()
		_quit = true
		_mutex.unlock()
	
	if _thread and _thread.is_alive():
		_thread.wait_to_finish()

func _tail_log() -> void:
	# 1. Wait for the engine to actually create the log file
	while not FileAccess.file_exists(_log_path):
		OS.delay_msec(100)
		if _should_quit():
			return

	var file := FileAccess.open(_log_path, FileAccess.READ)
	if not file:
		return
		
	# 2. Seek to the end immediately. We only care about NEW errors.
	file.seek_end() 

	# 3. The low-impact background loop
	while not _should_quit():
		var current_pos := file.get_position()
		var file_length := file.get_length()

		if current_pos < file_length:
			# New data was appended
			var line := file.get_line()
			if "ERROR:" in line or "USER ERROR:" in line:
				# Capture the timestamp the exact millisecond the thread parses it
				var timestamp := Time.get_ticks_usec()
				
				# Capture a few more lines for context (stack trace, etc.)
				var lines: Array[String] = [line.strip_edges()]
				for i in range(10):
					if file.get_position() >= file.get_length():
						break
					
					var pos_before := file.get_position()
					var next_line := file.get_line()
					
					# If we hit another error immediately, stop so the next iteration can catch it properly
					if "ERROR:" in next_line or "USER ERROR:" in next_line:
						# Rewind so the next iteration of the while loop can pick up this new error
						file.seek(pos_before)
						break
					lines.append(next_line.strip_edges())
				
				# Ferry the payload back to the main thread safely
				call_deferred("emit_signal", "cpp_error_caught", timestamp, "\n".join(lines))
				
		elif current_pos > file_length:
			# The log file was truncated/rotated (rare, but prevents infinite lockups)
			file.seek(0)
		else:
			# EOF reached. Sleep the thread for 16ms (roughly 1 frame at 60fps) 
			# to prevent CPU pegging before checking again.
			OS.delay_msec(16)

func _should_quit() -> bool:
	_mutex.lock()
	var q := _quit
	_mutex.unlock()
	return q
