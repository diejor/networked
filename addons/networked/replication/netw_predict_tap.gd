extends RefCounted
## Env-gated JSONL drain of the public prediction read surface, the instrument
## that turns a manual session into a capture.
##
## The tap touches only
## [method NetwPredictionHandle.journal],
## [member NetwPredictionHandle.stats], and
## [method NetwPredictionHandle.episode], so a live
## play session leaves the same evidence a scripted capture does. Reading is
## never a step, so the drain cannot move recorded state. What a drain CAN do
## is cost wall time, and an instrument whose cost scales with what it reports
## is disqualified by the campaign's own law, so the tap meters itself:
## [method cost] reports bytes, lines, and drain time, and [method close]
## prints them. It is an internal currency with no [code]class_name[/code]:
## the interface preloads it and drives it from its frame boundary only when
## [code]NETW_PREDICT_TAP[/code] names an output directory.
## [codeblock]
## NETW_PREDICT_TAP=/tmp/tap godot ...
##   -> /tmp/tap/<entity_id>.jsonl
##      one {stats, rows, settles, episode} line per changed drain
##      a second handle sharing the entity id writes <entity_id>~2.jsonl
## [/codeblock]
## Reader contract, chosen so a line costs what it changed rather than what
## the handle holds:
## [codeblock]
## stats    only the keys that changed since the last line. Carry the rest
##          forward. The first line for a handle is complete.
## rows     newly sealed journal rows, never repeated.
## settles  verdict overlays for earlier rows, or {transition, lost = true}.
## episode  the changed report, or {} while the last one stands.
## [/codeblock]
## Aggregate stats (the histograms and island membership) are cumulative, so
## they ride every [constant AGGREGATE_PERIOD]th line rather than every line.
## Writes are buffered and flushed at most once per second, so a hard kill
## can lose the last interval. Close the session normally, or call
## [method flush] before reading mid-run.

const _ENV := "NETW_PREDICT_TAP"

## Drains between full aggregate-stats snapshots, about once per second at a
## 60 Hz drain cadence. Cumulative values lose nothing between snapshots.
const AGGREGATE_PERIOD := 60

# Stats keys that are cumulative containers rather than scalars. Shipping them
# per line would rewrite hundreds of slowly-growing bytes every drain, which is
# the cost profile that disqualified the previous tap.
const _AGGREGATE_KEYS: Array[StringName] = [
	&"arrivals",
	&"replay_depth",
	&"consume_shape",
	&"island_members",
	&"simulated_members",
]

# Buffered writes are flushed at most this often, replacing the per-line flush
# that forced a disk write per frame and starved the host that carried it.
const _FLUSH_INTERVAL_USEC := 1_000_000

# The directory each entity's JSONL file is written under.
var _dir: String

# Handle instance id (int) -> the open append handle for its file. Keyed by
# instance, never by entity id: two entities may share a display-derived id,
# and a shared cursor would re-export whole journal rings every drain.
var _files: Dictionary = { }

# Handle instance id (int) -> {epoch, last_exported, pending, episode_json,
# last_scalars, lines}. Pending rows receive a later settlement overlay or an
# explicit loss record if the ring evicts them.
var _states: Dictionary = { }

# Claimed file basename (String) -> handle instance id. A later handle with
# an already-claimed entity id writes to "<id>~2.jsonl" instead.
var _names: Dictionary = { }

# Self-metering, so arming the tap can never again masquerade as a game
# defect: the instrument names its own cost instead of leaving it to be
# rediscovered as a starved peer.
var _bytes_written: int = 0
var _lines_written: int = 0
var _drains: int = 0
var _drain_usec: int = 0
var _last_flush_usec: int = 0


## True when the environment names an output directory, so the interface knows
## to build and drive a tap at all.
static func armed() -> bool:
	return not OS.get_environment(_ENV).is_empty()


func _init(dir: String = "") -> void:
	_dir = dir if not dir.is_empty() else OS.get_environment(_ENV)
	if not _dir.is_empty():
		DirAccess.make_dir_recursive_absolute(_dir)


