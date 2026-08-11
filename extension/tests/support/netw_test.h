#pragma once

/* The prelude every native case file opens with, and the only place either
 * tier's test framework is named.
 *
 * Include this FIRST and nothing else from doctest or from the engine's
 * `tests/`. It is never called `test_macros.h`: `config.py` puts `extension/`
 * on the engine's include path, so that name would shadow the engine's own
 * header for every engine translation unit.
 *
 * The two tiers, and what each can do:
 *
 *   hosted   the cases compiled into the library, run through a stock Godot
 *            scons -C extension netw_tests=yes
 *            sees res://, so it is the only tier that can reach GDScript.
 *            Has a live SceneTree and CAN advance a frame, but only between
 *            run() calls: the suite executes inside _initialize(), so no
 *            frame lands mid-case.
 *   module   the cases compiled into an engine that mounts this as a module
 *            scons -C extension/thirdparty/godot tests=yes
 *            cannot see res:// and therefore cannot load GDScript at all.
 *            Lands a frame mid-case and reaches engine internals.
 *
 * Placement follows the subject. Pure functions run in both tiers. A session
 * engine under declared configuration runs hosted and gains the module tier
 * when its shell is native. Physics integration runs in the module tier. A
 * stock project, kit compiler node, or real socket stays GdUnit because that
 * project, node, or socket is the subject. Frames are a runner capability,
 * not a placement input.
 *
 * Tag discipline, both tiers, in this order:
 *
 *   [Networked]   every case, so the engine's runner can select this module
 *   [<Family>]    the core under test, so a family slice runs its own cases:
 *                 [Codec], [Ring], [Handle], [Liveness], [Transport],
 *                 [Registry], [Wire]
 *   [Hosted]      the case is tier-portable and its file is reachable from
 *                 `test_networked_hosted.h`
 *   [SceneTree]   last, module-only, and only for a case that drives a frame
 *
 * A filter that matches nothing still exits zero, so a run is only evidence
 * when it is paired with `--list-test-cases` and a grep for the family.
 */

#if defined(NETW_MODULE)
#include "tests/signal_watcher.h"
#include "tests/test_macros.h"
#define NETW_TIER_MODULE 1
#elif defined(NETW_GDEXTENSION)
// The library is compiled with `-fno-exceptions`, and doctest answers that by
// withdrawing every `REQUIRE` unless this is set. The engine's own
// `test_macros.h` sets it for the same reason, so setting it here is what
// makes `REQUIRE` mean the same thing in both tiers: mark the case failed and
// keep going, rather than compile in one tier and not the other.
#if !__cpp_exceptions && !__EXCEPTIONS \
    && !defined(DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#endif
#include "thirdparty/doctest/doctest.h"
#define NETW_TIER_HOSTED 1
// Spellings the module tier has and the hosted tier does not. Each is a
// deliberate no-op rather than a stub to fill in: CoreGlobals is
// engine-internal, and a shared library links its own classes eagerly.
//
// The no-ops keep the module tier's SCOPE as well as its name.
// `TEST_FORCE_LINK` is a namespace definition in the engine, so it is only
// ever written at namespace scope, and a no-op that expands to an expression
// would not compile there.
#define ERR_PRINT_OFF ((void)0)
#define ERR_PRINT_ON ((void)0)
#define TEST_FORCE_LINK(m_name) \
    namespace ForceLink { \
    inline void force_link_##m_name() { \
    } \
    }
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#include <cmath>
#include <cstdio>

/* Numbers, both tiers.
 *
 * MEASURED, and wider than the float trap it was written for: streaming ANY
 * number out of the shared object segfaults. A bare
 * `std::ostringstream stream; stream << 42;` inside the library, loaded into
 * the stock Godot binary, dies in libstdc++ on the numeric facet — integers as
 * surely as doubles, and neither `symbols_visibility=visible` nor
 * `use_static_cpp=yes` changes it. Only the hosted tier is affected. The
 * module tier is one binary and streams fine.
 *
 * doctest stringifies both sides of a failing comparison, so in the library a
 * failing `CHECK(a == b)` on numbers reports `SIGSEGV` for that case and ENDS
 * THE RUN rather than naming the values that differed. The case that failed is
 * still named, so the run is never silent, but the cases after it do not run.
 * That cost is only paid on a failure, which is why the corpus is not rewritten
 * around it.
 *
 * The macros below buy the values back. Each computes the comparison as a bool,
 * preformats both operands with `snprintf` into scope-lived buffers, and checks
 * only the bool, so nothing numeric reaches the stringifier. Use them wherever
 * the values are what a reader would need. A `CHECK` on a bool, a string, or a
 * Godot type is unaffected. One spelling for both tiers means a `[Hosted]` case
 * cannot pick the crashing one.
 *
 * The buffers are locals rather than expressions because `CAPTURE` stringifies
 * lazily, at failure time: a temporary formatted inline is destroyed before
 * doctest reads it.
 */
