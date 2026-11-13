#include "XInputBattery.h"

#include <windows.h>
#include <XInput.h>

#include <cstdio>
#include <mutex>
#include <utility>

namespace
{
    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using XInputGetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using XInputGetBatteryInformationFn = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);

    struct XInputApi
    {
        HMODULE module{};
        XInputGetStateFn get_state{};
        XInputGetCapabilitiesFn get_capabilities{};
        XInputGetBatteryInformationFn get_battery{};
    };

    XInputApi LoadXInput()
    {
        XInputApi api;
        const wchar_t* candidates[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
        for (const wchar_t* name : candidates)
        {
            api.module = LoadLibraryW(name);
            if (api.module != nullptr)
                break;
        }
        if (api.module != nullptr)
        {
            api.get_state = reinterpret_cast<XInputGetStateFn>(
                GetProcAddress(api.module, "XInputGetState"));
            api.get_capabilities = reinterpret_cast<XInputGetCapabilitiesFn>(
                GetProcAddress(api.module, "XInputGetCapabilities"));
            api.get_battery = reinterpret_cast<XInputGetBatteryInformationFn>(
                GetProcAddress(api.module, "XInputGetBatteryInformation"));
        }
        return api;
    }

    const XInputApi& Api()
    {
        static XInputApi api = LoadXInput();
        return api;
    }

    int BatteryLevelToPercent(BYTE level)
    {
        switch (level)
        {
        case BATTERY_LEVEL_EMPTY: return 0;
        case BATTERY_LEVEL_LOW: return 25;
        case BATTERY_LEVEL_MEDIUM: return 60;
        case BATTERY_LEVEL_FULL: return 100;
        default: return -1;
        }
    }

    const wchar_t* BatteryLevelText(BYTE level)
    {
        switch (level)
        {
        case BATTERY_LEVEL_EMPTY: return L"空";
        case BATTERY_LEVEL_LOW: return L"低";
        case BATTERY_LEVEL_MEDIUM: return L"中";
        case BATTERY_LEVEL_FULL: return L"满";
        default: return L"未知";
        }
    }
}

std::vector<XInputDeviceInfo> EnumerateXInputDevices()
{
    std::vector<XInputDeviceInfo> result;
    const XInputApi& api = Api();
    if (api.get_state == nullptr)
        return result;

    for (unsigned long slot = 0; slot < XUSER_MAX_COUNT; ++slot)
    {
        XINPUT_STATE state{};
        if (api.get_state(static_cast<DWORD>(slot), &state) != ERROR_SUCCESS)
            continue;

        XInputDeviceInfo info;
        info.slot = slot;
        info.connected = true;
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"Xbox 兼容手柄（槽位 %lu）", slot);
        info.name = buffer;
        result.push_back(std::move(info));
    }
    return result;
}

BatteryReading ReadXInputBattery(const XInputDeviceInfo& device)
{
    BatteryReading result;
    result.device_name = device.name;
    result.source = L"XInput";

    const XInputApi& api = Api();
    if (api.get_state == nullptr || api.get_battery == nullptr)
    {
        result.error = L"系统未提供 XInput 接口";
        return result;
    }

    XINPUT_STATE state{};
    if (api.get_state(static_cast<DWORD>(device.slot), &state) != ERROR_SUCCESS)
    {
        result.error = L"手柄未连接";
        return result;
    }

    XINPUT_BATTERY_INFORMATION battery{};
    if (api.get_battery(static_cast<DWORD>(device.slot), BATTERY_DEVTYPE_GAMEPAD, &battery) != ERROR_SUCCESS)
    {
        result.error = L"手柄不支持电量查询";
        return result;
    }

    switch (battery.BatteryType)
    {
    case BATTERY_TYPE_DISCONNECTED:
        result.error = L"手柄未连接";
        return result;

    case BATTERY_TYPE_WIRED:
        result.ok = true;
        result.percent = -1;
        result.wired = true;
        result.detail = L"手柄通过 USB / 2.4G 接收器以有线模式报告，系统不提供电池百分比";
        result.source = L"XInput（有线）";
        return result;

    case BATTERY_TYPE_ALKALINE:
    case BATTERY_TYPE_NIMH:
    {
        const int percent = BatteryLevelToPercent(battery.BatteryLevel);
        if (percent < 0)
        {
            result.error = L"手柄电量等级无效";
            return result;
        }
        result.ok = true;
        result.percent = percent;
        result.wired = false;
        wchar_t buffer[128]{};
        swprintf_s(buffer, L"XInput 电量等级：%s（仅提供粗略档位）",
            BatteryLevelText(battery.BatteryLevel));
        result.detail = buffer;
        result.source = L"XInput（无线）";
        return result;
    }

    default:
        result.error = L"未知的手柄电池类型";
        return result;
    }
}
