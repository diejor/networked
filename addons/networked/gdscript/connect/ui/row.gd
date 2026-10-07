## One [NetwEndpoint] in a [ConnectBrowser] list.
class_name ConnectBrowserRow
extends PanelContainer

signal selected
signal activated

var endpoint: NetwEndpoint

@onready var button: Button = %ConnectButton
@onready var status_dot: StatusDot = %StatusDot
@onready var name_label: Label = %NameLabel
@onready var badge_label: Label = %BadgeLabel
@onready var address_label: Label = %AddressLabel
@onready var players_label: Label = %PlayersLabel
@onready var ping_label: Label = %PingLabel


func _ready() -> void:
	endpoint.changed.connect(on_changed)
	button.pressed.connect(selected.emit)
	button.gui_input.connect(on_button_gui_input)
	on_changed()


func on_changed() -> void:
	var info := endpoint.info
	status_dot.status = endpoint.status
	name_label.text = (
			endpoint.display_name if not endpoint.display_name.is_empty()
			else endpoint.address
	)
	badge_label.text = (
			endpoint.source if not endpoint.source.is_empty()
			else String(endpoint.peer_class).trim_suffix("MultiplayerPeer")
	)
	address_label.text = endpoint.address
	players_label.text = (
			"%d/%d" % [info.players, info.max_players]
			if info.max_players > 0 else "-"
	)
	ping_label.text = (
			"%d ms" % info.latency_ms if info.latency_ms >= 0 else "-"
	)


func on_button_gui_input(event: InputEvent) -> void:
	var click := event as InputEventMouseButton
	if click != null and click.double_click:
		activated.emit()
