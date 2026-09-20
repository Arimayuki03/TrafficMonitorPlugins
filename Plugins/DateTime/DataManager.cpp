#include "pch.h"
#include "DataManager.h"

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
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    //获取模块的路径
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t path[MAX_PATH]{};
    DWORD module_path_length = GetModuleFileNameW(hModule, path, MAX_PATH);
    //获取失败或路径被截断时跳过配置读取，保留默认值，避免把空/截断的路径当作配置文件路径
    if (module_path_length == 0 || module_path_length >= MAX_PATH)
    {
        m_config_path.clear();
        return;
    }
    std::wstring module_path = path;
    m_config_path = module_path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        //模块的文件名
        std::wstring module_file_name = module_path.substr(index + 1);
        m_config_path = config_dir + module_file_name;
    }
    m_config_path += L".ini";
    //m_setting_data.show_second = GetPrivateProfileInt(_T("config"), _T("show_second"), 0, m_config_path.c_str());
    //m_setting_data.show_label_text = GetPrivateProfileInt(_T("config"), _T("show_label_text"), 1, m_config_path.c_str());
    TCHAR buf[256];
    GetPrivateProfileString(_T("config"), _T("time_format"), _T("HH:mm:ss"), buf, 255, m_config_path.c_str());
    m_setting_data.time_format = buf;
    GetPrivateProfileString(_T("config"), _T("date_format"), _T("yyyy/MM/dd"), buf, 255, m_config_path.c_str());
    m_setting_data.date_format = buf;
}

static void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
{
    wchar_t buff[16];
    swprintf_s(buff, L"%d", value);
    WritePrivateProfileString(app_name, key_name, buff, file_path);
}

void CDataManager::SaveConfig() const
{
    //WritePrivateProfileInt(_T("config"), _T("show_second"), m_setting_data.show_second, m_config_path.c_str());
    //WritePrivateProfileInt(_T("config"), _T("show_label_text"), m_setting_data.show_label_text, config_path.c_str());
    //宿主未通过EI_CONFIG_DIR提供配置目录时m_config_path为空，此时不应写入（否则会写到win.ini）
    if (!m_config_path.empty())
    {
        WritePrivateProfileString(_T("config"), _T("time_format"), m_setting_data.time_format.c_str(), m_config_path.c_str());
        WritePrivateProfileString(_T("config"), _T("date_format"), m_setting_data.date_format.c_str(), m_config_path.c_str());
    }
}

void CDataManager::UpdateCurDateTimeText(const std::wstring& date_text, const std::wstring& time_text)
{
    //监控线程每秒调用：锁内一次性整体更新，避免UI线程读取时读到撕裂或已释放的字符串
    std::lock_guard<std::mutex> lock(m_cur_text_mutex);
    m_cur_date = date_text;
    m_cur_time = time_text;
}

const wchar_t* CDataManager::GetCurDateText()
{
    //UI线程调用：锁内拷贝到显示缓冲后返回，插件契约要求返回的指针持续有效
    std::lock_guard<std::mutex> lock(m_cur_text_mutex);
    m_cur_date_display = m_cur_date;
    return m_cur_date_display.c_str();
}

const wchar_t* CDataManager::GetCurTimeText()
{
    //UI线程调用：锁内拷贝到显示缓冲后返回，插件契约要求返回的指针持续有效
    std::lock_guard<std::mutex> lock(m_cur_text_mutex);
    m_cur_time_display = m_cur_time;
    return m_cur_time_display.c_str();
}

const CString& CDataManager::StringRes(UINT id)
{
    //格式化星期/月份时监控线程也会走到这里，与UI线程的首次插入需要互斥
    std::lock_guard<std::mutex> lock(m_string_table_mutex);
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

int CDataManager::DPI(int pixel)
{
    return m_dpi * pixel / 96;
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
        m_icons[id] = hIcon;
        return hIcon;
    }
}
