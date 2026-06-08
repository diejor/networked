class_name BomberAI
extends RefCounted
## Test-only AI driver for the bomber example game.
##
## Reads the world scene tree each tick, runs a pluggable [Goal]
## strategy, and presses or releases actions on a [NetwSceneRunner].
## A universal flee override dodges active bombs regardless of goal.
## [br][br]
## [codeblock]
## var ai := BomberAI.create(runner, &"valeria")
## ai.goal = BomberAI.Goal.score()
## ai.tick()
## [/codeblock]

const TILE_SIZE := 48
const BLAST_RANGE := 3
const ALIGN_RADIUS := 10.0

enum State { IDLE, PURSUING, FLEEING, STUNNED }

## Current high-level state for test assertions.
var state: State = State.IDLE

## The active goal. Assign to change behavior mid-test.
var goal: Goal = Goal.idle()

## Whether the flee override is active (default true).
var flee_enabled: bool = true

var _runner: NetwSceneRunner
var _player_name: StringName
var _waypoint_cell: Variant = null
var _prev_dir := Vector2i.ZERO
var _prev_bomb := false


## Creates an AI that will drive [param runner]'s player named
## [param player_name].
static func create(
		runner: NetwSceneRunner,
		player_name: StringName,
) -> BomberAI:
	var ai := BomberAI.new()
	ai._runner = runner
	ai._player_name = player_name
	return ai


## Call once per tick batch to update the AI's decisions.
func tick() -> void:
	var snap := _scan_world()
	if snap == null:
		_apply_actions(Vector2i.ZERO, false)
		state = State.IDLE
		return

	if snap.my_stunned:
		_apply_actions(Vector2i.ZERO, false)
		state = State.STUNNED
		return

	var grid_dir := Vector2i.ZERO
	var bomb := false
	var force_waypoint := false

	# Flee override.
	var flee_target: Variant = null
	if flee_enabled:
		flee_target = _flee_target(snap)

	if flee_target != null:
		grid_dir = _step_toward(snap, flee_target)
		bomb = false
		force_waypoint = true
		state = State.FLEEING
	else:
		var target: Variant = goal._target_cell(snap)
		if target != null:
			grid_dir = _step_toward(snap, target)
			state = State.PURSUING
		else:
			grid_dir = Vector2i.ZERO
			state = State.IDLE
		bomb = goal._wants_bomb(snap)

	var direction := _get_direction_for_step(
		snap,
		grid_dir,
		force_waypoint,
	)
	_apply_actions(direction, bomb)

# -- Perception ---------------------------------------------------------------


class WorldSnapshot:
	extends RefCounted

	var my_position := Vector2.ZERO
	var my_cell := Vector2i.ZERO
	var my_stunned := false
	var rock_cells: Array[Vector2i] = []
	var bomb_cells: Array[Vector2i] = []
	var player_cells: Dictionary = { }
	var wall_set: Dictionary = { }
	var grid_bounds := Rect2i()


func _scan_world() -> WorldSnapshot:
	var world := _find_world()
	if world == null:
		return null

	var player := _runner.find_player(_player_name) as Node2D
	if player == null:
		return null

	var snap := WorldSnapshot.new()
	snap.my_position = player.position
	snap.my_cell = _to_cell(player.position)
	snap.my_stunned = player.get("stunned") as bool

	var level: Node = world.level

	# Walls from TileMapLayer.
	var tilemap := level.get_node_or_null("Layer0") as TileMapLayer
	if tilemap:
		for cell: Vector2i in tilemap.get_used_cells():
			var atlas := tilemap.get_cell_atlas_coords(cell)
			if atlas == Vector2i(0, 0):
				snap.wall_set[cell] = true
		snap.grid_bounds = tilemap.get_used_rect()

	# Rocks.
	var rocks := level.get_node_or_null("Rocks")
	if rocks:
		for rock: Node in rocks.get_children():
			if is_instance_valid(rock) and rock is Node2D:
				snap.rock_cells.append(_to_cell(rock.position))

	# Bombs (Area2D children of level).
	for child: Node in level.get_children():
		if child is Area2D:
			snap.bomb_cells.append(_to_cell((child as Node2D).position))

	# Other players.
	var players := level.get_node_or_null("Players")
	if players:
		for p: Node in players.get_children():
			if p is Node2D:
				var p_name := _extract_player_name(p)
				if p_name != _player_name:
					snap.player_cells[p_name] = _to_cell(p.position)

	return snap


func _find_world() -> MultiplayerScene:
	if not _runner or not _runner.tree:
		return null
	var sm := _runner.tree.get_service(MultiplayerSceneManager)
	if not sm:
		return null
	return sm.active_scenes.get(&"World") as MultiplayerScene


