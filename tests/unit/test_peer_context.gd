## Unit tests for PeerContext bucket registry.
##
## No multiplayer or scene tree required — PeerContext is a plain RefCounted.
## Dummy bucket types are defined inline to avoid coupling to real components.
class_name TestPeerContext
extends GdUnitTestSuite


## Dummy bucket types used as distinct keys in these tests.
class BucketA extends RefCounted:
	var value: int = 0

class BucketB extends RefCounted:
	var label: String = ""


# ---------------------------------------------------------------------------
# get_bucket — lazy initialization
# ---------------------------------------------------------------------------

func test_get_bucket_creates_instance_on_first_access() -> void:
	var ctx: PeerContext = auto_free(PeerContext.new())
	var bucket := ctx.get_bucket(BucketA)
	assert_that(bucket).is_not_null()
	assert_that(bucket is BucketA).is_true()


# ---------------------------------------------------------------------------
# get_bucket — idempotency
# ---------------------------------------------------------------------------

func test_get_bucket_returns_same_instance_on_repeat_calls() -> void:
	var ctx: PeerContext = auto_free(PeerContext.new())
	var first := ctx.get_bucket(BucketA)
	var second := ctx.get_bucket(BucketA)
	assert_that(first).is_same(second)


# ---------------------------------------------------------------------------
# get_bucket — type isolation
# ---------------------------------------------------------------------------

func test_different_types_return_independent_instances() -> void:
	var ctx: PeerContext = auto_free(PeerContext.new())
	var a := ctx.get_bucket(BucketA)
	var b := ctx.get_bucket(BucketB)
	assert_that(a).is_not_same(b)


# ---------------------------------------------------------------------------
# get_bucket — data persistence
# ---------------------------------------------------------------------------

func test_bucket_data_persists_between_accesses() -> void:
	var ctx: PeerContext = auto_free(PeerContext.new())
	(ctx.get_bucket(BucketA) as BucketA).value = 42
	assert_that((ctx.get_bucket(BucketA) as BucketA).value).is_equal(42)


# ---------------------------------------------------------------------------
# Separate PeerContext instances are independent
# ---------------------------------------------------------------------------

func test_two_contexts_do_not_share_bucket_data() -> void:
	var ctx1: PeerContext = auto_free(PeerContext.new())
	var ctx2: PeerContext = auto_free(PeerContext.new())
	(ctx1.get_bucket(BucketA) as BucketA).value = 1
	(ctx2.get_bucket(BucketA) as BucketA).value = 2
	assert_that((ctx1.get_bucket(BucketA) as BucketA).value).is_equal(1)
	assert_that((ctx2.get_bucket(BucketA) as BucketA).value).is_equal(2)


func test_two_contexts_do_not_share_bucket_instances() -> void:
	var ctx1: PeerContext = auto_free(PeerContext.new())
	var ctx2: PeerContext = auto_free(PeerContext.new())
	assert_that(ctx1.get_bucket(BucketA)).is_not_same(ctx2.get_bucket(BucketA))
