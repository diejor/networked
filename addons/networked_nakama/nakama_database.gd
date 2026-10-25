## A [NetwDatabaseBackend] that keeps records in the Nakama storage of the
## session's [NakamaAccount].
##
## Every record is a Nakama storage object owned by the signed in user, so saves
## follow the account across devices. Each slot is one storage collection.
## [codeblock]
## Netw.configure_database(self, &"saves").backend(NakamaDatabase.new())
## [/codeblock]
## Storage requests settle with one of these codes. An uncertain code means the
## request was sent but Nakama never confirmed whether it applied.
## [codeblock lang=text]
## OK                    Nakama applied the request
## ERR_UNCONFIGURED      the session has no NakamaAccount
## ERR_UNAUTHORIZED      not signed in, or Nakama answered 401 or 403
## ERR_INVALID_DATA      Nakama answered another 4xx status
## ERR_TIMEOUT           canceled, or ended with no HTTP status. Uncertain
## ERR_CONNECTION_ERROR  Nakama answered nothing or a 5xx status. Uncertain
## FAILED                any other exception. Uncertain
## [/codeblock]
class_name NakamaDatabase
extends NetwDatabaseBackend

## Format version written into every stored value. A value with another version
## fails to read.
const FORMAT_VERSION := 1

## The collection listing every slot, and the prefix of each slot's collection.
@export var app := "netw_saves"


## Encodes [param text] for use in a Nakama collection or key. Different names
## always encode differently.
static func encoded(text: String) -> String:
	return text.uri_encode().replace(".", "%2E")


## Returns the storage key for [param address]. A record and a table snapshot
## with the same key are stored separately.
static func address_key(address: Dictionary) -> String:
	return "%d_%s_%s" % [
		int(address.get("kind", 0)),
		encoded(String(address.get("schema_name", ""))),
		encoded(String(address.get("key", ""))),
	]


## Returns the key prefix shared by every record of [param schema_name] and
## [param kind].
static func address_prefix(kind: int, schema_name: String) -> String:
	return "%d_%s_" % [kind, encoded(schema_name)]


## Encodes [param envelope] as the JSON string Nakama stores, with the envelope
## as base64 of [method @GlobalScope.var_to_bytes].
static func packed(envelope: Dictionary) -> String:
	return JSON.stringify(
		{
			"netw": FORMAT_VERSION,
			"body": Marshalls.raw_to_base64(var_to_bytes(envelope)),
		},
	)


## Decodes a stored [param value] back into an envelope. Returns an empty
## [Dictionary] for a value this library did not write, which fails the read
## with [constant @GlobalScope.ERR_FILE_UNRECOGNIZED].
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


static func _answer(error: Error, detail: String, uncertain: bool) -> Dictionary:
	return { "error": error, "detail": detail, "uncertain": uncertain }


static func _fault(result: Variant) -> Dictionary:
	if result == null:
		return _answer(ERR_CONNECTION_ERROR, "Nakama answered nothing", true)
	var exception: Variant = result.get_exception()
	var detail := String(exception.message)
	var status := int(exception.status_code)
	if bool(exception.cancelled) or status < 0:
		return _answer(ERR_TIMEOUT, detail, true)
	if status == 401 or status == 403:
		return _answer(ERR_UNAUTHORIZED, detail, false)
	if status >= 500:
		return _answer(ERR_CONNECTION_ERROR, detail, true)
	if status >= 400:
		return _answer(ERR_INVALID_DATA, detail, false)
	return _answer(FAILED, detail, true)


static func _write(account: NakamaAccount, writes: Array) -> Dictionary:
	if account.session == null:
		return _answer(ERR_UNAUTHORIZED, "no signed in Nakama account", false)
	var result = await account.client.write_storage_objects_async(
		account.session,
		writes,
	)
	if result == null or result.is_exception():
		return _fault(result)
	return _answer(OK, "", false)


static func _delete(account: NakamaAccount, keys: Array) -> Dictionary:
	if account.session == null:
		return _answer(ERR_UNAUTHORIZED, "no signed in Nakama account", false)
	var result = await account.client.delete_storage_objects_async(
		account.session,
		keys,
	)
	if result == null or result.is_exception():
		return _fault(result)
	return _answer(OK, "", false)


