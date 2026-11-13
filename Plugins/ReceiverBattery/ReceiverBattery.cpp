#include "ReceiverBattery.h"
#include "OptionsDlg.h"
#include "XInputBattery.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

CReceiverBattery CReceiverBattery::m_instance;

namespace
{
    HMODULE GetThisModule()
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetThisModule), &module);
        return module;
    }
}

CReceiverBattery& CReceiverBattery::Instance()
{
    return m_instance;
}

IPluginItem* CReceiverBattery::GetItem(int index)
{
    if (index == 0)
        return &m_item;
    return nullptr;
}

void CReceiverBattery::DataRequired()
{
    g_data.EnsureStarted();
}

ITMPlugin::OptionReturn CReceiverBattery::ShowOptionsDialog(void* hParent)
{
    return g_data.ShowOptionsDialog(reinterpret_cast<HWND>(hParent))
        ? ITMPlugin::OR_OPTION_CHANGED
        : ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CReceiverBattery::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return L"外设电量";
    case TMI_DESCRIPTION:
        return L"显示 2.4G 接收器、罗技鼠标和游戏手柄的电量。支持 AULA/Compx、ATK/VXE/VGN、罗技 HID++、XInput 与标准 HID 电量设备。";
    case TMI_AUTHOR:
        return L"Arimayuki03";
    case TMI_COPYRIGHT:
        return L"Copyright (C) 2026";
    case TMI_URL:
        return L"https://github.com/Arimayuki03/TrafficMonitorPlugins";
    case TMI_VERSION:
        return L"1.03";
    default:
        break;
    }
    return L"";
}

const wchar_t* CReceiverBattery::GetTooltipInfo()
{
    thread_local std::wstring tooltip;
    tooltip = g_data.GetTooltip();
    return tooltip.c_str();
}

void CReceiverBattery::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == ITMPlugin::EI_CONFIG_DIR)
    {
        // 只保存配置路径。查询线程延迟到 DataRequired 再启动，避免和
        // TrafficMonitor 加载插件、创建窗口的过程产生竞态。
        g_data.LoadConfig(data != nullptr ? data : L"");
    }
}

void CReceiverBattery::OnInitialize(ITrafficMonitor* pApp)
{
    g_data.SetApp(pApp);
}

void* CReceiverBattery::GetPluginIcon()
{
    // 不使用自定义图标：交由主程序使用默认插件图标，避免图标转换路径。
    return nullptr;
}

CReceiverBatteryData::CReceiverBatteryData() = default;

CReceiverBatteryData::~CReceiverBatteryData()
{
    Stop();
}

CReceiverBatteryData& CReceiverBatteryData::Instance()
{
    static CReceiverBatteryData instance;
    return instance;
}

std::wstring CReceiverBatteryData::GetModulePath() const
{
    HMODULE module = GetThisModule();
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    return std::wstring(path, length);
}

void CReceiverBatteryData::LoadConfig(const std::wstring& config_dir)
{
    const std::filesystem::path module_path(GetModulePath());
    std::filesystem::path config_path;
    if (config_dir.empty())
        config_path = module_path;
    else
        config_path = std::filesystem::path(config_dir) / module_path.filename();
    config_path += L".ini";

    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);
        m_config_path = config_path.wstring();
        path = m_config_path;
    }

    SettingData settings;
    settings.show_tooltip = GetPrivateProfileIntW(L"config", L"show_tooltip", 1, path.c_str()) != 0;
    settings.enable_generic_hid = GetPrivateProfileIntW(L"config", L"enable_generic_hid", 1, path.c_str()) != 0;
    settings.poll_interval = GetPrivateProfileIntW(L"config", L"poll_interval", 10, path.c_str());
    settings.selected_device = GetPrivateProfileIntW(L"config", L"selected_device", 0, path.c_str());
    const int stored_mask = static_cast<int>(GetPrivateProfileIntW(
        L"config", L"display_mask", -1, path.c_str()));
    if (stored_mask >= 0)
        settings.display_mask = stored_mask & 0x07;
    else
        settings.display_mask = GetPrivateProfileIntW(
            L"config", L"show_all_devices", 0, path.c_str()) != 0 ? 0x07 : 0;

    ApplySettings(settings);
}

