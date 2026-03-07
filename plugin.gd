@tool
extends EditorPlugin

const WEBRTC_DEP_PATH = "res://addons/webrtc/"
const TUBE_DEP_PATH = "res://addons/tube/"

func _enter_tree() -> void:
	var base_dir = get_script().resource_path.get_base_dir()
	var webrtc_path = base_dir.path_join("core/peer/backends/webrtc/")
	var tube_path = base_dir.path_join("core/peer/backends/tube/")
	
	var webrtc_changed = _sync_gdignore(WEBRTC_DEP_PATH, webrtc_path)
	var tube_changed = _sync_gdignore(TUBE_DEP_PATH, tube_path)
		
	if webrtc_changed or tube_changed:
		EditorInterface.get_resource_filesystem().scan()

func _sync_gdignore(dependency_path: String, target_folder: String) -> bool:
	var ignore_file_path = target_folder.path_join(".gdignore")
	var dependency_exists = DirAccess.dir_exists_absolute(dependency_path)
	var ignore_exists = FileAccess.file_exists(ignore_file_path)

	if dependency_exists == ignore_exists:
		if dependency_exists:
			DirAccess.remove_absolute(ignore_file_path)
		else:
			FileAccess.open(ignore_file_path, FileAccess.WRITE).close()
		return true
		
	return false
