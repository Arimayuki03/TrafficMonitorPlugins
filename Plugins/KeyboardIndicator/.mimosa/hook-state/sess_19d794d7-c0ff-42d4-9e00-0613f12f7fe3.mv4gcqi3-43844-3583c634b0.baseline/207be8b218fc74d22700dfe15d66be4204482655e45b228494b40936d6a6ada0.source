#include "pch.h"
#include "DataManager.h"
#include "KeyboardHook.h"
#include <GdiPlus.h>
#include <ctime>
#include <cstdlib>
#include <algorithm>
#pragma comment(lib,"GdiPlus.lib")

CDataManager CDataManager::m_instance;

namespace
{
    //INI的[stats]区保存各按键计数的键名前缀，如key_41=123表示虚拟键码0x41(A键)计数123
    const wchar_t KEY_COUNT_PREFIX[] = L"key_";
    //历史每日记录最多保留的天数，超出时移除最旧的
    const int MAX_HISTORY_DAYS = 90;
    //统计数据落盘的最小间隔(毫秒)，避免持续按键时每秒全量写INI；退出时仍会兜底保存
    const ULONGLONG MIN_SAVE_INTERVAL_MS = 5000;

    //获取当前日期的yyyy-MM-dd文本
    std::wstring GetTodayString()
    {
        time_t t = time(nullptr);
        struct tm local_time{};
        localtime_s(&local_time, &t);
        wchar_t buff[16];
        swprintf_s(buff, L"%04d-%02d-%02d", local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday);
        return buff;
    }

    void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
    {
        wchar_t buff[16];
        swprintf_s(buff, L"%d", value);
        WritePrivateProfileStringW(app_name, key_name, buff, file_path);
    }

    void WritePrivateProfileUInt64(const wchar_t* app_name, const wchar_t* key_name, unsigned long long value, const wchar_t* file_path)
    {
        wchar_t buff[24];
        swprintf_s(buff, L"%llu", value);
        WritePrivateProfileStringW(app_name, key_name, buff, file_path);
    }

    //读取INI中的一个字符串值，未找到时返回false
    bool ReadProfileString(const wchar_t* app_name, const wchar_t* key_name, wchar_t* buff, DWORD buff_size, const wchar_t* file_path)
    {
        return GetPrivateProfileStringW(app_name, key_name, L"", buff, buff_size, file_path) > 0;
    }

    //按键名缓存：虚拟键码->按键名，避免每次重建明细缓存时重复调用GetKeyNameTextW
    class CKeyNameCache
    {
    public:
        const std::wstring& Get(UINT vk)
        {
            auto iter = m_cache.find(vk);
            if (iter != m_cache.end())
                return iter->second;
            std::wstring name = CKeyboardHook::GetKeyName(vk, 0, false);
            return m_cache.emplace(vk, std::move(name)).first->second;
        }
    private:
        std::map<UINT, std::wstring> m_cache;
    };

    CKeyNameCache g_key_name_cache;     //按键名在同一进程内固定不变，进程级缓存即可

    //UTF-16转UTF-8(导出CSV用)
    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty())
            return std::string();
        int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(size > 0 ? size : 0), static_cast<char>(0));
        if (size > 0 &&
            ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr) > 0)
            return result;
        return result;      //转换失败时返回空串由调用方感知(文件变小)，不静默伪造内容
    }

    //当日明细按次数降序排序(次数相同时按键名排序保证稳定)
    void SortDetail(std::vector<std::pair<std::wstring, unsigned long long>>& detail)
    {
        std::sort(detail.begin(), detail.end(),
            [](const std::pair<std::wstring, unsigned long long>& a, const std::pair<std::wstring, unsigned long long>& b)
            {
                if (a.second != b.second)
                    return a.second > b.second;
                return a.first < b.first;
            });
    }
}

CDataManager::CDataManager()
{
    //初始化DPI
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);

    //初始化GDI+，不依赖主程序是否已初始化GDI+
    Gdiplus::GdiplusStartupInput gdiplus_startup_input;
    Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdiplus_startup_input, NULL);
}

