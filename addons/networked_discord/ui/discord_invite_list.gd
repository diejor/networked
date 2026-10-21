extends VBoxContainer

const InviteCard := preload(
	"res://addons/networked_discord/ui/discord_invite_card.gd"
)
const INVITE_CARD := preload(
	"res://addons/networked_discord/ui/discord_invite_card.tscn"
)

var discord: DiscordSocial:
	set(value):
		discord = value
		discord.party.invited.connect(_on_party_invited.bind(false))
		discord.party.join_requested.connect(_on_party_invited.bind(true))
		for friend: int in discord.party.invitations:
			_on_party_invited(friend, false)
		for friend: int in discord.party.join_requests:
			_on_party_invited(friend, true)


func _on_party_invited(friend: int, is_join_request: bool) -> void:
	for card: InviteCard in get_children():
		if card.sender.id == friend and card.is_join_request == is_join_request:
			card.queue_free()

	var card: InviteCard = INVITE_CARD.instantiate()
	card.party = discord.party
	card.sender = discord.profile(friend)
	card.is_join_request = is_join_request
	add_child(card)
