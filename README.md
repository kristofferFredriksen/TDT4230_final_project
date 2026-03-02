# Ocean Renderer (TDT4230 Final Project)

Real-time ocean rendering project built in C++ with OpenGL for TDT4230.

## Current Status

The project currently renders a large ocean grid with:
- OpenGL 4.3 core profile
- Shader-based rendering pipeline
- Orbit camera controls
- Time-driven update loop scaffold for wave animation
- Wireframe toggle for debugging

This is an in-progress baseline for implementing full Gerstner-wave ocean simulation and shading.

## Controls

- `Left mouse drag`: rotate orbit camera
- `W` / `S`: zoom camera in/out
- `F1`: toggle wireframe
- `Space`: pause/resume simulation time
- `Esc`: exit

## Build and Run

### 1. Clone with submodules

```bash
git clone --recursive <repo-url>
cd TDT4230_final_project
```

If already cloned without submodules:

```bash
git submodule update --init
```

### 2. Build (Linux/macOS)

Using Makefile helper:

```bash
make build
make run
```

Or with CMake directly:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
./ocean
```

### 3. Build (Windows)

Generate a Visual Studio project with CMake and build target `ocean`:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run the executable from the `build` directory in your chosen configuration.

## Dependencies

Included as git submodules under `lib/`:
- `glfw`
- `glad`
- `glm`
- `fmt`
- `stb`
- `lodepng`
- `arrrgh`

Note: CMake auto-generates GLAD loader files if needed and requires Python (`python3` or `py -3`).

## Project Structure

- `src/main.cpp`: app startup, GLFW init, command-line parsing
- `src/program.cpp`: main render/update loop
- `src/ocean.cpp`: ocean state, mesh generation, camera input, rendering
- `res/shaders/ocean.vert`: ocean vertex shader
- `res/shaders/ocean.frag`: ocean fragment shader

## Work In Progress

Planned/improving areas:
- Gerstner wave displacement and normals in shaders
- Better physically-inspired ocean shading (Fresnel/specular/reflection cues)
- Extended parameter controls and UI/debug controls
