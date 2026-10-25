## Logs each step of a Discord activity launch, as the main scene of an export
## run inside Discord.
extends Control

@onready var session: NetwSession = Netw.session(self)
@onready var output: RichTextLabel = $Output


func _ready() -> void:
	session.connection.invited.connect(_on_invited)
	var discord := session.service(DiscordActivityService) as DiscordActivityService
	if discord == null:
		write("Not inside a Discord activity, so nothing registered.")
		return

	write("Activity instance %s" % discord.instance_id)
	var signing_in := discord.sign_in()
	await signing_in.wait()
	if signing_in.is_failed:
		write("[color=red]Sign in failed[/color] %s" % signing_in.detail)
		return
	write(
		"[color=green]Signed in[/color] %s (%d)"
		% [discord.display_name, discord.user_id],
	)


func write(line: String) -> void:
	print(line)
	output.append_text(line + "\n")


func _on_invited(endpoint: NetwEndpoint) -> void:
	write("Invited to %s" % endpoint.address)
	Netw.prepare_join(self, &"Player")
	var joining := session.connection.join(endpoint)
	await joining.wait()
	if joining.is_failed:
		write("[color=red]Join failed[/color] %s" % joining.detail)
		return
	write(
		"[color=green]In the room[/color] as peer %d, server %s"
		% [multiplayer.get_unique_id(), multiplayer.is_server()],
	)
