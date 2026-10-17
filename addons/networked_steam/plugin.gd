@tool
extends EditorPlugin

const SteamExport := preload("steam_export.gd")

var steam_export: EditorExportPlugin


func _enter_tree() -> void:
	steam_export = SteamExport.new()
	add_export_plugin(steam_export)


func _exit_tree() -> void:
	remove_export_plugin(steam_export)
