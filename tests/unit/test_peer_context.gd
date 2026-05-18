## Unit tests for [NetwPeerContext] bucket registry.
class_name TestNetwPeerContext
extends NetworkedTestSuite


class BucketA extends RefCounted:
	var value: int = 0


class BucketB extends RefCounted:
	var label: String = ""


func test_get_bucket_creates_instance_on_first_access() -> void:
	var ctx: NetwPeerContext = auto_free(NetwPeerContext.new())
	var bucket := ctx.get_bucket(BucketA)
	assert_that(bucket).is_not_null()
	assert_that(bucket is BucketA).is_true()


func test_get_bucket_returns_same_instance_on_repeat_calls() -> void:
	var ctx: NetwPeerContext = auto_free(NetwPeerContext.new())
	var first := ctx.get_bucket(BucketA)
	var second := ctx.get_bucket(BucketA)
	assert_that(first).is_same(second)


func test_different_types_return_independent_instances() -> void:
	var ctx: NetwPeerContext = auto_free(NetwPeerContext.new())
	var a := ctx.get_bucket(BucketA)
	var b := ctx.get_bucket(BucketB)
	assert_that(a).is_not_same(b)


func test_bucket_data_persists_between_accesses() -> void:
	var ctx: NetwPeerContext = auto_free(NetwPeerContext.new())
	(ctx.get_bucket(BucketA) as BucketA).value = 42
	assert_that((ctx.get_bucket(BucketA) as BucketA).value).is_equal(42)


func test_two_contexts_do_not_share_bucket_data() -> void:
	var ctx1: NetwPeerContext = auto_free(NetwPeerContext.new())
	var ctx2: NetwPeerContext = auto_free(NetwPeerContext.new())
	(ctx1.get_bucket(BucketA) as BucketA).value = 1
	(ctx2.get_bucket(BucketA) as BucketA).value = 2
	assert_that((ctx1.get_bucket(BucketA) as BucketA).value).is_equal(1)
	assert_that((ctx2.get_bucket(BucketA) as BucketA).value).is_equal(2)


func test_two_contexts_do_not_share_bucket_instances() -> void:
	var ctx1: NetwPeerContext = auto_free(NetwPeerContext.new())
	var ctx2: NetwPeerContext = auto_free(NetwPeerContext.new())
	assert_that(ctx1.get_bucket(BucketA)).is_not_same(ctx2.get_bucket(BucketA))
