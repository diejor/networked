#include "support/netw_test.h"

#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

namespace TestNetwSubsystems {

using namespace netw;

TEST_CASE(
    "[Networked][Log][Hosted] a subsystem's name and its mask bit are one row"
) {
    NETW_CHECK_EQ(subsystem_count(), 17);

    for (int index = 0; index < subsystem_count(); ++index) {
        const char *name = subsystem_at(index);
        const Subsystem bit = subsystem_of(name);
        NETW_FORMAT_TEXT(name_text, name);
        CAPTURE(name_text);
        NETW_CHECK_EQ(int(bit != SUBSYSTEM_NONE), 1);
        NETW_CHECK_EQ(
            int(uint32_t(subsystem_of(subsystem_name(bit)))),
            int(uint32_t(bit))
        );
    }
}

TEST_CASE(
    "[Networked][Log][Hosted] every subsystem owns a distinct bit, and the "
    "profile mask reads the same rows"
) {
    uint32_t seen = 0;
    for (int index = 0; index < subsystem_count(); ++index) {
        const uint32_t bit = uint32_t(subsystem_of(subsystem_at(index)));
        NETW_CHECK_EQ(int(seen & bit), 0);
        seen |= bit;
    }
    NETW_CHECK_EQ(int(seen & uint32_t(profile::SUBSYSTEM_SESSION)) != 0, 1);
    NETW_CHECK_EQ(int(seen & uint32_t(profile::SUBSYSTEM_LAGCOMP)) != 0, 1);
    NETW_CHECK_EQ(
        int(uint32_t(profile::SUBSYSTEM_ALL) & seen) == int(seen),
        1
    );
}

TEST_CASE(
    "[Networked][Log][Hosted] a name outside the table selects nothing rather "
    "than answering a bit"
) {
    NETW_CHECK_EQ(int(uint32_t(subsystem_of("not_a_subsystem"))), 0);
    NETW_CHECK_EQ(int(uint32_t(subsystem_of(""))), 0);
    NETW_CHECK_EQ(int(uint32_t(subsystem_of(nullptr))), 0);

    NETW_FORMAT_TEXT(unnamed, subsystem_name(SUBSYSTEM_NONE));
    CAPTURE(unnamed);
    NETW_CHECK_EQ(int(subsystem_name(SUBSYSTEM_NONE)[0]), 0);

    NETW_FORMAT_TEXT(past_end, subsystem_at(subsystem_count()));
    CAPTURE(past_end);
    NETW_CHECK_EQ(int(subsystem_at(subsystem_count())[0]), 0);
    NETW_CHECK_EQ(int(subsystem_at(-1)[0]), 0);
}

} // namespace TestNetwSubsystems
