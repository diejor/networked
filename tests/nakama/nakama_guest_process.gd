extends SceneTree


func _initialize() -> void:
	await process_frame
	var account := NakamaAccount.new()
	account.host = NakamaTestServer.host()
	account.port = NakamaTestServer.DEFAULT_PORT
	root.add_child(account)
	await account.sign_in().wait()
	print("NAKAMA_GUEST=%s" % account.user_id)
	quit()