func _to_cell(pos: Vector2) -> Vector2i:
	return Vector2i(
		int(round(pos.x / TILE_SIZE - 0.5)),
		int(round(pos.y / TILE_SIZE - 0.5)),
	)


func _cell_center(cell: Vector2i) -> Vector2:
	return Vector2(
		cell.x * TILE_SIZE + TILE_SIZE * 0.5,
		cell.y * TILE_SIZE + TILE_SIZE * 0.5,
	)


func _extract_player_name(node: Node) -> StringName:
	var entity := NetwEntity.of(node)
	if entity:
		return entity.entity_id
	return StringName(NetwEntity.parse_entity(node.name))

# -- Strategy -----------------------------------------------------------------


func _flee_target(snap: WorldSnapshot) -> Variant:
	if snap.bomb_cells.is_empty():
		return null

	var dominated := _blast_cells(snap)
	if not dominated.has(snap.my_cell):
		return null

	# BFS to the nearest safe cell.
	return _bfs_to_safe(snap, dominated)


func _blast_cells(snap: WorldSnapshot) -> Dictionary:
	var cells := { }
	for bomb_cell: Vector2i in snap.bomb_cells:
		cells[bomb_cell] = true
		for dir: Vector2i in [
			Vector2i.UP,
			Vector2i.DOWN,
			Vector2i.LEFT,
			Vector2i.RIGHT,
		]:
			for dist in range(1, BLAST_RANGE + 1):
				var c := bomb_cell + dir * dist
				if snap.wall_set.has(c):
					break
				cells[c] = true
	return cells


func _bfs_to_safe(
		snap: WorldSnapshot,
		danger: Dictionary,
) -> Variant:
	var visited := { }
	var queue: Array[Array] = [[snap.my_cell, Vector2i.ZERO]]
	visited[snap.my_cell] = true

	while not queue.is_empty():
		var entry: Array = queue.pop_front()
		var cell: Vector2i = entry[0]
		var first_step: Vector2i = entry[1]

		if not danger.has(cell) and cell != snap.my_cell:
			return cell

		for dir: Vector2i in [
			Vector2i.UP,
			Vector2i.DOWN,
			Vector2i.LEFT,
			Vector2i.RIGHT,
		]:
			var next := cell + dir
			if visited.has(next):
				continue
			if _is_blocked(snap, next):
				continue
			visited[next] = true
			var step: Vector2i = first_step if first_step != Vector2i.ZERO \
			else dir
			queue.append([next, step])

	return null


func _step_toward(snap: WorldSnapshot, target: Vector2i) -> Vector2i:
	var target_blocked := _is_blocked(snap, target)
	if target == snap.my_cell or (target_blocked and \
					absi(snap.my_cell.x - target.x) + absi(snap.my_cell.y - target.y) <= 1):
		return Vector2i.ZERO

	# BFS from my_cell to target, return first step direction.
	var visited := { }
	# Each entry: [cell, first_direction]
	var queue: Array[Array] = [[snap.my_cell, Vector2i.ZERO]]
	visited[snap.my_cell] = true

	while not queue.is_empty():
		var entry: Array = queue.pop_front()
		var cell: Vector2i = entry[0]
		var first_dir: Vector2i = entry[1]

		if cell == target or (target_blocked and \
						absi(cell.x - target.x) + absi(cell.y - target.y) <= 1):
			return first_dir

		for dir: Vector2i in [
			Vector2i.UP,
			Vector2i.DOWN,
			Vector2i.LEFT,
			Vector2i.RIGHT,
		]:
			var next := cell + dir
			if visited.has(next):
				continue
			if _is_blocked(snap, next):
				continue
			visited[next] = true
			var step: Vector2i = first_dir if first_dir != Vector2i.ZERO \
			else dir
			queue.append([next, step])

	return _best_open_step_toward(snap, target)


func _best_open_step_toward(
		snap: WorldSnapshot,
		target: Vector2i,
) -> Vector2i:
	var best_step := Vector2i.ZERO
	var best_dist := 999999
	for dir: Vector2i in [
		Vector2i.UP,
		Vector2i.DOWN,
		Vector2i.LEFT,
		Vector2i.RIGHT,
	]:
		var next := snap.my_cell + dir
		if _is_blocked(snap, next):
			continue
		var dist := absi(next.x - target.x) + absi(next.y - target.y)
		if dist < best_dist:
			best_dist = dist
			best_step = dir
	return best_step


