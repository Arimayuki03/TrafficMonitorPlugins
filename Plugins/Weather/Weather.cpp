#include "pch.h"
#include "Weather.h"
#include "Common.h"
#include <fstream>
#include "DataManager.h"
#include "utilities/yyjson/yyjson.h"
#include "OptionsDlg.h"
#include <sstream>
#include "CurLocationHelper.h"
#include "utilities/Common.h"
#include "utilities/JsonHelper.h"
#include <iomanip>
#include "HistoryWeatherMgr.h"
#include "WeatherHistoryDlg.h"

CWeather CWeather::m_instance;

CWeather::CWeather()
{
}

CWeather& CWeather::Instance()
{
    return m_instance;
}

UINT CWeather::ThreadCallback(LPVOID dwUser)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    time_t cur_time = time(nullptr);
    if (cur_time - m_instance.m_last_request_time.load() > 3)   //确保请求天气信息的时间距离上次请求时超过3秒
    {
        m_instance.m_last_request_time = cur_time;

        //标记正在更新，并通知选项设置对话框禁用“更新”按钮(不能在后台线程直接操作UI)
        m_instance.DisableUpdateWeatherCommand();

        if (g_data.m_setting_data.auto_locate)      //自动获取当前城市
        {
            std::wstring cur_city = CCurLocationHelper::GetCurrentCity();
            int auto_located_city = CCurLocationHelper::FindCityCodeItem(cur_city);
            {
                std::lock_guard<std::recursive_mutex> lock(m_instance.m_data_mutex);
                g_data.m_auto_locate_succeed = (auto_located_city >= 0);
                g_data.m_auto_located = true;
                if (g_data.m_auto_locate_succeed)
                    g_data.m_setting_data.m_city_index = auto_located_city;
            }
        }

        //获取天气信息(使用https，避免明文http被劫持)
        std::wstring url{ L"https://www.nmc.cn/rest/weather?stationid=" };
        url += g_data.CurCity().code;
        std::string weather_data;
        if (CCommon::GetURL(url, weather_data, std::wstring(), true))
        {
            if (m_instance.ParseJsonData(weather_data))
            {
                //解析成功时，将天气信息保存到Weather.json
                std::ofstream stream{ g_data.m_config_dir + L"Weather.json" };
                if (stream.is_open())
                    stream << weather_data;
                //保存历史天气数据(先清理超过30天的过期数据)
                g_data.HistoryWeatherMgr().PruneExpiredData();
                g_data.HistoryWeatherMgr().Save();
            }
        }

        //通知选项设置对话框自动定位结果。对话框指针不能跨线程解引用，改为向其窗口发送消息
        if (m_instance.m_h_option_dlg != nullptr)
            ::PostMessage(m_instance.m_h_option_dlg, WM_WEATHER_AUTO_LOCATE_FINISHED, 0, 0);

        //启用选项设置中的“更新”按钮
        m_instance.EnableUpdateWeatherCommand();
    }
    m_instance.m_is_thread_runing = false;
    return 0;
}

//天气接口中获取到“9999”为无效值，将它忽略
static std::wstring GetJsonWString(yyjson_val* obj, const char* key)
{
    std::wstring str_value = utilities::JsonHelper::GetJsonWString(obj, key);
    if (str_value == L"9999")
        return std::wstring();
    return str_value;
}

static std::wstring GetWeatherString(const std::wstring& day_str, const std::wstring& night_str)
{
    if (day_str.empty())
        return night_str;
    if (night_str.empty())
        return day_str;

    if (day_str == night_str)
        return day_str;
    else
        return day_str + L'~' + night_str;
}

