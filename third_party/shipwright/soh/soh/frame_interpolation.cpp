#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <memory>
#include <cassert>
#include <math.h>

#include "frame_interpolation.h"
#include "soh/OTRGlobals.h"
#ifdef __3DS__
#include "fast/backends/gfx_profile_3ds.h"
#endif

/*
Frame interpolation.

The idea of this code is to interpolate all matrices.

The code contains two approaches. The first is to interpolate
all inputs in transformations, such as angles, scale and distances,
and then perform the same transformations with the interpolated values.
After evaluation for some reason some animations such rolling look strange.

The second approach is to simply interpolate the final matrices. This will
more or less simply interpolate the world coordinates for movements.
This will however make rotations ~180 degrees get the "paper effect".
The mitigation is to identify this case for actors and interpolate the
matrix but in model coordinates instead, by "removing" the rotation-
translation before interpolating, create a rotation matrix with the
interpolated angle which is then applied to the matrix.

Currently the code contains both methods but only the second one is currently
used.

Both approaches build a tree of instructions, containing matrices
at leaves. Every node is built from OPEN_DISPS/CLOSE_DISPS and manually
inserted FrameInterpolation_OpenChild/FrameInterpolation_Close child calls.
These nodes contain information that should suffice to identify the matrix,
so we can find it in an adjacent frame.

We can interpolate an arbitrary amount of frames between two original frames,
given a specific interpolation factor (0=old frame, 0.5=average of frames,
1.0=new frame).
*/

extern "C" {

void Matrix_Init(struct GameState* gameState);
void Matrix_Push(void);
void Matrix_Pop(void);
void Matrix_Get(MtxF* dest);
void Matrix_Put(MtxF* src);
void Matrix_Mult(MtxF* mf, u8 mode);
void Matrix_Translate(f32 x, f32 y, f32 z, u8 mode);
void Matrix_Scale(f32 x, f32 y, f32 z, u8 mode);
void Matrix_RotateX(f32 x, u8 mode);
void Matrix_RotateY(f32 y, u8 mode);
void Matrix_RotateZ(f32 z, u8 mode);
void Matrix_RotateZYX(s16 x, s16 y, s16 z, u8 mode);
void Matrix_TranslateRotateZYX(Vec3f* translation, Vec3s* rotation);
void Matrix_SetTranslateRotateYXZ(f32 translateX, f32 translateY, f32 translateZ, Vec3s* rot);
Mtx* Matrix_MtxFToMtx(MtxF* src, Mtx* dest);
Mtx* Matrix_ToMtx(Mtx* dest, char* file, s32 line);
Mtx* Matrix_NewMtx(struct GraphicsContext* gfxCtx, char* file, s32 line);
Mtx* Matrix_MtxFToNewMtx(MtxF* src, struct GraphicsContext* gfxCtx);
void Matrix_MultVec3f(Vec3f* src, Vec3f* dest);
void Matrix_MtxFCopy(MtxF* dest, MtxF* src);
void Matrix_MtxToMtxF(Mtx* src, MtxF* dest);
void Matrix_MultVec3fExt(Vec3f* src, Vec3f* dest, MtxF* mf);
void Matrix_Transpose(MtxF* mf);
void Matrix_ReplaceRotation(MtxF* mf);
void Matrix_MtxFToYXZRotS(MtxF* mf, Vec3s* rotDest, s32 flag);
void Matrix_MtxFToZYXRotS(MtxF* mf, Vec3s* rotDest, s32 flag);
void Matrix_RotateAxis(f32 angle, Vec3f* axis, u8 mode);
MtxF* Matrix_CheckFloats(MtxF* mf, char* file, s32 line);
void Matrix_SetTranslateScaleMtx2(Mtx* mtx, f32 scaleX, f32 scaleY, f32 scaleZ, f32 translateX, f32 translateY,
                                  f32 translateZ);

MtxF* Matrix_GetCurrent(void);

void SkinMatrix_MtxFMtxFMult(MtxF* mfA, MtxF* mfB, MtxF* dest);
}

static bool invert_matrix(const float m[16], float invOut[16]);

using namespace std;

namespace {

enum class Op {
    OpenChild,
    CloseChild,

