#pragma once
// Onyx SDK — media umbrella (audit gap G2 fix, sibling to Onyx.h).
//
// <Onyx/Onyx.h> deliberately leaves out Viewers/VideoPlayer.h: unlike every
// other viewer, its own header (not just its .cpp) directly `#include`s
// libavformat/libavcodec/libswscale/libswresample and miniaudio.h.
// ONYX_COMPONENT_MEDIA (root CMakeLists.txt) can build Onyx WITHOUT FFmpeg
// at all -- unconditionally including VideoPlayer.h from the main umbrella
// would break `#include <Onyx/Onyx.h>` itself for that configuration, not
// just cost extra compile time for consumers who never play a video.
//
// Pairs with the Onyx::Media target (Onyx_Media, root CMakeLists.txt,
// built only when ONYX_COMPONENT_MEDIA is ON) the same way <Onyx/Onyx.h>
// pairs with Onyx::Onyx. Only include this header from a build that has
// ONYX_COMPONENT_MEDIA enabled (the default) -- FFmpeg's own headers must
// be on the include path for it to compile at all, same as any consumer of
// Onyx::Media today.
#include <Onyx/Viewers/VideoPlayer.h>