CDataManager::~CDataManager()
{
    //退出前保存配置与统计数据
    SaveConfig();
    if (m_gdiplus_token != 0)
        Gdiplus::GdiplusShutdown(m_gdiplus_token);
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    m_config_path = config_dir + L"KeyboardIndicator.ini";
    const SettingData default_setting;
    m_setting_data.show_caps_lock = (GetPrivateProfileIntW(L"config", L"show_caps_lock", default_setting.show_caps_lock ? 1 : 0, m_config_path.c_str()) != 0);
    m_setting_data.show_num_lock = (GetPrivateProfileIntW(L"config", L"show_num_lock", default_setting.show_num_lock ? 1 : 0, m_config_path.c_str()) != 0);
    m_setting_data.show_scroll_lock = (GetPrivateProfileIntW(L"config", L"show_scroll_lock", default_setting.show_scroll_lock ? 1 : 0, m_config_path.c_str()) != 0);
    m_setting_data.draw_round_rect = (GetPrivateProfileIntW(L"config", L"draw_round_rect", default_setting.draw_round_rect ? 1 : 0, m_config_path.c_str()) != 0);
    m_setting_data.show_pressed_key = (GetPrivateProfileIntW(L"config", L"show_pressed_key", default_setting.show_pressed_key ? 1 : 0, m_config_path.c_str()) != 0);
    //按键显示时长限制在0~10000毫秒，与设置界面允许的范围一致
    int show_time = GetPrivateProfileIntW(L"config", L"pressed_key_show_time", default_setting.pressed_key_show_time, m_config_path.c_str());
    if (show_time < 0)
        show_time = 0;
    if (show_time > 10000)
        show_time = 10000;
    m_setting_data.pressed_key_show_time = show_time;
    //按键区预留宽度限制在0~300像素，与设置界面允许的范围一致，避免异常配置参与宽度计算时溢出
    int reserved_width = GetPrivateProfileIntW(L"config", L"pressed_key_reserved_width", default_setting.pressed_key_reserved_width, m_config_path.c_str());
    if (reserved_width < 0)
        reserved_width = 0;
    if (reserved_width > 300)
        reserved_width = 300;
    m_setting_data.pressed_key_reserved_width = reserved_width;
    m_setting_data.enable_key_stats = (GetPrivateProfileIntW(L"config", L"enable_key_stats", default_setting.enable_key_stats ? 1 : 0, m_config_path.c_str()) != 0);
    m_setting_data.show_key_count = (GetPrivateProfileIntW(L"config", L"show_key_count", default_setting.show_key_count ? 1 : 0, m_config_path.c_str()) != 0);
    //计数区预留宽度限制在0~300像素
    int count_width = GetPrivateProfileIntW(L"config", L"key_count_reserved_width", default_setting.key_count_reserved_width, m_config_path.c_str());
    if (count_width < 0)
        count_width = 0;
    if (count_width > 300)
        count_width = 300;
    m_setting_data.key_count_reserved_width = count_width;
    //每日按键目标(0为不启用)，限制在0~1000000
    int daily_goal = GetPrivateProfileIntW(L"config", L"daily_goal", default_setting.daily_goal, m_config_path.c_str());
    if (daily_goal < 0)
        daily_goal = 0;
    if (daily_goal > 1000000)
        daily_goal = 1000000;
    m_setting_data.daily_goal = daily_goal;
    m_setting_data.show_kpm = (GetPrivateProfileIntW(L"config", L"show_kpm", default_setting.show_kpm ? 1 : 0, m_config_path.c_str()) != 0);
    //KPM区预留宽度限制在0~300像素
    int kpm_width = GetPrivateProfileIntW(L"config", L"kpm_reserved_width", default_setting.kpm_reserved_width, m_config_path.c_str());
    if (kpm_width < 0)
        kpm_width = 0;
    if (kpm_width > 300)
        kpm_width = 300;
    m_setting_data.kpm_reserved_width = kpm_width;
    //加载持久化的统计数据
    LoadStatsData();
}