void CWeather::ParseWeatherInfo(WeatherInfo& weather_info, yyjson_val* forecast)
{
    if (forecast != nullptr)
    {
        yyjson_val* day_node = yyjson_obj_get(forecast, "day");
        yyjson_val* night_node = yyjson_obj_get(forecast, "night");
        if (day_node == nullptr || night_node == nullptr)
            return;
        yyjson_val* day_weather_node = yyjson_obj_get(day_node, "weather");
        yyjson_val* night_weather_node = yyjson_obj_get(night_node, "weather");
        if (day_weather_node == nullptr || night_weather_node == nullptr)
            return;

        std::wstring day_type = GetJsonWString(day_weather_node, "info");
        std::wstring night_type = GetJsonWString(night_weather_node, "info");
        weather_info.m_type = GetWeatherString(day_type, night_type);

        weather_info.m_high = GetJsonWString(day_weather_node, "temperature");
        weather_info.m_low = GetJsonWString(night_weather_node, "temperature");

        //风向和风力
        yyjson_val* day_wind_node = yyjson_obj_get(day_node, "wind");
        yyjson_val* night_wind_node = yyjson_obj_get(night_node, "wind");
        std::wstring str_day_direct = GetJsonWString(day_wind_node, "direct");
        std::wstring str_day_power = GetJsonWString(day_wind_node, "power");
        std::wstring str_night_direct = GetJsonWString(night_wind_node, "direct");
        std::wstring str_night_power = GetJsonWString(night_wind_node, "power");

        std::wstring str_direct = GetWeatherString(str_day_direct, str_night_direct);
        std::wstring str_power = GetWeatherString(str_day_power, str_night_power);
        weather_info.m_wind = str_direct + L' ' + str_power;
    }
}

CString CWeather::GetCurCity()
{
    //在锁内返回副本，避免后台线程更新城市名时调用方读到不一致的内容
    std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
    return m_cur_city;
}

//获取一个日期的字符串。如果日期是今天、明天或后天，则返回“今天”、“明天”和“后天”的字符串，否则返回几月几日
static std::wstring GetDateString(CTime date)
{
    date = CCommon::GetDateOnly(date);
    CTime now_date = CCommon::GetDateOnly(CTime::GetCurrentTime());
    const CTimeSpan one_day_span(1, 0, 0, 0);
    CTime tomorrow_date = now_date + one_day_span;
    CTime the_day_after_tomorrow_date = tomorrow_date + one_day_span;
    CTime yesterday_date = now_date - one_day_span;
    if (date == now_date)
        return g_data.StringRes(IDS_TODAY_WEATHER).GetString();
    else if (date == tomorrow_date)
        return g_data.StringRes(IDS_TOMMORROW_WEATHER).GetString();
    else if (date == the_day_after_tomorrow_date)
        return  g_data.StringRes(IDS_THE_DAY_AFTER_TOMMORROW_WEATHER).GetString();
    else if (date == yesterday_date)
        return  g_data.StringRes(IDS_YESTERDAY).GetString();
    else
        return utilities::StringHelper::StringFormat(g_data.StringRes(IDS_DATE_FORMAT).GetString(), { date.GetMonth(), date.GetDay() });
}

