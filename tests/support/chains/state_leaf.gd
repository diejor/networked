extends StateSyncBody

var score := 0


func _init() -> void:
	super()
	Netw.configure_property(self, &"score")