void CDataManager::SaveConfig()
{
    //未获取到配置目录时跳过写入，避免把配置错误地写入系统目录的win.ini
    if (m_config_path.empty())
        return;
    WritePrivateProfileInt(L"config", L"show_caps_lock", m_setting_data.show_caps_lock ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_num_lock", m_setting_data.show_num_lock ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_scroll_lock", m_setting_data.show_scroll_lock ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"draw_round_rect", m_setting_data.draw_round_rect ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_pressed_key", m_setting_data.show_pressed_key ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"pressed_key_show_time", m_setting_data.pressed_key_show_time, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"pressed_key_reserved_width", m_setting_data.pressed_key_reserved_width, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"enable_key_stats", m_setting_data.enable_key_stats ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_key_count", m_setting_data.show_key_count ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"key_count_reserved_width", m_setting_data.key_count_reserved_width, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"daily_goal", m_setting_data.daily_goal, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"show_kpm", m_setting_data.show_kpm ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"config", L"kpm_reserved_width", m_setting_data.kpm_reserved_width, m_config_path.c_str());
    //保存统计数据
    SaveStatsData();
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

// ==================== 按键统计 ====================

void CDataManager::UpdateKeyStats()
{
    //由监控线程(约1秒一次)调用：同步统计开关、清理卡住按键、处理跨天、汇入会话增量、按需落盘
    CKeyboardHook::Instance().SetStatsEnabled(m_setting_data.enable_key_stats);
    if (!m_setting_data.enable_key_stats)
        return;
    //卡住按键的过期清理不依赖按键显示是否开启，统计开启时必须持续执行
    CKeyboardHook::Instance().CleanupStuckKeys();
    //先跨天归档再汇入增量，使跨天后的按键计入新的一天
    RollDateIfNeeded();
    MergeSessionCounts();
    //落盘节流：距上次落盘不足MIN_SAVE_INTERVAL_MS毫秒时跳过(退出时兜底保存)，
    //避免持续按键时每秒全量写INI
    bool need_save = false;
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        need_save = m_stats_dirty && (GetTickCount64() - m_last_save_tick >= MIN_SAVE_INTERVAL_MS);
    }
    if (need_save)
        SaveStatsData();
}

unsigned long long CDataManager::GetStatsTotal() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats_total;
}

unsigned long long CDataManager::GetStatsToday() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats_today;
}

std::vector<std::pair<std::wstring, unsigned long long>> CDataManager::GetStatsTodayDetail() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_today_detail_cache;
}

std::vector<DailyKeyStat> CDataManager::GetStatsHistory() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_history;
}

void CDataManager::ClearAllStats()
{
    //在锁内完成"清钩子会话计数+清统计模型+归零会话基线"，与MergeSessionCounts的
    //"读取会话计数-汇入"互斥，否则清零前的旧会话计数会在清零后被当作增量重新汇入
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        CKeyboardHook::Instance().ResetKeyCounts();
        m_stats_total = 0;
        m_stats_today = 0;
        m_today_date = GetTodayString();
        std::fill(m_today_counts.begin(), m_today_counts.end(), 0);
        m_today_detail_cache.clear();
        m_history.clear();
        std::fill(m_last_session_counts.begin(), m_last_session_counts.end(), 0);
        //注意：m_saved_counts 不清零——它是"INI当前实际内容"的快照，
        //清零后差异写入才能识别出"INI里还有、内存里已为0"的 key_ 条目并删除它们；
        //若一并清零，差异判断恒为false，INI残留条目会在重启后被读回(清零失效)
        m_stats_dirty = true;
    }
    SaveStatsData();
}

void CDataManager::MergeSessionCounts()
{
    //在锁内读取钩子会话计数再计算增量，与ClearAllStats的锁内重置互斥，
    //避免"读到清零前计数-清零-按旧计数算增量"的交错导致清零回弹
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    unsigned long long session_counts[256];
    CKeyboardHook::Instance().GetKeyCounts(session_counts);
    unsigned long long delta_total = 0;
    for (int i = 0; i < 256; ++i)
    {
        const unsigned long long current = session_counts[i];
        const unsigned long long last = m_last_session_counts[i];
        //会话计数被清零(ResetKeyCounts)时当前值会小于快照，跳过以避免把旧数据当增量
        if (current < last)
        {
            m_last_session_counts[i] = current;
            continue;
        }
        const unsigned long long delta = current - last;
        if (delta == 0)
            continue;
        m_last_session_counts[i] = current;
        delta_total += delta;
        m_today_counts[i] += delta;
    }
    if (delta_total > 0)
    {
        m_stats_total += delta_total;
        m_stats_today += delta_total;
        m_stats_dirty = true;
        RebuildDetailCacheLocked();
    }
}

