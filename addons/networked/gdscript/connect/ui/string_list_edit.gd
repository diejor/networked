extends VBoxContainer

var _rows: Array[HBoxContainer] = []
var _placeholder: String = ""


func _ready() -> void:
	var add_button := _add_button()
	add_button.pressed.connect(add_entry.bind(""))
	move_child(add_button, get_child_count() - 1)


## Sets the placeholder text of every row, including rows added later.
func set_placeholder(text: String) -> void:
	_placeholder = text
	for row in _rows:
		_edit_of(row).placeholder_text = text


## Replaces the rows with [param entries], a [PackedStringArray] or an
## [Array].
func set_value(entries: Variant) -> void:
	for row in _rows:
		row.queue_free()
	_rows.clear()
	if entries is PackedStringArray or entries is Array:
		for entry: Variant in entries:
			add_entry(str(entry))


## Returns the rows that are not empty.
func get_value() -> PackedStringArray:
	var entries := PackedStringArray()
	for row in _rows:
		var text := _edit_of(row).text.strip_edges()
		if not text.is_empty():
			entries.append(text)
	return entries


func add_entry(text: String = "") -> void:
	var row := HBoxContainer.new()
	row.name = "Entry%d" % _rows.size()

	var edit := LineEdit.new()
	edit.name = "Edit"
	edit.text = text
	edit.placeholder_text = _placeholder
	edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(edit)

	var remove := Button.new()
	remove.text = "Remove"
	remove.pressed.connect(_remove_entry.bind(row))
	row.add_child(remove)

	_rows.append(row)
	add_child(row)
	if is_node_ready():
		move_child(row, _add_button().get_index())


func _add_button() -> Button:
	return $AddEntryButton as Button


func _edit_of(row: HBoxContainer) -> LineEdit:
	return row.get_node("Edit") as LineEdit


func _remove_entry(row: HBoxContainer) -> void:
	_rows.erase(row)
	row.queue_free()
	_renumber()


func _renumber() -> void:
	for at in _rows.size():
		_rows[at].name = "Entry%d" % at
