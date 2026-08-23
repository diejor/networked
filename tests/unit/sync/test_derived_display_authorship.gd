## The display's authorship question, asked of a derived set rather than of a
## synchronizer.
##
## A displayed value has to know whether this peer AUTHORS the stream feeding
## it: an authoring peer displays its own simulation and a receiving peer
## displays what arrived. When the only stream is a derived property set, that
## question is [method NetwMultiplayerCore.display_authors_streams]'s, and it
## is the one input to the role ladder that no other declaration shape
## reaches. This suite is the rig that reaches it, so a port of the pump has a
## corpus that can say it was wrong.
class_name TestDerivedDisplayAuthorship
extends NetwTestSuite

const DISPLAY_PLAYER := preload(
	"res://tests/support/sync/derived_display_player.gd"
)
const CONTROLLER_PLAYER := preload(
	"res://tests/support/sync/derived_controller_display_player.gd"
)

var rig: DerivedLoopbackRig


func before_test() -> void:
	rig = DerivedLoopbackRig.new()
	rig.player_type = DISPLAY_PLAYER


## Verify the peer authoring the state set displays its own simulation and the
## peer receiving it displays what arrived, which is the only difference
## between the two entities and is decided by the derived set alone.
func test_the_authoring_peer_and_the_receiving_peer_resolve_apart() -> void:
	await rig.setup(self)
	rig.sync_ticks(8)

	var server_display: NetwDisplayHandle = _display_of(rig.server_node)
	var client_display: NetwDisplayHandle = _display_of(rig.client_node)
	assert_object(server_display).is_not_null()
	assert_object(client_display).is_not_null()

	# The tracked value exists on both, so a role difference is a difference of
	# authorship rather than of declaration.
	assert_int(server_display.channel_census()[&"channels"]).is_greater(0)
	assert_int(client_display.channel_census()[&"channels"]).is_greater(0)

	assert_int(server_display.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.AUTHORITY
	)
	assert_int(client_display.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.REMOTE
	)


## Verify the same two peers swap roles when the set's write policy hands the
## stream to the controller, which is the arm that proves the authorship
## question is asked OF THE SET rather than answered from the peer id.
func test_a_controller_policed_set_makes_the_controller_the_author() -> void:
	rig.player_type = CONTROLLER_PLAYER
	await rig.setup(self)
	rig.sync_ticks(8)

	var server_display: NetwDisplayHandle = _display_of(rig.server_node)
	var client_display: NetwDisplayHandle = _display_of(rig.client_node)
	assert_object(server_display).is_not_null()
	assert_object(client_display).is_not_null()

	# The rig makes the client the controller, so the client is the peer whose
	# writes the set admits. Against the state-set arm above, the same rig and
	# the same nodes put the client on the other side of the question, and the
	# set's write policy is the only thing that changed.
	assert_int(client_display.resolved_display_role).is_not_equal(
		NetwDisplayHandle.DisplayRole.REMOTE
	)
	assert_int(server_display.resolved_display_role).is_not_equal(
		client_display.resolved_display_role
	)


func _display_of(node: Node) -> NetwDisplayHandle:
	var entity := NetwEntity.of(node)
	return entity.interpolation as NetwDisplayHandle if entity else null
