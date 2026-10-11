#include "pch.h"
#include "KeyboardHook.h"
#include <process.h>
#include <algorithm>

CKeyboardHook* volatile CKeyboardHook::m_instance = nullptr;

namespace
{
    //临界区的RAII封装
    class CLock
    {
    public:
        explicit CLock(CRITICAL_SECTION& cs) : m_cs(cs) { ::EnterCriticalSection(&m_cs); }
        ~CLock() { ::LeaveCriticalSection(&m_cs); }
    private:
        CRITICAL_SECTION& m_cs;
    };

    const UINT MODIFIER_KEYS[] = {
        VK_LCONTROL, VK_RCONTROL, VK_CONTROL,
        VK_LSHIFT, VK_RSHIFT, VK_SHIFT,
        VK_LMENU, VK_RMENU, VK_MENU,
        VK_LWIN, VK_RWIN,
    };
}

CKeyboardHook& CKeyboardHook::Instance()
{
    static CKeyboardHook instance;
    return instance;
}

CKeyboardHook::CKeyboardHook()
{
    ::InitializeCriticalSection(&m_cs);
    m_instance = this;
}

CKeyboardHook::~CKeyboardHook()
{
    //限时等待钩子线程退出；超时则放弃等待，保留临界区与句柄不销毁，
    //避免钩子线程还在使用已删除的临界区（未定义行为），少量内存留待进程退出回收
    Stop();
    m_instance = nullptr;
}

void CKeyboardHook::Start()
{
    CLock lock(m_cs);
    //检查钩子线程是否仍在运行
    if (m_thread != NULL)
    {
        DWORD exit_code = 0;
        if (::GetExitCodeThread(m_thread, &exit_code) && exit_code == STILL_ACTIVE)
            return;
        ::CloseHandle(m_thread);
        m_thread = NULL;
        m_thread_id = 0;
        m_hook = NULL;
    }
    //创建钩子线程，线程中安装低级键盘钩子并运行消息循环
    HANDLE h_thread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, NULL, 0, &m_thread_id);
    if (h_thread != NULL)
        m_thread = h_thread;
    else
        m_thread_id = 0;
}

bool CKeyboardHook::Stop()
{
    //钩子线程自身调用时(仅可能发生在DLL卸载引发静态对象析构的场景)，
    //不能等待自己退出，只先行卸载钩子
    DWORD thread_id_snapshot = 0;
    {
        CLock lock(m_cs);
        thread_id_snapshot = m_thread_id;
    }
    if (thread_id_snapshot != 0 && ::GetCurrentThreadId() == thread_id_snapshot)
    {
        CLock lock(m_cs);
        if (m_hook != NULL)
        {
            ::UnhookWindowsHookEx(m_hook);
            m_hook = NULL;
        }
        return false;
    }
    //步骤1：先在锁内通知钩子线程退出，并拿到线程句柄、ID的快照
    HANDLE h_thread = NULL;
    DWORD thread_id = 0;
    {
        CLock lock(m_cs);
        if (m_thread == NULL && m_thread_id == 0)
            return true;    //钩子线程已停止
        //先行卸载低级键盘钩子，使系统停止向钩子线程投递键盘事件
        if (m_hook != NULL)
        {
            ::UnhookWindowsHookEx(m_hook);
            m_hook = NULL;
        }
        if (m_thread != NULL)
        {
            h_thread = m_thread;
            thread_id = m_thread_id;
            if (thread_id != 0)
                ::PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        }
        else if (m_thread_id != 0)
        {
            //句柄不可用(线程已退出但尚未被回收)时仅按ID通知退出
            ::PostThreadMessageW(m_thread_id, WM_QUIT, 0, 0);
            m_thread_id = 0;
            return true;
        }
    }
    //步骤2：不持锁等待线程退出，线程退出路径中仍需进入临界区，持锁等待会互等
    if (h_thread != NULL)
    {
        DWORD wait_result = ::WaitForSingleObject(h_thread, 3000);
        //步骤3：根据等待结果回收资源；超时不删临界区，保证不再有线程使用它
        if (wait_result == WAIT_OBJECT_0)
        {
            CLock lock(m_cs);
            if (m_thread == h_thread)
            {
                ::CloseHandle(h_thread);
                m_thread = NULL;
                m_thread_id = 0;
                m_hook = NULL;
            }
            //m_thread已被Start()重新赋值时句柄由其接管，不在此关闭
            return true;
        }
        //等待超时：钩子已卸载、WM_QUIT已投递，线程即将自行退出；
        //保留句柄与临界区，返回false让调用方决定重试或放弃
        return false;
    }
    return true;
}

