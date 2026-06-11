## Tests generic component slots on [NetwEntity].
class_name TestEntitySlots
extends NetwTestSuite


func test_provide_and_slot_retrieval() -> void:
	var entity := NetwEntity.new()
	var node := Node.new()
	auto_free(node)

	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_null()

	entity.provide(NetwEntity.Slot.SAVE, node)
	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_equal(node)

	entity.provide(NetwEntity.Slot.SAVE, null)
	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_null()


func test_weakref_eviction() -> void:
	var entity := NetwEntity.new()
	var node := Node.new()

	entity.provide(NetwEntity.Slot.SAVE, node)
	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_equal(node)

	# Free the node to simulate GC eviction
	node.free()
	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_null()


func test_require_immediate() -> void:
	var entity := NetwEntity.new()
	var node := Node.new()
	auto_free(node)

	entity.provide(NetwEntity.Slot.SAVE, node)

	var received := [null]
	entity.require(NetwEntity.Slot.SAVE, func(comp: Object) -> void:
		received[0] = comp as Node
	)

	assert_that(received[0]).is_equal(node)


func test_require_deferred() -> void:
	var entity := NetwEntity.new()
	var node := Node.new()
	auto_free(node)

	var received := [null]
	entity.require(NetwEntity.Slot.SAVE, func(comp: Object) -> void:
		received[0] = comp as Node
	)

	assert_that(received[0]).is_null()

	entity.provide(NetwEntity.Slot.SAVE, node)
	assert_that(received[0]).is_equal(node)


func test_re_provide_overwrites_and_flushes() -> void:
	var entity := NetwEntity.new()
	var node1 := Node.new()
	var node2 := Node.new()
	auto_free(node1)
	auto_free(node2)

	entity.provide(NetwEntity.Slot.SAVE, node1)
	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_equal(node1)

	# Overwrite slot
	entity.provide(NetwEntity.Slot.SAVE, node2)
	assert_that(entity.slot(NetwEntity.Slot.SAVE)).is_equal(node2)

	# New requires should receive node2 immediately
	var received := [null]
	entity.require(NetwEntity.Slot.SAVE, func(comp: Object) -> void:
		received[0] = comp as Node
	)
	assert_that(received[0]).is_equal(node2)