    MatrixPush,
    MatrixPop,
    MatrixPut,
    MatrixMult,
    MatrixTranslate,
    MatrixScale,
    MatrixRotate1Coord,
    MatrixRotateZYX,
    MatrixTranslateRotateZYX,
    MatrixSetTranslateRotateYXZ,
    MatrixMtxFToMtx,
    MatrixToMtx,
    MatrixReplaceRotation,
    MatrixRotateAxis,
    SkinMatrixMtxFToMtx,

    Count // SoH-3DS: array bound for Path::ops
};

typedef pair<const void*, int> label;

union Data {
    Data() {
    }

    struct {
        MtxF src;
    } matrix_put;

    struct {
        MtxF mf;
        u8 mode;
    } matrix_mult;

    struct {
        f32 x, y, z;
        u8 mode;
    } matrix_translate, matrix_scale;

    struct {
        u32 coord;
        f32 value;
        u8 mode;
    } matrix_rotate_1_coord;

    struct {
        s16 x, y, z;
        u8 mode;
    } matrix_rotate_zyx;

    struct {
        Vec3f translation;
        Vec3s rotation;
    } matrix_translate_rotate_zyx;

    struct {
        f32 translateX, translateY, translateZ;
        Vec3s rot;
        // MtxF mtx;
        bool has_mtx;
    } matrix_set_translate_rotate_yxz;

    struct {
        MtxF src;
        Mtx* dest;
    } matrix_mtxf_to_mtx;

    struct {
        Mtx* dest;
        MtxF src;
        bool has_adjusted;
    } matrix_to_mtx;

    struct {
        MtxF mf;
    } matrix_replace_rotation;

    struct {
        f32 angle;
        Vec3f axis;
        u8 mode;
    } matrix_rotate_axis;

    struct {
        label key;
        size_t idx;
    } open_child;
};

// SoH-3DS: the recording tree is rebuilt with the same shape every tick (same
// actors, same limbs, same draw order). Rebuilding it from scratch cost ~2000
// map/vector allocations per tick; walking it to clear cost a full pointer
// chase over every node (14 ms/tick measured on hardware - the single largest
// cost in the game loop, and invisible in Azahar). Keep recently used nodes
// and capacities, emptying each node LAZILY when recording touches it. An
// intrusive oldest-use queue reclaims expired nodes without scanning either
// tree. CloseChild queues children before parents, so reclamation frees one
// leaf at a time instead of recursively destroying an arbitrarily big tree.
uint32_t record_generation = 1;
struct Path;
Path* oldest_path = nullptr;
Path* newest_path = nullptr;
size_t new_paths_this_tick = 0;

struct Path {
    struct ChildSlot {
        // Stable addresses for the expiry queue and children's parent links.
        // Growing repeated occurrences of a label must not relocate nodes.
        vector<unique_ptr<Path>> paths;
        size_t used = 0;
        size_t retained = 0;
        uint32_t generation = 0;
    };

    map<label, ChildSlot> children;
    // SoH-3DS: array, not map<Op, ...>: append() runs thousands of times per
    // tick (once per recorded matrix operation) and a red-black tree lookup
    // per call is pure overhead when the key is a 17-value enum.
    array<vector<Data>, static_cast<size_t>(Op::Count)> ops;
    vector<pair<Op, size_t>> items;
    uint32_t generation = 0;
    Path* parent = nullptr;
    label parent_key;
    size_t parent_index = 0;
    Path* older = nullptr;
    Path* newer = nullptr;

    void Unlink() {
        if (older) older->newer = newer;
        else if (oldest_path == this) oldest_path = newer;
        if (newer) newer->older = older;
        else if (newest_path == this) newest_path = older;
        older = newer = nullptr;
    }

    void Finish() {
        Unlink();
        older = newest_path;
        if (newest_path) newest_path->newer = this;
        else oldest_path = this;
        newest_path = this;
    }

    ~Path() { Unlink(); }

