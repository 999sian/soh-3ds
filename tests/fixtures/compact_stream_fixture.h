// Compact stream fixture dependencies. Production structs replace the marker below.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fast/triangle_emit_3ds.h"
constexpr unsigned MAX_TRI_BUFFER = COMPACT_FIXTURE_CAPACITY;
constexpr float N64_PRIM_DEPTH_MAX = 32767.0f;
constexpr unsigned G_TL_LOD = 1u << 16;
enum { G_CCMUX_PRIMITIVE=3,G_CCMUX_SHADE=4,G_CCMUX_ENVIRONMENT=5,
G_CCMUX_KEY_CENTER=6,G_CCMUX_CONVERT_K4=7,G_CCMUX_KEY_SCALE=8,
G_CCMUX_PRIMITIVE_ALPHA=10,G_CCMUX_ENV_ALPHA=12,G_CCMUX_LOD_FRACTION=13,
G_CCMUX_PRIM_LOD_FRAC=14,G_CCMUX_CONVERT_K5=15,G_ACMUX_PRIM_LOD_FRAC=16 };
struct ShaderProgram; struct TextureCacheNode;
struct GfxClipParameters { bool z_is_from_0_to_1, invertY; };
enum class Soh3dsVertexPath { Generic, Arm11, Compact, CompactClip };
enum class Soh3dsProfileSection { TriangleEmit, Pack, State };
struct Soh3dsProfileScope { explicit Soh3dsProfileScope(Soh3dsProfileSection) {} };
// PRODUCTION_STRUCTS
struct RDP {
 RGBA prim_color,env_color,fog_color,blend_color,grayscale_color,key_center,key_scale;
 uint8_t prim_lod_fraction; int16_t convert_k[6]; uint32_t other_mode_l; uint16_t prim_depth;
};