void CReceiverBatteryData::SaveConfig()
{
    std::wstring config_path;
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);
        config_path = m_config_path;
    }
    if (config_path.empty())
        return;

    const SettingData settings = GetSettings();
    auto write_int = [&](const wchar_t* key, int value)
    {
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"%d", value);
        WritePrivateProfileStringW(L"config", key, buffer, config_path.c_str());
    };

    write_int(L"show_tooltip", settings.show_tooltip ? 1 : 0);
    write_int(L"enable_generic_hid", settings.enable_generic_hid ? 1 : 0);
    write_int(L"poll_interval", settings.poll_interval);
    write_int(L"selected_device", settings.selected_device);
    write_int(L"display_mask", settings.display_mask);
}

SettingData CReceiverBatteryData::GetSettings() const
{
    std::lock_guard<std::mutex> lock(m_settings_mutex);
    return m_settings;
}

void CReceiverBatteryData::ApplySettings(const SettingData& settings)
{
    {
        std::lock_guard<std::mutex> lock(m_settings_mutex);
        m_settings = settings;
        m_settings.poll_interval = std::clamp(m_settings.poll_interval, 3, 300);
        m_settings.selected_device = std::clamp(m_settings.selected_device, 0, 3);
        m_settings.display_mask &= 0x07;
    }

    {
        std::lock_guard<std::mutex> lock(m_wake_mutex);
        m_refresh = true;
    }
    m_wake_cv.notify_all();
}

void CReceiverBatteryData::EnsureStarted()
{
    std::lock_guard<std::mutex> lock(m_worker_mutex);
    if (m_worker.joinable())
        return;
    m_stop.store(false);
    m_worker = std::thread(&CReceiverBatteryData::WorkerLoop, this);
}

void CReceiverBatteryData::Stop()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(m_worker_mutex);
        if (!m_worker.joinable())
            return;

        m_stop.store(true);
        {
            std::lock_guard<std::mutex> wake_lock(m_wake_mutex);
            m_refresh = true;
        }
        m_wake_cv.notify_all();
        worker = std::move(m_worker);
    }
    if (worker.joinable())
        worker.join();
}

