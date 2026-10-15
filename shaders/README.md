# Shaders

M1 adds GLSL sources, compiled to SPIR-V at build time with `glslc` and packaged next
to the executable. M0 creates a Vulkan device but does not compile or draw shaders.
