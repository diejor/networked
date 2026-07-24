extends RefCounted
## Env-gated JSONL drain of the public prediction read surface, the instrument
## that turns a manual session into a capture.
##
## The tap touches only
## [method NetwLagCompensationInterface.PredictionHandle.journal],
## [method NetwLagCompensationInterface.PredictionHandle.stats], and
## [method NetwLagCompensationInterface.PredictionHandle.episode], so a live
## play session leaves the same evidence a scripted capture does. Reading is
## never a step, so the drain cannot perturb the run it records. It is an
## internal currency with no [code]class_name[/code]: the interface preloads it
## and drives it from its frame boundary only when
## [code]NETW_PREDICT_TAP[/code] names an output directory.
## [codeblock]
## NETW_PREDICT_TAP=/tmp/tap godot ...
##   -> /tmp/tap/<entity_id>.jsonl
##      one {stats, rows, settles, episode} line per changed drain
## [/codeblock]

const _ENV := "NETW_PREDICT_TAP"

# The directory each entity's JSONL file is written under.
var _dir: String

# entity_id (String) -> the open append handle for its file.
var _files: Dictionary = { }

# entity_id (String) -> {epoch, last_exported, pending, episode_json}. Pending
# rows receive a later settlement overlay or an explicit loss record if the
# ring evicts them.
var _states: Dictionary = { }


## True when the environment names an output directory, so the interface knows
## to build and drive a tap at all.
static func armed() -> bool:
	return not OS.get_environment(_ENV).is_empty()


func _init(dir: String = "") -> void:
	_dir = dir if not dir.is_empty() else OS.get_environment(_ENV)
	if not _dir.is_empty():
		DirAccess.make_dir_recursive_absolute(_dir)


## Appends [param handle]'s counters, newly sealed rows, settlement overlays,
## and changed episode report to [param entity_id]'s JSONL file. A sealed row
## evicted before its verdict emits a
## [code]{transition, lost = true}[/code] settlement. Reads only the public
## prediction surface, so it never moves recorded state.
func drain(entity_id: StringName, handle: Variant) -> void:
	if _dir.is_empty() or handle == null:
		return
	var journal: NetwPredictJournal = handle.journal()
	if journal == null:
		return
	var state := _state_for(entity_id, journal.epoch())
	var rows: Array = []
	var settles: Array = []
	var episode: Dictionary = handle.episode()
	var episode_json := JSON.stringify(episode)
	if int(state[&"epoch"]) != journal.epoch():
		_lose_pending(state[&"pending"], settles)
		state[&"epoch"] = journal.epoch()
		state[&"last_exported"] = -1
		state[&"pending"] = { }
	_settle_pending(journal, state[&"pending"], settles)
	var closed := journal.last_closed()
	for transition: int in journal.transitions():
		if transition <= int(state[&"last_exported"]):
			continue
		if transition > closed:
			break
		var row := journal.row_at(transition)
		if row.is_empty():
			continue
		rows.append(row)
		state[&"last_exported"] = transition
		if _is_settled(int(row.get(&"flags", 0))):
			settles.append(_settlement(row))
		else:
			state[&"pending"][transition] = true
	var episode_changed := episode_json != String(state[&"episode_json"])
	if rows.is_empty() and settles.is_empty() and not episode_changed:
		return
	var file := _file_for(entity_id)
	if file == null:
		return
	file.store_line(
		JSON.stringify(
			{
				"stats": handle.stats(),
				"rows": rows,
				"settles": settles,
				"episode": episode,
			},
		),
	)
	file.flush()
	state[&"episode_json"] = episode_json


## Closes every open file. Called when the session ends so a reader sees a
## complete flush.
func close() -> void:
	for file: FileAccess in _files.values():
		if file:
			file.close()
	_files.clear()


# Returns the entity's export cursor and pending settlement set.
func _state_for(entity_id: StringName, epoch: int) -> Dictionary:
	var key := String(entity_id)
	if not _states.has(key):
		_states[key] = {
			&"epoch": epoch,
			&"last_exported": -1,
			&"pending": { },
			&"episode_json": "{}",
		}
	return _states[key]


# Names every pending row lost before clearing an old epoch.
func _lose_pending(pending: Dictionary, settles: Array) -> void:
	for transition: int in pending.keys():
		settles.append(
			{
				&"transition": transition,
				&"lost": true,
			},
		)


# Settles retained rows and names pending rows the ring already evicted.
func _settle_pending(
		journal: NetwPredictJournal,
		pending: Dictionary,
		settles: Array,
) -> void:
	for transition: int in pending.keys():
		var row := journal.row_at(transition)
		if row.is_empty():
			settles.append(
				{
					&"transition": transition,
					&"lost": true,
				},
			)
			pending.erase(transition)
		elif _is_settled(int(row.get(&"flags", 0))):
			settles.append(_settlement(row))
			pending.erase(transition)


# Acknowledgement or substitution gives a sealed row its final overlay.
func _is_settled(flags: int) -> bool:
	return bool(
		flags & (
				NetwPredictJournal.ROW_ACKED
				| NetwPredictJournal.ROW_SUBSTITUTED
				| NetwPredictJournal.ROW_SUPERSEDED
		),
	)


# Projects the mutable verdict overlay without repeating sealed evidence.
func _settlement(row: Dictionary) -> Dictionary:
	return {
		&"transition": row.get(&"transition", -1),
		&"flags": row.get(&"flags", 0),
		&"attribution": row.get(&"attribution", 0),
		&"domain": row.get(&"domain", 0),
		&"aligned_error": row.get(&"aligned_error", 0.0),
	}


func _file_for(entity_id: StringName) -> FileAccess:
	var key := String(entity_id)
	if _files.has(key):
		return _files[key]
	var file := FileAccess.open(_dir.path_join("%s.jsonl" % key), FileAccess.WRITE)
	_files[key] = file
	return file