func _is_blocked(snap: WorldSnapshot, cell: Vector2i) -> bool:
	if snap.wall_set.has(cell):
		return true
	for rock_cell: Vector2i in snap.rock_cells:
		if rock_cell == cell:
			return true
	for player_cell: Vector2i in snap.player_cells.values():
		if player_cell == cell:
			return true
	return false


func _get_direction_for_step(
		snap: WorldSnapshot,
		grid_dir: Vector2i,
		force_waypoint: bool,
) -> Vector2i:
	if grid_dir == Vector2i.ZERO:
		_waypoint_cell = null
		return Vector2i.ZERO

	var next_cell := snap.my_cell + grid_dir
	var needs_waypoint := force_waypoint or _waypoint_cell == null
	if not needs_waypoint:
		var current_waypoint: Vector2i = _waypoint_cell
		needs_waypoint = _arrived_at_cell(snap, current_waypoint) \
				or _is_blocked(snap, current_waypoint)

	if needs_waypoint:
		_waypoint_cell = next_cell

	var waypoint: Vector2i = _waypoint_cell
	return _get_steering_direction(snap, waypoint)


func _arrived_at_cell(snap: WorldSnapshot, cell: Vector2i) -> bool:
	return (_cell_center(cell) - snap.my_position).length() <= ALIGN_RADIUS


func _get_steering_direction(
		snap: WorldSnapshot,
		next_cell: Vector2i,
) -> Vector2i:
	var target_pos := _cell_center(next_cell)
	var offset := target_pos - snap.my_position
	var my_cell := snap.my_cell

	if next_cell.x != my_cell.x:
		# Moving horizontally. Center vertically first.
		if abs(offset.y) > ALIGN_RADIUS:
			return Vector2i(0, _sign(offset.y))
		# Once centered vertically, move horizontally.
		if abs(offset.x) > ALIGN_RADIUS:
			return Vector2i(_sign(offset.x), 0)
	elif next_cell.y != my_cell.y:
		# Moving vertically. Center horizontally first.
		if abs(offset.x) > ALIGN_RADIUS:
			return Vector2i(_sign(offset.x), 0)
		# Once centered horizontally, move vertically.
		if abs(offset.y) > ALIGN_RADIUS:
			return Vector2i(0, _sign(offset.y))
	else:
		if offset.length() <= ALIGN_RADIUS:
			return Vector2i.ZERO
		if abs(offset.x) >= abs(offset.y):
			return Vector2i(_sign(offset.x), 0)
		return Vector2i(0, _sign(offset.y))

	return Vector2i.ZERO


func _sign(val: float) -> int:
	if val < 0.0:
		return -1
	elif val > 0.0:
		return 1
	return 0

# -- Actuation ----------------------------------------------------------------


func _apply_actions(direction: Vector2i, bomb: bool) -> void:
	if not is_instance_valid(_runner):
		return

	# Release previous directions.
	if _prev_dir.x < 0 and direction.x >= 0:
		_runner.simulate_action_release("move_left")
	if _prev_dir.x > 0 and direction.x <= 0:
		_runner.simulate_action_release("move_right")
	if _prev_dir.y < 0 and direction.y >= 0:
		_runner.simulate_action_release("move_up")
	if _prev_dir.y > 0 and direction.y <= 0:
		_runner.simulate_action_release("move_down")

	# Press desired directions.
	if direction.x < 0 and _prev_dir.x >= 0:
		_runner.simulate_action_press("move_left")
	if direction.x > 0 and _prev_dir.x <= 0:
		_runner.simulate_action_press("move_right")
	if direction.y < 0 and _prev_dir.y >= 0:
		_runner.simulate_action_press("move_up")
	if direction.y > 0 and _prev_dir.y <= 0:
		_runner.simulate_action_press("move_down")

	# Bomb.
	if bomb and not _prev_bomb:
		_runner.simulate_action_press("set_bomb")
	elif not bomb and _prev_bomb:
		_runner.simulate_action_release("set_bomb")

	_prev_dir = direction
	_prev_bomb = bomb

# -- Goals --------------------------------------------------------------------


class Goal:
	extends RefCounted

	## Returns the grid cell the AI should move toward, or null.
	func _target_cell(_snap: WorldSnapshot) -> Variant:
		return null


	## Returns true if the AI should place a bomb now.
	func _wants_bomb(_snap: WorldSnapshot) -> bool:
		return false

	# Factories.


	## Do nothing.
	static func idle() -> Goal:
		return Goal.new()


	## Move toward and bomb the nearest rock.
	static func score() -> Goal:
		return ScoreGoal.new()


	## Chase a specific player and bomb when adjacent.
	static func hunt(target_name: StringName) -> Goal:
		var g := HuntGoal.new()
		g._target = target_name
		return g


	## Follow a player without bombing.
	static func follow(target_name: StringName) -> Goal:
		var g := FollowGoal.new()
		g._target = target_name
		return g


	## Wander to random cells, occasionally bombing.
	static func wander(rng: RandomNumberGenerator = null) -> Goal:
		var g := WanderGoal.new()
		g._rng = rng if rng else RandomNumberGenerator.new()
		return g


	## Cycle through random goals.
	static func random(rng: RandomNumberGenerator = null) -> Goal:
		var g := RandomGoal.new()
		g._rng = rng if rng else RandomNumberGenerator.new()
		return g


	## Stay put, but flee when threatened.
	static func flee_only() -> Goal:
		return Goal.new()