bool CWeather::ParseJsonData(std::string json_data)
{
    yyjson_doc* doc = yyjson_read(json_data.c_str(), json_data.size(), 0);
    if (doc == nullptr)
        return false;
    bool succeed = false;
    {
        //解析过程会写天气数据，与UI线程并发读取，需要加锁保护
        std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
        //把解析过程放在lambda中执行，保证yyjson文档在所有返回路径上都会被释放(原先提前return时泄漏)
        auto parse_body = [&]() -> bool {
        //获取Json根节点
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (root == nullptr)
            return false;
        //获取数据节点
        yyjson_val* data_node = yyjson_obj_get(root, "data");
        if (data_node == nullptr)
            return false;
        //获取实时天气节点
        yyjson_val* real_node = yyjson_obj_get(data_node, "real");
        if (real_node == nullptr)
            return false;

        //获取日期
        int year{};
        int month{};
        int day{};
        std::string str_date = utilities::JsonHelper::GetJsonString(real_node, "publish_time");
        if (str_date.size() >= 4)
            year = atoi(str_date.substr(0, 4).c_str());
        if (str_date.size() >= 7)
            month = atoi(str_date.substr(5, 2).c_str());
        if (str_date.size() >= 10)
            day = atoi(str_date.substr(8, 2).c_str());

        //获取城市
        yyjson_val* station_node = yyjson_obj_get(real_node, "station");
        std::wstring str_province = GetJsonWString(station_node, "province");
        std::wstring str_city = GetJsonWString(station_node, "city");
        m_cur_city.Format(_T("%s %s"), str_province.c_str(), str_city.c_str());

        //获取时间。str_date长度不足11时不能substr(11)，否则抛出未捕获的异常直接终止宿主进程
        std::string str_time;
        if (str_date.size() > 11)
            str_time = str_date.substr(11);
        std::vector<std::string> time_split;
        utilities::StringHelper::StringSplit(str_time, ':', time_split);
        int hour{};
        int minute{};
        if (time_split.size() >= 1)
            hour = atoi(time_split[0].c_str());
        if (time_split.size() >= 2)
            minute = atoi(time_split[1].c_str());
        //校验日期时间的合法性，避免构造非法的CTime
        if (year < 1970 || year > 3000 || month < 1 || month > 12 || day < 1 || day > 31
            || hour < 0 || hour > 23 || minute < 0 || minute > 59)
            return false;

        g_data.ResetText();

        g_data.m_update_time = CTime(year, month, day, hour, minute, 0);

        //获取当前天气
        yyjson_val* weather_node = yyjson_obj_get(real_node, "weather");
        g_data.m_weather_info[WEATHER_CURRENT].m_high = utilities::JsonHelper::GetJsonWString(weather_node, "temperature");
        g_data.m_weather_info[WEATHER_CURRENT].m_type = GetJsonWString(weather_node, "info");
        g_data.m_weather_info[WEATHER_CURRENT].is_cur_weather = true;

        //获取风力风向
        yyjson_val* wind_node = yyjson_obj_get(real_node, "wind");
        std::wstring wind_direct = GetJsonWString(wind_node, "direct");
        std::wstring wind_power = GetJsonWString(wind_node, "power");
        g_data.m_weather_info[WEATHER_CURRENT].m_wind = wind_direct + L' ' + wind_power;

        //空气质量
        yyjson_val* air_node = yyjson_obj_get(data_node, "air");
        if (air_node != nullptr)
        {
            g_data.m_aqi = GetJsonWString(air_node, "aqi");
            g_data.m_quality = GetJsonWString(air_node, "text");
        }

        //获取3天的天气
        yyjson_val* predict_node = yyjson_obj_get(data_node, "predict");
        if (predict_node == nullptr)
            return false;

        yyjson_val* forecast_arr = yyjson_obj_get(predict_node, "detail");
        if (forecast_arr != nullptr && yyjson_is_arr(forecast_arr))
        {
            yyjson_val* forecast_today = yyjson_arr_get_first(forecast_arr);
            yyjson_val* forecast_tommorrow = yyjson_arr_get(forecast_arr, 1);
            yyjson_val* forecast_day2 = yyjson_arr_get(forecast_arr, 2);
            ParseWeatherInfo(g_data.m_weather_info[WEATHER_TODAY], forecast_today);
            ParseWeatherInfo(g_data.m_weather_info[WEATHER_TOMMORROW], forecast_tommorrow);
            ParseWeatherInfo(g_data.m_weather_info[WEATHER_DAY2], forecast_day2);
            //添加到历史记录
            g_data.HistoryWeatherMgr().AddWeatherInfo(m_cur_city, forecast_today);
            g_data.HistoryWeatherMgr().AddWeatherInfo(m_cur_city, forecast_tommorrow);
            g_data.HistoryWeatherMgr().AddWeatherInfo(m_cur_city, forecast_day2);
            //获取所有天气并添加到历史记录
            for (int i = 3; ; i++)
            {
                yyjson_val* forecast = yyjson_arr_get(forecast_arr, i);
                if (forecast != nullptr)
                    g_data.HistoryWeatherMgr().AddWeatherInfo(m_cur_city, forecast);
                else
                    break;
            }
        }

        //生成鼠标提示字符串
        const WeatherInfo& weather_current{ g_data.m_weather_info[WEATHER_CURRENT] };
        const WeatherInfo& weather_today{ g_data.m_weather_info[WEATHER_TODAY] };
        const WeatherInfo& weather_tomorrow{ g_data.m_weather_info[WEATHER_TOMMORROW] };
        const WeatherInfo& weather_day2{ g_data.m_weather_info[WEATHER_DAY2] };
        CTime update_date = CCommon::GetDateOnly(g_data.m_update_time);
        const CTimeSpan one_day_span(1, 0, 0, 0);
        CTime tomorrow_date = update_date + one_day_span;
        CTime the_day_after_tomorrow_date = tomorrow_date + one_day_span;
        std::wstringstream wss;
        wss << str_city << L' ' << weather_current.ToString()
            << L" AQI: " << g_data.m_aqi << L' ' << g_data.m_quality
            << std::endl << g_data.StringRes(IDS_UPDATE_TIME).GetString() << L": " << g_data.GetUpdateTimeAsString().GetString()
            << std::endl << GetDateString(update_date) << L": " << weather_today.ToString()
            << std::endl << GetDateString(tomorrow_date) << L": " << weather_tomorrow.ToString()
            << std::endl << GetDateString(the_day_after_tomorrow_date) << L": " << weather_day2.ToString()
            ;
        m_tooltop_info = wss.str();

        return true;
        };
        succeed = parse_body();
    }
    yyjson_doc_free(doc);
    return succeed;
}