void CReceiverBatteryData::WorkerLoop()
{
    // 让主程序完成窗口/任务栏初始化后再访问 HID。
    for (int i = 0; i < 10 && !m_stop.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    while (!m_stop.load())
    {
        DoQueryOnce();
        if (m_stop.load())
            break;

        const SettingData settings = GetSettings();
        const int interval = std::clamp(settings.poll_interval, 3, 300);
        std::unique_lock<std::mutex> lock(m_wake_mutex);
        m_wake_cv.wait_for(lock, std::chrono::seconds(interval), [this]()
        {
            return m_stop.load() || m_refresh;
        });
        m_refresh = false;
    }
}

void CReceiverBatteryData::DoQueryOnce()
{
    const SettingData settings = GetSettings();
    const std::vector<HidDeviceInfo> devices = EnumerateHidDevices();

    QueryReceiver(devices);
    QueryLogitech(devices);
    QueryGamepad();
}

void CReceiverBatteryData::QueryReceiver(const std::vector<HidDeviceInfo>& devices)
{
    const SettingData settings = GetSettings();
    const HidDeviceInfo* chosen = nullptr;

    for (const auto& device : devices)
    {
        if (device.kind == DeviceKind::Aula2G4)
        {
            chosen = &device;
            break;
        }
    }
    if (chosen == nullptr)
    {
        for (const auto& device : devices)
        {
            if (device.kind == DeviceKind::AtkVxe2G4)
            {
                chosen = &device;
                break;
            }
        }
    }
    if (chosen == nullptr && settings.enable_generic_hid)
    {
        for (const auto& device : devices)
        {
            if (device.kind == DeviceKind::GenericHid)
            {
                chosen = &device;
                break;
            }
        }
    }

    if (chosen == nullptr)
    {
        UpdateNoDevice(SnapshotKind::Receiver2G4,
            settings.enable_generic_hid
                ? L"未检测到受支持的 2.4G 接收器"
                : L"未检测到受支持的 2.4G 接收器（标准 HID 电量已关闭）");
        return;
    }

    const BatteryReading reading = ReadBatteryFromDevice(*chosen);
    UpdateReading(SnapshotKind::Receiver2G4, reading, chosen->DisplayName(), chosen->IdString());
}

void CReceiverBatteryData::QueryLogitech(const std::vector<HidDeviceInfo>& devices)
{
    std::vector<const HidDeviceInfo*> candidates;
    for (const auto& device : devices)
    {
        if (device.kind == DeviceKind::LogitechHidpp)
            candidates.push_back(&device);
    }

    if (candidates.empty())
    {
        UpdateNoDevice(SnapshotKind::Logitech, L"未检测到罗技设备");
        return;
    }

    // 优先使用长报文接口；同为长报文时优先 USB 接收器，其次蓝牙。
    // 这样同时插着 Lightspeed 接收器和蓝牙鼠标时，会稳定选择 USB 接收器
    // 那一只鼠标，而不是随枚举顺序变化。
    auto candidate_score = [](const HidDeviceInfo* device)
    {
        int score = device->output_len >= 20 ? 10 : 0;
        if (device->path.find(L"vid_046d&pid_") != std::wstring::npos)
            score += 5;
        return score;
    };
    std::stable_sort(candidates.begin(), candidates.end(),
        [&](const HidDeviceInfo* a, const HidDeviceInfo* b)
        {
            return candidate_score(a) > candidate_score(b);
        });

    std::wstring last_error;
    for (const HidDeviceInfo* device : candidates)
    {
        const BatteryReading reading = ReadBatteryFromDevice(*device);
        if (reading.ok)
        {
            UpdateReading(SnapshotKind::Logitech, reading, device->DisplayName(), device->IdString());
            return;
        }
        if (!reading.error.empty())
            last_error = reading.error;
    }

    UpdateNoDevice(SnapshotKind::Logitech,
        last_error.empty() ? L"罗技设备无响应（可能已休眠）" : last_error);
}

void CReceiverBatteryData::QueryGamepad()
{
    const std::vector<XInputDeviceInfo> pads = EnumerateXInputDevices();
    if (pads.empty())
    {
        UpdateNoDevice(SnapshotKind::Gamepad, L"未检测到 XInput 手柄");
        return;
    }

    const BatteryReading reading = ReadXInputBattery(pads.front());
    wchar_t id[64]{};
    swprintf_s(id, L"XInput 槽位 %lu", pads.front().slot);
    UpdateReading(SnapshotKind::Gamepad, reading, pads.front().name, id);
}

void CReceiverBatteryData::UpdateReading(SnapshotKind kind, const BatteryReading& reading,
    const std::wstring& device_name, const std::wstring& device_id)
{
    BatterySnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        snapshot = m_snapshots[static_cast<int>(kind)];
    }

    snapshot.device_name = reading.device_name.empty() ? device_name : reading.device_name;
    snapshot.device_id = device_id;
    if (!reading.source.empty())
        snapshot.source = reading.source;
    if (!reading.detail.empty())
        snapshot.detail = reading.detail;

    if (reading.ok)
    {
        if (reading.percent >= 0)
            snapshot.percent = reading.percent;
        else if (reading.wired)
            snapshot.percent = -1;

        snapshot.charging = reading.charging;
        snapshot.wired = reading.wired;
        snapshot.flag = reading.flag;
        snapshot.connected = true;
        snapshot.has_data = true;
        snapshot.error.clear();
        GetLocalTime(&snapshot.last_success);
        snapshot.has_last_success = true;
    }
    else
    {
        snapshot.connected = false;
        if (!reading.error.empty())
            snapshot.error = reading.error;
        else
            snapshot.error = L"读取电量失败";
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_snapshots[static_cast<int>(kind)] = snapshot;
    }
}

void CReceiverBatteryData::UpdateNoDevice(SnapshotKind kind, const std::wstring& error)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    BatterySnapshot& snapshot = m_snapshots[static_cast<int>(kind)];
    snapshot.connected = false;
    snapshot.error = error;
    if (!snapshot.has_data)
    {
        snapshot.percent = -1;
        snapshot.device_name.clear();
        snapshot.device_id.clear();
        snapshot.source.clear();
        snapshot.detail.clear();
        snapshot.wired = false;
    }
}

BatterySnapshot CReceiverBatteryData::GetSnapshot(SnapshotKind kind) const
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_snapshots[static_cast<int>(kind)];
}

