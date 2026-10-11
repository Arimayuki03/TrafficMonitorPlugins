#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include "resource.h"

#define g_data CDataManager::Instance()

//历史每日按键统计记录
struct DailyKeyStat
{
    std::wstring date;              //日期，格式yyyy-MM-dd
    unsigned long long total{};     //当天总按键次数
    //当天各按键计数(下标为归并后的虚拟键码)，仅保存非零项
    std::map<int, unsigned long long> key_counts;
};

//按键排行筛选范围
enum class StatsRange
{
    Today = 0,      //今日
    Days7 = 1,      //近7天(含今日)
    Days30 = 2,     //近30天(含今日)
    All = 3,        //全部
    SingleDay = 4   //指定单日(配合GetStatsRankByDay使用)
};

//任务栏显示项目的标识。显示顺序与自定义颜色都以该标识为索引，
//枚举值会写入INI持久化，新增项目只能追加在PressedKey之后
enum class DisplayItem
{
    CapsLock = 0,       //Caps Lock 指示
    NumLock = 1,        //Num Lock 指示
    ScrollLock = 2,     //Scroll Lock 指示
    KeyCount = 3,       //累计按键计数
    Kpm = 4,            //当前打字速度
    PressedKey = 5,     //实时按下的按键
    Count = 6           //项目总数(不是有效项目)
};

constexpr int DISPLAY_ITEM_COUNT = static_cast<int>(DisplayItem::Count);
//自定义颜色未配置时的哨兵值：跟随主程序设置的颜色与明暗主题色
constexpr COLORREF ITEM_COLOR_DEFAULT = 0xFFFFFFFF;

struct SettingData
{
    bool show_caps_lock{ true };
    bool show_num_lock{ true };
    bool show_scroll_lock{ true };
    bool draw_round_rect{ false };  //是否使用圆角矩形
    bool show_pressed_key{ true };  //是否实时显示当前按下的按键
    int pressed_key_show_time{ 1000 };  //按键松开后继续显示的时长(毫秒)
    //以下三个"宽度"都是该显示区域的最小宽度(96DPI下的逻辑像素)：
    //区域至少保持这个宽度(任务栏宽度不会随内容长短抖动)，内容更长时自动扩展，0为完全按内容自适应。
    //按键区保留默认值可以保证按键按下时立即完整显示(主程序每隔1秒才重新计算显示区域宽度)
    int pressed_key_reserved_width{ 90 };   //按键显示区最小宽度
    std::atomic<bool> enable_key_stats{ false };    //是否启用按键统计(UI线程写、监控线程读)
    bool show_key_count{ true };        //是否在任务栏显示累计按键计数(仅在启用统计时有效)
    int key_count_reserved_width{ 60 }; //计数显示区最小宽度
    int daily_goal{ 0 };                //每日按键数目标，达到后任务栏计数变绿色，0为不启用
    bool show_kpm{ false };             //是否在任务栏计数右侧显示当前KPM(仅在启用统计与计数显示时有效)
    int kpm_reserved_width{ 70 };       //KPM显示区最小宽度
    //任务栏显示项自左向右的顺序(元素为DisplayItem)，下标0为最左。
    //默认顺序与历史版本的绘制顺序一致，保证升级后外观不变
    int item_order[DISPLAY_ITEM_COUNT]{
        static_cast<int>(DisplayItem::KeyCount), static_cast<int>(DisplayItem::Kpm),
        static_cast<int>(DisplayItem::CapsLock), static_cast<int>(DisplayItem::NumLock),
        static_cast<int>(DisplayItem::ScrollLock), static_cast<int>(DisplayItem::PressedKey) };
    //各显示项目的自定义颜色(下标为DisplayItem)，ITEM_COLOR_DEFAULT表示跟随主程序颜色
    COLORREF item_colors[DISPLAY_ITEM_COUNT]{
        ITEM_COLOR_DEFAULT, ITEM_COLOR_DEFAULT, ITEM_COLOR_DEFAULT,
        ITEM_COLOR_DEFAULT, ITEM_COLOR_DEFAULT, ITEM_COLOR_DEFAULT };

    //含atomic成员时隐式拷贝赋值被删除；设置对话框整体替换配置需要拷贝赋值，
    //用atomic的store显式实现(字段固定，新增字段时需同步维护)
    SettingData& operator=(const SettingData& other)
    {
        if (this != &other)
        {
            show_caps_lock = other.show_caps_lock;
            show_num_lock = other.show_num_lock;
            show_scroll_lock = other.show_scroll_lock;
            draw_round_rect = other.draw_round_rect;
            show_pressed_key = other.show_pressed_key;
            pressed_key_show_time = other.pressed_key_show_time;
            pressed_key_reserved_width = other.pressed_key_reserved_width;
            enable_key_stats.store(other.enable_key_stats.load(std::memory_order_relaxed), std::memory_order_relaxed);
            show_key_count = other.show_key_count;
            key_count_reserved_width = other.key_count_reserved_width;
            daily_goal = other.daily_goal;
            show_kpm = other.show_kpm;
            kpm_reserved_width = other.kpm_reserved_width;
            for (int i = 0; i < DISPLAY_ITEM_COUNT; ++i)
            {
                item_order[i] = other.item_order[i];
                item_colors[i] = other.item_colors[i];
            }
        }
        return *this;
    }
};

