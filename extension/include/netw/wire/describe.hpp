#pragma once

#include <cstdint>
#include <tuple>
#include <type_traits>

#include "godot/variant.hpp"
#include "netw/wire/stream.hpp"

namespace netw::wire {

enum class SpecKind {
    BITS,
    INT_RANGE,
    VARUINT,
    SVARINT,
    BOOL1,
    BYTES_CAPPED,
};

struct Spec {
    SpecKind kind = SpecKind::BOOL1;
    int64_t low = 0;
    int64_t high = 0;
};

constexpr Spec bits(int count) {
    return Spec{SpecKind::BITS, count, 0};
}

constexpr Spec int_range(int64_t low, int64_t high) {
    return Spec{SpecKind::INT_RANGE, low, high};
}

constexpr Spec varuint(int max_bytes = 10) {
    return Spec{SpecKind::VARUINT, max_bytes, 0};
}

constexpr Spec svarint(int max_bytes = 10) {
    return Spec{SpecKind::SVARINT, max_bytes, 0};
}

constexpr Spec bool1() {
    return Spec{SpecKind::BOOL1, 0, 0};
}

constexpr Spec bytes_capped(int cap) {
    return Spec{SpecKind::BYTES_CAPPED, cap, 0};
}

template <auto MemberPtr> struct Field {
    static constexpr auto member = MemberPtr;
    const char *name;
    Spec spec;
};

template <auto MemberPtr>
constexpr Field<MemberPtr> field(const char *name, Spec spec) {
    return Field<MemberPtr>{name, spec};
}

namespace detail {

template <class Owner, class Member>
constexpr Member member_type_of(Member Owner::*);

template <auto MemberPtr, class Stream, class Owner>
bool apply_field(const Spec &spec, Stream &stream, Owner &value) {
    using Member = decltype(member_type_of(MemberPtr));
    auto &slot = value.*MemberPtr;
    if constexpr (std::is_same_v<Member, godot::PackedByteArray>) {
        return stream.bytes_capped(slot, int(spec.low));
    } else if constexpr (std::is_same_v<Member, bool>) {
        return stream.bool1(slot);
    } else if (spec.kind == SpecKind::BITS) {
        using Unsigned = std::make_unsigned_t<Member>;
        uint64_t staged = uint64_t(Unsigned(slot));
        if (!stream.bits(staged, int(spec.low))) {
            return false;
        }
        slot = Member(Unsigned(staged));
        return true;
    } else if (spec.kind == SpecKind::VARUINT) {
        uint64_t staged = uint64_t(slot);
        if (!stream.varuint(staged, int(spec.low))) {
            return false;
        }
        slot = Member(staged);
        return true;
    } else if (spec.kind == SpecKind::SVARINT) {
        int64_t staged = int64_t(slot);
        if (!stream.svarint(staged, int(spec.low))) {
            return false;
        }
        slot = Member(staged);
        return true;
    } else {
        int64_t staged = int64_t(slot);
        if (!stream.int_range(staged, spec.low, spec.high)) {
            return false;
        }
        slot = Member(staged);
        return true;
    }
}

const char *spec_kind_name(SpecKind kind);
godot::Dictionary dump_field(const char *name, const Spec &spec);

} // namespace detail

template <class... Fields> struct Description {
    std::tuple<Fields...> fields;

    template <class Stream, class Owner>
    bool run(Stream &stream, Owner &value) const {
        bool healthy = true;
        std::apply(
            [&](const Fields &...each) {
                ((healthy = healthy
                      && detail::apply_field<std::decay_t<
                          decltype(each)>::member>(each.spec, stream, value)),
                 ...);
            },
            fields
        );
        return healthy;
    }

    godot::Array spec_dump() const {
        godot::Array out;
        std::apply(
            [&](const Fields &...each) {
                (out.push_back(detail::dump_field(each.name, each.spec)), ...);
            },
            fields
        );
        return out;
    }
};

template <class... Fields>
constexpr Description<Fields...> describe(Fields... fields) {
    return Description<Fields...>{std::make_tuple(fields...)};
}

} // namespace netw::wire
