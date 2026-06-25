## Connects a [MultiplayerTree] into the session shared by one Discord Activity
## instance.
##
## Every participant Discord places in one Activity instance sees the same
## [code]instance_id[/code], available before the SDK is even ready. That id is
## the only shared rendezvous key Discord hands us, so a [DiscordRendezvous]
## turns it into a live session: the first participant claims a room and hosts,
## every later participant finds the same id and joins. Crucially, [b]how[/b] that
## happens is the backend's own business, not the service's. A relay races for a
## shared storage record and elects a host client side. A dedicated server keys
## rooms server side and every client just joins. So the whole host-versus-join
## decision lives behind one operation, [method connect_session], and the
## [DiscordActivityService] never learns which path ran.
## [codeblock]
## instance_id ──► connect_session(instance_id, tree, payload) ──► Error
##                       │
##                       ├─ relay:     race a record, host or join the match
##                       └─ dedicated: join wss://host/?instance=<id>
## [/codeblock]
## [NakamaDiscordRendezvous] maps the id to a relay match through Nakama storage.
## [DedicatedDiscordRendezvous] maps it to a room on a game-owned WSS server with
## no shared store at all.
@abstract
class_name DiscordRendezvous
extends Resource


## Wires up any backend-specific seams this rendezvous owns, once the
## [DiscordActivityService] has entered the [param tree].
##
## This is where a backend reaches into the core transport it drives, so the
## service never names a concrete backend. [NakamaDiscordRendezvous] installs its
## [member NakamaWrapper.proxy_base_resolver] here so the relay socket is rewritten
## through Discord's iframe proxy. The default does nothing, which is correct for a
## backend like [DedicatedDiscordRendezvous] that needs no core seam.
## [codeblock]
## DiscordActivityService.service_entered ──► rendezvous.bind(service, tree)
## [/codeblock]
func bind(_service: DiscordActivityService, _tree: MultiplayerTree) -> void:
	pass


## Connects [param tree] into the session keyed by [param instance_id], hosting
## when first and joining otherwise, and returns the resulting [enum Error].
##
## This owns the entire host-or-join decision and any reconciliation a concurrent
## launch needs, so the [DiscordActivityService] only awaits the result. An
## implementation drives [method MultiplayerTree.host_player] or
## [method MultiplayerTree.join] itself, with [param payload] carrying the local
## player. Implementations await network round-trips, so this is asynchronous.
## Returns [constant OK] once the tree is online, or an [enum Error] when no
## session could be reached.
@abstract
func connect_session(
		instance_id: String, tree: MultiplayerTree, payload: JoinPayload,
) -> Error