class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig();
    const CString& StringRes(UINT id);      //根据资源id获取一个字符串资源
    int DPI(int pixel);
    HICON GetIcon(UINT id);
    //获取显示项目的名称(资源字符串)，供设置界面的顺序/颜色列表显示
    const CString& DisplayItemName(DisplayItem item);

    SettingData m_setting_data;

    // ==================== 按键统计 ====================
    //监控线程每秒调用：同步统计开关到钩子层，把会话增量汇入统计模型并按需落盘
    void UpdateKeyStats();
    //累计总按键次数(历史+当日+本会话)
    unsigned long long GetStatsTotal() const;
    //当日累计按键次数
    unsigned long long GetStatsToday() const;
    //当日各按键明细(按键名->次数，按次数降序)，供设置界面表格显示
    std::vector<std::pair<std::wstring, unsigned long long>> GetStatsTodayDetail() const;
    //历史每日记录(按日期升序，不含当日)，供设置界面表格显示
    std::vector<DailyKeyStat> GetStatsHistory() const;
    //按时间范围聚合的按键排行(按键名->次数，按次数降序)。
    //今日=当日明细；近N天=最近N个历史日(含当日)的按键明细之和；全部=全部历史+当日
    std::vector<std::pair<std::wstring, unsigned long long>> GetStatsRankByRange(StatsRange range) const;
    //指定单日(yyyy-MM-dd)的按键排行(按键名->次数，按次数降序)；当日传今日日期返回当日明细
    std::vector<std::pair<std::wstring, unsigned long long>> GetStatsRankByDay(const std::wstring& date) const;
    //获取历史每日记录中最大的单日按键数(用于柱状图归一化)，无历史时返回0
    unsigned long long GetStatsHistoryMax() const;
    //获取今日日期文本(yyyy-MM-dd)，供设置界面"今日"行保存真实日期
    std::wstring GetTodayDate() const;
    //导出每日统计(含今日)为CSV文件(UTF-8 BOM)，返回是否写入成功
    bool ExportStatsCsv(const std::wstring& file_path) const;
    //清零全部统计数据(含钩子层会话计数)，并立即保存
    void ClearAllStats();

private:
    //从INI加载统计数据
    void LoadStatsData();
    //把统计数据写入INI(节流+差异写入，文件IO在锁外执行)
    void SaveStatsData();
    //把钩子层会话计数增量汇入统计模型(调用方无需持锁)
    void MergeSessionCounts();
    //日期变更时把当日数据归档进历史
    void RollDateIfNeeded();
    //重建当日明细缓存(调用方须已持有m_stats_mutex)
    void RebuildDetailCacheLocked();

    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;
    int m_dpi{ 96 };
    ULONG_PTR m_gdiplus_token{ 0 };     //GDI+初始化令牌

    // ==================== 按键统计数据 ====================
    //写入方：仅监控线程(UpdateKeyStats/ClearAllStats)；
    //读取方：UI线程(设置表格、tooltip)。用互斥锁保护，锁内返回数据副本
    mutable std::mutex m_stats_mutex;
    mutable std::mutex m_save_mutex;        //串行化SaveStatsData：UI线程与监控线程都可能触发落盘
    unsigned long long m_stats_total{};     //累计总按键次数
    std::wstring m_today_date;              //当日日期yyyy-MM-dd
    unsigned long long m_stats_today{};     //当日按键次数
    std::vector<unsigned long long> m_today_counts{ std::vector<unsigned long long>(256, 0) };  //当日各按键计数(下标为归并后的虚拟键码)
    std::vector<std::pair<std::wstring, unsigned long long>> m_today_detail_cache;              //当日明细缓存(按键名->次数，按次数降序)
    std::vector<DailyKeyStat> m_history;    //历史每日记录(按日期升序，不含当日，含每日按键明细)
    std::vector<unsigned long long> m_last_session_counts{ std::vector<unsigned long long>(256, 0) };   //上次汇入时的会话计数快照
    std::vector<unsigned long long> m_saved_counts{ std::vector<unsigned long long>(256, 0) };          //上次落盘时已写入INI的各按键计数，用于差异写入
    int m_written_history_count{ 0 };       //上次落盘时已写入INI的历史条数，用于历史差异写入
    bool m_history_shifted{ false };        //90天窗口滑动标记(条数不变但内容前移，需全量重写)
    ULONGLONG m_last_save_tick{ 0 };        //上次落盘时间，用于节流
    bool m_stats_dirty{ false };    //统计有变化待保存
};
