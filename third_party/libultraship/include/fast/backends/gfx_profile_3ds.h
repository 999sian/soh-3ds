#pragma once

#include <cstdint>

// Optional hardware timing owned by the 3DS renderer. No SDK types leak into
// libultra translation units, and other renderers need not implement it.
enum class Soh3dsProfileSection : unsigned {
    DisplayList, Draw, Pack, State, Depth, Interpolate,
    Vertex, Triangle, TriangleState, Texture, TriangleKey, TriangleEmit, Count,
};

#ifdef __3DS__
extern "C" uint64_t Soh3dsProfileBegin(unsigned section) __attribute__((weak));
extern "C" void Soh3dsProfileEnd(unsigned section, uint64_t start) __attribute__((weak));
extern "C" void Soh3dsProfilePack(unsigned common, uint32_t vertices) __attribute__((weak));

inline void Soh3dsProfilePackBatch(bool common, uint32_t vertices) {
    if (Soh3dsProfilePack != nullptr) Soh3dsProfilePack(common, vertices);
}

class Soh3dsProfileScope {
  public:
    explicit Soh3dsProfileScope(Soh3dsProfileSection section)
        : section_(static_cast<unsigned>(section)),
          start_(Soh3dsProfileBegin != nullptr && Soh3dsProfileEnd != nullptr
                     ? Soh3dsProfileBegin(section_) : 0) {}
    ~Soh3dsProfileScope() {
        if (start_ != 0) Soh3dsProfileEnd(section_, start_);
    }
    Soh3dsProfileScope(const Soh3dsProfileScope&) = delete;
    Soh3dsProfileScope& operator=(const Soh3dsProfileScope&) = delete;

  private:
    unsigned section_;
    uint64_t start_;
};
#else
inline void Soh3dsProfilePackBatch(bool, uint32_t) {}
class Soh3dsProfileScope {
  public:
    explicit Soh3dsProfileScope(Soh3dsProfileSection) {}
};
#endif