void CWeather::LoadContextMenu()
{
    if (m_menu.m_hMenu == NULL)
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        m_menu.LoadMenu(IDR_MENU1);
    }
}

IPluginItem* CWeather::GetItem(int index)
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

const wchar_t* CWeather::GetTooltipInfo()
{
    if (g_data.m_setting_data.m_show_weather_in_tooltips)
    {
        //在锁内复制到静态缓冲再返回，避免后台线程更新提示内容时读到不一致的数据
        std::lock_guard<std::recursive_mutex> lock(m_data_mutex);
        static std::wstring tooltip_copy;
        tooltip_copy = m_tooltop_info;
        return tooltip_copy.c_str();
    }
    else
        return L"";
}

void CWeather::DataRequired()
{
    static int last_minute{ -1 };
    static int last_hour{ -1 };
    SYSTEMTIME system_time{};
    GetLocalTime(&system_time);
    //每隔30分钟获取一次天气
    int cur_minute = system_time.wMinute / 30;
    if (cur_minute != last_minute || last_hour != system_time.wHour)
    {
        last_minute = cur_minute;
        last_hour = system_time.wHour;
        SendWetherInfoQequest();
    }

}

ITMPlugin::OptionReturn CWeather::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle((HWND)hParent);
    //g_data.DPIFromWindow(pParent);
    COptionsDlg dlg(pParent);
    dlg.m_data = g_data.m_setting_data;
    auto rtn = dlg.DoModal();
    if (rtn == IDOK)
    {
        bool city_changed{ g_data.m_setting_data.m_city_index != dlg.m_data.m_city_index ||
            g_data.m_setting_data.auto_locate != dlg.m_data.auto_locate };
        g_data.m_setting_data = dlg.m_data;
        if (city_changed)
        {
            CWeather::Instance().SendWetherInfoQequest();   //城市改变后，重新发送天气请求
        }
        g_data.SaveConfig();
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CWeather::GetInfo(PluginInfoIndex index)
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
        return L"1.03";
    default:
        break;
    }
    return L"";
}

void CWeather::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
    {
        g_data.InitCityList();
        //从配置文件读取配置
        std::wstring cfg_dir(data);
        cfg_dir += L"Weather\\";
        CreateDirectory(cfg_dir.c_str(), NULL);
        g_data.LoadConfig(cfg_dir);
        //初始化天气
        Init();
    }
        break;
    default:
        break;
    }
}

