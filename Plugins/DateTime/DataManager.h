#pragma once
#include <string>
#include <map>
#include <mutex>
#include "DateTimeFormatHelper.h"

#define g_data CDataManager::Instance()

struct SettingData
{
    //bool show_second{};
    //bool show_label_text{};
    std::wstring time_format;
    std::wstring date_format;
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
    int DPI(int pixel);
    HICON GetIcon(UINT id);

public:
    //m_cur_time/m_cur_date由监控线程DataRequired写、UI线程GetItemValueText读：
    //写入通过UpdateCurDateTimeText在锁内整体更新；读取在锁内拷贝到显示缓冲后返回，
    //保证返回的指针在宿主下次调用前持续有效
    const wchar_t* GetCurTimeText();
    const wchar_t* GetCurDateText();
    void UpdateCurDateTimeText(const std::wstring& date_text, const std::wstring& time_text);
    CString m_sample_date_text;     //GetItemValueSampleText的返回值需要保持有效，因此保存在这里
    CString m_sample_time_text;
    //SYSTEMTIME m_system_time;
    SettingData m_setting_data;

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    std::mutex m_string_table_mutex;    //保护m_string_table：监控线程（格式化星期/月份）与UI线程都可能首次插入
    int m_dpi{ 96 };
    std::map<UINT, HICON> m_icons;
    std::mutex m_cur_text_mutex;        //保护m_cur_time/m_cur_date：监控线程写、UI线程读
    std::wstring m_cur_time;
    std::wstring m_cur_date;
    std::wstring m_cur_time_display;    //UI线程锁内拷贝用的显示缓冲
    std::wstring m_cur_date_display;
};
