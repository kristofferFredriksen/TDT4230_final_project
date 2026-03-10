# Ocean Renderer (TDT4230 Final Project)

Real-time ocean surface renderer built in C++ and OpenGL for TDT4230: Graphics and Visualisation.

The project uses Gerstner waves as the primary geometric wave model and combines them with a physically inspired water shader, skybox-based reflections, edge fog, and bloom to produce a stylized but dynamic open-ocean scene.

## Features

- Multi-wave Gerstner displacement in the vertex shader
- Analytic normals derived from displaced surface partial derivatives
- Wind-driven swell with additional secondary wave variation
- Skybox-coupled directional sunlight
- View-dependent reflection and Fresnel-based water shading
- Depth-based water coloration
- Crest brightening and subtle foam on peaks
- Mesh-edge fog to soften the transition into the skybox
- HDR bloom post-process pass
- Orbit camera and live parameter tuning
- Lightweight on-screen GUI implemented directly in OpenGL

## Rendering Overview

### Geometry

The ocean surface is generated as a large grid mesh in [`src/ocean.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/ocean.cpp). The mesh is displaced in [`res/shaders/ocean.vert`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/ocean.vert) by summing multiple Gerstner wave components with different:

- directions
- wavelengths
- amplitudes
- phase offsets
- steepness values

The vertex shader also computes partial derivatives analytically and uses them to build stable per-vertex normals.

### Shading

[`res/shaders/ocean.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/ocean.frag) handles the water appearance:

- skybox environment reflections
- directional sun highlights
- depth-based body color
- subsurface-inspired light tinting
- crest brightening
- subtle crest foam and breaking foam masks
- edge fog for skybox blending

The sun direction, color, and intensity are controlled from [`src/ocean.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/ocean.cpp) and aligned to the painted sun in the skybox.

### Post-Processing

[`src/program.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/program.cpp) renders the scene through an HDR framebuffer and applies a bloom pipeline:

1. render scene to HDR texture
2. extract bright pixels
3. blur bright texture with ping-pong Gaussian blur
4. composite bloom over the final frame

Relevant shaders:

- [`res/shaders/post.vert`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/post.vert)
- [`res/shaders/bloom_extract.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/bloom_extract.frag)
- [`res/shaders/bloom_blur.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/bloom_blur.frag)
- [`res/shaders/bloom_composite.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/bloom_composite.frag)

## Controls

### Camera / Runtime

- `Left mouse drag`: orbit camera
- `W` / `S`: zoom in / out
- `F1`: toggle wireframe
- `F2`: cycle debug modes
- `Space`: pause / resume time
- `Esc`: quit

### Keyboard Tuning

- `Up` / `Down`: increase / decrease amplitude scale
- `Right` / `Left`: increase / decrease steepness scale
- `Page Up` / `Page Down`: increase / decrease wave count
- `,` / `.`: rotate wind direction

### On-Screen GUI

A small custom GUI panel is rendered in the top-left corner. It supports mouse drag sliders for:

- amplitude
- steepness
- wind angle
- sun power
- wave count

Dragging inside the panel adjusts parameters. Dragging outside the panel continues to rotate the camera.

## Build and Run

### Clone

```bash
git clone --recursive <repo-url>
cd TDT4230_final_project
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

### Build with CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
./ocean
```

Run the built `ocean` executable from the `build` directory.

## Dependencies

The project includes its dependencies in `lib/`.

- `glfw`
- `glad`
- `glm`
- `fmt`
- `stb`
- `lodepng`
- `arrrgh`

Notes:

- The project requires OpenGL 4.3 core.
- CMake may generate GLAD loader files automatically if needed.
- Python (`python3` or `py -3`) may be needed for GLAD generation.

## Project Structure

- [`src/main.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/main.cpp): application startup and window setup
- [`src/program.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/program.cpp): render loop, skybox, bloom pipeline
- [`src/ocean.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/ocean.cpp): ocean state, mesh generation, camera/input, GUI, parameter upload
- [`src/ocean.hpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/ocean.hpp): ocean interface
- [`res/shaders/ocean.vert`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/ocean.vert): Gerstner displacement and analytic normals
- [`res/shaders/ocean.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/ocean.frag): ocean lighting, foam, fog, tonemapping
- [`res/shaders/skybox.vert`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/skybox.vert): skybox vertex shader
- [`res/shaders/skybox.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/skybox.frag): skybox fragment shader
- [`res/shaders/gui.vert`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/gui.vert): simple GUI vertex shader
- [`res/shaders/gui.frag`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/res/shaders/gui.frag): simple GUI fragment shader

## Notes

- The ocean size and resolution are defined in [`src/ocean.cpp`](/home/kristotf/NTNU/8.semester/TDT4230_Grafikk/final_project/TDT4230_final_project/src/ocean.cpp).
- The renderer currently favors visual quality over minimal geometry cost.
- The GUI is intentionally lightweight and custom-built rather than using a full external UI framework.