    // Empty this node if it still holds a previous tick's recording.
    void Touch() {
        if (generation == record_generation) {
            return;
        }
        generation = record_generation;
        for (auto& op : ops) {
            op.clear();
        }
        items.clear();
    }
};

struct Recording {
    // The root must also stay at the same address when recordings swap.
    unique_ptr<Path> root_path = make_unique<Path>();
};

void ReclaimExpiredPaths() {
    // Protect both interpolation inputs and a few ticks of intermittent reuse.
    constexpr uint32_t keep_ticks = 8;
    // A fixed allowance drains a past burst even after allocations stop. Pay
    // for additional reclamation with this tick's creations, so sustained
    // effects cannot add nodes faster than they can be retired. Work depends
    // on recording activity, never scene duration or total retained history.
    size_t budget = 32 + new_paths_this_tick;
    while (budget-- && oldest_path && record_generation - oldest_path->generation > keep_ticks) {
        Path* expired = oldest_path;
        assert(expired->children.empty()); // Descendants precede their parent.
        auto& children = expired->parent->children;
        auto slot = children.find(expired->parent_key);
        const size_t idx = expired->parent_index;
        slot->second.paths[idx].reset();
        if (--slot->second.retained == 0) {
            children.erase(slot);
        }
    }
}

bool is_recording;
vector<Path*> current_path;
uint32_t camera_epoch;
uint32_t previous_camera_epoch;
Recording current_recording;
Recording previous_recording;

bool next_is_actor_pos_rot_matrix;
bool has_inv_actor_mtx;
MtxF inv_actor_mtx;
size_t inv_actor_mtx_path_index;

Data& append(Op op) {
    Path* path = current_path.back();
    auto& m = path->ops[static_cast<size_t>(op)];
    path->items.emplace_back(op, m.size());
    return m.emplace_back();
}

struct InterpolateCtx {
    float step;
    float w;
    unordered_map<Mtx*, MtxF> mtx_replacements;
    MtxF tmp_mtxf, tmp_mtxf2;
    Vec3f tmp_vec3f;
    Vec3s tmp_vec3s;
    MtxF actor_mtx;

    MtxF* new_replacement(Mtx* addr) {
        return &mtx_replacements[addr];
    }

    void interpolate_mtxf(MtxF* res, MtxF* o, MtxF* n) {
        for (size_t i = 0; i < 4; i++) {
            for (size_t j = 0; j < 4; j++) {
                res->mf[i][j] = w * o->mf[i][j] + step * n->mf[i][j];
            }
        }
    }

    float lerp(f32 o, f32 n) {
        return w * o + step * n;
    }

    void lerp_vec3f(Vec3f* res, Vec3f* o, Vec3f* n) {
        res->x = lerp(o->x, n->x);
        res->y = lerp(o->y, n->y);
        res->z = lerp(o->z, n->z);
    }

    float interpolate_angle(f32 o, f32 n) {
        if (o == n)
            return n;
        o = fmodf(o, static_cast<f32>(2.0f * M_PI));
        if (o < 0.0f) {
            o += static_cast<f32>(2.0f * M_PI);
        }
        n = fmodf(n, static_cast<f32>(2.0f * M_PI));
        if (n < 0.0f) {
            n += static_cast<f32>(2.0f * M_PI);
        }
        if (fabsf(o - n) > M_PI) {
            if (o < n) {
                o += static_cast<f32>(2.0f * M_PI);
            } else {
                n += static_cast<f32>(2.0f * M_PI);
            }
        }
        if (fabsf(o - n) > M_PI / 2) {
            // return n;
        }
        return lerp(o, n);
    }

    s16 interpolate_angle(s16 os, s16 ns) {
        if (os == ns)
            return ns;
        int o = (u16)os;
        int n = (u16)ns;
        u16 res;
        int diff = o - n;
        if (-0x8000 <= diff && diff <= 0x8000) {
            if (diff < -0x4000 || diff > 0x4000) {
                return ns;
            }
            res = (u16)(w * o + step * n);
        } else {
            if (o < n) {
                o += 0x10000;
            } else {
                n += 0x10000;
            }
            diff = o - n;
            if (diff < -0x4000 || diff > 0x4000) {
                return ns;
            }
            res = (u16)(w * o + step * n);
        }
        if (os / 327 == ns / 327 && (s16)res / 327 != os / 327) {
            int bp = 0;
        }
        return res;
    }

    void interpolate_angles(Vec3s* res, Vec3s* o, Vec3s* n) {
        res->x = interpolate_angle(o->x, n->x);
        res->y = interpolate_angle(o->y, n->y);
        res->z = interpolate_angle(o->z, n->z);
    }

