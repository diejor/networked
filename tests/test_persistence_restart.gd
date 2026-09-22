class_name TestPersistenceRestart
extends NetwTestSuite

const WRITER := "res://tests/support/persistence_restart_writer.gd"

var root: String
var holder: Node


func before_test() -> void:
	root = "user://netw_persistence_restart/%d" % Time.get_ticks_usec()


func after_test() -> void:
	if is_instance_valid(holder):
		get_tree().set_multiplayer(null, holder.get_path())
		holder.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(root))
	await super.after_test()


func test_a_record_and_a_table_snapshot_survive_a_process_restart() -> void:
	var output: Array = []
	var code := OS.execute(
		OS.get_executable_path(),
		[
			"--headless",
			"--path",
			ProjectSettings.globalize_path("res://"),
			"--script",
			WRITER,
			"--",
			root,
		],
		output,
		true,
	)
	assert_int(code) \
			.override_failure_message("the writer process exited %d\n%s" % [
				code,
				"\n".join(output),
			]) \
			.is_equal(0)

	var rows := PersistenceRestartRows.new()
	holder = PersistenceRestartRows.mount(get_tree(), &"Reader")
	var db := await PersistenceRestartRows.open(holder, root)
	assert_object(db).is_not_null()

	var read: Dictionary = await db.read(
		rows.players,
		PersistenceRestartRows.HERO,
	).wait()
	assert_int(read.error).is_equal(OK)
	assert_bool(read.found).is_true()
	assert_int(read.values[&"gold"]).is_equal(PersistenceRestartRows.GOLD)

	var mobs := Netw.table(holder, &"restart_mobs")
	var loaded: Dictionary = await mobs.load(
		db,
		PersistenceRestartRows.FOREST,
	).wait()
	assert_int(loaded.error).is_equal(OK)
	assert_bool(loaded.found).is_true()
	assert_array(Array(loaded.ids)).is_equal(PersistenceRestartRows.MOB_IDS)
	assert_array(Array(mobs.read_routes())).is_equal(Array(loaded.routes))
	assert_array(Array(mobs.read_column(rows.hp))) \
			.is_equal(PersistenceRestartRows.MOB_HP)
	await db.close().wait()
	await db.delete_slot(PersistenceRestartRows.SLOT).wait()