void CDataManager::RollDateIfNeeded()
{
    const std::wstring today = GetTodayString();
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    if (m_today_date.empty())
    {
        m_today_date = today;
        m_stats_dirty = true;
        return;
    }
    if (m_today_date != today)
    {
        //跨天：把当日数据(含各按键明细)归档进历史，当日计数清零重新开始
        DailyKeyStat stat;
        stat.date = m_today_date;
        stat.total = m_stats_today;
        for (int i = 0; i < 256; ++i)
        {
            if (m_today_counts[i] > 0)
                stat.key_counts.emplace(i, m_today_counts[i]);
        }
        m_history.push_back(std::move(stat));
        while (static_cast<int>(m_history.size()) > MAX_HISTORY_DAYS)
            m_history.erase(m_history.begin());
        m_stats_today = 0;
        std::fill(m_today_counts.begin(), m_today_counts.end(), 0);
        m_today_detail_cache.clear();
        m_today_date = today;
        m_stats_dirty = true;
        //跨天后当日各按键计数与已落盘快照全部不一致(计数清零)，差异写入会负责删除INI中的旧键
        //达到90天上限发生回绕时，历史条数与已写条数相等，条数比较无法感知变化，
        //须用窗口滑动标记强制全量重写历史块
        if (static_cast<int>(m_history.size()) >= MAX_HISTORY_DAYS)
            m_history_shifted = true;
    }
}

void CDataManager::LoadStatsData()
{
    if (m_config_path.empty())
        return;
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    wchar_t buff[32] = { 0 };
    //累计总次数
    if (ReadProfileString(L"stats", L"total", buff, 32, m_config_path.c_str()))
        m_stats_total = static_cast<unsigned long long>(_wcstoui64(buff, nullptr, 10));
    //当日日期与当日总数
    wchar_t date_buff[24] = { 0 };
    if (ReadProfileString(L"stats", L"today_date", date_buff, 24, m_config_path.c_str()))
        m_today_date = date_buff;
    if (m_today_date.empty())
        m_today_date = GetTodayString();
    if (ReadProfileString(L"stats", L"today_total", buff, 32, m_config_path.c_str()))
        m_stats_today = static_cast<unsigned long long>(_wcstoui64(buff, nullptr, 10));
    //当日各按键计数(仅非零项写入INI，读取时全量扫描256个键码)
    std::fill(m_today_counts.begin(), m_today_counts.end(), 0);
    for (int vk = 0; vk < 256; ++vk)
    {
        wchar_t key_name[16];
        swprintf_s(key_name, L"%s%02X", KEY_COUNT_PREFIX, vk);
        if (ReadProfileString(L"stats", key_name, buff, 32, m_config_path.c_str()))
        {
            unsigned long long count = static_cast<unsigned long long>(_wcstoui64(buff, nullptr, 10));
            if (count > 0)
                m_today_counts[vk] = count;
        }
    }
    //历史每日记录，两种格式：
    //新格式 history_0=yyyy-MM-dd|total|vk1:count1,vk2:count2,... (含每日按键明细)
    //旧格式 history_0=yyyy-MM-dd,total (无明细，仅总数，兼容读取)
    m_history.clear();
    for (int i = 0; i < MAX_HISTORY_DAYS; ++i)
    {
        wchar_t hist_key[24];
        swprintf_s(hist_key, L"history_%d", i);
        wchar_t hist_buff[8192] = { 0 };
        if (!ReadProfileString(L"stats", hist_key, hist_buff, 8192, m_config_path.c_str()))
            break;
        DailyKeyStat stat;
        //按'|'拆出日期、总数、明细段
        wchar_t* context = nullptr;
        wchar_t* date_part = wcstok_s(hist_buff, L"|", &context);
        wchar_t* total_part = (date_part != nullptr) ? wcstok_s(nullptr, L"|", &context) : nullptr;
        wchar_t* detail_part = (total_part != nullptr) ? wcstok_s(nullptr, L"|", &context) : nullptr;
        if (date_part == nullptr || total_part == nullptr)
            continue;
        stat.date = date_part;
        stat.total = static_cast<unsigned long long>(_wcstoui64(total_part, nullptr, 10));
        //解析明细段"vk:count,vk:count,..."(旧格式无此段)
        if (detail_part != nullptr)
        {
            wchar_t* item_context = nullptr;
            wchar_t* item = wcstok_s(detail_part, L",", &item_context);
            while (item != nullptr)
            {
                //每项为"vk:count"
                wchar_t* pair_context = nullptr;
                wchar_t* vk_str = wcstok_s(item, L":", &pair_context);
                wchar_t* count_str = (vk_str != nullptr) ? wcstok_s(nullptr, L":", &pair_context) : nullptr;
                if (vk_str != nullptr && count_str != nullptr)
                {
                    int vk = static_cast<int>(wcstoul(vk_str, nullptr, 16));
                    unsigned long long count = static_cast<unsigned long long>(_wcstoui64(count_str, nullptr, 10));
                    if (vk >= 0 && vk < 256 && count > 0)
                        stat.key_counts.emplace(vk, count);
                }
                item = wcstok_s(nullptr, L",", &item_context);
            }
        }
        m_history.push_back(std::move(stat));
    }
    //重建当日明细缓存
    m_today_detail_cache.clear();
    for (int i = 0; i < 256; ++i)
    {
        if (m_today_counts[i] > 0)
            m_today_detail_cache.emplace_back(g_key_name_cache.Get(static_cast<UINT>(i)), m_today_counts[i]);
    }
    SortDetail(m_today_detail_cache);
    m_stats_dirty = false;
    //同步会话基线与已落盘快照：本次加载的数据与INI当前内容一致，
    //首个保存周期只写真正新增的按键，避免全量重写
    std::copy(m_today_counts.begin(), m_today_counts.end(), m_saved_counts.begin());
    m_written_history_count = static_cast<int>(m_history.size());
    CKeyboardHook::Instance().GetKeyCounts(m_last_session_counts.data());
}

