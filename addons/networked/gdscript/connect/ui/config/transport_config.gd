## Authored transport settings for [ConnectBrowser].
class_name ConnectTransportConfig
extends Resource

func peer_class() -> StringName:
	return &""


func client_settings() -> Dictionary:
	return { }


func host_settings() -> Dictionary:
	return client_settings()


func browse_settings() -> Dictionary:
	return { }


static func apply_to(
		snapshot: Dictionary,
		configs: Array[ConnectTransportConfig],
) -> Dictionary:
	var result := snapshot.duplicate(true)
	for config: ConnectTransportConfig in configs:
		if config == null or config.peer_class() != result.get("peer_class"):
			continue
		var host: Dictionary = result.get("host_settings", { })
		var client: Dictionary = result.get("client_settings", { })
		host.merge(config.host_settings(), true)
		client.merge(config.client_settings(), true)
		result.host_settings = host
		result.client_settings = client
	return result
