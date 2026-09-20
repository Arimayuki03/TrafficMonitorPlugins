#include "pch.h"
#include "DataManager.h"
#include <GdiPlus.h>
#pragma comment(lib,"GdiPlus.lib")

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
    //初始化DPI
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);

    //初始化GDI+，不能依赖宿主进程已经初始化GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplus_token, &gdiplusStartupInput, NULL);
}

CDataManager::~CDataManager()
{
    SaveConfig();
    if (m_gdiplus_token != 0)
        Gdiplus::GdiplusShutdown(m_gdiplus_token);
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

static void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
{
    wchar_t buff[16];
    swprintf_s(buff, L"%d", value);
    WritePrivateProfileString(app_name, key_name, buff, file_path);
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    m_config_path = config_dir + L"KeyboardIndicator.ini";
    m_setting_data.show_caps_lock = (GetPrivateProfileIntW(L"config", L"show_caps_lock", 1, m_config_path.c_str()) != 0);
    m_setting_data.show_num_lock = (GetPrivateProfileIntW(L"config", L"show_num_lock", 1, m_config_path.c_str()) != 0);
    m_setting_data.show_scroll_lock = (GetPrivateProfileIntW(L"config", L"show_scroll_lock", 1, m_config_path.c_str()) != 0);
    m_setting_data.draw_round_rect = (GetPrivateProfileIntW(L"config", L"draw_round_rect", 0, m_config_path.c_str()) != 0);
    m_setting_data.show_pressed_key = (GetPrivateProfileIntW(L"config", L"show_pressed_key", 1, m_config_path.c_str()) != 0);
    //按键显示时长限制在0~10000毫秒，与设置界面允许的范围一致
    int show_time = GetPrivateProfileIntW(L"config", L"pressed_key_show_time", 1000, m_config_path.c_str());
    if (show_time < 0)
        show_time = 0;
    if (show_time > 10000)
        show_time = 10000;
    m_setting_data.pressed_key_show_time = show_time;
    //按键区预留宽度限制在0~300像素，与设置界面允许的范围一致，避免畸形配置参与宽度计算时溢出
    int reserved_width = GetPrivateProfileIntW(L"config", L"pressed_key_reserved_width", 90, m_config_path.c_str());
    if (reserved_width < 0)
        reserved_width = 0;
    if (reserved_width > 300)
        reserved_width = 300;
    m_setting_data.pressed_key_reserved_width = reserved_width;
}

void CDataManager::SaveConfig() const
{
    //未获取到配置目录时跳过写入，避免把配置错误地写入系统目录的win.ini
    if (m_config_path.empty())
        return;
    WritePrivateProfileInt(L"config", L"show_caps_lock", m_setting_data.show_caps_lock, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_num_lock", m_setting_data.show_num_lock, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_scroll_lock", m_setting_data.show_scroll_lock, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"draw_round_rect", m_setting_data.draw_round_rect, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_pressed_key", m_setting_data.show_pressed_key, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"pressed_key_show_time", m_setting_data.pressed_key_show_time, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"pressed_key_reserved_width", m_setting_data.pressed_key_reserved_width, m_config_path.c_str());
}

const CString& CDataManager::StringRes(UINT id)
{
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
        //加载失败时不缓存，允许下次重试
        if (hIcon != nullptr)
            m_icons[id] = hIcon;
        return hIcon;
    }
}