unsigned int WINAPI CKeyboardHook::ThreadProc(LPVOID /*lpParameter*/)
{
    //增加对当前DLL模块的引用，确保DLL卸载时钩子线程的代码仍然有效，
    //线程退出时通过FreeLibraryAndExitThread释放该引用
    HMODULE h_module = NULL;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&ThreadProc, &h_module);
    //安装低级键盘钩子
    HHOOK h_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, h_module, 0);
    if (h_hook == NULL)
    {
        //安装失败时退出线程，使Start()下次调用时能检测到线程已结束并重新尝试安装
        if (h_module != NULL)
            ::FreeLibraryAndExitThread(h_module, 0);
        return 1;
    }
    //记录钩子句柄，供Stop()在等待线程退出前先行卸载
    if (m_instance != nullptr)
    {
        CLock lock(m_instance->m_cs);
        m_instance->m_hook = h_hook;
    }
    //消息循环，低级钩子要求安装它的线程必须不断取消息，否则钩子会被系统移除
    MSG msg;
    while (::GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    if (h_hook != NULL)
        ::UnhookWindowsHookEx(h_hook);
    //消息循环退出后清空记录的钩子句柄，避免Stop()重复卸载
    if (m_instance != nullptr)
    {
        CLock lock(m_instance->m_cs);
        m_instance->m_hook = NULL;
    }
    if (h_module != NULL)
        ::FreeLibraryAndExitThread(h_module, 0);
    return 0;
}

LRESULT CALLBACK CKeyboardHook::LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    //钩子回调中只做简单的记录操作，立即返回，避免影响系统输入的响应速度
    if (code >= 0 && m_instance != nullptr)
    {
        const KBDLLHOOKSTRUCT* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        UINT vk = info->vkCode;
        bool extended = (info->flags & LLKHF_EXTENDED) != 0;
        bool injected = (info->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
        switch (wParam)
        {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            m_instance->AddKey(vk, info->scanCode, extended, injected);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            m_instance->RemoveKey(vk);
            break;
        default:
            break;
        }
    }
    return ::CallNextHookEx(NULL, code, wParam, lParam);
}

void CKeyboardHook::AddKey(UINT vk, UINT scan_code, bool extended, bool injected)
{
    //忽略无效的虚拟键码
    if (vk == 0)
        return;
    bool already_down = false;
    {
        //统计与显示共用一次锁定和遍历：按键已在列表中说明是自动重复，跳过计数并直接返回
        CLock lock(m_cs);
        for (const auto& key : m_pressed_keys)
        {
            if (key.vk == vk)
            {
                already_down = true;
                break;
            }
        }
        //注入的按键(远程桌面、自动化脚本或低完整性级别进程发送的按键)不计入统计
        if (!already_down && !injected && m_stats_enabled.load(std::memory_order_relaxed))
        {
            CountKey(vk);
            RecordKpmTick();
        }
        if (already_down)
            return;
    }
    KeyInfo info;
    info.vk = vk;
    info.scan_code = scan_code;
    info.extended = extended;
    info.is_modifier = IsModifier(vk);
    info.down_tick = GetTickCount64();
    info.name = GetKeyName(vk, scan_code, extended);
    CLock lock(m_cs);
    //再次检查，防止在获取按键名期间该按键已被其他消息添加
    for (const auto& key : m_pressed_keys)
    {
        if (key.vk == vk)
            return;
    }
    m_pressed_keys.push_back(info);
}