void CDataManager::SaveStatsData()
{
    if (m_config_path.empty())
        return;
    //整个保存流程用专用互斥量串行化：UI线程(设置对话框刷新/清零/保存配置)与
    //监控线程的保存周期可能重叠，不串行化会同文件并发写入互相覆盖
    std::lock_guard<std::mutex> save_lock(m_save_mutex);
    //锁内拷贝落盘所需的数据快照并记录差异键集合，锁外执行文件IO，
    //避免约350次WritePrivateProfileString期间阻塞UI线程的统计读取
    unsigned long long total = 0;
    unsigned long long today = 0;
    std::wstring today_date;
    std::vector<std::pair<int, unsigned long long>> keys_to_write;     //需写入的键码与计数
    std::vector<int> keys_to_remove;                                   //需删除的键码(上次写了、本次为0)
    std::vector<std::wstring> history;                                  //历史记录(已格式化的INI值)
    int history_count = 0;                                              //当前历史条数
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        if (!m_stats_dirty)
            return;
        total = m_stats_total;
        today = m_stats_today;
        today_date = m_today_date;
        history_count = static_cast<int>(m_history.size());
        for (int vk = 0; vk < 256; ++vk)
        {
            const unsigned long long count = m_today_counts[vk];
            const unsigned long long written = m_saved_counts[vk];
            if (count != written)
            {
                if (count > 0)
                    keys_to_write.emplace_back(vk, count);
                else
                    keys_to_remove.push_back(vk);
            }
        }
        history.reserve(m_history.size());
        for (const auto& day : m_history)
        {
            //新格式：日期|总数|vk:count,vk:count,...(明细按vk升序，保证写入稳定)。
            //用wstring拼接：256个按键的明细可远超1KB，固定缓冲会溢出
            std::wstring line = day.date;
            line += L'|';
            line += std::to_wstring(day.total);
            if (!day.key_counts.empty())
            {
                line += L'|';
                for (const auto& kc : day.key_counts)
                {
                    wchar_t item[32];
                    swprintf_s(item, L"%02X:%llu", kc.first, kc.second);
                    if (line.back() != L'|')
                        line += L',';
                    line += item;
                }
            }
            history.push_back(std::move(line));
        }
    }
    //锁外写INI
    WritePrivateProfileUInt64(L"stats", L"total", total, m_config_path.c_str());
    WritePrivateProfileStringW(L"stats", L"today_date", today_date.c_str(), m_config_path.c_str());
    WritePrivateProfileUInt64(L"stats", L"today_total", today, m_config_path.c_str());
    for (const auto& key : keys_to_write)
    {
        wchar_t key_name[16];
        swprintf_s(key_name, L"%s%02X", KEY_COUNT_PREFIX, key.first);
        WritePrivateProfileUInt64(L"stats", key_name, key.second, m_config_path.c_str());
    }
    for (int vk : keys_to_remove)
    {
        wchar_t key_name[16];
        swprintf_s(key_name, L"%s%02X", KEY_COUNT_PREFIX, vk);
        WritePrivateProfileStringW(L"stats", key_name, nullptr, m_config_path.c_str());
    }
    //历史每日记录：仅条数变化时才重写(写入是追加语义，同条数时内容不变不用重写；
    //条数增加时只需写新增条目，条数因90天上限回绕时才全量重写并清理多余条目)
    const int written_count = m_written_history_count;
    //条数变化(正常追加/清理)或窗口滑动(90天回绕，条数恰好相等)时重写历史块
    if (history_count != written_count || m_history_shifted)
    {
        for (int i = 0; i < MAX_HISTORY_DAYS; ++i)
        {
            wchar_t hist_key[24];
            swprintf_s(hist_key, L"history_%d", i);
            if (i < history_count)
            {
                //只写变化区段：新增时写[written_count, history_count)；滑动/回绕时全量重写
                if (m_history_shifted || i >= written_count || history_count < written_count)
                    WritePrivateProfileStringW(L"stats", hist_key, history[i].c_str(), m_config_path.c_str());
            }
            else
            {
                WritePrivateProfileStringW(L"stats", hist_key, nullptr, m_config_path.c_str());
            }
        }
        m_history_shifted = false;
    }
    //写回成功后更新"已写入"快照与时间戳
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_last_save_tick = GetTickCount64();
        if (m_saved_counts.size() == 256)
        {
            for (const auto& key : keys_to_write)
                m_saved_counts[key.first] = key.second;
            for (int vk : keys_to_remove)
                m_saved_counts[vk] = 0;
        }
        m_written_history_count = history_count;
        //期间统计又有变化时保持dirty，等下个周期再写
        if (m_stats_total == total && m_stats_today == today)
            m_stats_dirty = false;
    }
}

