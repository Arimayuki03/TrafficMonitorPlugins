#pragma once
#include <string>
#include <vector>
#include <atomic>

//低级键盘钩子，用于实时捕获当前按下的按键。
//钩子安装在一个独立的后台线程中（低级钩子要求安装它的线程运行消息循环），
//按键状态保存在临界区保护的成员变量中，供绘图线程随时读取。
//同时提供按键统计：每次物理按下计一次（自动重复与注入按键不计入），
//计数保存在原子数组中，读写均无需加锁。
class CKeyboardHook
{
public:
    static CKeyboardHook& Instance();

    //启动钩子线程（幂等，线程异常退出后可自动重启）
    void Start();
    //停止钩子线程（先卸载钩子，再发送WM_QUIT并等待线程结束后回收句柄）。
    //返回false表示等待超时，线程可能仍在运行（钩子已卸载），可再次调用重试
    bool Stop();

    //获取当前要显示的按键文本，如"Ctrl+Shift+A"。
    //有按键按住时返回当前按下的所有按键（修饰键在前）；
    //无按键按住时，如果最后松开的按键距今不超过show_time_ms毫秒，则返回该按键。
    //没有需要显示的内容时返回false。
    bool GetDisplayText(std::wstring& text, int show_time_ms);
    //获取最后松开的按键文本（用于鼠标提示），没有记录时返回false
    bool GetLastKeyText(std::wstring& text) const;
    //当前是否有按键正被按住
    bool IsAnyKeyDown() const;

    //设置是否启用按键统计
    void SetStatsEnabled(bool enable);
    //读取各按键累计按下次数的快照（数组下标为归并后的虚拟键码0~255）
    void GetKeyCounts(unsigned long long counts[256]) const;
    //清零按键统计
    void ResetKeyCounts();
    //打字速度统计：获取当前每分钟按键数(KPM，基于最近60秒滑动窗口)、
    //本会话峰值KPM与平均KPM(会话总按键/会话总分钟)
    void GetTypingSpeed(unsigned long& current_kpm, unsigned long& peak_kpm, unsigned long& avg_kpm) const;
    //清理按住超过5分钟仍未松开的"卡住"按键(如锁屏期间丢失KEYUP消息的按键)。
    //过期清理不能只依赖显示路径(GetDisplayText仅在开启按键显示时被调用)，
    //否则关闭按键显示而只开统计时，卡住按键会永久残留在按下列表，导致其计数停滞
    void CleanupStuckKeys();

    //将虚拟键码转换为用于显示的按键名称
    static std::wstring GetKeyName(UINT vk, UINT scan_code, bool extended);
    //统计用按键归并：修饰键的左右变体与两个Win键归并为同一逻辑按键
    static UINT NormalizeKeyForStats(UINT vk);

private:
    CKeyboardHook();
    ~CKeyboardHook();
    CKeyboardHook(const CKeyboardHook&) = delete;
    CKeyboardHook& operator=(const CKeyboardHook&) = delete;

    //按键信息
    struct KeyInfo
    {
        UINT vk{};              //虚拟键码
        UINT scan_code{};       //扫描码
        bool extended{};        //是否为扩展键
        bool is_modifier{};     //是否为修饰键(Ctrl/Shift/Alt/Win)
        ULONGLONG down_tick{};  //按下时的时间
        std::wstring name;      //按键的显示名称
    };

    static unsigned int WINAPI ThreadProc(LPVOID lpParameter);
    static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static bool IsModifier(UINT vk);
    void AddKey(UINT vk, UINT scan_code, bool extended, bool injected);
    void RemoveKey(UINT vk);
    //累计一次按键计数（仅在统计启用时由AddKey调用，调用方须已持有m_cs）
    void CountKey(UINT vk);
    //按"最近60秒滑动窗口"折算当前KPM(跨度钳制[5,60]秒)。
    //仅读取原子量，钩子线程与任意读取线程均可调用
    unsigned long ComputeWindowKpm() const;
    //打字速度计数：当前秒槽位+会话累计(仅在统计启用时由AddKey调用，调用方须已持有m_cs)
    void RecordKpmTick();

    static CKeyboardHook* volatile m_instance;      //用于钩子回调中访问实例

    mutable CRITICAL_SECTION m_cs;
    std::vector<KeyInfo> m_pressed_keys;    //当前按下的按键
    KeyInfo m_last_released;                //最后松开的按键
    ULONGLONG m_last_release_tick{};        //最后松开按键的时间
    //按键统计：各按键自本会话以来的累计按下次数（下标为归并后的虚拟键码）。
    //钩子线程写、绘图/监控线程读，使用原子变量避免加锁
    std::atomic<unsigned long long> m_key_counts[256]{};
    std::atomic<bool> m_stats_enabled{ false };     //是否启用按键统计
    //打字速度：每秒按键数环形缓冲(最近60秒)，钩子线程写、任意线程读。
    //秒槽位无锁分桶：各槽位由钩子线程独占写入(同一秒内串行)，读取方汇总快照即可
    static const int KPM_WINDOW_SECONDS = 60;
    mutable std::atomic<unsigned long long> m_kpm_seconds[KPM_WINDOW_SECONDS]{};    //各秒槽位的按键数
    mutable std::atomic<unsigned long long> m_kpm_second_stamp[KPM_WINDOW_SECONDS]{};   //各槽位最后写入时的秒时钟(区分窗口轮次)
    std::atomic<unsigned long long> m_session_first_tick{ 0 };      //会话第一次按键的时间(毫秒时钟)
    std::atomic<unsigned long long> m_session_key_total{ 0 };       //会话累计按键总数(与各秒槽位独立累加)
    mutable std::atomic<unsigned long> m_peak_kpm{ 0 };             //会话KPM峰值
    HANDLE m_thread{};                      //钩子线程句柄
    unsigned m_thread_id{};                 //钩子线程id
    HHOOK m_hook{};                         //钩子句柄，供Stop()在等待线程退出前先行卸载
};
