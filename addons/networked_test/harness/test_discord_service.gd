## A [DiscordActivityService] that runs outside a browser as if inside the
## activity [member fake_instance_id].
## [codeblock]
## var discord := NetwTestDiscordService.new()
## discord.application_id = 42
## discord.fake_instance_id = "i-1-gc-2-3"
## discord.fake_worker = func(nonce: String) -> String: return sign(nonce)
## tree.add_child(discord)
## [/codeblock]
class_name NetwTestDiscordService
extends DiscordActivityService

## The activity instance this service reports as
## [member DiscordActivityService.instance_id].
var fake_instance_id: String:
	set(value):
		fake_instance_id = value
		instance_id = value

## Stands in for the token Worker. It takes the nonce and returns the token
## the Worker would sign.
var fake_worker: Callable


func _grant(nonce: String) -> NetwPromise:
	return NetwPromise.resolved({ "id_token": fake_worker.call(nonce) })
