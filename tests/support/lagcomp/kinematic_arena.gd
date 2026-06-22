## Programmatic static collision geometry for the move_and_slide replay fixture.
##
## A flat floor (infinite WorldBoundaryShape2D placed at floor_y) plus an
## optional vertical wall, built in code so the fixture needs no authored scene.
## Add it under the suite, auto_free it, await one physics_frame to commit the
## shapes, then the space is ready for body_test_motion queries.
class_name KinematicArena
extends Node2D

## World-space Y of the floor surface. A body rests with its collider bottom on
## this line, so an 8px half-extent box centers at floor_y - 8.
var floor_y: float

## World-space X of the wall's inner face, when a wall was requested.
var wall_x: float


func _init(
		with_wall: bool = false,
		floor_y_: float = 200.0,
		wall_x_: float = 100.0,
) -> void:
	floor_y = floor_y_
	wall_x = wall_x_

	var floor_body := StaticBody2D.new()
	floor_body.name = "Floor"
	floor_body.position = Vector2(0, floor_y)
	var floor_shape := CollisionShape2D.new()
	var plane := WorldBoundaryShape2D.new()
	# Normal up, distance 0: the boundary passes through the body origin
	# (y = floor_y) and the solid half-space is everything below it.
	plane.normal = Vector2.UP
	plane.distance = 0.0
	floor_shape.shape = plane
	floor_body.add_child(floor_shape)
	add_child(floor_body)

	if with_wall:
		var wall := StaticBody2D.new()
		wall.name = "Wall"
		# Shape is 20px wide, so the body origin sits 10px right of the inner
		# face the moving body collides with.
		wall.position = Vector2(wall_x + 10.0, 0.0)
		var wall_shape := CollisionShape2D.new()
		var rect := RectangleShape2D.new()
		rect.size = Vector2(20.0, 4000.0)
		wall_shape.shape = rect
		wall.add_child(wall_shape)
		add_child(wall)