void CKeyboardHook::CountKey(UINT vk)
{
    //调用方须已持有m_cs且vk非0(AddKey入口已过滤)；修饰键的左右变体与两个Win键归并为同一逻辑按键
    UINT normalized = NormalizeKeyForStats(vk);
    m_key_counts[normalized].fetch_add(1, std::memory_order_relaxed);
}

void CKeyboardHook::RecordKpmTick()
{
    //打字速度计数：在持有m_cs的统计路径中调用(与CountKey同条件)。
    //当前秒的槽位序号按秒时钟取模，同一秒内的按键累加到同一槽位。
    //槽位随秒时钟循环复用，写入前比对槽位时间戳：不等于当前秒说明属于上一轮窗口，
    //先清零再计数，避免60秒前的旧计数被反复累加导致KPM随时间膨胀
    const ULONGLONG tick = GetTickCount64();
    const unsigned long long second = tick / 1000;
    const int slot = static_cast<int>(second % KPM_WINDOW_SECONDS);
    unsigned long long slot_count = 0;
    if (m_kpm_second_stamp[slot].load(std::memory_order_relaxed) != second)
    {
        //先清零计数再打时间戳：读取端"先读计数后读戳"，此顺序保证不会读到
        //"新时间戳+上一轮旧计数"的撕裂组合而虚高KPM
        m_kpm_seconds[slot].store(0, std::memory_order_relaxed);
        m_kpm_second_stamp[slot].store(second, std::memory_order_relaxed);
        slot_count = m_kpm_seconds[slot].fetch_add(1, std::memory_order_relaxed) + 1;
    }
    else
    {
        slot_count = m_kpm_seconds[slot].fetch_add(1, std::memory_order_relaxed) + 1;
    }
    //会话首按时间与累计总数(用于平均KPM)
    unsigned long long expected = 0;
    m_session_first_tick.compare_exchange_strong(expected, tick, std::memory_order_relaxed);
    m_session_key_total.fetch_add(1, std::memory_order_relaxed);
    //峰值KPM：与"当前KPM"用同一口径(最近60秒窗口折算)，保证峰值不会比
    //界面显示的当前值虚高数倍；用CAS单调抬升，不回落
    const unsigned long kpm_now = ComputeWindowKpm();
    unsigned long peak = m_peak_kpm.load(std::memory_order_relaxed);
    while (kpm_now > peak && !m_peak_kpm.compare_exchange_weak(peak, kpm_now, std::memory_order_relaxed))
    {
    }
}

unsigned long CKeyboardHook::ComputeWindowKpm() const
{
    //当前KPM：汇总最近60秒槽位的按键数。
    //每槽位带写入时间戳，过滤窗口外的陈旧数据；时间跨度=最老数据龄+1(含当前秒)，
    //钳制在[5,60]秒——刚开始打字时只用几秒的数据，避免被60秒分母严重低估
    const unsigned long long now_second = GetTickCount64() / 1000;
    unsigned long long window_keys = 0;
    ULONGLONG oldest_age = 0;       //有数据的最老槽位距当前的秒数(0=仅当前秒)
    for (int i = 0; i < KPM_WINDOW_SECONDS; ++i)
    {
        const unsigned long long count = m_kpm_seconds[i].load(std::memory_order_relaxed);
        if (count == 0)
            continue;
        const unsigned long long stamp = m_kpm_second_stamp[i].load(std::memory_order_relaxed);
        if (stamp == 0 || now_second - stamp >= KPM_WINDOW_SECONDS)
            continue;
        window_keys += count;
        const unsigned long long age = now_second - stamp;
        if (age > oldest_age)
            oldest_age = age;
    }
    ULONGLONG window_span = oldest_age + 1;
    if (window_span < 5)
        window_span = 5;
    if (window_span > KPM_WINDOW_SECONDS)
        window_span = KPM_WINDOW_SECONDS;
    return static_cast<unsigned long>(window_keys * 60 / window_span);
}

