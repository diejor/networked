## The base type of every configuration resource a session registers.
##
## Core dispatches on the concrete resource type, never on this base, so a
## subclass is a plain typed payload: it carries authoring facts to the one
## interface that consumes it and holds no behavior of its own. Register a
## configuration through [method MultiplayerAPI.object_configuration_add] while
## the session is [constant NetwEmbeddingHandle.Phase.DECLARING]. The consuming
## interface owns the registered values for the session lifetime, so a
## registrar node that frees afterwards drops nothing.
## [codeblock]
## var config := NetwSceneConfig.new()
## config.initial_scenes = [preload("res://lobby.tscn")]
## api.object_configuration_add(self, config)
## [/codeblock]
## This class exists so the catalog of registrable configurations is readable
## from its inheritance list: [NetwSessionConfig], [NetwSceneConfig],
## [NetwClockConfig], and [NetwLagCompensationConfig]. Call parameters such as
## [NetwHostConfig] and [NetwConnectTarget] are arguments to connect verbs, not
## registered configurations, so they stay outside this hierarchy.
@abstract
class_name NetwObjectConfig
extends Resource
