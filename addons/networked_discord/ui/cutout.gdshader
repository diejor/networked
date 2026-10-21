shader_type canvas_item;

uniform vec2 size = vec2(32.0);
uniform float corner_radius = 1000.0;
uniform float inset = 0.0;
uniform int hole_count = 0;
uniform vec4 holes[8];

varying vec2 local;

void vertex() {
	local = VERTEX;
}

void fragment() {
	vec2 half_size = size * 0.5 - vec2(inset);
	float radius = min(corner_radius, min(half_size.x, half_size.y));
	vec2 q = abs(local - size * 0.5) - half_size + vec2(radius);
	float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
	for (int i = 0; i < hole_count; i++) {
		vec2 p = local - holes[i].xy;
		p.x -= clamp(p.x, -holes[i].w, holes[i].w);
		d = max(d, holes[i].z - length(p));
	}
	COLOR.a *= clamp(0.5 - d / max(fwidth(d), 0.0001), 0.0, 1.0);
}
