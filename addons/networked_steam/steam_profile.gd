## A Steam user's name, avatar and game, as the player sees them, from
## [method SteamSocial.profile].
##
## Every property reads what Steam knows now, and [signal changed] tells when
## to read again. The same object stands for the same user, so a friend row
## keeps one and redraws on [signal changed].
## [codeblock]
## func show_friend(id: int) -> void:
##     friend = steam.profile(id)
##     friend.changed.connect(_on_friend_changed)
##     _on_friend_changed()
##
## func _on_friend_changed() -> void:
##     name_label.text = friend.display_name
##     avatar_rect.texture = friend.avatar
## [/codeblock]
class_name SteamProfile
extends RefCounted

## Emitted when any property may have changed, including when
## [member avatar] has loaded.
signal changed

## The Steam ID.
var id: int:
	get:
		return _id

## The Steam display name, empty while Steam does not know the user.
var display_name: String:
	get:
		return Steam.getFriendPersonaName(_id)

## The medium Steam avatar, [code]null[/code] until it has loaded.
var avatar: Texture2D:
	get:
		return _avatar

## [code]true[/code] while the user plays this game. For the player it is
## [code]true[/code] while they are in a session.
var is_playing: bool:
	get:
		if _id == Steam.getSteamID():
			var session := Netw.session(_social)
			return session != null and session.local_player != null
		return Steam.getFriendGamePlayed(_id).get("id", 0) == Steam.getAppID()

var _social: SteamSocial
var _id: int
var _avatar: Texture2D


func _init(social: SteamSocial, steam_id: int) -> void:
	_social = social
	_id = steam_id
