#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/log.hpp"

namespace TestLogLevelLaws {

using godot::Array;
using godot::String;
using netw::log::Level;

struct LevelGuard {
    Level held = netw::log::level();

    ~LevelGuard() {
        netw::log::set_level(held);
    }
};

TEST_CASE(
    "[Networked][Log][Hosted] LOG1 a level that was set is the level that "
    "is read"
) {
    LevelGuard guard;
    for (const Level each :
         {Level::TRACE,
          Level::DEBUG,
          Level::INFO,
          Level::WARN,
          Level::ERROR,
          Level::NONE}) {
        netw::log::set_level(each);
        NETW_CHECK_EQ(int(netw::log::level()), int(each));
    }
}

TEST_CASE(
    "[Networked][Log][Hosted] LOG2 a threshold admits its own level and "
    "everything above it"
) {
    LevelGuard guard;
    netw::log::set_level(Level::WARN);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::TRACE)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::DEBUG)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::INFO)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::WARN)), 1);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::ERROR)), 1);

    netw::log::set_level(Level::DEBUG);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::TRACE)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::DEBUG)), 1);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::INFO)), 1);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::WARN)), 1);
}

TEST_CASE(
    "[Networked][Log][Hosted] LOG3 NONE admits nothing to the printed "
    "stream, which is a separate question from what it raises"
) {
    LevelGuard guard;
    netw::log::set_level(Level::NONE);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::TRACE)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::WARN)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::ERROR)), 0);
    NETW_CHECK_EQ(int(netw::log::enabled(Level::NONE)), 0);
}

TEST_CASE(
    "[Networked][Log][Hosted] LOG7 a fault is raised whatever the "
    "threshold reads, so no level can silence one"
) {
    LevelGuard guard;
    NETW_CHECK_EQ(int(netw::log::is_fault(Level::WARN)), 1);
    NETW_CHECK_EQ(int(netw::log::is_fault(Level::ERROR)), 1);
    NETW_CHECK_EQ(int(netw::log::is_fault(Level::TRACE)), 0);
    NETW_CHECK_EQ(int(netw::log::is_fault(Level::DEBUG)), 0);
    NETW_CHECK_EQ(int(netw::log::is_fault(Level::INFO)), 0);
    NETW_CHECK_EQ(int(netw::log::is_fault(Level::NONE)), 0);

    for (const Level each : {Level::TRACE, Level::WARN, Level::NONE}) {
        netw::log::set_level(each);
        NETW_CHECK_EQ(int(netw::log::is_fault(Level::ERROR)), 1);
        NETW_CHECK_EQ(int(netw::log::is_fault(Level::WARN)), 1);
    }
}

TEST_CASE(
    "[Networked][Log][Hosted] LOG4 every level has a name, and a string "
    "outside the table reads INFO"
) {
    NETW_CHECK_EQ(int(netw::log::level_named("trace")), int(Level::TRACE));
    NETW_CHECK_EQ(int(netw::log::level_named("debug")), int(Level::DEBUG));
    NETW_CHECK_EQ(int(netw::log::level_named("info")), int(Level::INFO));
    NETW_CHECK_EQ(int(netw::log::level_named("warn")), int(Level::WARN));
    NETW_CHECK_EQ(int(netw::log::level_named("error")), int(Level::ERROR));
    NETW_CHECK_EQ(int(netw::log::level_named("none")), int(Level::NONE));
    NETW_CHECK_EQ(int(netw::log::level_named("  TRACE  ")), int(Level::TRACE));
    NETW_CHECK_EQ(
        int(netw::log::level_named("core.network=trace")),
        int(Level::INFO)
    );
    NETW_CHECK_EQ(int(netw::log::level_named("")), int(Level::INFO));
}

TEST_CASE(
    "[Networked][Log][Hosted] LOG5 args fill the message and an empty "
    "array passes it through untouched"
) {
    Array one;
    one.append(7);
    NETW_CHECK_EQ(int(netw::log::format_args("peer=%d", one) == "peer=7"), 1);

    Array pair;
    pair.append("racing");
    pair.append(3);
    NETW_CHECK_EQ(
        int(netw::log::format_args("%s has %d", pair) == "racing has 3"),
        1
    );

    NETW_CHECK_EQ(
        int(netw::log::format_args("100% done", Array()) == "100% done"),
        1
    );
}

TEST_CASE(
    "[Networked][Log][Hosted] LOG6 a message the args cannot fill is kept "
    "whole rather than emptied"
) {
    Array one;
    one.append(1);
    NETW_CHECK_EQ(
        int(netw::log::format_args("no placeholder", one) == "no placeholder"),
        1
    );
}

} // namespace TestLogLevelLaws
