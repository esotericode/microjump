# Shaders

`scene.vert` / `scene.frag` implement HDR forward shading with GGX materials,
hemisphere ambient, local lights, the sun shadow map and distance fog. The vertex
shader supports non-mirrored affine model transforms through cofactor normals.
`shadow.vert` writes the sun's depth map; `sky.frag` fills the HDR background.
`tonemap.frag` applies exposure and tone mapping before sRGB output and the UI.

`particle.vert` emits camera-facing quads without a vertex buffer. `particle.frag`
gives them a soft radial edge, hemisphere ambient, a modest wrapped directional
diffuse lobe, emission and matching distance fog. Dust catches sunlight while
following the much dimmer moonlight at night. The renderer draws them after opaque
geometry in the same HDR scope, sorted
back to front along the camera axis. Straight-alpha blending happens before tone
mapping, with reverse-Z depth testing and no depth writes. Particles do not cast
shadows. Their edges fade smoothly, but intersections with geometry still use
ordinary depth clipping; scene depth is not sampled for intersection fading.

`frame.glsl` shares frame uniforms, draw constants and sky/fog helpers. CMake
compiles each entry point with `glslc --target-env=vulkan1.3` into `shaders/*.spv`
beside the executable. The runtime finds them relative to the executable,
including in the installed zip; it never invokes a shader compiler.
