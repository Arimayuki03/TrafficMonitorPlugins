#include "pch.h"
#include "KeyboardIndicator.h"
#include "DataManager.h"
#include "KeyboardHook.h"
#include "OptionsDlg.h"

CKeyboardIndicator CKeyboardIndicator::m_instance;

CKeyboardIndicator::CKeyboardIndicator()
{
}

CKeyboardIndicator& CKeyboardIndicator::Instance()
{
    return m_instance;
}

IPluginItem* CKeyboardIndicator::GetItem(int item_index)
{
    switch (item_index)
    {
    case 0:
        return &m_item;
    default:
        break;
    }
    return nullptr;
}

const wchar_t* CKeyboardIndicator::GetTooltipInfo()
{
    //m_tooltip_info由监控线程写入、UI线程读取，锁内拷贝到缓冲后返回，避免读到正在被重写的字符串
    std::lock_guard<std::mutex> lock(m_tooltip_mutex);
    m_tooltip_info_buffer = m_tooltip_info;
    return m_tooltip_info_buffer.c_str();
}

void CKeyboardIndicator::DataRequired()
{
    //同步按键统计：更新统计开关、汇入会话增量、跨天归档与按需落盘
    //（统计关闭时内部只同步开关状态，开销可忽略）
    g_data.UpdateKeyStats();
    //仅在"实时显示按下的按键"功能开启时保持键盘钩子运行，关闭时卸载钩子线程
    //(无法感知插件被宿主禁用的时机，此时钩子仍会随插件数据刷新保持运行)
    if (g_data.m_setting_data.show_pressed_key || g_data.m_setting_data.enable_key_stats)
        CKeyboardHook::Instance().Start();
    else
        CKeyboardHook::Instance().Stop();
    //更新鼠标提示文本：锁定键状态、最近按下的按键与按键统计
    std::wstring tooltip;
    tooltip += L"Caps Lock: ";
    tooltip += (IsCapsLockOn() ? L"On" : L"Off");
    tooltip += L"\r\nNum Lock: ";
    tooltip += (IsNumLockOn() ? L"On" : L"Off");
    tooltip += L"\r\nScroll Lock: ";
    tooltip += (IsScrollLockOn() ? L"On" : L"Off");
    std::wstring last_key;
    if (CKeyboardHook::Instance().GetLastKeyText(last_key))
    {
        tooltip += L"\r\nLast key: ";
        tooltip += last_key;
    }
    if (g_data.m_setting_data.enable_key_stats)
    {
        //统计摘要：累计与当日按键次数
        wchar_t buff[96];
        swprintf_s(buff, L"\r\nKeys total: %llu\r\nKeys today: %llu",
            g_data.GetStatsTotal(), g_data.GetStatsToday());
        tooltip += buff;
    }
    //先组装到局部串，锁内一次性更新成员，避免UI线程读到不一致或已释放的内容
    std::lock_guard<std::mutex> lock(m_tooltip_mutex);
    m_tooltip_info = tooltip;
}

ITMPlugin::OptionReturn CKeyboardIndicator::ShowOptionsDialog(void* parent_hwnd)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* parent_wnd = CWnd::FromHandle((HWND)parent_hwnd);
    COptionsDlg dlg(parent_wnd);
    dlg.m_data = g_data.m_setting_data;
    if (dlg.DoModal() == IDOK)
    {
        g_data.m_setting_data = dlg.m_data;
        g_data.SaveConfig();
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CKeyboardIndicator::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return g_data.StringRes(IDS_PLUGIN_NAME).GetString();
    case TMI_DESCRIPTION:
        return g_data.StringRes(IDS_PLUGIN_DESCRIPTION).GetString();
    case TMI_AUTHOR:
        return L"zhongyang219";
    case TMI_COPYRIGHT:
        return L"Copyright (C) 2025 By ZhongYang";
    case ITMPlugin::TMI_URL:
        return L"https://github.com/zhongyang219/TrafficMonitorPlugins";
    case TMI_VERSION:
        return L"1.13";
    default:
        break;
    }
    return L"";
}

void CKeyboardIndicator::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        //从配置文件目录读取配置
        g_data.LoadConfig(std::wstring(data));
        break;
    default:
        break;
    }
}

void* CKeyboardIndicator::GetPluginIcon()
{
    return g_data.GetIcon(IDI_ICON1);
}

bool CKeyboardIndicator::IsCapsLockOn()
{
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

bool CKeyboardIndicator::IsNumLockOn()
{
    return (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
}

bool CKeyboardIndicator::IsScrollLockOn()
{
    return (GetKeyState(VK_SCROLL) & 0x0001) != 0;
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CKeyboardIndicator::Instance();
}