void CKeyboardHook::GetTypingSpeed(unsigned long& current_kpm, unsigned long& peak_kpm, unsigned long& avg_kpm) const
{
    current_kpm = ComputeWindowKpm();
    peak_kpm = m_peak_kpm.load(std::memory_order_relaxed);
    //读取路径的current是快照估算，若高于已记录峰值(刚跨越秒边界)则顺带抬升，保证单调
    unsigned long peak_snap = peak_kpm;
    while (current_kpm > peak_snap && !m_peak_kpm.compare_exchange_weak(peak_snap, current_kpm, std::memory_order_relaxed))
    {
    }
    peak_kpm = m_peak_kpm.load(std::memory_order_relaxed);
    //平均KPM：会话总按键/会话时长(分钟)，会话不足1分钟时按60秒折算
    const unsigned long long total = m_session_key_total.load(std::memory_order_relaxed);
    const unsigned long long first = m_session_first_tick.load(std::memory_order_relaxed);
    if (first > 0 && total > 0)
    {
        ULONGLONG elapsed = GetTickCount64() - first;
        if (elapsed < 60000)
            elapsed = 60000;
        avg_kpm = static_cast<unsigned long>(total * 60000 / elapsed);
    }
    else
    {
        avg_kpm = 0;
    }
}

void CKeyboardHook::SetStatsEnabled(bool enable)
{
    m_stats_enabled.store(enable, std::memory_order_relaxed);
}

void CKeyboardHook::GetKeyCounts(unsigned long long counts[256]) const
{
    for (int i = 0; i < 256; ++i)
        counts[i] = m_key_counts[i].load(std::memory_order_relaxed);
}

void CKeyboardHook::ResetKeyCounts()
{
    for (int i = 0; i < 256; ++i)
        m_key_counts[i].store(0, std::memory_order_relaxed);
    //打字速度统计一并清零(会话从当前时刻重新开始)
    for (int i = 0; i < KPM_WINDOW_SECONDS; ++i)
    {
        m_kpm_seconds[i].store(0, std::memory_order_relaxed);
        m_kpm_second_stamp[i].store(0, std::memory_order_relaxed);
    }
    m_session_first_tick.store(0, std::memory_order_relaxed);
    m_session_key_total.store(0, std::memory_order_relaxed);
    m_peak_kpm.store(0, std::memory_order_relaxed);
}

void CKeyboardHook::CleanupStuckKeys()
{
    //清理按住超过5分钟的按键，视为按键状态丢失(如锁屏期间丢失KEYUP消息)；
    //统计查重依赖按下列表，残留条目会使该按键的后续物理按下被误判为自动重复而不计数
    const ULONGLONG now = GetTickCount64();
    CLock lock(m_cs);
    for (auto iter = m_pressed_keys.begin(); iter != m_pressed_keys.end();)
    {
        if (now - iter->down_tick > 300000)
            iter = m_pressed_keys.erase(iter);
        else
            ++iter;
    }
}

UINT CKeyboardHook::NormalizeKeyForStats(UINT vk)
{
    switch (vk)
    {
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VK_CONTROL;
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VK_SHIFT;
    case VK_LMENU:
    case VK_RMENU:
        return VK_MENU;
    case VK_LWIN:
    case VK_RWIN:
        return VK_LWIN;     //左右Win键共用一个槽位，名称统一显示为Win
    default:
        return vk;
    }
}

