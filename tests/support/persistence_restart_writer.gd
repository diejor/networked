extends SceneTree


func _initialize() -> void:
	write.call_deferred()


func write() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		quit(2)
		return
	var rows := PersistenceRestartRows.new()
	var holder := PersistenceRestartRows.mount(self, &"Writer")
	var db := await PersistenceRestartRows.open(holder, args[0])
	if db == null:
		quit(3)
		return
	var wrote: Error = await db.write(
		rows.players,
		PersistenceRestartRows.HERO,
		{&"gold": PersistenceRestartRows.GOLD},
	).wait()
	if wrote != OK:
		quit(4)
		return
	var mobs := Netw.table(holder, &"restart_mobs")
	mobs.write_routes(PackedInt64Array([5, 6]))
	mobs.write_column(rows.hp, PackedInt64Array(PersistenceRestartRows.MOB_HP))
	if mobs.commit() != OK:
		quit(5)
		return
	var saved: Error = await mobs.save(
		db,
		PersistenceRestartRows.FOREST,
		PackedStringArray(PersistenceRestartRows.MOB_IDS),
	).wait()
	if saved != OK:
		quit(6)
		return
	var closed: Error = await db.close().wait()
	set_multiplayer(null, holder.get_path())
	holder.free()
	quit(0 if closed == OK else 7)
