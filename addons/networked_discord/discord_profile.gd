## A Discord user's name, avatar, status and game, as the signed in player
## sees them, from [method DiscordSocial.profile].
##
## Every property reads what Discord knows now, and [signal changed] tells
## when to read again. The same object stands for the same user, so a friend
## row keeps one and redraws on [signal changed].
## [codeblock]
## func show_friend(id: int) -> void:
##     friend = discord.profile(id)
##     friend.changed.connect(_on_friend_changed)
##     _on_friend_changed()
##
## func _on_friend_changed() -> void:
##     name_label.text = friend.display_name
##     avatar_rect.texture = friend.avatar
## [/codeblock]
class_name DiscordProfile
extends RefCounted

## Emitted when any property may have changed, including when
## [member avatar] has downloaded and when the player signs in or out.
signal changed

## The values [member status] takes.
enum Status {
	## Offline, or appearing offline.
	OFFLINE,
	## Online.
	ONLINE,
	## Online and away.
	IDLE,
	## Online with notifications muted.
	DO_NOT_DISTURB,
}

## The Discord user ID.
var id: int:
	get:
		return _id

## The Discord display name, empty while signed out or while Discord does
## not know the user.
var display_name: String:
	get:
		var user := _user
		return user.display_name() if user != null else ""

## The Discord avatar, [code]null[/code] while signed out, until it has
## downloaded, and in a headless run.
var avatar: Texture2D:
	get:
		return _avatar if _social.user_id != 0 else null

## The online status Discord shows.
var status: Status:
	get:
		var user := _user
		if user == null:
			return Status.OFFLINE
		match user.status():
			DiscordStatusType.ONLINE, DiscordStatusType.STREAMING:
				return Status.ONLINE
			DiscordStatusType.IDLE:
				return Status.IDLE
			DiscordStatusType.DND:
				return Status.DO_NOT_DISTURB
		return Status.OFFLINE

## [code]true[/code] while the user plays this game. For the signed in
## player it is [code]true[/code] while Discord shows them in a session.
var is_playing: bool:
	get:
		if _id != 0 and _id == _social.user_id:
			return _playing_since > 0.0
		return _activity != null

## How many people are in the user's party in this game, or [code]0[/code].
var party_size: int:
	get:
		var activity := _activity
		var party := activity.party() if activity != null else null
		return party.current_size() if party != null else 0

## The most people the user's party in this game takes, or [code]0[/code].
var party_max: int:
	get:
		var activity := _activity
		var party := activity.party() if activity != null else null
		return party.max_size() if party != null else 0

## When the user started playing this game, in Unix seconds, or
## [code]0[/code]. For the signed in player it is when their session
## started.
var playing_since: float:
	get:
		if _id != 0 and _id == _social.user_id:
			return _playing_since
		var activity := _activity
		var times := activity.timestamps() if activity != null else null
		return times.start() / 1000.0 if times != null else 0.0

var _social: DiscordSocial
var _id: int
var _avatar: Texture2D
var _playing_since: float
var _user: DiscordUserHandle:
	get:
		return _social.client.get_user(_id) if _social.user_id != 0 else null
var _activity: DiscordActivity:
	get:
		var user := _user
		return user.game_activity() if user != null else null


func _init(social: DiscordSocial, user_id: int) -> void:
	_social = social
	_id = user_id
	social.friends_changed.connect(changed.emit)
