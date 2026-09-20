#include "pch.h"
#include "DataManager.h"
#include "Common.h"
#include <vector>
#include <sstream>
#include "../utilities/FilePathHelper.h"
#include "../utilities/IniHelper.h"

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
    //初始化DPI
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);
}

CDataManager::~CDataManager()
{
    SaveConfig();
    //释放缓存的图标
    for (const auto& icon : m_icons)
    {
        if (icon.second != nullptr)
            DestroyIcon(icon.second);
    }
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    //获取模块的路径
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hModule, path, MAX_PATH);
    std::wstring module_path = path;
    m_config_path = module_path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        //模块的文件名
        std::wstring module_file_name = module_path.substr(index + 1);
        m_config_path = config_dir + module_file_name;
    }
    m_config_path = utilities::CFilePathHelper(m_config_path).GetFilePathWithoutExtension();
    m_config_path += L".ini";
    utilities::CIniHelper ini(m_config_path);
    std::wstring connection_name = ini.GetString(L"config", L"connection_name");
    std::lock_guard<std::mutex> lock(m_data_mutex);
    if (connection_name.empty() && !m_connections.empty())
    {
        connection_name = m_connections[0].description;
    }
    m_setting_data.current_connection_name = connection_name;
    RebuildCurrentIPv4();
}

void CDataManager::SaveConfig() const
{
    if (!m_config_path.empty())
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        utilities::CIniHelper ini(m_config_path);
        ini.WriteString(L"config", L"connection_name", m_setting_data.current_connection_name);
        ini.Save();
    }
}

const CString& CDataManager::StringRes(UINT id)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    auto iter = m_string_table.find(id);
    if (iter != m_string_table.end())
    {
        return iter->second;
    }
    else
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        m_string_table[id].LoadString(id);
        return m_string_table[id];
    }
}

void CDataManager::DPIFromWindow(CWnd* pWnd)
{
    CWindowDC dc(pWnd);
    HDC hDC = dc.GetSafeHdc();
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
}

int CDataManager::DPI(int pixel)
{
    return m_dpi * pixel / 96;
}

float CDataManager::DPIF(float pixel)
{
    return m_dpi * pixel / 96;
}

int CDataManager::RDPI(int pixel)
{
    return pixel * 96 / m_dpi;
}

HICON CDataManager::GetIcon(UINT id)
{
    auto iter = m_icons.find(id);
    if (iter != m_icons.end())
    {
        return iter->second;
    }
    else
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        HICON hIcon = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(id), IMAGE_ICON, DPI(16), DPI(16), 0);
        //加载失败时不缓存，允许下次重试
        if (hIcon != nullptr)
            m_icons[id] = hIcon;
        return hIcon;
    }
}

void CDataManager::UpdateConnections()
{
    //GetAdapterInfo在锁外执行（耗时网络API），锁内只做列表与快照的重建
    std::vector<NetWorkConection> connections;
    CAdapterCommon::GetAdapterInfo(connections);
    std::lock_guard<std::mutex> lock(m_data_mutex);
    m_connections = std::move(connections);
    RebuildCurrentIPv4();
}

void CDataManager::SetCurrentConnectionName(const std::wstring& connection_name)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    m_setting_data.current_connection_name = connection_name;
    RebuildCurrentIPv4();
}

std::wstring CDataManager::GetCurrentConnectionName() const
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_setting_data.current_connection_name;
}

void CDataManager::RebuildCurrentIPv4()
{
    //需在持有m_data_mutex时调用
    m_current_ipv4.clear();
    for (const auto& item : m_connections)
    {
        if (item.description == m_setting_data.current_connection_name)
        {
            m_current_ipv4 = item.ip_address;
            break;
        }
    }
}

std::wstring CDataManager::GetCurrentIPv4Address() const
{
    //只读快照，供UI线程拷出使用，不再在此重赋成员
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_current_ipv4;
}

std::vector<NetWorkConection> CDataManager::GetAllConnections() const
{
    //拷出副本，UI线程不在监控线程重建列表期间持有引用
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_connections;
}
