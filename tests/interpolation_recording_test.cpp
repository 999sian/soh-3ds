// Compile the entire production recorder. Only engine math and FPS selection
// are replaced; MatrixMtxFToMtx interpolation itself needs neither subsystem.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstddef>
#include <new>

static size_t allocatedBytes = 0, allocationCalls = 0;
struct alignas(std::max_align_t) AllocationHeader { size_t bytes; };
void* operator new(size_t bytes) {
    auto* p = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + bytes));
    if (!p) throw std::bad_alloc();
    p->bytes = bytes;
    allocatedBytes += bytes;
    ++allocationCalls;
    return p + 1;
}
void operator delete(void* pointer) noexcept {
    if (!pointer) return;
    auto* p = static_cast<AllocationHeader*>(pointer) - 1;
    allocatedBytes -= p->bytes;
    std::free(p);
}
void operator delete(void* pointer, size_t) noexcept { ::operator delete(pointer); }
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, size_t) noexcept { ::operator delete(pointer); }
#include "../third_party/shipwright/soh/soh/frame_interpolation.cpp"

extern "C" {
void Matrix_Push() { std::abort(); }
void Matrix_Pop() { std::abort(); }
void Matrix_Put(MtxF*) { std::abort(); }
void Matrix_Mult(MtxF*, u8) { std::abort(); }
void Matrix_Translate(f32, f32, f32, u8) { std::abort(); }
void Matrix_Scale(f32, f32, f32, u8) { std::abort(); }
void Matrix_RotateX(f32, u8) { std::abort(); }
void Matrix_RotateY(f32, u8) { std::abort(); }
void Matrix_RotateZ(f32, u8) { std::abort(); }
void Matrix_RotateZYX(s16, s16, s16, u8) { std::abort(); }
void Matrix_TranslateRotateZYX(Vec3f*, Vec3s*) { std::abort(); }
void Matrix_SetTranslateRotateYXZ(f32, f32, f32, Vec3s*) { std::abort(); }
MtxF* Matrix_GetCurrent() { std::abort(); }
void Matrix_ReplaceRotation(MtxF*) { std::abort(); }
void Matrix_RotateAxis(f32, Vec3f*, u8) { std::abort(); }
void SkinMatrix_MtxFMtxFMult(MtxF*, MtxF*, MtxF*) { std::abort(); }
}

// These walkers run only in the test, outside the recorder's timed work.
const Path& Dereference(const Path& p) { return p; }
const Path& Dereference(const std::unique_ptr<Path>& p) { return *p; }
bool Exists(const Path&) { return true; }
bool Exists(const std::unique_ptr<Path>& p) { return bool(p); }
struct Retained { size_t nodes = 0, bytes = 0; };
void Measure(const Path& path, Retained& total) {
    ++total.nodes;
    total.bytes += sizeof(Path) + path.items.capacity() * sizeof(path.items[0]);
    for (const auto& ops : path.ops) total.bytes += ops.capacity() * sizeof(Data);
    for (const auto& [key, slot] : path.children) {
        total.bytes += sizeof(key) + sizeof(slot) + slot.paths.capacity() * sizeof(slot.paths[0]);
        for (const auto& p : slot.paths) if (Exists(p)) Measure(Dereference(p), total);
    }
}
Retained Measure() {
    Retained total;
    Measure(Dereference(current_recording.root_path), total);
    Measure(Dereference(previous_recording.root_path), total);
    return total;
}