static func _list(
		account: NakamaAccount,
		collection: String,
		limit: int,
		cursor: String,
) -> Dictionary:
	if account.session == null:
		var unsigned := _answer(
			ERR_UNAUTHORIZED,
			"no signed in Nakama account",
			false,
		)
		unsigned["objects"] = []
		unsigned["cursor"] = ""
		return unsigned
	var result = await account.client.list_storage_objects_async(
		account.session,
		collection,
		account.user_id,
		limit,
		cursor,
	)
	var answer := _answer(OK, "", false)
	answer["objects"] = []
	answer["cursor"] = ""
	if result == null or result.is_exception():
		answer.merge(_fault(result), true)
		return answer
	answer["objects"] = result.objects
	answer["cursor"] = String(result.cursor) if result.cursor != null else ""
	return answer


## Returns the storage collection holding [param slot]'s records.
func collection_for(slot: String) -> String:
	return "%s_%s" % [app, encoded(slot)]


## Adds [param slot] to the slot list, then resolves a
## [NakamaDatabase.Connection] for its collection.
func _open(session: Object, slot: StringName) -> NetwPromise:
	var promise := NetwPromise.new()
	_open_async(promise, session, String(slot))
	return promise


## Resolves every slot in the slot list.
func _list_slots(session: Object) -> NetwPromise:
	var promise := NetwPromise.new()
	_list_slots_async(promise, session)
	return promise


## Deletes every record in [param slot] and removes it from the slot list. On
## failure the slot stays listed, so the delete can be retried.
func _delete_slot(session: Object, slot: StringName) -> NetwPromise:
	var promise := NetwPromise.new()
	_delete_slot_async(promise, session, String(slot))
	return promise


func _signed_in_account(
		session: Object,
		promise: NetwPromise,
) -> NakamaAccount:
	var account: NakamaAccount = null
	if session != null:
		account = session.service_get(NakamaAccount) as NakamaAccount
	if account == null:
		promise.reject(
			ERR_UNCONFIGURED,
			"NakamaDatabase has no NakamaAccount to store records with. Add a "
			+ "NakamaAccount node to the session.",
		)
		return null
	var signing_in := account.sign_in()
	await signing_in.wait()
	if signing_in.is_failed:
		promise.reject(signing_in.code, signing_in.detail)
		return null
	return account


# Writes the slot's manifest entry, then answers a connection over its
# collection. The manifest entry lands first, so a slot holding records is
# never missing from the listing.
func _open_async(promise: NetwPromise, session: Object, slot: String) -> void:
	var account := await _signed_in_account(session, promise)
	if account == null:
		return
	var noted := await NakamaDatabase._write(
		account,
		[
			NakamaWriteStorageObject.new(
				app,
				encoded(slot),
				1,
				1,
				JSON.stringify({ "slot": slot }),
				"",
			),
		],
	)
	if noted.error != OK:
		promise.reject(noted.error, noted.detail)
		return
	promise.resolve(NakamaDatabase.Connection.new(account, collection_for(slot)))


# Pages the whole manifest collection, so a slot an earlier run wrote appears.
func _list_slots_async(promise: NetwPromise, session: Object) -> void:
	var account := await _signed_in_account(session, promise)
	if account == null:
		return
	var names := PackedStringArray()
	var cursor := ""
	while true:
		var answer := await NakamaDatabase._list(account, app, 100, cursor)
		if answer.error != OK:
			promise.reject(answer.error, answer.detail)
			return
		for object in answer.objects:
			var stored: Variant = JSON.parse_string(object.value)
			var name: String = object.key.uri_decode()
			match stored:
				{ "slot": var named, .. } when named is String:
					name = named
			if not names.has(name):
				names.append(name)
		cursor = answer.cursor
		if cursor.is_empty() or answer.objects.is_empty():
			break
	names.sort()
	promise.resolve(names)


# Drains every remote page of the slot's collection before dropping its
# manifest entry, so no schema is left behind and a failure stays retryable.
func _delete_slot_async(
		promise: NetwPromise,
		session: Object,
		slot: String,
) -> void:
	var account := await _signed_in_account(session, promise)
	if account == null:
		return
	var collection := collection_for(slot)
	while true:
		var answer := await NakamaDatabase._list(account, collection, 100, "")
		if answer.error != OK:
			promise.resolve(answer.error)
			return
		if answer.objects.is_empty():
			break
		var keys: Array = []
		for object in answer.objects:
			keys.append(NakamaStorageObjectId.new(collection, object.key, "", ""))
		var removed := await NakamaDatabase._delete(account, keys)
		if removed.error != OK:
			promise.resolve(removed.error)
			return
	var dropped := await NakamaDatabase._delete(
		account,
		[NakamaStorageObjectId.new(app, encoded(slot), "", "")],
	)
	promise.resolve(dropped.error)


