include(${PROJECT_SOURCE_DIR}/cmake/Deps/Ffmpeg.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/Miniaudio.cmake)
onyx_dep_ffmpeg()
onyx_dep_miniaudio()

onyx_add_layer(Media
    PUBLIC_DEPS Onyx::Ui
    DEPS ffmpeg::ffmpeg miniaudio::miniaudio)
