## A [NetwDatabaseBackend] that keeps records in Nakama storage.
##
## Every record is one Nakama storage object owned by the authenticated user, so
## a save follows the account rather than the device. A slot is one storage
## collection, and [member app] names the manifest collection that lists them.
## [codeblock]
## var backend := NakamaDatabase.new()
## backend.wrapper = wrapper
## Netw.configure_database(self, &"saves").backend(backend)
## [/codeblock]
## [br][br]
## Nakama cannot enumerate its own collections, so [method _list_slots] reads a
## manifest this backend maintains. [method _open] writes a slot's manifest
## entry before any record under it, so a slot that holds data is always listed.
## [br][br]
## Every storage request answers one of these codes, and each verb says where
## the code lands. An uncertain code means the request left the process and
## Nakama never said whether it applied.
## [codeblock lang=text]
## OK                    Nakama applied the request
## ERR_UNAUTHORIZED      no authenticated session, or Nakama answered 401 or 403
## ERR_UNAVAILABLE       the Nakama addon is absent
## ERR_INVALID_DATA      Nakama answered another 4xx status
## ERR_TIMEOUT           canceled, or ended with no HTTP status. Uncertain
## ERR_CONNECTION_ERROR  Nakama answered nothing or a 5xx status. Uncertain
## FAILED                any other exception. Uncertain
## [/codeblock]
class_name NakamaDatabase
extends NetwDatabaseBackend

## Version stamped into every stored value, refused on read when it disagrees.
const FORMAT_VERSION := 1

## The manifest collection, and the prefix every slot's collection carries.
@export var app := "netw_saves"

## The [NakamaWrapper] this backend performs its storage through.
##
## A backend with no wrapper rejects every verb with
## [constant @GlobalScope.ERR_UNCONFIGURED] rather than answering an empty save.
var wrapper


## Encodes [param text] so it survives as one Nakama collection or key segment.
##
## The encoding is injective, so two different names never meet in one key.
static func encoded(text: String) -> String:
	return text.uri_encode().replace(".", "%2E")


## Returns the storage key naming [param address].
##
## [code]kind[/code] leads, so a record and a table snapshot spelled with the
## same key are two objects.
static func address_key(address: Dictionary) -> String:
	return "%d_%s_%s" % [
		int(address.get("kind", 0)),
		encoded(String(address.get("schema_name", ""))),
		encoded(String(address.get("key", ""))),
	]


## Returns the key prefix every record of [param schema_name] at [param kind]
## carries.
static func address_prefix(kind: int, schema_name: String) -> String:
	return "%d_%s_" % [kind, encoded(schema_name)]


## Encodes [param envelope] into the JSON string Nakama stores.
##
## The payload is a [Variant] the game declared, which JSON cannot carry, so the
## envelope travels as base64 of its binary form.
static func packed(envelope: Dictionary) -> String:
	return JSON.stringify(
		{
			"netw": FORMAT_VERSION,
			"body": Marshalls.raw_to_base64(var_to_bytes(envelope)),
		},
	)