    void interpolate_branch(Path* old_path, Path* new_path) {
        for (auto& item : new_path->items) {
            Data& new_op = new_path->ops[static_cast<size_t>(item.first)][item.second];

            if (item.first == Op::OpenChild) {
                Path* newChild =
                    new_path->children.find(new_op.open_child.key)->second.paths[new_op.open_child.idx].get();
                Path* oldChild = newChild;
                if (auto it = old_path->children.find(new_op.open_child.key); it != old_path->children.end()) {
                    // Only slots the previous tick actually recorded are valid
                    // as an interpolation source.
                    if (it->second.generation == record_generation - 1 && new_op.open_child.idx < it->second.used) {
                        oldChild = it->second.paths[new_op.open_child.idx].get();
                    }
                }
                interpolate_branch(oldChild, newChild);
                continue;
            }

            {
                auto& oldOps = old_path->ops[static_cast<size_t>(item.first)];
                if (item.second < oldOps.size()) {
                    Data& old_op = oldOps[item.second];
                    switch (item.first) {
                        case Op::OpenChild:
                            break;
                        case Op::CloseChild:
                            break;

                        case Op::MatrixPush:
                            Matrix_Push();
                            break;

                        case Op::MatrixPop:
                            Matrix_Pop();
                            break;

                        case Op::MatrixPut:
                            interpolate_mtxf(&tmp_mtxf, &old_op.matrix_put.src, &new_op.matrix_put.src);
                            Matrix_Put(&tmp_mtxf);
                            break;

                        case Op::MatrixMult:
                            interpolate_mtxf(&tmp_mtxf, &old_op.matrix_mult.mf, &new_op.matrix_mult.mf);
                            Matrix_Mult(&tmp_mtxf, new_op.matrix_mult.mode);
                            break;

                        case Op::MatrixTranslate:
                            Matrix_Translate(lerp(old_op.matrix_translate.x, new_op.matrix_translate.x),
                                             lerp(old_op.matrix_translate.y, new_op.matrix_translate.y),
                                             lerp(old_op.matrix_translate.z, new_op.matrix_translate.z),
                                             new_op.matrix_translate.mode);
                            break;

                        case Op::MatrixScale:
                            Matrix_Scale(lerp(old_op.matrix_scale.x, new_op.matrix_scale.x),
                                         lerp(old_op.matrix_scale.y, new_op.matrix_scale.y),
                                         lerp(old_op.matrix_scale.z, new_op.matrix_scale.z), new_op.matrix_scale.mode);
                            break;

                        case Op::MatrixRotate1Coord: {
                            float v = interpolate_angle(old_op.matrix_rotate_1_coord.value,
                                                        new_op.matrix_rotate_1_coord.value);
                            u8 mode = new_op.matrix_rotate_1_coord.mode;
                            switch (new_op.matrix_rotate_1_coord.coord) {
                                case 0:
                                    Matrix_RotateX(v, mode);
                                    break;

                                case 1:
                                    Matrix_RotateY(v, mode);
                                    break;

                                case 2:
                                    Matrix_RotateZ(v, mode);
                                    break;
                            }
                            break;
                        }

                        case Op::MatrixRotateZYX:
                            Matrix_RotateZYX(interpolate_angle(old_op.matrix_rotate_zyx.x, new_op.matrix_rotate_zyx.x),
                                             interpolate_angle(old_op.matrix_rotate_zyx.y, new_op.matrix_rotate_zyx.y),
                                             interpolate_angle(old_op.matrix_rotate_zyx.z, new_op.matrix_rotate_zyx.z),
                                             new_op.matrix_rotate_zyx.mode);
                            break;

                        case Op::MatrixTranslateRotateZYX:
                            lerp_vec3f(&tmp_vec3f, &old_op.matrix_translate_rotate_zyx.translation,
                                       &new_op.matrix_translate_rotate_zyx.translation);
                            interpolate_angles(&tmp_vec3s, &old_op.matrix_translate_rotate_zyx.rotation,
                                               &new_op.matrix_translate_rotate_zyx.rotation);
                            Matrix_TranslateRotateZYX(&tmp_vec3f, &tmp_vec3s);
                            break;

                        case Op::MatrixSetTranslateRotateYXZ:
                            interpolate_angles(&tmp_vec3s, &old_op.matrix_set_translate_rotate_yxz.rot,
                                               &new_op.matrix_set_translate_rotate_yxz.rot);
                            Matrix_SetTranslateRotateYXZ(lerp(old_op.matrix_set_translate_rotate_yxz.translateX,
                                                              new_op.matrix_set_translate_rotate_yxz.translateX),
                                                         lerp(old_op.matrix_set_translate_rotate_yxz.translateY,
                                                              new_op.matrix_set_translate_rotate_yxz.translateY),
                                                         lerp(old_op.matrix_set_translate_rotate_yxz.translateZ,
                                                              new_op.matrix_set_translate_rotate_yxz.translateZ),
                                                         &tmp_vec3s);
                            if (new_op.matrix_set_translate_rotate_yxz.has_mtx &&
                                old_op.matrix_set_translate_rotate_yxz.has_mtx) {
                                actor_mtx = *Matrix_GetCurrent();
                            }
                            break;

                        case Op::MatrixMtxFToMtx:
                            interpolate_mtxf(new_replacement(new_op.matrix_mtxf_to_mtx.dest),
                                             &old_op.matrix_mtxf_to_mtx.src, &new_op.matrix_mtxf_to_mtx.src);
                            break;

                        case Op::MatrixToMtx: {
                            //*new_replacement(new_op.matrix_to_mtx.dest) = *Matrix_GetCurrent();
                            if (old_op.matrix_to_mtx.has_adjusted && new_op.matrix_to_mtx.has_adjusted) {
                                interpolate_mtxf(&tmp_mtxf, &old_op.matrix_to_mtx.src, &new_op.matrix_to_mtx.src);
                                SkinMatrix_MtxFMtxFMult(&actor_mtx, &tmp_mtxf,
                                                        new_replacement(new_op.matrix_to_mtx.dest));
                            } else {
                                interpolate_mtxf(new_replacement(new_op.matrix_to_mtx.dest), &old_op.matrix_to_mtx.src,
                                                 &new_op.matrix_to_mtx.src);
                            }
                            break;
                        }

                        case Op::MatrixReplaceRotation:
                            interpolate_mtxf(&tmp_mtxf, &old_op.matrix_replace_rotation.mf,
                                             &new_op.matrix_replace_rotation.mf);
                            Matrix_ReplaceRotation(&tmp_mtxf);
                            break;

                        case Op::MatrixRotateAxis:
                            lerp_vec3f(&tmp_vec3f, &old_op.matrix_rotate_axis.axis, &new_op.matrix_rotate_axis.axis);
                            Matrix_RotateAxis(
                                interpolate_angle(old_op.matrix_rotate_axis.angle, new_op.matrix_rotate_axis.angle),
                                &tmp_vec3f, new_op.matrix_rotate_axis.mode);
                            break;

                        case Op::SkinMatrixMtxFToMtx:
                            break;
                    }
                }
            }
        }
    }
};

} // anonymous namespace

unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step) {
#ifdef __3DS__
    Soh3dsProfileScope interpolationProfile(Soh3dsProfileSection::Interpolate);
#endif
    InterpolateCtx ctx;
    ctx.step = step;
    ctx.w = 1.0f - step;
    ctx.interpolate_branch(previous_recording.root_path.get(), current_recording.root_path.get());
    return ctx.mtx_replacements;
}

void FrameInterpolation_StartRecord(void) {
    // Swap and bump the generation: the tree that becomes `current` still
    // holds the recording from two ticks ago and is emptied node by node as
    // this tick touches it (see Path::Touch). Interpolation reads
    // `previous_recording`, whose generation is untouched.
    std::swap(previous_recording, current_recording);
    ++record_generation;
    new_paths_this_tick = 0;
    current_path.clear();
    current_recording.root_path->Touch();
    current_path.push_back(current_recording.root_path.get());
    if (OTRGlobals::Instance->GetInterpolationFPS() != 20) {
        is_recording = true;
    }
}

void FrameInterpolation_ShrinkRecording(void) {
    // Free both trees outright: the next tick rebuilds only what it records,
    // and the shape of the new scene is different anyway.
    is_recording = false;
    current_path.clear();
    previous_recording = {};
    current_recording = {};
    ++record_generation;
}

