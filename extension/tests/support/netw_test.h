#pragma once

#if defined(NETW_MODULE)
#include "tests/signal_watcher.h"
#include "tests/test_macros.h"
#define NETW_TIER_MODULE 1
#elif defined(NETW_GDEXTENSION)
#if !__cpp_exceptions && !__EXCEPTIONS \
    && !defined(DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#endif
#include "thirdparty/doctest/doctest.h"
#define NETW_TIER_HOSTED 1
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

#define NETW_FORMAT_DOUBLE(m_name, m_value) \
    char m_name[40]; \
    std::snprintf(m_name, sizeof(m_name), "%.9g", (double)(m_value))

#define NETW_FORMAT_INT(m_name, m_value) \
    char m_name[24]; \
    std::snprintf(m_name, sizeof(m_name), "%lld", (long long)(m_value))

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