#define NETW_FORMAT_DOUBLE(m_name, m_value) \
    char m_name[40]; \
    std::snprintf(m_name, sizeof(m_name), "%.9g", (double)(m_value))

#define NETW_FORMAT_INT(m_name, m_value) \
    char m_name[24]; \
    std::snprintf(m_name, sizeof(m_name), "%lld", (long long)(m_value))

/* Text, for the same reason and one step worse.
 *
 * doctest prints `char[N]` as a string and `const char *` as a POINTER, and the
 * pointer path dies in the same numeric facet the integer path dies in. So
 * `CAPTURE(text.get_data())` segfaults the run where `CAPTURE` of a `char[N]`
 * array does not. Copy into an array and capture that.
 */
#define NETW_FORMAT_TEXT(m_name, m_value) \
    char m_name[256]; \
    std::snprintf(m_name, sizeof(m_name), "%s", (const char *)(m_value))

#define NETW_CHECK_EQ(m_a, m_b) \
    do { \
        const long long netw_lhs = (long long)(m_a); \
        const long long netw_rhs = (long long)(m_b); \
        const bool netw_ok = netw_lhs == netw_rhs; \
        NETW_FORMAT_INT(netw_lhs_text, netw_lhs); \
        NETW_FORMAT_INT(netw_rhs_text, netw_rhs); \
        CAPTURE(netw_lhs_text); \
        CAPTURE(netw_rhs_text); \
        CHECK(netw_ok); \
    } while (0)

/* Ordering, on doubles.
 *
 * `NETW_CHECK_EQ` compares as `long long` because equality of counts is exact.
 * The four below compare as `double` instead, because the values a law orders
 * are magnitudes — a divergence against an epsilon, a tail against a bound —
 * and widening a count to a double is exact for every count a run can reach.
 */
#define NETW_CHECK_ORDER(m_a, m_b, m_op) \
    do { \
        const double netw_lhs = (double)(m_a); \
        const double netw_rhs = (double)(m_b); \
        const bool netw_ok = netw_lhs m_op netw_rhs; \
        NETW_FORMAT_DOUBLE(netw_lhs_text, netw_lhs); \
        NETW_FORMAT_DOUBLE(netw_rhs_text, netw_rhs); \
        CAPTURE(netw_lhs_text); \
        CAPTURE(netw_rhs_text); \
        CHECK(netw_ok); \
    } while (0)

#define NETW_CHECK_LT(m_a, m_b) NETW_CHECK_ORDER(m_a, m_b, <)
#define NETW_CHECK_LE(m_a, m_b) NETW_CHECK_ORDER(m_a, m_b, <=)
#define NETW_CHECK_GT(m_a, m_b) NETW_CHECK_ORDER(m_a, m_b, >)
#define NETW_CHECK_GE(m_a, m_b) NETW_CHECK_ORDER(m_a, m_b, >=)

#define NETW_CHECK_CLOSE(m_a, m_b, m_eps) \
    do { \
        const double netw_lhs = (double)(m_a); \
        const double netw_rhs = (double)(m_b); \
        const bool netw_ok \
            = std::fabs(netw_lhs - netw_rhs) <= (double)(m_eps); \
        NETW_FORMAT_DOUBLE(netw_lhs_text, netw_lhs); \
        NETW_FORMAT_DOUBLE(netw_rhs_text, netw_rhs); \
        CAPTURE(netw_lhs_text); \
        CAPTURE(netw_rhs_text); \
        CHECK(netw_ok); \
    } while (0)
