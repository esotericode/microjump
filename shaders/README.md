# Shaders

`scene.vert` / `scene.frag` implement the first original forward draw: world-space
vertices/normals, linear directional shading and sRGB output. CMake compiles them
with `glslc --target-env=vulkan1.3` into `shaders/*.spv` beside the executable.
The runtime finds them relative to the executable, including in the installed zip;
it never invokes a shader compiler. Shadows and richer materials belong to M3.
