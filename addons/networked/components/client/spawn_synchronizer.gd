class_name SpawnSynchronizer
extends MultiplayerSynchronizer

func _init() -> void:
	name = "SpawnSynchronizer"
	unique_name_in_owner = true
	

#func _enter_tree() -> void:
	#set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
