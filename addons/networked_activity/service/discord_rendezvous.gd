## Resolves a Discord [code]instance_id[/code] into a [JoinTarget] the session
## can connect through.
##
## Every participant Discord places in one Activity instance sees the same
## [code]instance_id[/code], available before the SDK is even ready. That id is
## the only shared rendezvous key Discord hands us, so a [DiscordRendezvous]
## turns it into a concrete transport: the first participant claims a room, every
## later participant resolves the same id to that room and joins it. The
## transport behind the room is pluggable, which is why this is an abstract base
## and the game picks a concrete subclass.
## [codeblock]
## instance_id ──► resolve(instance_id, tree) ──► JoinTarget(backend, address)
##                                                      │
##                            MultiplayerTree.join(target, payload)
## [/codeblock]
## [NakamaDiscordRendezvous] maps the id to a relay match through Nakama storage.
## [DedicatedDiscordRendezvous] maps it to a room on a game-owned WSS server with
## no shared store at all.
@abstract
class_name DiscordRendezvous
extends Resource


## Resolves [param instance_id] into a [JoinTarget], deciding host versus join.
##
## A returned target with an empty [member JoinTarget.address] means this
## participant should host a fresh room. A non-empty address means an existing
## room was found and this participant should join it. Implementations await
## network round-trips, so this is asynchronous. The [param tree] is supplied so
## a backend that needs the shared [method MultiplayerTree.get_nakama_session]
## account or other services can reach them. Returns [code]null[/code] when no
## transport can be resolved.
@abstract
func resolve(instance_id: String, tree: MultiplayerTree) -> JoinTarget


## Reconciles the rendezvous record after this participant hosted a room, and
## returns a [JoinTarget] to defer to when a concurrent host won the claim.
##
## Two participants can both find an empty record and both host. This runs right
## after a successful host so the implementation can publish its room, read the
## record back, and detect that another room won. Returning a non-[code]null[/code]
## target tells the caller to tear its own room down and join the winner instead.
## Returning [code]null[/code] (the default, and the only outcome for a backend
## with no shared record) means this participant keeps hosting.
func commit_host(_instance_id: String, _tree: MultiplayerTree) -> JoinTarget:
	return null