void CKeyboardHook::RemoveKey(UINT vk)
{
    CLock lock(m_cs);
    //修饰键的左右键与通用键是同一个逻辑键的不同虚拟键码(如VK_CONTROL/VK_LCONTROL/VK_RCONTROL都表示Ctrl)，
    //系统可能只发送其中一种变体的KEYUP，仅按精确虚拟键码移除会残留同族条目，这里一并清除
    static const UINT CONTROL_FAMILY[] = { VK_CONTROL, VK_LCONTROL, VK_RCONTROL };
    static const UINT SHIFT_FAMILY[] = { VK_SHIFT, VK_LSHIFT, VK_RSHIFT };
    static const UINT MENU_FAMILY[] = { VK_MENU, VK_LMENU, VK_RMENU };
    static const UINT WIN_FAMILY[] = { VK_LWIN, VK_RWIN };
    const UINT* family = NULL;
    UINT family_size = 0;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL)
    {
        family = CONTROL_FAMILY;
        family_size = 3;
    }
    else if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT)
    {
        family = SHIFT_FAMILY;
        family_size = 3;
    }
    else if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)
    {
        family = MENU_FAMILY;
        family_size = 3;
    }
    else if (vk == VK_LWIN || vk == VK_RWIN)
    {
        family = WIN_FAMILY;
        family_size = 2;
    }
    bool last_key_updated = false;
    for (auto iter = m_pressed_keys.begin(); iter != m_pressed_keys.end();)
    {
        bool match = (iter->vk == vk);
        if (!match && family != NULL && iter->is_modifier)
        {
            for (UINT i = 0; i < family_size && !match; ++i)
                match = (iter->vk == family[i]);
        }
        if (match)
        {
            if (!last_key_updated)
            {
                //仅首个匹配条目更新最后松开的按键，同族其他变体只移除
                m_last_released = *iter;
                m_last_release_tick = GetTickCount64();
                last_key_updated = true;
            }
            iter = m_pressed_keys.erase(iter);
            if (family == NULL)
                return;
        }
        else
        {
            ++iter;
        }
    }
}

bool CKeyboardHook::GetDisplayText(std::wstring& text, int show_time_ms)
{
    text.clear();
    ULONGLONG now = GetTickCount64();
    CLock lock(m_cs);
    if (!m_pressed_keys.empty())
    {
        //任何按键(包括Ctrl/Shift等修饰键)按住超过5分钟视为按键状态丢失(如锁屏期间丢失KEYUP消息)，将其清除
        for (auto iter = m_pressed_keys.begin(); iter != m_pressed_keys.end();)
        {
            if (now - iter->down_tick > 300000)
                iter = m_pressed_keys.erase(iter);
            else
                ++iter;
        }
        //显示按下的按键，修饰键按Ctrl、Shift、Alt、Win的顺序排在前面，其余按键按按下的顺序显示
        std::vector<std::wstring> added;
        for (UINT vk : MODIFIER_KEYS)
        {
            for (const auto& key : m_pressed_keys)
            {
                if (key.is_modifier && key.vk == vk && added.size() < 8)
                {
                    //左右修饰键显示相同的名称，不重复显示
                    if (std::find(added.begin(), added.end(), key.name) == added.end())
                    {
                        added.push_back(key.name);
                        if (!text.empty())
                            text += L'+';
                        text += key.name;
                    }
                }
            }
        }
        for (const auto& key : m_pressed_keys)
        {
            if (!key.is_modifier && added.size() < 8)
            {
                if (std::find(added.begin(), added.end(), key.name) == added.end())
                {
                    added.push_back(key.name);
                    if (!text.empty())
                        text += L'+';
                    text += key.name;
                }
            }
        }
    }
    else if (m_last_released.vk != 0 && show_time_ms > 0 && now - m_last_release_tick <= static_cast<ULONGLONG>(show_time_ms))
    {
        //没有按键按下时，最后松开的按键在设定时间内继续显示
        text = m_last_released.name;
    }
    return !text.empty();
}

