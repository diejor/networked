class_name TestNakamaDatabase
extends NetwTestSuite

func address(kind: int, schema_name: String, key: String) -> Dictionary:
	return { "kind": kind, "schema_name": schema_name, "key": key }


func test_different_names_never_share_a_key() -> void:
	assert_str(NakamaDatabase.address_key(address(0, "players", "a/b"))) \
			.is_not_equal(NakamaDatabase.address_key(address(0, "players/a", "b")))
	assert_str(NakamaDatabase.address_key(address(0, "players", "hero"))) \
			.is_not_equal(NakamaDatabase.address_key(address(1, "players", "hero")))
	assert_str(NakamaDatabase.address_key(address(0, "p", ".."))) \
			.is_equal("0_p_%2E%2E")


func test_a_session_with_no_account_refuses_every_verb() -> void:
	var tree: MultiplayerViewport = auto_free(MultiplayerViewport.new())
	add_child(tree)
	var database := NakamaDatabase.new()

	var opening := database._open(tree.api, &"slot1")
	await opening.wait()
	assert_bool(opening.get_is_failed()).is_true()
	assert_int(opening.get_code()).is_equal(ERR_UNCONFIGURED)

	var listing := database._list_slots(tree.api)
	await listing.wait()
	assert_int(listing.get_code()).is_equal(ERR_UNCONFIGURED)

	var deleting := database._delete_slot(tree.api, &"slot1")
	await deleting.wait()
	assert_int(deleting.get_code()).is_equal(ERR_UNCONFIGURED)
