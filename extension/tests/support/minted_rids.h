#pragma once

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"

namespace netw_test {

class MintedRids {
    godot::RID_Owner<int> owner;
    godot::LocalVector<godot::RID> minted;

public:
    MintedRids() = default;
    MintedRids(const MintedRids &) = delete;
    MintedRids &operator=(const MintedRids &) = delete;

    ~MintedRids() {
        for (const godot::RID &each : minted) {
            owner.free(each);
        }
    }

    godot::RID mint(int p_value = 0) {
        const godot::RID made = owner.make_rid(p_value);
        minted.push_back(made);
        return made;
    }
};

} // namespace netw_test
