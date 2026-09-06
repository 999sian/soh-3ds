#include "ship/controller/controldevice/controller/mapping/sdl/SDLGyroMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerGyroMapping.h"
#include <spdlog/spdlog.h>
#include "ship/Context.h"

#include "ship/config/ConsoleVariable.h"
#include "ship/utils/StringHelper.h"
#include "ship/controller/controldeck/ControlDeck.h"


#ifdef __3DS__
#include <cmath>
#include <cstdint>

// SoH-3DS: SDL's 3DS joystick driver has no controller-attached sensors
// (its SetSensorsEnabled returns SDL_Unsupported), so
// SDL_GameControllerHasSensor(SDL_SENSOR_GYRO) is always false and the
// gamepad loops below can never find a gyro. The gyroscope exists as a
// standalone SDL_Sensor instead, and SDL's n3ds sensor driver passes
// hidGyroRead()'s raw angular rate through untouched - so open that sensor
// lazily and normalize raw ticks to rad/s (the unit controller-attached
// gyros report), keeping the sensitivity scale comparable to desktop.
extern "C" int32_t HIDUSER_GetGyroscopeRawToDpsCoefficient(float* coeff);
extern "C" int32_t HIDUSER_EnableGyroscope(void);

namespace {
bool Soh3dsReadGyroRadians(float out[3]) {
    static SDL_Sensor* sGyro = nullptr;
    static float sRadiansPerRaw = 0.0f;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        if (SDL_InitSubSystem(SDL_INIT_SENSOR) == 0) {
            for (int index = 0; index < SDL_NumSensors(); ++index) {
                if (SDL_SensorGetDeviceType(index) == SDL_SENSOR_GYRO) {
                    sGyro = SDL_SensorOpen(index);
                    break;
                }
            }
        }
        if (sGyro != nullptr) {
            // raw = dps * coefficient (nominally 14.375); guard against a
            // failed or nonsense read with that nominal value.
            float rawPerDps = 0.0f;
            if (HIDUSER_GetGyroscopeRawToDpsCoefficient(&rawPerDps) != 0 || rawPerDps <= 0.0f) {
                rawPerDps = 14.375f;
            }
            sRadiansPerRaw = static_cast<float>(M_PI / 180.0) / rawPerDps;
            SPDLOG_INFO("SoH-3DS gyro: opened SDL sensor, raw/dps={}", rawPerDps);
        } else {
            SPDLOG_WARN("SoH-3DS gyro: no SDL gyro sensor available");
        }
    }
    if (sGyro == nullptr) {
        return false;
    }
    SDL_SensorUpdate();
    float raw[3] = {};
    if (SDL_SensorGetData(sGyro, raw, 3) != 0) {
        return false;
    }
    out[0] = raw[0] * sRadiansPerRaw;
    out[1] = raw[1] * sRadiansPerRaw;
    out[2] = raw[2] * sRadiansPerRaw;
    return true;
}
} // namespace
#endif
namespace Ship {
SDLGyroMapping::SDLGyroMapping(uint8_t portIndex, float sensitivity, float neutralPitch, float neutralYaw,
                               float neutralRoll)
    : ControllerInputMapping(PhysicalDeviceType::SDLGamepad),
      ControllerGyroMapping(PhysicalDeviceType::SDLGamepad, portIndex, sensitivity), mNeutralPitch(neutralPitch),
      mNeutralYaw(neutralYaw), mNeutralRoll(neutralRoll) {
}

void SDLGyroMapping::Recalibrate() {
#ifdef __3DS__
    {
        float gyroRadians[3];
        if (Soh3dsReadGyroRadians(gyroRadians)) {
            mNeutralPitch = gyroRadians[0];
            mNeutralYaw = gyroRadians[1];
            mNeutralRoll = gyroRadians[2];
            return;
        }
    }
#endif
    for (const auto& [instanceId, gamepad] : Context::GetRawInstance()
                                                 ->GetControlDeck()
                                                 ->GetConnectedPhysicalDeviceManager()
                                                 ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
        if (!SDL_GameControllerHasSensor(gamepad, SDL_SENSOR_GYRO)) {
            continue;
        }

        // just use gyro on the first gyro supported device we find
        float gyroData[3];
        SDL_GameControllerSetSensorEnabled(gamepad, SDL_SENSOR_GYRO, SDL_TRUE);
        SDL_GameControllerGetSensorData(gamepad, SDL_SENSOR_GYRO, gyroData, 3);

        mNeutralPitch = gyroData[0];
        mNeutralYaw = gyroData[1];
        mNeutralRoll = gyroData[2];
        return;
    }

    // if we didn't find a gyro device zero everything out
    mNeutralPitch = 0;
    mNeutralYaw = 0;
    mNeutralRoll = 0;
}

void SDLGyroMapping::UpdatePad(float& x, float& y) {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        x = 0;
        y = 0;
        return;
    }

#ifdef __3DS__
    {
        float gyroRadians[3];
        if (Soh3dsReadGyroRadians(gyroRadians)) {
            x = (gyroRadians[0] - mNeutralPitch) * mSensitivity;
            y = (gyroRadians[1] - mNeutralYaw) * mSensitivity;
            return;
        }
    }
#endif

    for (const auto& [instanceId, gamepad] : Context::GetRawInstance()
                                                 ->GetControlDeck()
                                                 ->GetConnectedPhysicalDeviceManager()
                                                 ->GetConnectedSDLGamepadsForPort(mPortIndex)) {
        if (!SDL_GameControllerHasSensor(gamepad, SDL_SENSOR_GYRO)) {
            continue;
        }

        // just use gyro on the first gyro supported device we find
        float gyroData[3];
        SDL_GameControllerSetSensorEnabled(gamepad, SDL_SENSOR_GYRO, SDL_TRUE);
        SDL_GameControllerGetSensorData(gamepad, SDL_SENSOR_GYRO, gyroData, 3);

        x = (gyroData[0] - mNeutralPitch) * mSensitivity;
        y = (gyroData[1] - mNeutralYaw) * mSensitivity;
        return;
    }

    // if we didn't find a gyro device zero everything out
    x = 0;
    y = 0;
}

std::string SDLGyroMapping::GetGyroMappingId() {
    return StringHelper::Sprintf("P%d", mPortIndex);
}

void SDLGyroMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".GyroMappings." + GetGyroMappingId();

    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.GyroMappingClass", mappingCvarKey.c_str()).c_str(), "SDLGyroMapping");
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.Sensitivity", mappingCvarKey.c_str()).c_str(), mSensitivity);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.NeutralPitch", mappingCvarKey.c_str()).c_str(), mNeutralPitch);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.NeutralYaw", mappingCvarKey.c_str()).c_str(), mNeutralYaw);
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetFloat(
        StringHelper::Sprintf("%s.NeutralRoll", mappingCvarKey.c_str()).c_str(), mNeutralRoll);

    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void SDLGyroMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".GyroMappings." + GetGyroMappingId();

    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.GyroMappingClass", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Sensitivity", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.NeutralPitch", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.NeutralYaw", mappingCvarKey.c_str()).c_str());
    Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.NeutralRoll", mappingCvarKey.c_str()).c_str());

    Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string SDLGyroMapping::GetPhysicalDeviceName() {
    return "SDL Gamepad";
}
} // namespace Ship
