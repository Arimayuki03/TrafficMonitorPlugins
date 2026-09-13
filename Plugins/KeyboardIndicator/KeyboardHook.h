#pragma once
#include <string>
#include <vector>

//低级键盘钩子，用于实时捕获当前按下的按键。
//钩子安装在一个独立的后台线程中（低级钩子要求安装它的线程运行消息循环），
//按键状态保存在临界区保护的成员变量中，供绘图线程随时读取。
class CKeyboardHook
{
public:
    static CKeyboardHook& Instance();

    //启动钩子线程（幂等，线程异常退出后可自动重启）
    void Start();
    //通知钩子线程退出（向其发送WM_QUIT，不等待）
    void Stop();

    //获取当前要显示的按键文本，如"Ctrl+Shift+A"。
    //有按键按住时返回当前按下的所有按键（修饰键在前）；
    //无按键按住时，如果最后松开的按键距今不超过show_time_ms毫秒，则返回该按键。
    //没有需要显示的内容时返回false。
    bool GetDisplayText(std::wstring& text, int show_time_ms);
    //获取最后按下的按键文本（用于鼠标提示），没有记录时返回false
    bool GetLastKeyText(std::wstring& text) const;
    //当前是否有按键正被按住
    bool IsAnyKeyDown() const;

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
    //将虚拟键码转换为用于显示的按键名称
    static std::wstring GetKeyName(UINT vk, UINT scan_code, bool extended);
    static bool IsModifier(UINT vk);
    void AddKey(UINT vk, UINT scan_code, bool extended);
    void RemoveKey(UINT vk);

    static CKeyboardHook* volatile m_instance;      //用于钩子回调中访问实例

    mutable CRITICAL_SECTION m_cs;
    std::vector<KeyInfo> m_pressed_keys;    //当前按下的按键
    KeyInfo m_last_released;                //最后松开的按键
    ULONGLONG m_last_release_tick{};        //最后松开按键的时间
    HANDLE m_thread{};                      //钩子线程句柄
    unsigned m_thread_id{};                 //钩子线程id
};
