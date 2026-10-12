#include "netw/persist/record_file.hpp"

#include "godot/file_access.hpp"
#include "godot/file_system.hpp"
#include "godot/utility.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const uint8_t MAGIC[] = {'N', 'E', 'T', 'W', 'D', 'I', 'C', 'T'};
constexpr int MAGIC_LENGTH = 8;

bool is_word_start(char32_t p_char) {
    return (p_char >= 'a' && p_char <= 'z') || (p_char >= 'A' && p_char <= 'Z')
        || p_char == '_';
}

bool is_word_part(char32_t p_char) {
    return is_word_start(p_char) || (p_char >= '0' && p_char <= '9');
}

bool builds_an_object(const String &p_word) {
    return p_word == "Object" || p_word == "Resource" || p_word == "SubResource"
        || p_word == "ExtResource";
}

bool names_an_object(const String &p_text) {
    const int64_t length = p_text.length();
    int64_t at = 0;
    while (at < length) {
        const char32_t here = p_text[at];
        if (here == '"') {
            at += 1;
            while (at < length && p_text[at] != '"') {
                at += p_text[at] == '\\' ? 2 : 1;
            }
            at += 1;
            continue;
        }
        if (here == ';') {
            while (at < length && p_text[at] != '\n') {
                at += 1;
            }
            continue;
        }
        if (is_word_start(here)) {
            const int64_t start = at;
            while (at < length && is_word_part(p_text[at])) {
                at += 1;
            }
            if (builds_an_object(p_text.substr(start, at - start))) {
                return true;
            }
            continue;
        }
        at += 1;
    }
    return false;
}

Dictionary decoded_binary(const PackedByteArray &p_bytes) {
    if (p_bytes.size() < MAGIC_LENGTH) {
        return Dictionary();
    }
    for (int at = 0; at < MAGIC_LENGTH; ++at) {
        if (uint8_t(p_bytes[at]) != MAGIC[at]) {
            return Dictionary();
        }
    }
    const Variant held = gd::bytes_to_var(p_bytes.slice(MAGIC_LENGTH));
    if (held.get_type() != Variant::DICTIONARY) {
        return Dictionary();
    }
    return Dictionary(held);
}

Dictionary decoded_text(const PackedByteArray &p_bytes) {
    const String text = gd::utf8_text(p_bytes);
    if (names_an_object(text)) {
        return Dictionary();
    }
    const Variant held = gd::str_to_var(text);
    if (held.get_type() != Variant::DICTIONARY) {
        return Dictionary();
    }
    return Dictionary(held);
}

} // namespace

const char *extension_of(RecordFormat p_format) {
    return p_format == RecordFormat::TEXT ? "tdict" : "dict";
}

bool format_of_path(const String &p_path, RecordFormat &r_format) {
    const String extension = p_path.get_extension().to_lower();
    if (extension == extension_of(RecordFormat::BINARY)) {
        r_format = RecordFormat::BINARY;
        return true;
    }
    if (extension == extension_of(RecordFormat::TEXT)) {
        r_format = RecordFormat::TEXT;
        return true;
    }
    return false;
}

RecordFormat other_format(RecordFormat p_format) {
    return p_format == RecordFormat::TEXT ? RecordFormat::BINARY
                                          : RecordFormat::TEXT;
}

PackedByteArray encoded(const Dictionary &p_envelope, RecordFormat p_format) {
    if (p_format == RecordFormat::TEXT) {
        return (gd::var_to_str(p_envelope) + String("\n")).to_utf8_buffer();
    }
    PackedByteArray bytes;
    for (int at = 0; at < MAGIC_LENGTH; ++at) {
        bytes.push_back(MAGIC[at]);
    }
    bytes.append_array(gd::var_to_bytes(p_envelope));
    return bytes;
}

Dictionary decoded(const PackedByteArray &p_bytes, RecordFormat p_format) {
    return p_format == RecordFormat::TEXT ? decoded_text(p_bytes)
                                          : decoded_binary(p_bytes);
}

String staging_path_for(const String &p_path) {
    return p_path + String(".tmp");
}

Error read_record_file(const String &p_path, Dictionary &r_envelope) {
    RecordFormat format = RecordFormat::BINARY;
    if (!format_of_path(p_path, format)) {
        return ERR_FILE_UNRECOGNIZED;
    }
    if (!gd::file_exists(p_path)) {
        return ERR_FILE_NOT_FOUND;
    }
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
    if (file.is_null()) {
        return ERR_FILE_CANT_OPEN;
    }
    const PackedByteArray bytes = file->get_buffer(int64_t(file->get_length()));
    file->close();
    r_envelope = decoded(bytes, format);
    return OK;
}

Error write_record_file(const String &p_path, const Dictionary &p_envelope) {
    RecordFormat format = RecordFormat::BINARY;
    if (!format_of_path(p_path, format)) {
        return ERR_FILE_UNRECOGNIZED;
    }
    const String folder = p_path.get_base_dir();
    if (!DirAccess::dir_exists_absolute(folder)) {
        const Error made = DirAccess::make_dir_recursive_absolute(folder);
        if (made != OK) {
            return made;
        }
    }

    const String staging = staging_path_for(p_path);
    Ref<FileAccess> file = FileAccess::open(staging, FileAccess::WRITE);
    if (file.is_null()) {
        return ERR_FILE_CANT_WRITE;
    }
    file->store_buffer(encoded(p_envelope, format));
    file->flush();
    file->close();
    file = Ref<FileAccess>();

    const Error moved = DirAccess::rename_absolute(staging, p_path);
    if (moved != OK) {
        DirAccess::remove_absolute(staging);
    }
    return moved;
}

} // namespace netw::persist
