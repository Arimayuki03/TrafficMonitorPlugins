#pragma once

#include <string>
#include <vector>

enum class DeviceKind
{
    Unknown,
    Aula2G4,
    AtkVxe2G4,
    LogitechHidpp,
    GenericHid
};

// 一个可查询电量的 HID 顶层集合。
struct HidDeviceInfo
{
    std::wstring path;
    std::wstring product;
    std::wstring manufacturer;
    unsigned short vid{};
    unsigned short pid{};
    unsigned short usage_page{};
    unsigned short usage{};
    unsigned short input_len{};
    unsigned short output_len{};
    unsigned short feature_len{};
    bool is_aula{};
    bool is_atk{};
    bool is_logitech{};
    bool has_hid_battery{};
    DeviceKind kind{ DeviceKind::Unknown };

    std::wstring DisplayName() const;
    std::wstring IdString() const;
};

struct BatteryReading
{
    bool ok{};
    int percent{ -1 };
    bool charging{};
    bool wired{};
    unsigned char flag{};
    std::wstring source;
    std::wstring device_name;
    std::wstring error;
    std::wstring detail;
};

// 枚举系统中所有 HID 接口，只返回本插件能处理的对象。
std::vector<HidDeviceInfo> EnumerateHidDevices();

// 按设备类型选择对应协议读取一次电量。
BatteryReading ReadBatteryFromDevice(const HidDeviceInfo& device);
