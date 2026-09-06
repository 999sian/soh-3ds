#pragma once
class OTRGlobals {
  public:
    inline static OTRGlobals* Instance = nullptr;
    int GetInterpolationFPS() { return 60; }
};
