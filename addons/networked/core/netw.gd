@tool
## Top-level namespace for the Networked addon.
##
## Consolidates all sub-namespaces and global entry points.
class_name Netw
extends Object


## Static entry point for all debug and logging functionality.
static var dbg := NetwDbg.new()


## Returns [code]true[/code] if the current process is running under a GdUnit4
## test environment.
static func is_test_env() -> bool:
	var args := OS.get_cmdline_args() + OS.get_cmdline_user_args()
	for arg in args:
		if "GdUnit" in arg or "GdUnitTestRunner.tscn" in arg:
			return true
	return false
