#include "netw/persist/record_format.hpp"

#include "godot/class_db.hpp"
#include "godot/resource_format.hpp"
#include "netw/api/record.hpp"
#include "netw/persist/record_file.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *RECORD_TYPE = "DictionaryRecord";

bool is_record_path(const String &p_path) {
    RecordFormat format = RecordFormat::BINARY;
    return format_of_path(p_path, format);
}

bool handles_record_type(const String &p_type) {
    return p_type == RECORD_TYPE || p_type == "NetwRecord"
        || p_type == "Resource";
}

Ref<Resource> loaded_record(const String &p_path, Error &r_error) {
    Dictionary envelope;
    r_error = read_record_file(p_path, envelope);
    if (r_error != OK) {
        return Ref<Resource>();
    }
    if (envelope.is_empty()) {
        r_error = ERR_FILE_UNRECOGNIZED;
        return Ref<Resource>();
    }
    Ref<DictionaryRecord> record;
    record.instantiate();
    record->set_data(envelope);
    return record;
}

Error saved_record(const Ref<Resource> &p_resource, const String &p_path) {
    DictionaryRecord *record
        = Object::cast_to<DictionaryRecord>(p_resource.ptr());
    if (record == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    return write_record_file(p_path, record->get_data());
}

bool is_record(const Ref<Resource> &p_resource) {
    return Object::cast_to<DictionaryRecord>(p_resource.ptr()) != nullptr;
}

#if defined(NETW_MODULE)

class DictionaryRecordLoader : public ResourceFormatLoader {
    GDSOFTCLASS(DictionaryRecordLoader, ResourceFormatLoader);

public:
    Ref<Resource> load(
        const String &p_path,
        const String &p_original_path,
        Error *r_error,
        bool p_use_sub_threads,
        float *r_progress,
        CacheMode p_cache_mode
    ) override {
        Error answered = OK;
        const Ref<Resource> made = loaded_record(p_path, answered);
        if (r_error != nullptr) {
            *r_error = answered;
        }
        return made;
    }

    void get_recognized_extensions(List<String> *p_extensions) const override {
        p_extensions->push_back(extension_of(RecordFormat::BINARY));
        p_extensions->push_back(extension_of(RecordFormat::TEXT));
    }

    bool handles_type(const String &p_type) const override {
        return handles_record_type(p_type);
    }

    String get_resource_type(const String &p_path) const override {
        return is_record_path(p_path) ? String(RECORD_TYPE) : String();
    }

    bool has_custom_uid_support() const override {
        return true;
    }
};

class DictionaryRecordSaver : public ResourceFormatSaver {
    GDSOFTCLASS(DictionaryRecordSaver, ResourceFormatSaver);

public:
    Error save(
        const Ref<Resource> &p_resource,
        const String &p_path,
        uint32_t p_flags
    ) override {
        return saved_record(p_resource, p_path);
    }

    bool recognize(const Ref<Resource> &p_resource) const override {
        return is_record(p_resource);
    }

    void get_recognized_extensions(
        const Ref<Resource> &p_resource,
        List<String> *p_extensions
    ) const override {
        if (!is_record(p_resource)) {
            return;
        }
        p_extensions->push_back(extension_of(RecordFormat::BINARY));
        p_extensions->push_back(extension_of(RecordFormat::TEXT));
    }
};

#else

PackedStringArray record_extensions() {
    PackedStringArray out;
    out.push_back(extension_of(RecordFormat::BINARY));
    out.push_back(extension_of(RecordFormat::TEXT));
    return out;
}

class DictionaryRecordLoader : public ResourceFormatLoader {
    GDCLASS(DictionaryRecordLoader, ResourceFormatLoader);

protected:
    static void _bind_methods() {
    }

public:
    PackedStringArray _get_recognized_extensions() const override {
        return record_extensions();
    }

    bool _handles_type(const StringName &p_type) const override {
        return handles_record_type(p_type);
    }

    String _get_resource_type(const String &p_path) const override {
        return is_record_path(p_path) ? String(RECORD_TYPE) : String();
    }

    int64_t _get_resource_uid(const String &p_path) const override {
        return -1;
    }

    Variant _load(
        const String &p_path,
        const String &p_original_path,
        bool p_use_sub_threads,
        int32_t p_cache_mode
    ) const override {
        Error answered = OK;
        const Ref<Resource> made = loaded_record(p_path, answered);
        if (answered != OK) {
            return int(answered);
        }
        return made;
    }
};

class DictionaryRecordSaver : public ResourceFormatSaver {
    GDCLASS(DictionaryRecordSaver, ResourceFormatSaver);

protected:
    static void _bind_methods() {
    }

public:
    Error _save(
        const Ref<Resource> &p_resource,
        const String &p_path,
        uint32_t p_flags
    ) override {
        return saved_record(p_resource, p_path);
    }

    bool _recognize(const Ref<Resource> &p_resource) const override {
        return is_record(p_resource);
    }

    PackedStringArray _get_recognized_extensions(
        const Ref<Resource> &p_resource
    ) const override {
        return is_record(p_resource) ? record_extensions()
                                     : PackedStringArray();
    }
};

#endif

Ref<DictionaryRecordLoader> &installed_loader() {
    static Ref<DictionaryRecordLoader> held;
    return held;
}

Ref<DictionaryRecordSaver> &installed_saver() {
    static Ref<DictionaryRecordSaver> held;
    return held;
}

} // namespace

void install_record_formats() {
#if defined(NETW_MODULE)
    installed_loader().instantiate();
    ResourceLoader::add_resource_format_loader(installed_loader());
    installed_saver().instantiate();
    ResourceSaver::add_resource_format_saver(installed_saver());
#else
    GDREGISTER_INTERNAL_CLASS(DictionaryRecordLoader);
    GDREGISTER_INTERNAL_CLASS(DictionaryRecordSaver);
    installed_loader().instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(
        installed_loader()
    );
    installed_saver().instantiate();
    ResourceSaver::get_singleton()->add_resource_format_saver(
        installed_saver()
    );
#endif
}

void remove_record_formats() {
    if (installed_loader().is_valid()) {
#if defined(NETW_MODULE)
        ResourceLoader::remove_resource_format_loader(installed_loader());
#else
        ResourceLoader::get_singleton()->remove_resource_format_loader(
            installed_loader()
        );
#endif
        installed_loader().unref();
    }
    if (installed_saver().is_valid()) {
#if defined(NETW_MODULE)
        ResourceSaver::remove_resource_format_saver(installed_saver());
#else
        ResourceSaver::get_singleton()->remove_resource_format_saver(
            installed_saver()
        );
#endif
        installed_saver().unref();
    }
}

} // namespace netw::persist
