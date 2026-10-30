#pragma once

#include <cassert>

namespace Onyx::Services {

void MarkMainThread();
bool IsMainThread();

}

#ifdef NDEBUG
#define ONYX_ASSERT_MAIN_THREAD() ((void)0)
#else
#define ONYX_ASSERT_MAIN_THREAD() assert(::Onyx::Services::IsMainThread())
#endif
