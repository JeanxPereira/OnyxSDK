#include "HeadlessGL.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--gl-smoke") == 0) {
        Onyx::OracleTool::HeadlessGL gl;
        std::string err;
        if (!gl.Init(err)) { std::fprintf(stderr, "skip: %s\n", err.c_str()); return 77; }
        if (!gl.BeginFrame(64, 64, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::vector<uint8_t> rgba;
        if (!gl.EndFrame(rgba, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        return rgba.size() == 64u * 64u * 4u ? 0 : 1;
    }
    std::fprintf(stderr, "onyx-oracle: no command\n");
    return 1;
}
