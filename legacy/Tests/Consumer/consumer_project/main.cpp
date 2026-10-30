// Trivial consumer TU: the whole exam is that this compiles and
// consumer_app links against Onyx::Onyx pulled in via add_subdirectory()
// from outside the Onyx source tree (see this directory's CMakeLists.txt).
// No Onyx API is exercised -- Tools/ColdStart already proves the umbrella's
// symbol closure resolves; this file only has to prove the *build* itself
// is reachable from a foreign project root.
#include <Onyx/Onyx.h>

int main() {
    return 0;
}