std::vector<std::pair<std::wstring, unsigned long long>> CDataManager::GetStatsRankByRange(StatsRange range) const
{
    //按时间范围聚合按键排行；今日直接返回当日明细，其余范围累加对应历史日的明细
    if (range == StatsRange::Today)
        return GetStatsTodayDetail();
    std::map<std::wstring, unsigned long long> aggregated;
    unsigned long long days = 0;
    switch (range)
    {
    case StatsRange::Days7: days = 7; break;
    case StatsRange::Days30: days = 30; break;
    case StatsRange::All: days = MAX_HISTORY_DAYS + 1; break;   //全部=所有历史+当日
    default: return GetStatsTodayDetail();
    }
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    //取最近的days个历史日(含今日)，历史按日期升序存储
    for (auto iter = m_history.rbegin(); iter != m_history.rend() && days > 1; ++iter, --days)
    {
        for (const auto& kc : iter->key_counts)
        {
            if (kc.first >= 0 && kc.first < 256)
                aggregated[g_key_name_cache.Get(static_cast<UINT>(kc.first))] += kc.second;
        }
    }
    //今日明细并入(近N天与全部都包含今日)
    for (int i = 0; i < 256; ++i)
    {
        if (m_today_counts[i] > 0)
            aggregated[g_key_name_cache.Get(static_cast<UINT>(i))] += m_today_counts[i];
    }
    std::vector<std::pair<std::wstring, unsigned long long>> result(aggregated.begin(), aggregated.end());
    SortDetail(result);
    return result;
}

