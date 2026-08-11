## The swarm's replicated schema, declared beside the code that publishes it.
##
## Every column index here is a plain [int], so the whole schema lives in
## [code]static var[/code] initializers with no session in sight. Each session
## compiles these declarations into its own handles and user code fetches one
## through [method NetwMultiplayer.table_find].
## [codeblock]
## var mobs := api.table_find(SwarmTables.Mobs.NAME)          # setup, once
## api.table_write_column(mobs, SwarmTables.Mobs.pos, my_positions)  # hot path
## [/codeblock]
## [member SwarmTables.Mobs.pos] is quantized because position is what a link
## spends its bytes on. [member SwarmTables.Mobs.vel] is not, because it takes
## the raw memcpy path and the client only uses it to extrapolate.
##
## [br][br]Touch these classes before the session goes online. A class's static
## initializers run on first access rather than at load, and a wire id is the
## name-sorted position among sealed tables, so both peers must have declared
## the same set before either sends a frame.
class_name SwarmTables
extends RefCounted

## The mob table: one row per swarm member, published every tick.
class Mobs extends RefCounted:
	## The name [method NetwMultiplayer.table_find] answers to.
	const NAME := &"Mob"

	static var table := Netw.configure_schema(NAME)

	## World position, quantized to three centimetres over a 1 km cube.
	static var pos := table.vector3(
		&"pos",
		NetwQuantizeFixed.new().step(0.03).limits(-512.0, 512.0),
	)

	## Velocity, unquantized so it takes the raw memcpy path.
	static var vel := table.vector3(&"vel")

	## Remaining health.
	static var hp := table.u16(&"hp")


## The burning effect: a component table keyed on the same routes as
## [SwarmTables.Mobs], holding only the rows that have it.
##
## A hundred burning mobs cost a hundred rows rather than a masked section
## across two thousand. Joining back to [SwarmTables.Mobs] is one
## [method NetwMultiplayer.table_get_rows] call.
class Burning extends RefCounted:
	## The name [method NetwMultiplayer.table_find] answers to.
	const NAME := &"Burning"

	static var table := Netw.configure_schema(NAME)

	## Damage per second while the effect lasts.
	static var dps := table.f32(&"dps")

	## Seconds of burning left.
	static var left := table.f32(&"left")


## Forces both declarations to compile and registers them, so a session
## adopting the registry finds them however late the classes are first touched.
static func declare_all() -> void:
	Mobs.table.register()
	Burning.table.register()