static int stableLabel, particleLabels[24], leafLabel;
static Mtx stableDest, particleDest[24];
void RecordMatrix(Mtx* dest, float value) {
    MtxF m{};
    m.mf[0][0] = value;
    FrameInterpolation_RecordMatrixMtxFToMtx(&m, dest);
}
void Record(int frame, bool churn) {
    FrameInterpolation_StartRecord();
    FrameInterpolation_RecordOpenChild(&stableLabel, 0);
    RecordMatrix(&stableDest, float(frame));
    FrameInterpolation_RecordCloseChild();
    if (churn) {
        for (int i = 0; i < 24; ++i) {
            // Same slot address, fresh epoch each tick, nested draw paths.
            FrameInterpolation_RecordOpenChild(&particleLabels[i], frame);
            FrameInterpolation_RecordOpenChild(&leafLabel, 0);
            RecordMatrix(&particleDest[i], float(frame + 100));
            FrameInterpolation_RecordCloseChild();
            FrameInterpolation_RecordCloseChild();
        }
    }
    FrameInterpolation_StopRecord();
}
static Mtx duplicateDest[64];
void RecordDuplicates(int frame, int count) {
    FrameInterpolation_StartRecord();
    for (int i = 0; i < count; ++i) {
        FrameInterpolation_RecordOpenChild(&stableLabel, 42);
        RecordMatrix(&duplicateDest[i], float(frame * 100 + i));
        FrameInterpolation_RecordCloseChild();
    }
    FrameInterpolation_StopRecord();
}
int main() {
    OTRGlobals globals;
    OTRGlobals::Instance = &globals;
    Retained baseline;
    size_t baselineHeap = 0;
    for (int frame = 0; frame < 2000; ++frame) {
        Record(frame, true);
        auto result = FrameInterpolation_Interpolate(0.5f);
        assert(result.at(&stableDest).mf[0][0] == (frame ? frame - 0.5f : 0.0f));
        // A recycled epoch has no old identity and must use its current value.
        assert(result.at(&particleDest[0]).mf[0][0] == frame + 100.0f);
        if (frame == 100) {
            baseline = Measure();
            baselineHeap = allocatedBytes;
        }
        if (frame > 100 && frame % 100 == 0) {
            const auto retained = Measure();
            if (retained.nodes > baseline.nodes + 100 || retained.bytes > baseline.bytes + 65536) {
                std::fprintf(stderr, "retention grew: %zu -> %zu nodes, %zu -> %zu retained bytes at tick %d\n",
                             baseline.nodes, retained.nodes, baseline.bytes, retained.bytes, frame);
                return 1;
            }
            assert(allocatedBytes <= baselineHeap + 65536);
        }
    }
    std::printf("2000 epoch ticks plateau: %zu nodes / %zu retained bytes / %zu live allocated bytes\n",
                baseline.nodes, baseline.bytes, baselineHeap);
    // Removing all particles must eventually reclaim their entire nested trees.
    for (int frame = 2000; frame < 2200; ++frame) Record(frame, false);
    assert(Measure().nodes == 4); // Two roots and their stable child.
    FrameInterpolation_ShrinkRecording();
    assert(Measure().nodes == 2);
    Record(0, true);
    assert(FrameInterpolation_Interpolate(0.5f).at(&stableDest).mf[0][0] == 0.0f);

    FrameInterpolation_ShrinkRecording();
    RecordDuplicates(0, 64);
    RecordDuplicates(1, 64);
    for (int frame = 2; frame < 30; ++frame) {
        const auto before = Measure().nodes;
        RecordDuplicates(frame, 1);
        const auto after = Measure().nodes;
        // A quiet tick retires at most the fixed allowance, even after a burst.
        assert(before - after <= 32);
    }
    assert(Measure().nodes == 4);
    // Retired occurrence slots must be safely recreated without changing the
    // matching occurrence's interpolation source.
    RecordDuplicates(30, 64);
    auto result = FrameInterpolation_Interpolate(0.5f);
    assert(result.at(&duplicateDest[0]).mf[0][0] == 2950.0f);
    assert(result.at(&duplicateDest[63]).mf[0][0] == 3063.0f);
    RecordDuplicates(31, 64);
    RecordDuplicates(32, 64);
    const auto beforeStable = allocationCalls;
    for (int frame = 33; frame < 1033; ++frame) RecordDuplicates(frame, 64);
    assert(allocationCalls == beforeStable); // Lazy reuse survives reclamation.
    FrameInterpolation_ShrinkRecording();

    // A draw branch can omit its closing scope (as the quest/randomizer menu
    // did). Reusing that unclosed ancestor must not pin the expiry queue and
    // retain every camera epoch drawn beneath it.
    for (int frame = 0; frame < 1000; ++frame) {
        FrameInterpolation_StartRecord();
        FrameInterpolation_RecordOpenChild(&stableLabel, 0);
        FrameInterpolation_RecordOpenChild(&leafLabel, frame);
        RecordMatrix(&stableDest, float(frame));
        FrameInterpolation_RecordCloseChild();
        if (frame < 2) FrameInterpolation_RecordCloseChild();
        FrameInterpolation_StopRecord();
        if (frame == 100) baselineHeap = allocatedBytes;
        if (frame > 100 && allocatedBytes > baselineHeap + 65536) {
            std::fprintf(stderr, "unclosed scope retention grew: %zu -> %zu bytes at tick %d\n",
                         baselineHeap, allocatedBytes, frame);
            return 1;
        }
        assert(FrameInterpolation_Interpolate(0.5f).at(&stableDest).mf[0][0] == float(frame));
    }
    FrameInterpolation_ShrinkRecording();
    std::puts("interpolation recorder regression passed");
}
