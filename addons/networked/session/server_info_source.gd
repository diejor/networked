## Abstract resource that builds a [ServerInfo] in response to a probe.
##
## Assign a subclass to [member MultiplayerTree.server_info_source] to control
## what server-browser-style clients see. The default,
## [DefaultServerInfoSource], reports live player counts derived from the
## session roster; games can subclass to add motd, version, or game-mode
## fields without touching transport code.
@tool
@abstract
class_name ServerInfoSource
extends Resource

## Builds a fresh [ServerInfo] for [param tree]. Called on the host inside
## the auth callback; must not mutate session state.
@abstract
func build_server_info(tree: MultiplayerTree) -> ServerInfo
