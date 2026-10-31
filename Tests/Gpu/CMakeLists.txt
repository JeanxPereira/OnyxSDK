onyx_add_tests(Gpu)
onyx_add_tests(Gpu SUITE Device SKIP_RETURN_CODE 77)
onyx_add_spirv(OnyxGpuDeviceTests Shaders/Triangle.vert Shaders/Triangle.frag Shaders/TriangleDepth.vert Shaders/TriangleDepth.frag)
