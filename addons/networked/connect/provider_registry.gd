## Maps [StringName] ids to [LobbyProvider] instances for the browser
## to look up at join time.
##
## Browsers add the registry under their scene and call
## [method register] with each available provider. [JoinTarget]s carry
## the provider id in [member JoinTarget.provider_id]. The join
## dispatch looks the provider up via [method get_provider].
class_name ProviderRegistry
extends Node


var _providers: Dictionary = {}


## Adds [param provider] under [param id]. Replaces any existing
## registration for the same id.
func register(id: StringName, provider: LobbyProvider) -> void:
	if provider == null:
		return
	_providers[id] = provider


## Removes the provider registered under [param id], if any.
func unregister(id: StringName) -> void:
	_providers.erase(id)


## Returns the provider registered under [param id], or
## [code]null[/code] when none exists.
func get_provider(id: StringName) -> LobbyProvider:
	return _providers.get(id, null)


## Returns the list of currently registered ids.
func list_providers() -> Array[StringName]:
	var out: Array[StringName] = []
	for key in _providers.keys():
		out.append(key)
	return out


## Returns [code]true[/code] when [param id] has a registered provider.
func has_provider(id: StringName) -> bool:
	return _providers.has(id)
