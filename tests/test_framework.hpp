#pragma once
#define DOCTEST_CONFIG_USE_STD_HEADERS
#ifdef __clang__
// doctest 2.4.12 still includes the retired <ciso646> detection header.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#endif
#include <doctest/doctest.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
