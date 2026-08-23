## The typed scene declaration a session registers with [NetwMultiplayer].
##
## Core dispatches on this resource type, not on a manager node class, so the
## same declaration reaches the session whether a [MultiplayerSceneManager]
## authored it or a test rig built it directly. Every row goes in through
## [method declare_scene] or [method declare_spawn_data], which copy what they
## are handed, so nothing this resource holds is a container the author still
## owns and can edit out from under the session. A change made after the
## resource is registered is not lost and is not silent either: it emits
## [signal declaration_changed], and the session republishes the whole
## declaration in one act. An author rebuilding many rows wraps them in
## [method declare], so the session hears one change rather than one per row.
## [codeblock]
## var config := NetwSceneConfig.new()
## config.isolation = NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD
## config.declare_scene(&"Lobby", LOBBY, true)
## config.declare_scene(&"Arena", ARENA)
## config.declare_spawn_data(&"Arena", { "round": 1 })
## api.service_install(config)
##
## # Later, and the session hears it:
## config.declare_scene(&"Annex", ANNEX)
## [/codeblock]
## [member initial_scenes] names the levels brought online at startup, in the
## order they come online. Every other level activates on demand.
class_name NetwSceneConfig
extends NetwObjectConfig

## Emitted when any declared row changes, so a session holding this resource
## republishes rather than reading a value that moved under it.
signal declaration_changed()

var _scenes: Dictionary[StringName, PackedScene] = { }
var _spawn_data: Dictionary[StringName, Variant] = { }
var _initial_scenes: Array[PackedScene] = []
# Nesting depth of declare(), which holds the announcement so a rebuild of many
# rows reaches the session once rather than once per row.
var _authoring_depth := 0

## Isolation the scenes this config declares take when their own recipe names
## none.
##
## Isolation is a per-scene fact, so a scene that declares its own through
## [method NetwScriptModel.SceneMarkConfig.isolated] overrides this. The row
## exists for scenes declared as a bare [PackedScene], which have nowhere else
## to say it.
@export var isolation: NetwMultiplayer.SceneIsolation = \
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE:
	set(value):
		isolation = value
		_announce()

## Scenes activated when the session starts, in the order they come online.
##
## Reads answer a copy and writes replace the list, because a list a caller
## kept a handle on would be a second way to change the declaration, and only
## [signal declaration_changed] announces the first. Pass [code]true[/code] to
## [method declare_scene] to add one startup scene without replacing the list.
@export var initial_scenes: Array[PackedScene]:
	set(value):
		_initial_scenes.assign(value)
		_announce()
	get:
		return _initial_scenes.duplicate()

## Optional custom level constructor. When valid, the session calls it with the
## activation data instead of instantiating a [PackedScene], so a game builds
## its own level root while replication, membership, and the verbs stay
## unchanged. The data it receives is whatever
## [method declare_spawn_data] named for the scene, or the scene name when
## none was declared.
##
## Signature: [code]func(data: Variant) -> Node[/code]
var level_spawn_function: Callable:
	set(value):
		level_spawn_function = value
		_announce()


## Optional node the session parents this declaration's scenes under.
##
## A [MultiplayerSceneManager] names itself here, so its scenes co-locate under
## it and tree order matches on every peer. Left [code]null[/code], as a
## tree-less declaration leaves it, the session anchors them at
## [member NetwMultiplayer.root] instead.
var anchor: Node:
	set(value):
		anchor = value
		_announce()

## Runs [param authoring] against this config with
## [signal declaration_changed] held, and announces once when it returns.
##
## An author that rebuilds every row is making one change, and a session that
## heard each row separately would republish a declaration that was still being
## written. [param authoring] receives this config.
## [codeblock]
## config.declare(func(decl: NetwSceneConfig) -> void:
##     decl.clear_declarations()
##     decl.declare_scene(&"Lobby", LOBBY, true)
##     decl.declare_scene(&"Arena", ARENA)
## )
## [/codeblock]
func declare(authoring: Callable) -> NetwSceneConfig:
	if not authoring.is_valid():
		return self
	_authoring_depth += 1
	authoring.call(self)
	_authoring_depth -= 1
	_announce()
	return self


## Declares that [param scene_name] names [param packed], and returns this
## config so declarations chain.
##
## [param initial] also appends [param packed] to [member initial_scenes], and
## [param spawn_data] is what a [member level_spawn_function] receives for this
## scene. A container passed as [param spawn_data] is copied, so editing the
## original afterwards changes nothing here.
## [codeblock]
## config.declare_scene(&"Lobby", LOBBY, true) \
##       .declare_scene(&"Arena", ARENA, false, { "round": 1 })
## [/codeblock]
func declare_scene(
		scene_name: StringName,
		packed: PackedScene,
		initial: bool = false,
		spawn_data: Variant = null,
) -> NetwSceneConfig:
	if scene_name.is_empty() or packed == null:
		return self
	_scenes[scene_name] = packed
	if spawn_data != null:
		_spawn_data[scene_name] = _detached(spawn_data)
	if initial and not _initial_scenes.has(packed):
		_initial_scenes.append(packed)
	_announce()
	return self


## Declares the data [member level_spawn_function] receives for
## [param scene_name], and returns this config so declarations chain.
##
## Separate from [method declare_scene] because a session whose levels are
## built by [member level_spawn_function] declares names with no
## [PackedScene] behind them. A container passed as [param data] is copied.
func declare_spawn_data(
		scene_name: StringName,
		data: Variant,
) -> NetwSceneConfig:
	if scene_name.is_empty():
		return self
	_spawn_data[scene_name] = _detached(data)
	_announce()
	return self


## Forgets every declared row, including [member initial_scenes]. The scalar
## rows [member isolation] and [member level_spawn_function] survive, because
## they describe the session rather than any one scene.
func clear_declarations() -> void:
	_scenes.clear()
	_spawn_data.clear()
	_initial_scenes.clear()
	_announce()


## Every declared scene name, including a name that
## [method declare_spawn_data] gave data to and no [PackedScene] backs.
func declared_scene_names() -> Array[StringName]:
	var names: Array[StringName] = []
	names.assign(_scenes.keys())
	for scene_name: StringName in _spawn_data:
		if not names.has(scene_name):
			names.append(scene_name)
	return names


## The [PackedScene] declared for [param scene_name], or [code]null[/code].
func declared_scene(scene_name: StringName) -> PackedScene:
	return _scenes.get(scene_name) as PackedScene


## The spawn data declared for [param scene_name], copied, or [param fallback]
## when [method declare_spawn_data] never named it.
func declared_spawn_data(
		scene_name: StringName,
		fallback: Variant = null,
) -> Variant:
	if not _spawn_data.has(scene_name):
		return fallback
	return _detached(_spawn_data[scene_name])


# Announces unless an enclosing declare() is still writing rows.
func _announce() -> void:
	if _authoring_depth == 0:
		declaration_changed.emit()


# A container the author keeps editing would alias the declared row, so the
# session would read a value nobody declared. Everything else is a value.
func _detached(value: Variant) -> Variant:
	if value is Dictionary or value is Array:
		return value.duplicate(true)
	return value
