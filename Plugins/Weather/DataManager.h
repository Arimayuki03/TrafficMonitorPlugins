#pragma once
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include "resource.h"
#include "HistoryWeatherMgr.h"
#include "CommonData.h"

#define g_data CDataManager::Instance()

struct CityCodeItem
{
    std::wstring name;
    std::wstring code;
};

class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    //保护设置类数据(m_setting_data、m_auto_located、m_auto_locate_succeed、m_update_time)的互斥量。
    //这些数据会被后台线程(自动定位/解析)与UI线程并发读写。锁内可能再次加锁，使用recursive_mutex。
    //加锁顺序：CWeather::m_data_mutex -> m_settings_mutex -> m_string_table_mutex/m_history_mutex，禁止反向嵌套
    mutable std::recursive_mutex m_settings_mutex;

    void InitCityList();        //初始化城市列表
    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig() const;
    SettingData GetSettingData() const;    //在锁内返回设置数据的副本
    const CString& StringRes(UINT id);      //根据资源id获取一个字符串资源
    void DPIFromWindow(CWnd* pWnd);
    int DPI(int pixel);
    float DPIF(float pixel);
    int RDPI(int pixel);
    HICON GetIcon(UINT id, bool big = false);   //获取一个图标。big为true时大小为24x24，否则为16x16
    CityCodeItem CurCity() const;
    void ResetText();
    HICON GetWeatherIcon(const std::wstring& weather_type, bool big = false);
    CHistoryWeatherMgr& HistoryWeatherMgr();
    const std::vector<CityCodeItem>& CityList() const { return m_city_list; }

    WeatherInfo& GetWeather();

    std::map<WeahterSelected, WeatherInfo> m_weather_info;
    CTime m_update_time;        //更新时间
    std::wstring m_aqi{};       //AQI
    std::wstring m_quality;     //空气质量

    CString GetUpdateTimeAsString();  //获取更新日间的字符串格式

    SettingData m_setting_data;
    std::wstring m_config_dir;
    bool m_auto_located{};          //是否执行了自动定位
    bool m_auto_locate_succeed{};   //自动定位是否成功

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    mutable std::recursive_mutex m_string_table_mutex;      //保护m_string_table(后台解析线程与UI线程都会调用StringRes)
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;      //保存16x16大小的图标资源
    std::map<UINT, HICON> m_icons_big;  //保存24x24大小的图标资源
    std::map<std::wstring, UINT> m_weather_icon_id;     //保存所有天气图标的ID
    int m_dpi{ 96 };
    CHistoryWeatherMgr m_history_weather_mgr;
    std::vector<CityCodeItem> m_city_list;
};