void FrameInterpolation_StopRecord(void) {
    previous_camera_epoch = camera_epoch;
    // Keep the expiry queue in child-before-parent order even when a draw
    // branch leaves a scope open. Otherwise a reused, unclosed ancestor can
    // remain at the queue head forever and prevent expired nodes retiring.
    while (current_path.size() > 1) {
        current_path.back()->Finish();
        current_path.pop_back();
    }
    is_recording = false;
    ReclaimExpiredPaths();
}

void FrameInterpolation_RecordOpenChild(const void* a, int b) {
    if (!is_recording)
        return;
    label key = { a, b };
    Path* parent = current_path.back();
    Path::ChildSlot& slot = parent->children[key];
    if (slot.generation != record_generation) {
        slot.generation = record_generation;
        slot.used = 0;
    }
    const size_t idx = slot.used++;
    append(Op::OpenChild).open_child = { key, idx };
    if (idx == slot.paths.size()) {
        slot.paths.emplace_back();
    }
    if (!slot.paths[idx]) {
        slot.paths[idx] = make_unique<Path>();
        ++slot.retained;
        ++new_paths_this_tick;
        slot.paths[idx]->parent = parent;
        slot.paths[idx]->parent_key = key;
        slot.paths[idx]->parent_index = idx;
    }
    Path* child = slot.paths[idx].get();
    child->Touch();
    current_path.push_back(child);
}

void FrameInterpolation_RecordCloseChild(void) {
    if (!is_recording)
        return;
    // append(Op::CloseChild);
    if (has_inv_actor_mtx && current_path.size() == inv_actor_mtx_path_index) {
        has_inv_actor_mtx = false;
    }
    current_path.back()->Finish();
    current_path.pop_back();
}

void FrameInterpolation_DontInterpolateCamera(void) {
    camera_epoch = previous_camera_epoch + 1;
}

int FrameInterpolation_GetCameraEpoch(void) {
    return (int)camera_epoch;
}

void FrameInterpolation_RecordActorPosRotMatrix(void) {
    if (!is_recording)
        return;
    next_is_actor_pos_rot_matrix = true;
}

void FrameInterpolation_RecordMatrixPush(void) {
    if (!is_recording)
        return;
    append(Op::MatrixPush);
}

void FrameInterpolation_RecordMatrixPop(void) {
    if (!is_recording)
        return;
    append(Op::MatrixPop);
}

void FrameInterpolation_RecordMatrixPut(MtxF* src) {
    if (!is_recording)
        return;
    append(Op::MatrixPut).matrix_put = { *src };
}

void FrameInterpolation_RecordMatrixMult(MtxF* mf, u8 mode) {
    if (!is_recording)
        return;
    append(Op::MatrixMult).matrix_mult = { *mf, mode };
}

void FrameInterpolation_RecordMatrixTranslate(f32 x, f32 y, f32 z, u8 mode) {
    if (!is_recording)
        return;
    append(Op::MatrixTranslate).matrix_translate = { x, y, z, mode };
}

void FrameInterpolation_RecordMatrixScale(f32 x, f32 y, f32 z, u8 mode) {
    if (!is_recording)
        return;
    append(Op::MatrixScale).matrix_scale = { x, y, z, mode };
}

void FrameInterpolation_RecordMatrixRotate1Coord(u32 coord, f32 value, u8 mode) {
    if (!is_recording)
        return;
    append(Op::MatrixRotate1Coord).matrix_rotate_1_coord = { coord, value, mode };
}

void FrameInterpolation_RecordMatrixRotateZYX(s16 x, s16 y, s16 z, u8 mode) {
    if (!is_recording)
        return;
    append(Op::MatrixRotateZYX).matrix_rotate_zyx = { x, y, z, mode };
}

void FrameInterpolation_RecordMatrixTranslateRotateZYX(Vec3f* translation, Vec3s* rotation) {
    if (!is_recording)
        return;
    append(Op::MatrixTranslateRotateZYX).matrix_translate_rotate_zyx = { *translation, *rotation };
}

void FrameInterpolation_RecordMatrixSetTranslateRotateYXZ(f32 translateX, f32 translateY, f32 translateZ, Vec3s* rot) {
    if (!is_recording)
        return;
    auto& d = append(Op::MatrixSetTranslateRotateYXZ).matrix_set_translate_rotate_yxz = { translateX, translateY,
                                                                                          translateZ, *rot };
    if (next_is_actor_pos_rot_matrix) {
        d.has_mtx = true;
        // d.mtx = *Matrix_GetCurrent();
        invert_matrix((const float*)Matrix_GetCurrent()->mf, (float*)inv_actor_mtx.mf);
        next_is_actor_pos_rot_matrix = false;
        has_inv_actor_mtx = true;
        inv_actor_mtx_path_index = current_path.size();
    }
}

