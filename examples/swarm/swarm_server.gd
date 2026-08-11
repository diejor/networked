## Server-side swarm simulation in plain arrays, with no node for any mob.
##
## This is the whole product claim in one script: the simulation is an ordinary
## [code]for[/code] loop over columns the game owns, and the only thing
## Networked is asked for is identity ([method NetwMultiplayer.claim_routes])
## and delivery ([method NetwMultiplayer.table_commit]).
## [codeblock]
## _physics_process(dt)
## ┠╴ my loop        pos += vel * dt, burning ticks down
## ┠╴ write_routes   the row order
## ┠╴ write_column   one call per column, a reference store
## ┖╴ commit         snapshots, and my arrays are mine again
## [/codeblock]
## Quantization, MTU framing, per-row freshness, tombstones, and the late-join
## heal all happen below [method NetwMultiplayer.table_commit]. Nothing here
## knows about any of them.
extends Node
class_name SwarmServer

## How many mobs the first wave spawns.
@export var wave_size: int = 200

## Metres per second each mob drifts around the ring.
@export var orbit_speed: float = 2.0

## Radius of the ring the wave is seeded on.
@export var ring_radius: float = 24.0

## Whether [method start] drives [method simulate] from
## [method Node._physics_process]. Turn it off to step the simulation yourself,
## which is what a deterministic test does.
@export var self_driven: bool = true

# My storage. Plain arrays, never handed to anyone but commit.
var routes := PackedInt64Array()
var pos := PackedVector3Array()
var vel := PackedVector3Array()
var hp := PackedInt32Array()

var burning_routes := PackedInt64Array()
var burning_dps := PackedFloat32Array()
var burning_left := PackedFloat32Array()

# Fractional damage owed but not yet whole, local only. Health is an integer
# column, so without this a slow burn would truncate to zero every tick and
# never land at all.
var _burning_debt := PackedFloat32Array()

var _mobs: RID
var _burning: RID
var _elapsed: float = 0.0

var api: NetwMultiplayer:
	get:
		return multiplayer as NetwMultiplayer


func _ready() -> void:
	SwarmTables.declare_all()
	set_physics_process(false)


## Binds this simulation to the session's own table handles and seeds the first
## wave. Call once the session is online and this peer is the authority.
func start() -> void:
	if not api.is_server():
		return
	_mobs = api.table_find(SwarmTables.Mobs.NAME)
	_burning = api.table_find(SwarmTables.Burning.NAME)
	spawn_wave(wave_size)
	set_physics_process(self_driven)


## Claims [param count] fresh routes and seeds a ring of mobs on them.
## [br][br][b]Server Only.[/b]
func spawn_wave(count: int) -> void:
	var fresh := api.claim_routes(count)
	var base := routes.size()
	routes.append_array(fresh)
	for i in count:
		var angle := TAU * float(base + i) / float(maxi(1, count))
		pos.append(
			Vector3(cos(angle), 0.0, sin(angle)) * ring_radius,
		)
		vel.append(Vector3(-sin(angle), 0.0, cos(angle)) * orbit_speed)
		hp.append(100)


## Sets [param route] alight, adding it to the [SwarmTables.Burning] table
## without touching [SwarmTables.Mobs] at all.
## [br][br][b]Server Only.[/b]
func ignite(route: int, rate: float, seconds: float) -> void:
	if burning_routes.has(route):
		return
	burning_routes.append(route)
	burning_dps.append(rate)
	burning_left.append(seconds)
	_burning_debt.append(0.0)


func _physics_process(delta: float) -> void:
	simulate(delta)


## Advances the swarm by [param delta] and publishes both tables.
##
## The whole step is the caller's own loop over columns. Networked sees the
## arrays once, at the two [method NetwMultiplayer.table_commit] calls, and
## nothing between them crosses a boundary per row.
## [br][br][b]Server Only.[/b]
func simulate(delta: float) -> void:
	_elapsed += delta
	for i in pos.size():
		pos[i] += vel[i] * delta

	_burn(delta)
	_reap()
	_publish()


# Ticks the effect down and applies its damage through one bulk join, so a
# hundred burning mobs cost one crossing rather than a hundred.
func _burn(delta: float) -> void:
	if burning_routes.is_empty():
		return
	var rows := api.table_get_rows(_mobs, burning_routes)
	for i in range(burning_routes.size() - 1, -1, -1):
		if rows[i] >= 0:
			_burning_debt[i] += burning_dps[i] * delta
			var whole := int(_burning_debt[i])
			if whole > 0:
				_burning_debt[i] -= float(whole)
				hp[rows[i]] = maxi(0, hp[rows[i]] - whole)
		burning_left[i] -= delta
		if burning_left[i] <= 0.0 or rows[i] < 0:
			_swap_remove_burning(i)


# Releases the identities of anything that died, which tombstones them on every
# peer and drops their rows from both tables.
func _reap() -> void:
	var dead := PackedInt64Array()
	for i in range(hp.size() - 1, -1, -1):
		if hp[i] > 0:
			continue
		dead.append(routes[i])
		_swap_remove_mob(i)
	if not dead.is_empty():
		api.release_routes(dead)


func _publish() -> void:
	api.table_write_routes(_mobs, routes)
	api.table_write_column(_mobs, SwarmTables.Mobs.pos, pos)
	api.table_write_column(_mobs, SwarmTables.Mobs.vel, vel)
	api.table_write_column(_mobs, SwarmTables.Mobs.hp, hp)
	api.table_commit(_mobs)

	api.table_write_routes(_burning, burning_routes)
	api.table_write_column(_burning, SwarmTables.Burning.dps, burning_dps)
	api.table_write_column(_burning, SwarmTables.Burning.left, burning_left)
	api.table_commit(_burning)


func _swap_remove_mob(at: int) -> void:
	var last := routes.size() - 1
	routes[at] = routes[last]
	pos[at] = pos[last]
	vel[at] = vel[last]
	hp[at] = hp[last]
	routes.resize(last)
	pos.resize(last)
	vel.resize(last)
	hp.resize(last)


func _swap_remove_burning(at: int) -> void:
	var last := burning_routes.size() - 1
	burning_routes[at] = burning_routes[last]
	burning_dps[at] = burning_dps[last]
	burning_left[at] = burning_left[last]
	_burning_debt[at] = _burning_debt[last]
	burning_routes.resize(last)
	burning_dps.resize(last)
	burning_left.resize(last)
	_burning_debt.resize(last)
