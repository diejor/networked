extends "res://examples/bomber/game/bomber_session.gd"

const DISCORD_APP := 1519144648411447338


func _init() -> void:
	Netw.configure_admission(admit).accept(
		DiscordIdToken.signed_by(
			DISCORD_APP,
			"res://examples/bomber/activity_token.pub",
		),
	)


func _ready() -> void:
	$LobbyProviders/WebTorrentLobbyDirectory.free()


func admit(request: NetwJoinRequest) -> Error:
	return OK if request.account.begins_with("discord:") else ERR_UNAUTHORIZED