void CDataManager::RebuildDetailCacheLocked()
{
    //调用方须已持有m_stats_mutex；按键名走进程级缓存，避免每次调用GetKeyNameTextW
    m_today_detail_cache.clear();
    for (int i = 0; i < 256; ++i)
    {
        if (m_today_counts[i] > 0)
            m_today_detail_cache.emplace_back(g_key_name_cache.Get(static_cast<UINT>(i)), m_today_counts[i]);
    }
    SortDetail(m_today_detail_cache);
}

std::vector<std::pair<std::wstring, unsigned long long>> CDataManager::GetStatsRankByDay(const std::wstring& date) const
{
    //指定单日的按键排行；date为今日时直接返回当日明细
    const std::wstring today = GetTodayString();
    if (date == today)
        return GetStatsTodayDetail();
    std::map<std::wstring, unsigned long long> aggregated;
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        for (const auto& day : m_history)
        {
            if (day.date != date)
                continue;
            for (const auto& kc : day.key_counts)
            {
                if (kc.first >= 0 && kc.first < 256)
                    aggregated[g_key_name_cache.Get(static_cast<UINT>(kc.first))] += kc.second;
            }
            break;      //日期唯一，找到即止
        }
    }
    std::vector<std::pair<std::wstring, unsigned long long>> result(aggregated.begin(), aggregated.end());
    SortDetail(result);
    return result;
}

std::wstring CDataManager::GetTodayDate() const
{
    return GetTodayString();
}

unsigned long long CDataManager::GetStatsHistoryMax() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    unsigned long long max_total = 0;
    for (const auto& day : m_history)
    {
        if (day.total > max_total)
            max_total = day.total;
    }
    return max_total;
}

bool CDataManager::ExportStatsCsv(const std::wstring& file_path) const
{
    //导出每日历史(含今日)为CSV：日期,总按键数,各按键明细(按键名:次数，分号分隔)。
    //文件以UTF-8 BOM开头，Excel可直接打开中文不乱码
    std::wstring content;
    content += L"\ufeffdate,total,keys\r\n";
    const std::wstring today = GetTodayString();
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        for (const auto& day : m_history)
        {
            content += day.date;
            content += L',';
            content += std::to_wstring(day.total);
            content += L',';
            std::wstring keys;
            for (const auto& kc : day.key_counts)
            {
                if (!keys.empty())
                    keys += L';';
                std::wstring key_label = g_key_name_cache.Get(static_cast<UINT>(kc.first));
                //RFC4180：字段内双引号双写(部分键盘布局的键名可能含引号)
                std::wstring escaped;
                escaped.reserve(key_label.size());
                for (wchar_t ch : key_label)
                {
                    if (ch == L'"')
                        escaped += L"\"\"";
                    else
                        escaped += ch;
                }
                keys += escaped;
                keys += L':';
                keys += std::to_wstring(kc.second);
            }
            //按键名可能含逗号/分号(如","和";"键)，整段keys加引号避免破坏CSV列结构
            content += L'"';
            content += keys;
            content += L'"';
            content += L"\r\n";
        }
        //今日行(可能有尚未归档的数据)
        content += today;
        content += L',';
        content += std::to_wstring(m_stats_today);
        content += L',';
        std::wstring keys;
        for (const auto& item : m_today_detail_cache)
        {
            if (!keys.empty())
                keys += L';';
            std::wstring key_label = item.first;
            std::wstring escaped;
            escaped.reserve(key_label.size());
            for (wchar_t ch : key_label)
            {
                if (ch == L'"')
                    escaped += L"\"\"";
                else
                    escaped += ch;
            }
            keys += escaped;
            keys += L':';
            keys += std::to_wstring(item.second);
        }
        content += L'"';
        content += keys;
        content += L'"';
        content += L"\r\n";
    }
    //UTF-8写入
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file_path.c_str(), L"wb") == 0 && fp != nullptr)
    {
        const std::string utf8 = WideToUtf8(content);
        const size_t written = fwrite(utf8.data(), 1, utf8.size(), fp);
        fclose(fp);
        return written == utf8.size();
    }
    return false;
}
