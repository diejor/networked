#pragma once

#include "godot/rid.hpp"

namespace netw::entity_ids {

godot::RID mint();
bool minted(const godot::RID &p_entity);

bool retain(const godot::RID &p_entity);

void release(const godot::RID &p_entity);

int holders(const godot::RID &p_entity);

int outstanding();

void shutdown();

} // namespace netw::entity_ids