## Decodes a stored [param value] back into an envelope.
##
## A value this library did not write answers an empty [Dictionary], which the
## reading session refuses as [constant @GlobalScope.ERR_FILE_UNRECOGNIZED]
## rather than reading as an empty save.
static func unpacked(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return { }
	if int((value as Dictionary).get("netw", 0)) != FORMAT_VERSION:
		return { }
	var body := String((value as Dictionary).get("body", ""))
	var raw := Marshalls.base64_to_raw(body)
	if raw.is_empty():
		return { }
	var held: Variant = bytes_to_var(raw)
	return held if typeof(held) == TYPE_DICTIONARY else { }


## Returns the storage collection holding [param slot]'s records.
func collection_for(slot: String) -> String:
	return "%s_%s" % [app, encoded(slot)]


## Writes [param slot]'s manifest entry, then resolves a
## [NakamaDatabase.Connection] over its collection. It rejects with
## [constant @GlobalScope.ERR_UNCONFIGURED] when [member wrapper] is unset, and
## with the code of a manifest write that failed.
func _open(session: Object, slot: StringName) -> NetwPromise:
	var promise := NetwPromise.new()
	_open_async(promise, String(slot))
	return promise


## Resolves every slot the manifest lists, reading it page by page. It rejects
## with [constant @GlobalScope.ERR_UNCONFIGURED] when [member wrapper] is unset,
## and with the code of the first page that failed.
func _list_slots(session: Object) -> NetwPromise:
	var promise := NetwPromise.new()
	_list_slots_async(promise)
	return promise


## Deletes every record in [param slot], then its manifest entry, and resolves
## [constant @GlobalScope.OK] or the code of the first request that failed. A
## failure leaves the slot listed, so the delete can run again. It rejects with
## [constant @GlobalScope.ERR_UNCONFIGURED] when [member wrapper] is unset.
func _delete_slot(session: Object, slot: StringName) -> NetwPromise:
	var promise := NetwPromise.new()
	_delete_slot_async(promise, String(slot))
	return promise


# Writes the slot's manifest entry, then answers a connection over its
# collection. The manifest entry lands first, so a slot holding records is
# never missing from the listing.
func _open_async(promise: NetwPromise, slot: String) -> void:
	if wrapper == null:
		promise.reject(ERR_UNCONFIGURED, "this database has no Nakama wrapper")
		return
	var noted: Dictionary = await wrapper.write_storage_objects(
		[
			{
				"collection": app,
				"key": encoded(slot),
				"read": 1,
				"write": 1,
				"value": JSON.stringify({ "slot": slot }),
			},
		],
	)
	if int(noted["error"]) != OK:
		promise.reject(int(noted["error"]), String(noted["detail"]))
		return
	promise.resolve(NakamaDatabase.Connection.new(wrapper, collection_for(slot)))


# Pages the whole manifest collection, so a slot an earlier run wrote appears.
func _list_slots_async(promise: NetwPromise) -> void:
	if wrapper == null:
		promise.reject(ERR_UNCONFIGURED, "this database has no Nakama wrapper")
		return
	var names := PackedStringArray()
	var cursor := ""
	while true:
		var answer: Dictionary = await wrapper.list_storage_objects(
			app,
			100,
			cursor,
			wrapper.own_user_id(),
		)
		if int(answer["error"]) != OK:
			promise.reject(int(answer["error"]), String(answer["detail"]))
			return
		var objects: Array = answer["objects"]
		for object in objects:
			var name := _slot_name_of(object)
			if not names.has(name):
				names.append(name)
		var next := String(answer["cursor"])
		if next.is_empty() or objects.is_empty():
			break
		cursor = next
	names.sort()
	promise.resolve(names)


# Reads a manifest row's slot name, falling back to the key it was stored under.
func _slot_name_of(object: Dictionary) -> String:
	var value: Variant = object.get("value")
	if typeof(value) == TYPE_DICTIONARY and (value as Dictionary).has("slot"):
		return String((value as Dictionary)["slot"])
	return String(object.get("key", "")).uri_decode()


# Drains every remote page of the slot's collection before dropping its
# manifest entry, so no schema is left behind and a failure stays retryable.
func _delete_slot_async(promise: NetwPromise, slot: String) -> void:
	if wrapper == null:
		promise.reject(ERR_UNCONFIGURED, "this database has no Nakama wrapper")
		return
	var collection := collection_for(slot)
	while true:
		var answer: Dictionary = await wrapper.list_storage_objects(
			collection,
			100,
			"",
			wrapper.own_user_id(),
		)
		if int(answer["error"]) != OK:
			promise.resolve(int(answer["error"]))
			return
		var objects: Array = answer["objects"]
		if objects.is_empty():
			break
		var ids: Array = []
		for object in objects:
			ids.append({ "collection": collection, "key": String(object["key"]) })
		var removed: Dictionary = await wrapper.delete_storage_objects(ids)
		if int(removed["error"]) != OK:
			promise.resolve(int(removed["error"]))
			return
	var dropped: Dictionary = await wrapper.delete_storage_objects(
		[{ "collection": app, "key": encoded(slot) }],
	)
	promise.resolve(int(dropped["error"]))


## One open slot of Nakama storage, and the only object that performs its I/O.
##
## Every verb resolves once the service has answered, never once a request has
## been queued, so an acknowledged write is a write Nakama holds.
class Connection extends NetwDatabaseConnection:

	## The [NakamaWrapper] this connection performs its storage through.
	var wrapper

	## The Nakama storage collection holding this slot's records.
	var collection: String

	func _init(p_wrapper, p_collection: String) -> void:
		wrapper = p_wrapper
		collection = p_collection

	## Reads the object at [param address]. A missing object is
	## [code]found[/code] false at [constant @GlobalScope.OK], and a failed read
	## carries its code in [code]error[/code]. A value this library did not
	## write answers an empty envelope.
	func _read(address: Dictionary) -> NetwPromise:
		var promise := NetwPromise.new()
		_read_async(promise, address)
		return promise

	## Reads one page of the collection and keeps the records of the requested
	## schema. A failed page carries its code in [code]error[/code]. Nakama
	## cannot filter by key, so a page can hold no records and still carry a
	## cursor.
	func _scan(request: Dictionary) -> NetwPromise:
		var promise := NetwPromise.new()
		_scan_async(promise, request)
		return promise

	## Applies each operation as its own request, in order, so the batch is not
	## atomic. [code]error[/code] is always [constant @GlobalScope.OK], each
	## operation's code lands in [code]errors[/code], and an uncertain code sets
	## [code]uncertain[/code].
	func _write_batch(operations: Array) -> NetwPromise:
		var promise := NetwPromise.new()
		_write_batch_async(promise, operations)
		return promise

	## Resolves [constant @GlobalScope.OK], because the connection holds nothing
	## to release.
	func _close() -> NetwPromise:
		return NetwPromise.resolved(OK)

	# A record the service does not hold is absence at OK. A record it could not
	# be asked about carries the error the service gave.
	func _read_async(promise: NetwPromise, address: Dictionary) -> void:
		var answer: Dictionary = await wrapper.read_storage_objects(
			[
				{
					"collection": collection,
					"key": NakamaDatabase.address_key(address),
				},
			],
		)
		var reply := {
			"error": int(answer["error"]),
			"detail": String(answer["detail"]),
			"found": false,
		}
		var objects: Array = answer["objects"]
		if int(answer["error"]) == OK and not objects.is_empty():
			reply["found"] = true
			reply["envelope"] = NakamaDatabase.unpacked(objects[0]["value"])
		promise.resolve(reply)

	# Nakama pages a whole collection and cannot filter by key prefix, so a page
	# holding only other schemas answers no records and a cursor that continues.
	func _scan_async(promise: NetwPromise, request: Dictionary) -> void:
		var prefix := NakamaDatabase.address_prefix(
			int(request.get("kind", 0)),
			String(request.get("schema_name", "")),
		)
		var answer: Dictionary = await wrapper.list_storage_objects(
			collection,
			int(request.get("limit", 100)),
			String(request.get("cursor", "")),
			wrapper.own_user_id(),
		)
		var reply := {
			"error": int(answer["error"]),
			"detail": String(answer["detail"]),
			"records": [],
			"cursor": "",
		}
		if int(answer["error"]) != OK:
			promise.resolve(reply)
			return
		var records: Array = []
		for object in answer["objects"]:
			var name := String(object["key"])
			if not name.begins_with(prefix):
				continue
			records.append(
				{
					"key": name.substr(prefix.length()).uri_decode(),
					"envelope": NakamaDatabase.unpacked(object["value"]),
				},
			)
		reply["records"] = records
		reply["cursor"] = String(answer["cursor"])
		promise.resolve(reply)

	# Operations are applied one at a time in the order they were given, and
	# each one's own answer is reported. A batch is not atomic here.
	func _write_batch_async(promise: NetwPromise, operations: Array) -> void:
		var errors := PackedInt32Array()
		var uncertain := PackedByteArray()
		for operation in operations:
			var answer := await _apply(operation)
			errors.append(int(answer["error"]))
			uncertain.append(1 if bool(answer["uncertain"]) else 0)
		promise.resolve(
			{
				"error": OK,
				"detail": "",
				"errors": errors,
				"uncertain": uncertain,
			},
		)

	# Performs one operation and answers what the service said about it.
	func _apply(operation: Dictionary) -> Dictionary:
		var key := NakamaDatabase.address_key(operation.get("address", { }))
		var kind := String(operation.get("kind", ""))
		if kind == "replace":
			return await wrapper.write_storage_objects(
				[
					{
						"collection": collection,
						"key": key,
						"read": 1,
						"write": 1,
						"value": NakamaDatabase.packed(
							operation.get("envelope", { }),
						),
					},
				],
			)
		if kind == "erase":
			return await wrapper.delete_storage_objects(
				[{ "collection": collection, "key": key }],
			)
		return NakamaWrapper.storage_answer(
			ERR_INVALID_DATA,
			"'%s' is no storage operation" % kind,
			false,
		)
