extends Node
# The game starts quiet: the master volume is 5 % at launch (the Options screen or the sound code can raise it later).

const START_VOLUME := 0.05

func _ready() -> void:
	AudioServer.set_bus_volume_db(0, linear_to_db(START_VOLUME))
