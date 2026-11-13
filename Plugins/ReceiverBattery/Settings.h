#pragma once

#include <string>

enum class DeviceSelection
{
    Auto = 0,
    Receiver2G4 = 1,
    Logitech = 2,
    Gamepad = 3
};

// 插件设置。字段都提供了默认值，首次运行不需要配置文件。
struct SettingData
{
    bool show_tooltip{ true };          // 鼠标提示显示详细信息
    bool enable_generic_hid{ true };    // 没有已知 2.4G 协议时，尝试标准 HID 电量
    int poll_interval{ 10 };            // 查询间隔（秒）
    int selected_device{ 0 };           // 0=自动，1=2.4G，2=罗技，3=手柄
    int display_mask{ 0 };              // 自定义同时显示：1=2.4G，2=罗技，4=手柄；0=使用 selected_device
};
