## Test session that inherits a replaced seam without declaring one of its own.
##
## The seam its base replaced is still replaced here, so the session's seam
## resolution has to walk past a leaf that declares nothing before it reaches
## the native base.
extends "res://tests/support/chains/seam_base.gd"