SnapshotKind CReceiverBatteryData::GetActiveKind() const
{
    const SettingData settings = GetSettings();
    if (settings.selected_device >= 1 && settings.selected_device <= 3)
        return static_cast<SnapshotKind>(settings.selected_device - 1);

    const BatterySnapshot receiver = GetSnapshot(SnapshotKind::Receiver2G4);
    if (receiver.has_data)
        return SnapshotKind::Receiver2G4;
    const BatterySnapshot logitech = GetSnapshot(SnapshotKind::Logitech);
    if (logitech.has_data)
        return SnapshotKind::Logitech;
    const BatterySnapshot gamepad = GetSnapshot(SnapshotKind::Gamepad);
    if (gamepad.has_data)
        return SnapshotKind::Gamepad;
    return SnapshotKind::Receiver2G4;
}

BatterySnapshot CReceiverBatteryData::GetActiveSnapshot() const
{
    return GetSnapshot(GetActiveKind());
}

std::wstring CReceiverBatteryData::GetActiveLabel()
{
    const SnapshotKind kind = GetActiveKind();
    const BatterySnapshot snapshot = GetSnapshot(kind);
    if (!snapshot.has_data)
        return L"外设";

    switch (kind)
    {
    case SnapshotKind::Receiver2G4:
        return L"2.4G";
    case SnapshotKind::Logitech:
        return L"罗技";
    case SnapshotKind::Gamepad:
        return L"手柄";
    default:
        break;
    }
    return L"外设";
}

std::wstring CReceiverBatteryData::GetValueText(SnapshotKind kind)
{
    const BatterySnapshot snapshot = GetSnapshot(kind);
    if (snapshot.percent >= 0)
        return std::to_wstring(snapshot.percent) + L"%";
    if (snapshot.has_data && snapshot.wired)
        return L"有线";
    return L"--";
}

std::wstring CReceiverBatteryData::GetDisplayText()
{
    const SettingData settings = GetSettings();
    const int mask = settings.display_mask & 0x07;
    if (mask == 0)
    {
        const SnapshotKind kind = GetActiveKind();
        const std::wstring value = GetValueText(kind);
        if (value == L"--")
            return L"--";
        return GetActiveLabel() + L" " + value;
    }

    std::wstring text;
    auto append = [&](int bit, const wchar_t* label, SnapshotKind kind)
    {
        if ((mask & bit) == 0)
            return;
        const BatterySnapshot snapshot = GetSnapshot(kind);
        if (!snapshot.has_data)
            return;
        if (!text.empty())
            text += L" | ";
        text += label;
        text += L" ";
        if (snapshot.percent >= 0)
            text += std::to_wstring(snapshot.percent) + L"%";
        else if (snapshot.wired)
            text += L"有线";
        else
            text += L"--";
    };

    append(0x01, L"2.4G", SnapshotKind::Receiver2G4);
    append(0x02, L"罗技", SnapshotKind::Logitech);
    append(0x04, L"手柄", SnapshotKind::Gamepad);
    return text.empty() ? L"--" : text;
}

std::wstring CReceiverBatteryData::GetTooltip()
{
    const SettingData settings = GetSettings();
    if (!settings.show_tooltip)
        return L"";

    std::wstringstream text;
    text << L"外设电量\n";

    auto add_line = [&](const wchar_t* title, SnapshotKind kind)
    {
        const BatterySnapshot snapshot = GetSnapshot(kind);
        text << title << L": ";
        if (snapshot.percent >= 0)
            text << snapshot.percent << L"%";
        else if (snapshot.has_data && snapshot.wired)
            text << L"有线";
        else
            text << L"--";
        if (kind == SnapshotKind::Logitech && snapshot.has_data &&
            !snapshot.device_name.empty() &&
            snapshot.device_name != L"罗技 USB 接收器" &&
            snapshot.device_name != L"罗技蓝牙设备")
        {
            text << L"（" << snapshot.device_name << L"）";
        }
        if (snapshot.has_data && !snapshot.connected)
            text << L"（无响应）";
        text << L"\n";
    };

    add_line(L"2.4G", SnapshotKind::Receiver2G4);
    add_line(L"罗技", SnapshotKind::Logitech);
    add_line(L"手柄", SnapshotKind::Gamepad);
    return text.str();
}

bool CReceiverBatteryData::ShowOptionsDialog(HWND parent)
{
    COptionsDlg dialog(parent, GetSettings());
    if (!dialog.DoModal())
        return false;
    ApplySettings(dialog.GetSettings());
    SaveConfig();
    return true;
}

ITMPlugin* TMPluginGetInstance()
{
    return &CReceiverBattery::Instance();
}
