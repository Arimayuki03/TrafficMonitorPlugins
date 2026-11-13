#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "PluginInterface.h"
#include "Settings.h"
#include "HidBattery.h"
#include "ReceiverBatteryItem.h"

enum class SnapshotKind
{
    Receiver2G4 = 0,
    Logitech = 1,
    Gamepad = 2
};

constexpr int kSnapshotCount = 3;

struct BatterySnapshot
{
    int percent{ -1 };
    bool has_data{};
    bool connected{};
    bool charging{};
    bool wired{};
    unsigned char flag{};
    std::wstring device_name;
    std::wstring device_id;
    std::wstring source;
    std::wstring error;
    std::wstring detail;
    SYSTEMTIME last_success{};
    bool has_last_success{};
};

class CReceiverBatteryData
{
public:
    static CReceiverBatteryData& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig();

    SettingData GetSettings() const;
    void ApplySettings(const SettingData& settings);

    void EnsureStarted();
    void Stop();

    BatterySnapshot GetSnapshot(SnapshotKind kind) const;
    SnapshotKind GetActiveKind() const;
    BatterySnapshot GetActiveSnapshot() const;
    std::wstring GetActiveLabel();
    std::wstring GetTooltip();
    std::wstring GetValueText(SnapshotKind kind);
    std::wstring GetDisplayText();

    bool ShowOptionsDialog(HWND parent);
    void SetApp(ITrafficMonitor* app) { m_app = app; }

private:
    CReceiverBatteryData();
    ~CReceiverBatteryData();

    CReceiverBatteryData(const CReceiverBatteryData&) = delete;
    CReceiverBatteryData& operator=(const CReceiverBatteryData&) = delete;

    void WorkerLoop();
    void DoQueryOnce();
    void QueryReceiver(const std::vector<HidDeviceInfo>& devices);
    void QueryLogitech(const std::vector<HidDeviceInfo>& devices);
    void QueryGamepad();
    void UpdateReading(SnapshotKind kind, const BatteryReading& reading,
        const std::wstring& device_name, const std::wstring& device_id);
    void UpdateNoDevice(SnapshotKind kind, const std::wstring& error);
    std::wstring GetModulePath() const;

    mutable std::mutex m_state_mutex;
    std::array<BatterySnapshot, kSnapshotCount> m_snapshots;

    mutable std::mutex m_settings_mutex;
    SettingData m_settings;

    mutable std::mutex m_config_mutex;
    std::wstring m_config_path;

    std::mutex m_worker_mutex;
    std::thread m_worker;
    std::atomic<bool> m_stop{ false };

    std::mutex m_wake_mutex;
    std::condition_variable m_wake_cv;
    bool m_refresh{ false };

    ITrafficMonitor* m_app{};
};

#define g_data CReceiverBatteryData::Instance()

class CReceiverBattery : public ITMPlugin
{
private:
    CReceiverBattery() = default;

public:
    static CReceiverBattery& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;
    virtual void* GetPluginIcon() override;

private:
    static CReceiverBattery m_instance;
    CReceiverBatteryItem m_item;
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