bool CKeyboardHook::GetLastKeyText(std::wstring& text) const
{
    CLock lock(m_cs);
    if (m_last_released.vk == 0)
        return false;
    text = m_last_released.name;
    return true;
}

bool CKeyboardHook::IsAnyKeyDown() const
{
    CLock lock(m_cs);
    return !m_pressed_keys.empty();
}

bool CKeyboardHook::IsModifier(UINT vk)
{
    for (UINT modifier : MODIFIER_KEYS)
    {
        if (vk == modifier)
            return true;
    }
    return false;
}

std::wstring CKeyboardHook::GetKeyName(UINT vk, UINT scan_code, bool extended)
{
    switch (vk)
    {
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_PAUSE: return L"Pause";
    case VK_CAPITAL: return L"CapsLk";
    case VK_ESCAPE: return L"Esc";
    case VK_SPACE: return L"Space";
    case VK_PRIOR: return L"PgUp";
    case VK_NEXT: return L"PgDn";
    case VK_END: return L"End";
    case VK_HOME: return L"Home";
    case VK_LEFT: return L"←";
    case VK_UP: return L"↑";
    case VK_RIGHT: return L"→";
    case VK_DOWN: return L"↓";
    case VK_INSERT: return L"Ins";
    case VK_DELETE: return L"Del";
    case VK_APPS: return L"Menu";
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT: return L"Shift";
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL: return L"Ctrl";
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU: return L"Alt";
    case VK_LWIN:
    case VK_RWIN: return L"Win";
    case VK_CANCEL: return L"Brk";
    case VK_MULTIPLY: return L"Num*";
    case VK_ADD: return L"Num+";
    case VK_SUBTRACT: return L"Num-";
    case VK_DECIMAL: return L"Num.";
    case VK_DIVIDE: return L"Num/";
    case VK_NUMLOCK: return L"NumLk";
    case VK_SCROLL: return L"ScrLk";
    case VK_OEM_1: return L";";
    case VK_OEM_PLUS: return L"=";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    case VK_OEM_3: return L"`";
    case VK_OEM_4: return L"[";
    case VK_OEM_5: return L"\\";
    case VK_OEM_6: return L"]";
    case VK_OEM_7: return L"'";
    case VK_SNAPSHOT: return L"PrtSc";
    case VK_VOLUME_MUTE: return L"Mute";
    case VK_VOLUME_DOWN: return L"Vol-";
    case VK_VOLUME_UP: return L"Vol+";
    case VK_MEDIA_NEXT_TRACK: return L"Next";
    case VK_MEDIA_PREV_TRACK: return L"Prev";
    case VK_MEDIA_PLAY_PAUSE: return L"Play";
    case VK_MEDIA_STOP: return L"Stop";
    case VK_BROWSER_BACK: return L"Back";
    case VK_BROWSER_FORWARD: return L"Fwd";
    case VK_BROWSER_REFRESH: return L"Refresh";
    case VK_BROWSER_HOME: return L"Home";
    default:
        break;
    }
    if (vk >= '0' && vk <= '9')
        return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= 'A' && vk <= 'Z')
        return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        wchar_t buff[8];
        swprintf_s(buff, L"F%d", vk - VK_F1 + 1);
        return buff;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        wchar_t buff[8];
        swprintf_s(buff, L"Num%d", vk - VK_NUMPAD0);
        return buff;
    }
    //其他按键通过扫描码获取系统定义的按键名称
    UINT sc = scan_code;
    if (sc == 0)
        sc = ::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);     //扫描码无效时(如程序注入的按键)通过虚拟键码获取
    wchar_t name[32] = { 0 };
    LONG param = static_cast<LONG>((sc & 0xFF) << 16 | (extended ? (1 << 24) : 0));
    if (param != 0 && ::GetKeyNameTextW(param, name, 32) != 0 && name[0] != 0)
        return name;
    wchar_t fallback[16];
    swprintf_s(fallback, L"Key%02X", vk);
    return fallback;
}
