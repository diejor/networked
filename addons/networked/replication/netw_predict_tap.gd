extends RefCounted
## Env-gated JSONL drain of the public prediction read surface, the instrument
## that turns a manual session into a capture.
##
## The tap touches nothing but [method NetwLagCompensationInterface.PredictionHandle.journal]
## and [method NetwLagCompensationInterface.PredictionHandle.stats], so a live
## play session leaves the same rows and counters a scripted capture does and a
## feel report stops being only a feel report. Reading is never a step, so the
## drain cannot perturb the run it records. It is an internal currency with no
## [code]class_name[/code]: the interface preloads it and drives it from its
## frame boundary only when [code]NETW_PREDICT_TAP[/code] names an output
## directory.
## [codeblock]
## NETW_PREDICT_TAP=/tmp/tap godot ...
##   -> /tmp/tap/<entity_id>.jsonl, one {stats, rows} line per drained frame
## [/codeblock]

const _ENV := "NETW_PREDICT_TAP"

# The directory each entity's JSONL file is written under.
var _dir: String

# entity_id (String) -> the open append handle for its file.
var _files: Dictionary = { }

# entity_id (String) -> the set of transitions already written, so a re-drain
# appends only rows authored since the last one and the ring can rotate a
# transition out without it being written twice or missed.
var _drained: Dictionary = { }


## True when the environment names an output directory, so the interface knows to
## build and drive a tap at all.
static func armed() -> bool:
	return not OS.get_environment(_ENV).is_empty()


func _init(dir: String = "") -> void:
	_dir = dir if not dir.is_empty() else OS.get_environment(_ENV)
	if not _dir.is_empty():
		DirAccess.make_dir_recursive_absolute(_dir)


## Appends [param handle]'s counters and any journal rows authored since the last
## drain to [param entity_id]'s JSONL file. Reads only the public journal and
## stats surface, so it never moves a fingerprint or a counter.
func drain(entity_id: StringName, handle: Variant) -> void:
	if _dir.is_empty() or handle == null:
		return
	var journal: NetwPredictJournal = handle.journal()
	if journal == null:
		return
	var seen: Dictionary = _drained.get_or_add(String(entity_id), { })
	var rows: Array = []
	for transition: int in journal.transitions():
		if seen.has(transition):
			continue
		var row := journal.row_at(transition)
		if row.is_empty():
			continue
		seen[transition] = true
		rows.append(row)
	if rows.is_empty():
		return
	var file := _file_for(entity_id)
	if file == null:
		return
	file.store_line(JSON.stringify({ "stats": handle.stats(), "rows": rows }))
	file.flush()


## Closes every open file. Called when the session ends so a reader sees a
## complete flush.
func close() -> void:
	for file: FileAccess in _files.values():
		if file:
			file.close()
	_files.clear()


func _file_for(entity_id: StringName) -> FileAccess:
	var key := String(entity_id)
	if _files.has(key):
		return _files[key]
	var file := FileAccess.open(_dir.path_join("%s.jsonl" % key), FileAccess.WRITE)
	_files[key] = file
	return file
