## Unit tests for [NetwPeerContext] bucket registry.
class_name TestNetwPeerContext
extends NetwTestSuite

class BucketA extends RefCounted:
	var value: int = 0


class BucketB extends RefCounted:
	var label: String = ""


func test_bucket_lifecycle_and_isolation_flow() -> void:
	var ctx: NetwPeerContext = auto_free(NetwPeerContext.new())
	var ctx2: NetwPeerContext = auto_free(NetwPeerContext.new())
	var first := ctx.get_bucket(BucketA) as BucketA
	var repeat := ctx.get_bucket(BucketA) as BucketA
	var other_type := ctx.get_bucket(BucketB) as BucketB

	assert_that(first).is_not_null()
	assert_that(first).is_same(repeat)
	assert_that(first).is_not_same(other_type)

	first.value = 42
	assert_that((ctx.get_bucket(BucketA) as BucketA).value).is_equal(42)

	var isolated := ctx2.get_bucket(BucketA) as BucketA
	isolated.value = 7
	assert_that(isolated).is_not_same(first)
	assert_that((ctx.get_bucket(BucketA) as BucketA).value).is_equal(42)
	assert_that((ctx2.get_bucket(BucketA) as BucketA).value).is_equal(7)
