class_name TeleportPromise
extends RefCounted

## Returned by [method TPComponent.teleport] to observe the completion of a teleport.
##
## Survives the client node's lifetime — safe to await even when the client player
## is destroyed and respawned during the teleport handshake.
signal completed
