## Test double for [DiscordActivityService] that runs the connect path with no
## Discord SDK and an injected instance id.
##
## A real Discord Activity only exists inside the iframe, where the browser
## supplies the instance id and the SDK answers the handshake. This fixture stands
## in for both: set [member fake_instance_id] (the rendezvous key every
## participant shares) and [member fake_device_id] (a per-participant id so two
## instances are distinct backend users), and it creates no SDK so
## [method DiscordActivityService.start] and
## [method DiscordActivityService.authenticate] no-op while the rendezvous still
## drives host-or-join.
## [codeblock]
## var service := NetwTestDiscordService.new()
## service.rendezvous = NakamaDiscordRendezvous.new()
## service.fake_instance_id = "room1"
## service.fake_device_id = "alice"
## tree.add_child(service)
## await service.connect_activity(payload)
## [/codeblock]
class_name NetwTestDiscordService
extends DiscordActivityService

## Instance id this fixture reports, standing in for the browser query string.
var fake_instance_id: String = ""

## Device id this fixture reports, standing in for the authenticated Discord user
## id so two local instances are distinct backend users.
var fake_device_id: String = ""


# No browser SDK in a test, so start()/authenticate() short-circuit.
func _create_sdk() -> DiscordSDK:
	return null


# Inject the instance id the browser would otherwise carry.
func _resolve_instance() -> void:
	_instance_id = fake_instance_id
	_device_id = fake_device_id