void* CWeather::GetPluginIcon()
{
    return g_data.GetIcon(IDI_WEATHER);
}

void CWeather::SendWetherInfoQequest()
{
    //原子地检查并置位，避免同时启动两个刷新线程
    bool expected = false;
    if (m_is_thread_runing.compare_exchange_strong(expected, true))
    {
        if (AfxBeginThread(ThreadCallback, nullptr) == NULL)
        {
            m_is_thread_runing = false;
        }
    }
}

void CWeather::ShowContextMenu(CWnd* pWnd)
{
    LoadContextMenu();
    CMenu* context_menu = m_menu.GetSubMenu(0);
    if (context_menu != nullptr)
    {
        //“更新天气”菜单项的状态在UI线程中刷新(后台线程不能直接操作菜单)
        context_menu->EnableMenuItem(ID_UPDATE_WEATHER, MF_BYCOMMAND | (m_update_in_progress ? MF_GRAYED : MF_ENABLED));
        CPoint point1;
        GetCursorPos(&point1);
        DWORD id = context_menu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point1.x, point1.y, pWnd);
        //点击了“选项”
        if (id == ID_OPTIONS)
        {
            AFX_MANAGE_STATE(AfxGetStaticModuleState());
            COptionsDlg dlg;
            dlg.m_data = g_data.m_setting_data;
            auto rtn = dlg.DoModal();
            if (rtn == IDOK)
            {
                bool city_changed{ g_data.m_setting_data.m_city_index != dlg.m_data.m_city_index };
                g_data.m_setting_data = dlg.m_data;
                if (city_changed)
                {
                    CWeather::Instance().SendWetherInfoQequest();   //城市改变后，重新发送天气请求
                }
            }
        }
        //点击了“更新天气”
        else if (id == ID_UPDATE_WEATHER)
        {
            SendWetherInfoQequest();
        }
    }
}

void CWeather::DisableUpdateWeatherCommand()
{
    //只设置标志，按钮/菜单状态由UI线程刷新，不能在后台线程直接操作UI
    m_update_in_progress = true;
    if (m_h_option_dlg != nullptr)
        ::PostMessage(m_h_option_dlg, WM_WEATHER_UPDATE_STATE_CHANGED, FALSE, 0);
}

void CWeather::EnableUpdateWeatherCommand()
{
    m_update_in_progress = false;
    if (m_h_option_dlg != nullptr)
        ::PostMessage(m_h_option_dlg, WM_WEATHER_UPDATE_STATE_CHANGED, TRUE, 0);
}

const wchar_t* CWeather::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0:
        return g_data.StringRes(IDS_UPDATE_WEATHER).GetString();
    case 1:
        return g_data.StringRes(IDS_WEATHER_HISTORY).GetString();
    default:
        return nullptr;
    }
}

void* CWeather::GetCommandIcon(int command_index)
{
    switch (command_index)
    {
    case 0:
        return g_data.GetIcon(IDI_UPDATE);
        break;
    default:
        return nullptr;
    }
}

void CWeather::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    CWnd* parent = CWnd::FromHandle((HWND)hWnd);
    switch (command_index)
    {
        //更新天气
    case 0:
        SendWetherInfoQequest();
        break;
        //显示天气历史记录
    case 1:
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        CWeatherHistoryDlg dlg(parent);
        dlg.DoModal();
    }
    default:
        break;
    }
}

void CWeather::Init()
{
    //从Weather.json获取天气
    std::wstring json_path{ g_data.m_config_dir + L"Weather.json" };
    std::string weather_data;
    if (utilities::CCommon::GetFileContent(json_path.c_str(), weather_data))
    {
        ParseJsonData(weather_data);
    }
}

int CWeather::GetCommandCount()
{
    return 2;
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CWeather::Instance();
}
