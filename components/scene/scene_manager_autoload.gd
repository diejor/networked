extends Node

@onready var transition_anim: AnimationPlayer = Client.scene_manager.get_node("%TransitionAnim")
@onready var transition_progress: TextureProgressBar = Client.scene_manager.get_node("%TransitionProgress")

func connect_player(client_data: Dictionary) -> void:
	client_data.peer_id = Client.uid
	Client.scene_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data
	)
	
	get_tree().unload_current_scene.call_deferred()
	
