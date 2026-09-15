#pragma once

#include "netw_test.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw_test {

struct RowImage {
    godot::Vector<uint64_t> bit_words;
    godot::Vector<uint64_t> element_codes;
    godot::Vector<int32_t> element_column;
    godot::Vector<int32_t> element_index;
    int64_t bit_count = 0;
    int32_t column_count = 0;
    bool taken = false;
};

inline RowImage take_row_image(
    const netw::wire::WirePlan &p_plan,
    const netw::wire::CodeRow &p_row
) {
    RowImage image;
    if (!p_plan.valid()) {
        return image;
    }
    image.bit_count = p_plan.row_bits();
    image.column_count = int32_t(p_plan.column_count());
    for (int64_t at = 0; at < image.bit_count; at += 64) {
        const int64_t left = image.bit_count - at;
        const int span = left < 64 ? int(left) : 64;
        image.bit_words.push_back(p_row.read_bits(at, span));
    }
    for (int32_t column = 0; column < image.column_count; ++column) {
        const netw::wire::ColumnPlan &slot = p_plan.column(uint32_t(column));
        for (int element = 0; element < slot.stride; ++element) {
            image.element_codes.push_back(p_row.read(slot, element));
            image.element_column.push_back(column);
            image.element_index.push_back(int32_t(element));
        }
    }
    image.taken = true;
    return image;
}

enum class ImageDisagreement : uint8_t {
    NONE,
    ABSENT,
    SHAPE,
    BITS,
};

struct ImageComparison {
    ImageDisagreement disagreement = ImageDisagreement::NONE;
    int64_t differing_bit = -1;
    int64_t differing_column = -1;
    int64_t differing_element = -1;
    uint64_t staged_code = 0;
    uint64_t accepted_code = 0;

    bool agrees() const {
        return disagreement == ImageDisagreement::NONE;
    }
};

inline ImageComparison compare_row_images(
    const RowImage &p_staged,
    const RowImage &p_accepted
) {
    ImageComparison verdict;
    if (!p_staged.taken || !p_accepted.taken) {
        verdict.disagreement = ImageDisagreement::ABSENT;
        return verdict;
    }
    if (p_staged.bit_count != p_accepted.bit_count
        || p_staged.column_count != p_accepted.column_count
        || p_staged.bit_words.size() != p_accepted.bit_words.size()
        || p_staged.element_codes.size() != p_accepted.element_codes.size()) {
        verdict.disagreement = ImageDisagreement::SHAPE;
        return verdict;
    }
    for (int32_t word = 0; word < p_staged.bit_words.size(); ++word) {
        const uint64_t differ
            = p_staged.bit_words[word] ^ p_accepted.bit_words[word];
        if (differ == 0) {
            continue;
        }
        int lowest = 0;
        while (((differ >> lowest) & uint64_t(1)) == 0) {
            ++lowest;
        }
        verdict.disagreement = ImageDisagreement::BITS;
        verdict.differing_bit = int64_t(word) * 64 + lowest;
        break;
    }
    if (verdict.agrees()) {
        return verdict;
    }
    for (int32_t slot = 0; slot < p_staged.element_codes.size(); ++slot) {
        if (p_staged.element_codes[slot] == p_accepted.element_codes[slot]) {
            continue;
        }
        verdict.differing_column = int64_t(p_staged.element_column[slot]);
        verdict.differing_element = int64_t(p_staged.element_index[slot]);
        verdict.staged_code = p_staged.element_codes[slot];
        verdict.accepted_code = p_accepted.element_codes[slot];
        break;
    }
    return verdict;
}

struct RowImageLedger {
    struct Entry {
        uint64_t token = 0;
        uint64_t revision = 0;
        RowImage staged;
        RowImage accepted;
        bool has_staged = false;
        bool has_accepted = false;
        bool restated = false;
    };

    struct Verdict {
        int64_t agreed = 0;
        int64_t diverged = 0;
        int64_t accepted_unstaged = 0;
        int64_t staged_unaccepted = 0;
        int64_t restated_revisions = 0;
        uint64_t fault_token = 0;
        uint64_t fault_revision = 0;
        ImageComparison fault;

        bool clean() const {
            return diverged == 0 && accepted_unstaged == 0
                && restated_revisions == 0;
        }
    };

    godot::Vector<Entry> entries;

    int32_t slot_of(uint64_t p_token, uint64_t p_revision) const {
        for (int32_t at = 0; at < entries.size(); ++at) {
            if (entries[at].token == p_token
                && entries[at].revision == p_revision) {
                return at;
            }
        }
        return -1;
    }

    Entry &open(uint64_t p_token, uint64_t p_revision) {
        const int32_t found = slot_of(p_token, p_revision);
        if (found >= 0) {
            return entries.ptrw()[found];
        }
        Entry made;
        made.token = p_token;
        made.revision = p_revision;
        entries.push_back(made);
        return entries.ptrw()[entries.size() - 1];
    }

    void stage(
        uint64_t p_token,
        uint64_t p_revision,
        const netw::wire::WirePlan &p_plan,
        const netw::wire::CodeRow &p_row
    ) {
        Entry &entry = open(p_token, p_revision);
        const RowImage image = take_row_image(p_plan, p_row);
        if (entry.has_staged
            && !compare_row_images(entry.staged, image).agrees()) {
            entry.restated = true;
        }
        entry.staged = image;
        entry.has_staged = true;
    }

    void accept(
        uint64_t p_token,
        uint64_t p_revision,
        const netw::wire::WirePlan &p_plan,
        const netw::wire::CodeRow &p_row
    ) {
        Entry &entry = open(p_token, p_revision);
        const RowImage image = take_row_image(p_plan, p_row);
        if (entry.has_accepted
            && !compare_row_images(entry.accepted, image).agrees()) {
            entry.restated = true;
        }
        entry.accepted = image;
        entry.has_accepted = true;
    }

    Verdict audit() const {
        Verdict verdict;
        for (int32_t at = 0; at < entries.size(); ++at) {
            const Entry &entry = entries[at];
            if (entry.restated) {
                ++verdict.restated_revisions;
            }
            if (!entry.has_accepted) {
                if (entry.has_staged) {
                    ++verdict.staged_unaccepted;
                }
                continue;
            }
            if (!entry.has_staged) {
                ++verdict.accepted_unstaged;
                if (verdict.fault.agrees()) {
                    verdict.fault_token = entry.token;
                    verdict.fault_revision = entry.revision;
                    verdict.fault.disagreement = ImageDisagreement::ABSENT;
                }
                continue;
            }
            const ImageComparison compared
                = compare_row_images(entry.staged, entry.accepted);
            if (compared.agrees()) {
                ++verdict.agreed;
                continue;
            }
            ++verdict.diverged;
            if (verdict.fault.agrees()) {
                verdict.fault_token = entry.token;
                verdict.fault_revision = entry.revision;
                verdict.fault = compared;
            }
        }
        return verdict;
    }
};

} // namespace netw_test
