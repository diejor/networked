## Reusable right-aligned "chip" (pill badge) drawer for [Tree] cells.
##
## A [Tree] cell can only draw one string of its own, so any secondary tags
## (role, username, later: latency, backend) have to be painted by a custom
## draw callback. This owns that drawing so no single panel re-implements the
## pill geometry, and so the same chip row can be reused by any cell.
##
## Drawing is immediate mode and allocation-free per frame: the styleboxes are
## built once and only their colors change per chip. Call [method draw] from
## inside a [method TreeItem.set_custom_draw_callback] callback (the only place
## the tree is mid-draw, so [method CanvasItem.draw_style_box] is valid).
## [codeblock]
## var chips := TreeChips.new(0.85)   # 15% smaller than the row font
##
## func _draw_row(item: TreeItem, rect: Rect2) -> void:
##     chips.draw(item.get_tree(), rect, [
##         TreeChips.Chip.new("Dev", peer_color),
##         TreeChips.Chip.new("◈ LISTEN", role_color),   # rightmost
##     ])
## [/codeblock]
@tool
extends RefCounted

## One pill: its [member text] and the accent [member color] used for the
## label, a translucent fill, and the border.
class Chip:
	extends RefCounted
	var text: String
	var color: Color


	func _init(chip_text: String, chip_color: Color) -> void:
		text = chip_text
		color = chip_color

## Chip font size relative to the cell's own font. [code]0.85[/code] renders the
## pill ~15% smaller so it reads as secondary to the row label.
var font_scale: float

## Gap between adjacent chips. The [Tree] cell rect already accounts for row
## buttons, so chips do not add an extra outer right margin.
var margin: float = 0.0
var gap: float = 4.0

var _fill: StyleBoxFlat


func _init(chip_font_scale: float = 0.85) -> void:
	font_scale = chip_font_scale
	_fill = StyleBoxFlat.new()
	_fill.set_corner_radius_all(6)
	_fill.set_border_width_all(1)


## Paints [param chips] right-aligned within [param rect]. The last entry sits
## flush right, earlier entries stack to its left. A null/empty entry is skipped.
## [br][br]
## Must run during [param tree]'s draw pass.
func draw(tree: Tree, rect: Rect2, chips: Array) -> void:
	if chips.is_empty():
		return
	var font := tree.get_theme_font(&"font", &"Tree")
	var base := tree.get_theme_font_size(&"font_size", &"Tree")
	if not font or base <= 0:
		return
	var fsize := int(round(base * font_scale))

	var pad := Vector2(6, 2)
	# Height comes from the primary font's line height, not the measured string:
	# glyphs like ◈/▸ fall back to a taller font and would otherwise jitter the
	# pill height between roles. Only the width tracks the text.
	var line_h := font.get_height(fsize)
	var x := rect.end.x - margin
	for i: int in range(chips.size() - 1, -1, -1):
		var chip: Chip = chips[i]
		if not chip or chip.text.is_empty():
			continue
		var text_w := font.get_string_size(chip.text, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize).x
		var size := Vector2(text_w + pad.x * 2.0, line_h + pad.y * 2.0)
		var pill := Rect2(
			Vector2(x - size.x, rect.position.y + (rect.size.y - size.y) * 0.5),
			size,
		)
		_fill.bg_color = Color(chip.color, 0.18)
		_fill.border_color = Color(chip.color, 0.55)
		tree.draw_style_box(_fill, pill)
		var baseline := pill.position.y + pad.y + font.get_ascent(fsize)
		tree.draw_string(
			font,
			Vector2(pill.position.x + pad.x, baseline),
			chip.text,
			HORIZONTAL_ALIGNMENT_LEFT,
			-1,
			fsize,
			chip.color,
		)
		x = pill.position.x - gap
