#pragma once
#include <string>
#include <map>
#include <mutex>
#include "resource.h"

#define g_data CDataManager::Instance()

#define BATTERY_COLOR_HIGH RGB(128, 194, 105)   //电池电量颜色——高
#define BATTERY_COLOR_LOW RGB(241, 197, 0)      //电池电量颜色——低
#define BATTERY_COLOR_CRITICAL RGB(206, 23, 0)  //电池电量颜色——电量不足

//电量显示样式
enum class BatteryType
{
    NUMBER,             //仅数字
    ICON,               //仅图标
    NUMBER_BESIDE_ICON  //数字显示在图标旁边
};

struct SettingData
{
    BatteryType battery_type{};
    bool show_battery_in_tooltip{};
    bool show_percent{};
    bool show_charging_animation{};     //显示充电动画
};

class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig() const;
    const CString& StringRes(UINT id);      //根据资源id获取一个字符串资源
    void DPIFromWindow(CWnd* pWnd);
    int DPI(int pixel);
    float DPIF(float pixel);
    int RDPI(int pixel);
    HICON GetIcon(UINT id);
    bool UpdateSystemPowerStatus();     //由监控线程调用，锁内获取系统电量，失败时保留原值
    SYSTEM_POWER_STATUS GetSystemPowerStatusCopy() const;   //锁内整体拷出m_sysPowerStatus副本，避免监控线程写入与UI线程逐字段读取撕裂
    bool IsAcOnline() const;        //电源是否已接通

    std::wstring GetBatteryString() const;
    COLORREF GetBatteryColor() const;

    SettingData m_setting_data;
    SYSTEM_POWER_STATUS m_sysPowerStatus{};   // 系统电量信息，仅由监控线程DataRequired写，读取请通过GetSystemPowerStatusCopy获取副本
    ULONG_PTR m_gdiplusToken{ 0 };

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    mutable std::recursive_mutex m_string_table_mutex;      //保护m_string_table(监控线程DataRequired首次插入与UI线程并发访问)
    std::map<UINT, HICON> m_icons;
    mutable std::mutex m_sysPowerStatus_mutex;              //保护m_sysPowerStatus的跨线程读写
    int m_dpi{ 96 };
};
