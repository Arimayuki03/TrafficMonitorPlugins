#pragma once
#include <string>
#include <map>
#include <mutex>
#include "resource.h"
#include "AdapterCommon.h"

#define g_data CDataManager::Instance()


struct SettingData
{
    std::wstring current_connection_name;         //选择的网络连接名称
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

    //以下接口的共享数据会被宿主监控线程(DataRequired)与UI线程并发访问，内部由m_data_mutex保护
    void UpdateConnections();       //刷新网络连接列表并在锁内重建IPv4快照（监控线程定时刷新、UI线程手动刷新时调用）
    void SetCurrentConnectionName(const std::wstring& connection_name);     //更改所选连接并在锁内重算IPv4快照（UI线程调用）
    std::wstring GetCurrentConnectionName() const;      //锁内拷出所选连接名称，只读不写
    std::wstring GetCurrentIPv4Address() const;     //锁内拷出IPv4快照的副本，只读不写
    std::vector<NetWorkConection> GetAllConnections() const;        //锁内拷出连接列表副本，只读不写

    SettingData m_setting_data;

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;
    int m_dpi{ 96 };

    std::vector<NetWorkConection> m_connections;    //所有网络连接
    std::wstring m_current_ipv4;                //当前显示的IPv4地址值快照，仅UpdateConnections/LoadConfig/SetCurrentConnectionName在锁内整体赋值
    mutable std::mutex m_data_mutex;    //保护m_connections、m_current_ipv4、m_setting_data.current_connection_name、m_string_table

    void RebuildCurrentIPv4();      //根据当前选择重算IPv4快照，需在持有m_data_mutex时调用
};