void FrameInterpolation_RecordMatrixMtxFToMtx(MtxF* src, Mtx* dest) {
    if (!is_recording)
        return;
    append(Op::MatrixMtxFToMtx).matrix_mtxf_to_mtx = { *src, dest };
}

void FrameInterpolation_RecordMatrixToMtx(Mtx* dest, char* file, s32 line) {
    if (!is_recording)
        return;
    auto& d = append(Op::MatrixToMtx).matrix_to_mtx = { dest };
    if (has_inv_actor_mtx) {
        d.has_adjusted = true;
        SkinMatrix_MtxFMtxFMult(&inv_actor_mtx, Matrix_GetCurrent(), &d.src);
    } else {
        d.src = *Matrix_GetCurrent();
    }
}

void FrameInterpolation_RecordMatrixReplaceRotation(MtxF* mf) {
    if (!is_recording)
        return;
    append(Op::MatrixReplaceRotation).matrix_replace_rotation = { *mf };
}

void FrameInterpolation_RecordMatrixRotateAxis(f32 angle, Vec3f* axis, u8 mode) {
    if (!is_recording)
        return;
    append(Op::MatrixRotateAxis).matrix_rotate_axis = { angle, *axis, mode };
}

void FrameInterpolation_RecordSkinMatrixMtxFToMtx(MtxF* src, Mtx* dest) {
    if (!is_recording)
        return;
    FrameInterpolation_RecordMatrixMtxFToMtx(src, dest);
}

// https://stackoverflow.com/questions/1148309/inverting-a-4x4-matrix
static bool invert_matrix(const float m[16], float invOut[16]) {
    float inv[16], det;
    int i;

    // clang-format off
    inv[0] = m[5]  * m[10] * m[15] -
             m[5]  * m[11] * m[14] -
             m[9]  * m[6]  * m[15] +
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] -
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] +
              m[4]  * m[11] * m[14] +
              m[8]  * m[6]  * m[15] -
              m[8]  * m[7]  * m[14] -
              m[12] * m[6]  * m[11] +
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] -
             m[4]  * m[11] * m[13] -
             m[8]  * m[5] * m[15] +
             m[8]  * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] -
               m[8]  * m[6] * m[13] -
               m[12] * m[5] * m[10] +
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
              m[1]  * m[11] * m[14] +
              m[9]  * m[2] * m[15] -
              m[9]  * m[3] * m[14] -
              m[13] * m[2] * m[11] +
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
             m[0]  * m[11] * m[14] -
             m[8]  * m[2] * m[15] +
             m[8]  * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
              m[0]  * m[11] * m[13] +
              m[8]  * m[1] * m[15] -
              m[8]  * m[3] * m[13] -
              m[12] * m[1] * m[11] +
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
              m[0]  * m[10] * m[13] -
              m[8]  * m[1] * m[14] +
              m[8]  * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
             m[1]  * m[7] * m[14] -
             m[5]  * m[2] * m[15] +
             m[5]  * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
              m[0]  * m[7] * m[14] +
              m[4]  * m[2] * m[15] -
              m[4]  * m[3] * m[14] -
              m[12] * m[2] * m[7] +
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
              m[0]  * m[7] * m[13] -
              m[4]  * m[1] * m[15] +
              m[4]  * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
               m[0]  * m[6] * m[13] +
               m[4]  * m[1] * m[14] -
               m[4]  * m[2] * m[13] -
               m[12] * m[1] * m[6] +
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
              m[1] * m[7] * m[10] +
              m[5] * m[2] * m[11] -
              m[5] * m[3] * m[10] -
              m[9] * m[2] * m[7] +
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
               m[0] * m[7] * m[9] +
               m[4] * m[1] * m[11] -
               m[4] * m[3] * m[9] -
               m[8] * m[1] * m[7] +
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    // clang-format on

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (det == 0) {
        return false;
    }

    det = 1.0f / det;

    for (i = 0; i < 16; i++) {
        invOut[i] = inv[i] * det;
    }

    return true;
}