class ScoreGoal:
	extends Goal

	func _target_cell(snap: WorldSnapshot) -> Variant:
		if snap.rock_cells.is_empty():
			return null
		var best: Vector2i = snap.rock_cells[0]
		var best_dist := _manhattan(snap.my_cell, best)
		for rock: Vector2i in snap.rock_cells:
			var d := _manhattan(snap.my_cell, rock)
			if d < best_dist:
				best_dist = d
				best = rock
		return best


	func _wants_bomb(snap: WorldSnapshot) -> bool:
		for rock: Vector2i in snap.rock_cells:
			if _manhattan(snap.my_cell, rock) <= 1:
				return true
		return false


	func _manhattan(a: Vector2i, b: Vector2i) -> int:
		return absi(a.x - b.x) + absi(a.y - b.y)


class HuntGoal:
	extends Goal

	var _target: StringName


	func _target_cell(snap: WorldSnapshot) -> Variant:
		if snap.player_cells.has(_target):
			return snap.player_cells[_target]
		return null


	func _wants_bomb(snap: WorldSnapshot) -> bool:
		if snap.player_cells.has(_target):
			var t: Vector2i = snap.player_cells[_target]
			return _manhattan(snap.my_cell, t) <= 1
		return false


	func _manhattan(a: Vector2i, b: Vector2i) -> int:
		return absi(a.x - b.x) + absi(a.y - b.y)


class FollowGoal:
	extends Goal

	var _target: StringName


	func _target_cell(snap: WorldSnapshot) -> Variant:
		if snap.player_cells.has(_target):
			return snap.player_cells[_target]
		return null


class WanderGoal:
	extends Goal

	var _rng: RandomNumberGenerator
	var _wander_target: Variant = null
	var _bomb_cooldown := 0


	func _target_cell(snap: WorldSnapshot) -> Variant:
		if _wander_target == null or _arrived_at_wander_target(snap):
			_wander_target = _pick_random_cell(snap)
		return _wander_target


	func _wants_bomb(_snap: WorldSnapshot) -> bool:
		_bomb_cooldown -= 1
		if _bomb_cooldown > 0:
			return false
		if _rng.randf() < 0.05:
			_bomb_cooldown = 20
			return true
		return false


	func _pick_random_cell(snap: WorldSnapshot) -> Vector2i:
		var bounds := snap.grid_bounds
		for attempt in 20:
			var cell := Vector2i(
				_rng.randi_range(bounds.position.x, bounds.end.x - 1),
				_rng.randi_range(bounds.position.y, bounds.end.y - 1),
			)
			if not _cell_is_blocked(snap, cell):
				return cell
		return snap.my_cell


	func _cell_is_blocked(snap: WorldSnapshot, cell: Vector2i) -> bool:
		if snap.wall_set.has(cell):
			return true
		for rock_cell: Vector2i in snap.rock_cells:
			if rock_cell == cell:
				return true
		for player_cell: Vector2i in snap.player_cells.values():
			if player_cell == cell:
				return true
		return false


	func _arrived_at_wander_target(snap: WorldSnapshot) -> bool:
		return snap.my_cell == _wander_target


class RandomGoal:
	extends Goal

	var _rng: RandomNumberGenerator
	var _current: Goal = null
	var _ticks_left := 0


	func _target_cell(snap: WorldSnapshot) -> Variant:
		_maybe_switch(snap)
		return _current._target_cell(snap)


	func _wants_bomb(snap: WorldSnapshot) -> bool:
		return _current._wants_bomb(snap)


	func _maybe_switch(snap: WorldSnapshot) -> void:
		_ticks_left -= 1
		if _ticks_left > 0 and _current != null:
			return
		_ticks_left = _rng.randi_range(30, 80)
		var options: Array[Goal] = [
			Goal.score(),
			Goal.wander(_rng),
		]
		# Add hunt goals for any visible players.
		for pname: StringName in snap.player_cells:
			options.append(Goal.hunt(pname))
		_current = options[_rng.randi_range(0, options.size() - 1)]
