#version 450

// Trivial passthrough: RenderSkeleton (GL) colors every line segment
// per-vertex (bone/root/joint-dot/axis colors are baked into the
// `LineVert` buffer itself, not looked up from a uniform), so this stage
// has no lighting or texturing to do — see overlay.vert's top comment.
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = vColor;
}
