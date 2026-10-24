// SE Fluid: a fluid-looking surface, animated by the host's clock.
//
// This is the `shade()` of a gmpi_ui shader brush (gmpi_ui/GmpiApiShaders.h).
// The host compiles it into its own programs; regenerate the blob table with
//
//   cmake -DSHADERC=<bgfx shaderc> -DUSER_SHADER=fluid.sh -DUSER_NAME=fluid \
//         -DUSER_OUT=shaders/compiled/shaders_fluid.cpp \
//         -P <gmpi_ui>/backends/gpu/shaders/compile_shaders.cmake
//
// (run from this folder). shaderc must be the one built from the bgfx gmpi_ui
// pins: GMPI_GPU_BUILD_SHADERC=ON on a build that includes gmpi_gpu.cmake.
//
// Three levels of domain-warped value-noise fBM: a stateless "advection"
// look, not a simulation, so every frame is a pure function of (p, time,
// params), identical on every renderer and free to redraw at any rate.
//
//   p             brush space; the module maps its bounds to 0..1
//   u_params[0]   x speed, y scale, z turbulence, w energy
//   u_params[1]   colour A (linear rgb)
//   u_params[2]   colour B
//   u_params[3]   x opacity

float fluidHash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float fluidNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = fluidHash(i);
  float b = fluidHash(i + vec2(1.0, 0.0));
  float c = fluidHash(i + vec2(0.0, 1.0));
  float d = fluidHash(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fluidFbm(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 5; ++i) {
    v += a * fluidNoise(p);
    p = p * 2.03 + vec2(17.0, 9.0);
    a *= 0.5;
  }
  return v;
}

vec4 shade(vec2 p) {
  float energy = u_params[0].w;
  float t = u_time.x * u_params[0].x * (1.0 + energy);
  float k = u_params[0].z;
  vec2 q0 = (p - 0.5) * u_params[0].y;

  vec2 q = vec2(fluidFbm(q0 + t * 0.10),
                fluidFbm(q0 + vec2(5.2, 1.3) - t * 0.07));
  vec2 r = vec2(fluidFbm(q0 + k * q + vec2(1.7, 9.2) + t * 0.15),
                fluidFbm(q0 + k * q + vec2(8.3, 2.8) - t * 0.12));
  float f = fluidFbm(q0 + k * r);

  vec3 rgb = mix(u_params[1].rgb, u_params[2].rgb, smoothstep(0.2, 0.9, f)) * (0.6 + 0.8 * energy);
  return vec4(rgb, u_params[3].x); // straight linear RGBA; the host's template blends it
}
