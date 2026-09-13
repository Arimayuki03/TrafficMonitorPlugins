#pragma once
#include <string>
#include <map>
#include "resource.h"

#define g_data CDataManager::Instance()


struct SettingData
{
    bool show_caps_lock{ true };
    bool show_num_lock{ true };
    bool show_scroll_lock{ true };
    bool draw_round_rect{ false };  //是否使用圆角矩形
    bool show_pressed_key{ true };  //是否实时显示当前按下的按键
    int pressed_key_show_time{ 1000 };  //按键松开后继续显示的时长(毫秒)
    int pressed_key_reserved_width{ 90 };   //按键显示区预留宽度(96DPI下的逻辑像素)，主程序每隔1秒才重新计算显示区域宽度，固定预留宽度可以保证按键按下时立即完整显示，0为按内容自适应
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

    SettingData m_setting_data;

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;
    int m_dpi{ 96 };
    ULONG_PTR m_gdiplus_token{ 0 };     //GDI+初始化令牌
};
