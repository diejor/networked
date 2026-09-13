extends VBoxContainer

const URLS := &"urls"
const USERNAME := &"username"
const CREDENTIAL := &"credential"
const _SERVER_EDIT := preload(
	"res://addons/networked/gdscript/connect/ui/ice_server_edit.tscn"
)

var _cards: Array[VBoxContainer] = []

@onready var _add_button: Button = $AddServerButton


func _ready() -> void:
	_add_button.pressed.connect(add_server)


func set_value(servers: Array) -> void:
	for card in _cards:
		card.queue_free()
	_cards.clear()
	for server: Variant in servers:
		if server is Dictionary:
			add_server(server)


func get_value() -> Array:
	var servers: Array = []
	for card in _cards:
		var server: Dictionary = card.get_meta(&"server", { }).duplicate(true)
		var urls_edit := card.get_node("Fields/URLsEdit") as LineEdit
		var username_edit := card.get_node("Fields/UsernameEdit") as LineEdit
		var credential_edit := card.get_node(
			"Fields/CredentialEdit",
		) as LineEdit
		server[URLS] = _split(urls_edit.text)
		_set_optional(server, USERNAME, username_edit.text)
		_set_optional(server, CREDENTIAL, credential_edit.text)
		servers.append(server)
	return servers


func add_server(server: Dictionary = { }) -> void:
	var card := _SERVER_EDIT.instantiate() as VBoxContainer
	card.name = "Server%d" % _cards.size()
	card.set_meta(&"server", server.duplicate(true))
	var title := card.get_node("Header/Title") as Label
	title.text = "ICE server %d" % (_cards.size() + 1)
	var remove := card.get_node("Header/Remove") as Button
	remove.pressed.connect(_remove_server.bind(card))
	var urls_edit := card.get_node("Fields/URLsEdit") as LineEdit
	urls_edit.text = _joined_urls(server)
	var username_edit := card.get_node("Fields/UsernameEdit") as LineEdit
	username_edit.text = str(
		server.get(
			USERNAME,
			"",
		),
	)
	var credential_edit := card.get_node("Fields/CredentialEdit") as LineEdit
	credential_edit.text = str(
		server.get(CREDENTIAL, ""),
	)

	_cards.append(card)
	add_child(card)
	var add_button := get_node_or_null("AddServerButton") as Button
	if add_button != null:
		move_child(card, add_button.get_index())


func _remove_server(card: VBoxContainer) -> void:
	_cards.erase(card)
	card.queue_free()
	_renumber()


func _renumber() -> void:
	for at in _cards.size():
		_cards[at].name = "Server%d" % at
		var title := _cards[at].get_node("Header/Title") as Label
		title.text = "ICE server %d" % (at + 1)


func _joined_urls(server: Dictionary) -> String:
	var urls: Variant = server.get(URLS, [])
	if urls is Array or urls is PackedStringArray:
		return ", ".join(urls)
	return str(urls)


func _split(text: String) -> Array:
	var urls: Array = []
	for piece in text.split(",", false):
		var url := piece.strip_edges()
		if not url.is_empty():
			urls.append(url)
	return urls


func _set_optional(
		server: Dictionary,
		key: StringName,
		value: String,
) -> void:
	if value.is_empty():
		server.erase(key)
	else:
		server[key] = value