## One open slot of Nakama storage. Every method resolves after Nakama answers.
class Connection extends NetwDatabaseConnection:
	## The [NakamaAccount] whose storage holds this slot.
	var account: NakamaAccount

	## The Nakama storage collection holding this slot's records.
	var collection: String


	func _init(p_account: NakamaAccount, p_collection: String) -> void:
		account = p_account
		collection = p_collection


	## Reads the object at [param address]. A missing object is
	## [code]found[/code] [code]false[/code] with [constant @GlobalScope.OK].
	func _read(address: Dictionary) -> NetwPromise:
		var promise := NetwPromise.new()
		_read_async(promise, address)
		return promise


	## Reads one page of the collection and keeps the records of the requested
	## schema. A page can hold no records and still return a cursor.
	func _scan(request: Dictionary) -> NetwPromise:
		var promise := NetwPromise.new()
		_scan_async(promise, request)
		return promise


	## Sends each operation as its own request, in order. Each operation's code
	## is in [code]errors[/code], and [code]error[/code] is always
	## [constant @GlobalScope.OK].
	func _write_batch(operations: Array) -> NetwPromise:
		var promise := NetwPromise.new()
		_write_batch_async(promise, operations)
		return promise


	## Resolves [constant @GlobalScope.OK].
	func _close() -> NetwPromise:
		return NetwPromise.resolved(OK)


	# A record the service does not hold is absence at OK. A record it could not
	# be asked about carries the error the service gave.
	func _read_async(promise: NetwPromise, address: Dictionary) -> void:
		var reply := {
			"error": ERR_UNAUTHORIZED,
			"detail": "no signed in Nakama account",
			"found": false,
		}
		if account.session == null:
			promise.resolve(reply)
			return
		var result = await account.client.read_storage_objects_async(
			account.session,
			[
				NakamaStorageObjectId.new(
					collection,
					NakamaDatabase.address_key(address),
					account.user_id,
					"",
				),
			],
		)
		if result == null or result.is_exception():
			var fault := NakamaDatabase._fault(result)
			reply.error = fault.error
			reply.detail = fault.detail
			promise.resolve(reply)
			return
		reply.error = OK
		reply.detail = ""
		if not result.objects.is_empty():
			reply["found"] = true
			reply["envelope"] = NakamaDatabase.unpacked(
				JSON.parse_string(result.objects[0].value),
			)
		promise.resolve(reply)


	# Nakama pages a whole collection and cannot filter by key prefix, so a page
	# holding only other schemas answers no records and a cursor that continues.
	func _scan_async(promise: NetwPromise, request: Dictionary) -> void:
		var prefix := NakamaDatabase.address_prefix(
			int(request.get("kind", 0)),
			String(request.get("schema_name", "")),
		)
		var answer := await NakamaDatabase._list(
			account,
			collection,
			int(request.get("limit", 100)),
			String(request.get("cursor", "")),
		)
		var reply := {
			"error": answer.error,
			"detail": answer.detail,
			"records": [],
			"cursor": "",
		}
		if answer.error != OK:
			promise.resolve(reply)
			return
		var records: Array = []
		for object in answer.objects:
			var name: String = object.key
			if not name.begins_with(prefix):
				continue
			records.append(
				{
					"key": name.substr(prefix.length()).uri_decode(),
					"envelope": NakamaDatabase.unpacked(
						JSON.parse_string(object.value),
					),
				},
			)
		reply["records"] = records
		reply["cursor"] = answer.cursor
		promise.resolve(reply)


	# Operations are applied one at a time in the order they were given, and
	# each one's own answer is reported. A batch is not atomic here.
	func _write_batch_async(promise: NetwPromise, operations: Array) -> void:
		var errors := PackedInt32Array()
		var uncertain := PackedByteArray()
		for operation: Dictionary in operations:
			var key := NakamaDatabase.address_key(operation.get("address", { }))
			var answer := NakamaDatabase._answer(
				ERR_INVALID_DATA,
				"'%s' is no storage operation" % operation.get("kind"),
				false,
			)
			match operation:
				{ "kind": "replace", .. }:
					answer = await NakamaDatabase._write(
						account,
						[
							NakamaWriteStorageObject.new(
								collection,
								key,
								1,
								1,
								NakamaDatabase.packed(
									operation.get("envelope", { }),
								),
								"",
							),
						],
					)
				{ "kind": "erase", .. }:
					answer = await NakamaDatabase._delete(
						account,
						[NakamaStorageObjectId.new(collection, key, "", "")],
					)
			errors.append(answer.error)
			uncertain.append(1 if answer.uncertain else 0)
		promise.resolve(
			{
				"error": OK,
				"detail": "",
				"errors": errors,
				"uncertain": uncertain,
			},
		)
