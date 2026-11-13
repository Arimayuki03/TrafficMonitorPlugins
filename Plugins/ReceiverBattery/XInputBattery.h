#pragma once

#include <string>
#include <vector>

#include "HidBattery.h"

struct XInputDeviceInfo
{
    unsigned long slot{};
    bool connected{};
    std::wstring name;
};

std::vector<XInputDeviceInfo> EnumerateXInputDevices();
BatteryReading ReadXInputBattery(const XInputDeviceInfo& device);
