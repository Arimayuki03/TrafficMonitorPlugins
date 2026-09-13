#pragma once
#include "PluginInterface.h"
#include "WeatherItem.h"
#include <string>
#include <atomic>
#include <mutex>
#include "OptionsDlg.h"

class CWeather : public ITMPlugin
{
private:
    CWeather();

public:
    static CWeather& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void* GetPluginIcon() override;
    void SendWetherInfoQequest();
    void ShowContextMenu(CWnd* pWnd);
    void DisableUpdateWeatherCommand();
    void EnableUpdateWeatherCommand();
    int GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void* GetCommandIcon(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;

    void Init();
    static void ParseWeatherInfo(WeatherInfo& weather_info, yyjson_val* forecast);
    CString GetCurCity();

    //保护天气数据的互斥量(后台解析线程写、UI线程读)。由于锁内可能再次加锁，使用recursive_mutex
    std::recursive_mutex m_data_mutex;

    //选项设置对话框的窗口句柄。对话框在OnInitDialog/OnDestroy中注册/注销，
    //后台线程只向它PostMessage，不直接调用对话框的成员函数
    HWND m_h_option_dlg{};

private:
    static UINT ThreadCallback(LPVOID dwUser);
    bool ParseJsonData(std::string json_data);
    void LoadContextMenu();

private:
    static CWeather m_instance;
    CWeatherItem m_item;
    std::atomic<bool> m_is_thread_runing{ false };
    std::wstring m_tooltop_info;
    std::atomic<unsigned __int64> m_last_request_time{ 0 }; //上次请求天气的时间
    std::atomic<bool> m_update_in_progress{ false };        //是否正在更新天气(用于在UI线程中刷新菜单/按钮状态)
    CMenu m_menu;
    CString m_cur_city;
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
