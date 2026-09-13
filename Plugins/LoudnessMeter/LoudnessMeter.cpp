#include "pch.h"
#include "LoudnessMeter.h"
#include "DataManager.h"
#include "OptionsDlg.h"
#pragma comment(lib, "ole32.lib")

CLoudnessMeter CLoudnessMeter::m_instance;

CLoudnessMeter::CLoudnessMeter()
{
    //COM初始化和设备创建移到OnInitialize中进行，避免在DllMain(静态对象构造)期间调用
}

CLoudnessMeter::~CLoudnessMeter()
{
    if (m_timer_id != 0)
        ::KillTimer(NULL, m_timer_id);
    if (pMeterInfo != nullptr)
        pMeterInfo->Release();
    if (pDevice != nullptr)
        pDevice->Release();
    if (pEnumerator != nullptr)
        pEnumerator->Release();
    if (m_com_inited)
        CoUninitialize();
}

CLoudnessMeter& CLoudnessMeter::Instance()
{
    return m_instance;
}

void CLoudnessMeter::OnInitialize(ITrafficMonitor* /*pApp*/)
{
    //初始化COM。宿主已按其他模式初始化时(RPC_E_CHANGED_MODE)可以直接使用，但无需配对CoUninitialize
    HRESULT hr = CoInitialize(NULL);
    if (hr == RPC_E_CHANGED_MODE)
        m_com_inited = false;
    else if (SUCCEEDED(hr))
        m_com_inited = true;
    else
        return;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator)))
        pEnumerator = nullptr;
    else
        InitDevice();
}

IPluginItem* CLoudnessMeter::GetItem(int index)
{
    switch (index)
    {
    case 0:
        return &m_item;
    default:
        break;
    }
    return nullptr;
}

const wchar_t* CLoudnessMeter::GetTooltipInfo()
{
    return m_tooltip_info.c_str();
}

void CLoudnessMeter::DataRequired()
{
}

ITMPlugin::OptionReturn CLoudnessMeter::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle((HWND)hParent);
    COptionsDlg dlg(pParent);
    dlg.m_data = g_data.m_setting_data;
    if (dlg.DoModal() == IDOK)
    {
        g_data.m_setting_data = dlg.m_data;
        g_data.SaveConfig();
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CLoudnessMeter::GetInfo(PluginInfoIndex index)
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
        return L"Copyright (C) by Zhong Yang 2025";
    case ITMPlugin::TMI_URL:
        return L"https://github.com/zhongyang219/TrafficMonitorPlugins";
        break;
    case TMI_VERSION:
        return L"1.00";
    default:
        break;
    }
    return L"";
}

void CLoudnessMeter::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        //从配置文件读取配置
        g_data.LoadConfig(std::wstring(data));
        //启动采集数据的定时器
        EnsureTimer();

        break;
    default:
        break;
    }
}

void* CLoudnessMeter::GetPluginIcon()
{
    return g_data.GetIcon(IDI_ICON1);
}

int CLoudnessMeter::GetCommandCount()
{
    return 1;
}

const wchar_t* CLoudnessMeter::GetCommandName(int command_index)
{
    if (command_index == 0)
        return g_data.StringRes(IDS_RE_INIT).GetString();
    return nullptr;
}

void* CLoudnessMeter::GetCommandIcon(int command_index)
{
    if (command_index == 0)
        return g_data.GetIcon(IDI_UPDATE);
    return nullptr;
}

void CLoudnessMeter::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    if (command_index == 0)
    {
        InitDevice();
    }
}

void CLoudnessMeter::DoDataAcquire()
{
    float peakValue = 0.0f;
    if (pMeterInfo != nullptr)
    {
        HRESULT hr = pMeterInfo->GetPeakValue(&peakValue);
        if (SUCCEEDED(hr))
        {
            if (peakValue > 1e-6f)
            {
                float dB = static_cast<float>(20 * log10(peakValue));
                m_item.SetValue(dB, peakValue * 100, CLoudnessMeterItem::DB_VALID);
                //生成鼠标提示信息
                wchar_t buff[32]{};
                swprintf_s(buff, L"%.2f dB", dB);
                m_tooltip_info = buff;
            }
            else
            {
                m_item.SetValue(0, 0, CLoudnessMeterItem::DB_MUTE);
                m_tooltip_info = g_data.StringRes(IDS_MUTE).GetString();
            }
        }
        else
        {
            //获取失败(如默认播放设备已失效)，标记为无效，等待用户重新初始化
            m_item.SetValue(0, 0, CLoudnessMeterItem::DB_INVALID);
            m_tooltip_info.clear();
        }
    }
    else
    {
        m_item.SetValue(0, 0, CLoudnessMeterItem::DB_INVALID);
        m_tooltip_info.clear();
    }
}

void CLoudnessMeter::InitDevice()
{
    //释放已有接口，避免重复初始化时泄漏
    if (pMeterInfo != nullptr)
    {
        pMeterInfo->Release();
        pMeterInfo = nullptr;
    }
    if (pDevice != nullptr)
    {
        pDevice->Release();
        pDevice = nullptr;
    }
    if (pEnumerator == nullptr)
        return;
    if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)))
    {
        pDevice = nullptr;
        return;
    }
    if (FAILED(pDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, NULL, (void**)&pMeterInfo)))
        pMeterInfo = nullptr;
}

void CLoudnessMeter::EnsureTimer()
{
    //先销毁已有定时器再创建，避免EI_CONFIG_DIR多次到达时重复创建定时器
    if (m_timer_id != 0)
        ::KillTimer(NULL, m_timer_id);
    m_timer_id = ::SetTimer(NULL, 1265, 50, [](HWND, UINT, UINT_PTR, DWORD) {
        m_instance.DoDataAcquire();
    });
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CLoudnessMeter::Instance();
}
