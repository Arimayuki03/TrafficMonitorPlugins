#pragma once
#include <map>
#include <mutex>
#include "utilities/JsonHelper.h"
#include "CommonData.h"

class CHistoryWeatherMgr
{
public:
    CHistoryWeatherMgr() {}
    bool Save() const;
    //清理超过30天的历史数据，避免历史数据无限增长(在Save之前由调用方调用)
    void PruneExpiredData();
    bool Load();

    struct Date
    {
        int year;
        int month;
        int day;

        bool operator==(const Date& another) const;
        bool operator<(const Date& another) const;
    };

    struct HistoryWeather
    {
        CString type;
        CString high_temp;
        CString low_temp;
        CString wind;
        CString city;

        bool IsValid() const;
    };

    //保存每个城市每个日期的历史天气
    using HistoryWeahterMap = std::map<CString, std::map<Date, HistoryWeather>>;

    void AddWeatherInfo(const CString& city, Date date, const WeatherInfo& weather_info);
    void AddWeatherInfo(const CString& city, yyjson_val* forecast);
    bool IsWeatherExist(const CString& city, const Date& date) const;
    HistoryWeahterMap GetHistoryWeatherCopy() const;    //在锁内返回历史天气数据的副本，避免与后台刷新线程产生数据竞争

    static CString GetTemperatureString(const CString& low_temp, const CString& high_temp);

private:
    //保护m_history_weather_list的互斥量(后台解析线程写、UI线程读)。锁与数据同生命周期，
    //保证SaveConfig在析构路径调用Save时加锁依然安全。锁内可能再次加锁，使用recursive_mutex
    mutable std::recursive_mutex m_history_mutex;
    HistoryWeahterMap m_history_weather_list;
};

