## Proves a player's Nakama account to a server over ENet or WebRTC, with a
## join token the Nakama server signs.
##
## The server runs [code]addons/networked_nakama/server/networked.js[/code]
## with its private key, the game ships the matching public key, and a signed
## in [NakamaAccount] fetches the tokens.
## [codeblock]
## const JOIN_KEY := "res://join_token.pub"
##
## func _init() -> void:
##     Netw.configure_admission(admit).accept(NakamaIdToken.new(JOIN_KEY))
##
## func admit(request: NetwJoinRequest) -> Error:
##     return OK if request.account else ERR_UNAUTHORIZED
## [/codeblock]
class_name NakamaIdToken
extends NetwIdToken

## The issuer the server module signs every join token with.
const ISSUER := "networked-nakama"

## The audience the server module signs for when
## [code]NETWORKED_JOIN_AUDIENCE[/code] is unset. Games sharing one Nakama
## server each set their own.
const AUDIENCE := "networked"


func _init(public_key_path: String, token_audience := AUDIENCE) -> void:
	key = load(public_key_path)
	issuer = ISSUER
	audience = token_audience
	prefix = "nakama"
