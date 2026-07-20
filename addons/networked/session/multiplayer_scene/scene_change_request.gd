## One player's pending scene-change request, exactly as the wire carried it.
##
## Server authority emits [signal NetwSceneInterface.change_requested] with this
## before a move. It arrives already [member admitted] or not by the default the
## request kind and scene mark imply, and a listener overrides that verdict with
## [method allow] or [method deny]. The object carries only what the request
## sent, so everything else is derived from the session by the listener rather
## than duplicated here: read [member NetwParticipant.current_scene] for the
## origin scene, [method Netw.is_multiplayer_scene] for the mark, and the
## concurrency mode for whole-session intent. That derivability rule is why the
## object never grows.
## [codeblock]
## api.scenes.change_requested.connect(func(rq: SceneChangeRequest) -> void:
##     if match_started and rq.targets(&"Dungeon"):
##         rq.deny())
## [/codeblock]
class_name SceneChangeRequest
extends RefCounted

## The participant that asked.
var participant: NetwParticipant

## The declared destination name, or [code]&""[/code] when the request carried a
## resource path.
var scene_name: StringName

## The normalized [code]res://[/code] destination path, or [code]""[/code] when
## the request carried a declared name.
var scene_path: String

## The typed args the client sent alongside the request.
var args: Array

## Whether the server will admit this request. Starts at the default the request
## kind and scene mark imply, and a listener overrides it with [method allow] or
## [method deny].
var admitted := false


func _init(
		requesting_participant: NetwParticipant,
		requested_name: StringName,
		requested_path: String,
		requested_args: Array,
) -> void:
	participant = requesting_participant
	scene_name = requested_name
	scene_path = requested_path
	args = requested_args


## Admits this request, overriding a deny-default such as a [method targets]
## match the server chooses to allow or a scene held back by
## [method NetwScriptModel.SceneMarkConfig.gated].
func allow() -> void:
	admitted = true


## Vetoes this request, so the server denies the move.
func deny() -> void:
	admitted = false


## Returns [code]true[/code] when this request targets [param scene_ref].
##
## [param scene_ref] matches by declared name, [code]res://[/code] path, or
## [code]uid://[/code] path, so one listener line matches both a named
## [method NetwSceneInterface.request_change] and a path-backed request that
## resolves to the same scene file.
func targets(scene_ref: Variant) -> bool:
	var ref := String(scene_ref)
	if ref.begins_with("res://") or ref.begins_with("uid://"):
		return not scene_path.is_empty() \
				and ResourceUID.ensure_path(ref) == scene_path
	if not scene_name.is_empty() and scene_name == StringName(ref):
		return true
	if not scene_path.is_empty():
		return scene_path.get_file().get_basename() == ref
	return false
