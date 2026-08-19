#pragma once
// Onyx SDK — test-harness umbrella (sibling to Onyx.h).
//
// LINK Onyx::TestKit to use anything declared here. Same reason as
// <Onyx/Exchange.h> and <Onyx/CliRender.h>: the main umbrella names only
// headers whose symbols ship in a target Onyx::Onyx itself links
// (Onyx_Core, Onyx_Render, Onyx_Shell), and Onyx_TestKit is not one of
// them -- it is opt-in on purpose, because a shipping application should
// not carry the SDK's test rig. Until v1.0 these three headers were named
// by <Onyx/Onyx.h> on the argument that "the declarations are free"; they
// are not free, they are an LNK2019 the moment a consumer calls one, which
// is the whole promise the umbrella makes. See <Onyx/Onyx.h>'s inclusion
// rule, clause (1).
//
// The intended shape in a consuming project is the SDK's own: link this
// from the test executable only, never from the application target.
//
//   target_link_libraries(MyTests PRIVATE Onyx::Core Onyx::TestKit)
//   #include <Onyx/TestKit.h>
//
// One target, not a headless/render pair: none of Goldens/DecodeSmoke/
// RenderCompare touches a GPU or a renderer type (RenderCompare reads PNG
// files and does pixel math), so Onyx_TestKit links Onyx_Core alone -- see
// Include/Onyx/TestKit/RenderCompare.h for that design call in full.
//
// Third-party dependencies to compile a TU that includes ONLY this header:
// none. stb_image is private to Onyx_TestKit's own sources.
#include <Onyx/TestKit/DecodeSmoke.h>
#include <Onyx/TestKit/Goldens.h>
#include <Onyx/TestKit/RenderCompare.h>