## Appends [param handle]'s changed counters, newly sealed rows, settlement
## overlays, and changed episode report to [param entity_id]'s JSONL file. A
## sealed row evicted before its verdict emits a
## [code]{transition, lost = true}[/code] settlement. Reads only the public
## prediction surface, so it never moves recorded state.
func drain(entity_id: StringName, handle: Variant) -> void:
	if _dir.is_empty() or handle == null:
		return
	var journal: NetwPredictJournal = handle.journal()
	if journal == null:
		return
	var began := Time.get_ticks_usec()
	_drains += 1
	var state := _state_for(handle, journal.epoch())
	var rows: Array = []
	var settles: Array = []
	# The episode retains one entry per comparison it has settled, so a
	# long-lived one is tens of kilobytes. Detaching it to detect a change would
	# copy that much every drain, and a latched episode is both the largest and
	# the least likely to change, so the digest answers the question first and
	# the report is detached only once it says yes.
	var episode_stamp := _episode_stamp(handle.episode_digest())
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
	var episode_changed := episode_stamp != String(state[&"episode_stamp"])
	if rows.is_empty() and settles.is_empty() and not episode_changed:
		_drain_usec += Time.get_ticks_usec() - began
		return
	var file := _file_for(entity_id, handle)
	if file == null:
		_drain_usec += Time.get_ticks_usec() - began
		return
	var line := JSON.stringify(
		{
			"stats": _stats_delta(state, handle.stats.to_dictionary()),
			"rows": rows,
			"settles": settles,
			"episode": handle.episode() if episode_changed else { },
		},
	)
	file.store_line(line)
	_bytes_written += line.length() + 1
	_lines_written += 1
	state[&"lines"] = int(state[&"lines"]) + 1
	state[&"episode_stamp"] = episode_stamp
	var now := Time.get_ticks_usec()
	if now - _last_flush_usec >= _FLUSH_INTERVAL_USEC:
		flush()
	_drain_usec += now - began


## Flushes every open file's buffered lines to disk, bounding what a crash can
## lose to nothing. The drain does this once per second on its own.
func flush() -> void:
	for file: FileAccess in _files.values():
		if file:
			file.flush()
	_last_flush_usec = Time.get_ticks_usec()


## Returns what the tap has cost the run it records, so a capture harness can
## echo the instrument's own price beside the numbers it produced.
## [codeblock]
## {
##  ┠╴bytes (int)             JSONL bytes written across all files
##  ┠╴lines (int)             lines written across all files
##  ┠╴drains (int)            drain calls, written or not
##  ┖╴mean_drain_usec (float) wall cost of one drain call
## }
## [/codeblock]
func cost() -> Dictionary:
	return {
		&"bytes": _bytes_written,
		&"lines": _lines_written,
		&"drains": _drains,
		&"mean_drain_usec": float(_drain_usec) / _drains if _drains > 0 else 0.0,
	}


## Closes every open file and prints the tap's self-reported cost. Called when
## the session ends so a reader sees a complete flush.
func close() -> void:
	for file: FileAccess in _files.values():
		if file:
			file.close()
	_files.clear()
	if _drains > 0:
		var report := cost()
		print(
			"[tap] wrote %d bytes over %d lines, %d drains, %.1f us mean" % [
				report[&"bytes"],
				report[&"lines"],
				report[&"drains"],
				report[&"mean_drain_usec"],
			],
		)


# The changed subset of scalar stats, with the cumulative aggregates riding
# every AGGREGATE_PERIODth line. The first line ships everything.
func _stats_delta(state: Dictionary, stats: Dictionary) -> Dictionary:
	var last: Dictionary = state[&"last_scalars"]
	var aggregates_due := int(state[&"lines"]) % AGGREGATE_PERIOD == 0
	var out := { }
	for key: StringName in stats:
		if key in _AGGREGATE_KEYS:
			if aggregates_due:
				out[key] = stats[key]
			continue
		if not last.has(key) or last[key] != stats[key]:
			out[key] = stats[key]
			last[key] = stats[key]
	return out


# Returns the handle's export cursor and pending settlement set.
func _state_for(handle: Variant, epoch: int) -> Dictionary:
	var key: int = handle.get_instance_id()
	if not _states.has(key):
		_states[key] = {
			&"epoch": epoch,
			&"last_exported": -1,
			&"pending": { },
			&"episode_stamp": "",
			&"last_scalars": { },
			&"lines": 0,
		}
	return _states[key]


# A cheap identity for an episode report. Every field it names only ever
# advances while the episode lives, so equal stamps mean nothing was appended
# and the report can be left out of this drain's line.
func _episode_stamp(digest: Dictionary) -> String:
	if digest.is_empty():
		return ""
	return "%s:%s" % [
		digest.get(&"id", -1),
		digest.get(&"revision", -1),
	]


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


func _file_for(entity_id: StringName, handle: Variant) -> FileAccess:
	var key: int = handle.get_instance_id()
	if _files.has(key):
		return _files[key]
	var basename := String(entity_id)
	var claimed := basename
	var ordinal := 2
	while _names.has(claimed) and int(_names[claimed]) != key:
		claimed = "%s~%d" % [basename, ordinal]
		ordinal += 1
	_names[claimed] = key
	var file := FileAccess.open(
		_dir.path_join("%s.jsonl" % claimed),
		FileAccess.WRITE,
	)
	_files[key] = file
	return file
