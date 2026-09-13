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
    Stop();
    ::DeleteCriticalSection(&m_cs);
    m_instance = nullptr;
}

void CKeyboardHook::Start()
{
    //检查钩子线程是否仍在运行
    if (m_thread != NULL)
    {
        DWORD exit_code = 0;
        if (::GetExitCodeThread(m_thread, &exit_code) && exit_code == STILL_ACTIVE)
            return;
        ::CloseHandle(m_thread);
        m_thread = NULL;
        m_thread_id = 0;
    }
    //创建钩子线程，线程中安装低级键盘钩子并运行消息循环
    HANDLE h_thread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, NULL, 0, &m_thread_id);
    if (h_thread != NULL)
        m_thread = h_thread;
    else
        m_thread_id = 0;
}

void CKeyboardHook::Stop()
{
    if (m_thread_id != 0)
    {
        //通知钩子线程退出。此函数只发送消息不等待，因此可以在卸载DLL时安全调用
        ::PostThreadMessageW(m_thread_id, WM_QUIT, 0, 0);
    }
}

unsigned int WINAPI CKeyboardHook::ThreadProc(LPVOID /*lpParameter*/)
{
    //增加对当前DLL模块的引用，确保DLL卸载时钩子线程的代码仍然有效，
    //线程退出时通过FreeLibraryAndExitThread释放该引用
    HMODULE h_module = NULL;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&ThreadProc, &h_module);
    //安装低级键盘钩子
    HHOOK h_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, h_module, 0);
    //消息循环，低级钩子要求安装它的线程必须不断取消息，否则钩子会被系统移除
    MSG msg;
    while (::GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    if (h_hook != NULL)
        ::UnhookWindowsHookEx(h_hook);
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
        switch (wParam)
        {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            m_instance->AddKey(vk, info->scanCode, extended);
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

void CKeyboardHook::AddKey(UINT vk, UINT scan_code, bool extended)
{
    //忽略无效的虚拟键码
    if (vk == 0)
        return;
    KeyInfo info;
    info.vk = vk;
    info.scan_code = scan_code;
    info.extended = extended;
    info.is_modifier = IsModifier(vk);
    info.down_tick = GetTickCount64();
    info.name = GetKeyName(vk, scan_code, extended);
    CLock lock(m_cs);
    //如果按键已经在列表中(自动重复按键)，则不重复添加
    for (const auto& key : m_pressed_keys)
    {
        if (key.vk == vk)
            return;
    }
    m_pressed_keys.push_back(info);
}

void CKeyboardHook::RemoveKey(UINT vk)
{
    CLock lock(m_cs);
    for (auto iter = m_pressed_keys.begin(); iter != m_pressed_keys.end(); ++iter)
    {
        if (iter->vk == vk)
        {
            m_last_released = *iter;
            m_last_release_tick = GetTickCount64();
            m_pressed_keys.erase(iter);
            return;
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
        //按住Ctrl/Shift等普通字符输入不放超过5分钟视为按键状态丢失(如锁屏期间丢失KEYUP消息)，将其清除
        for (auto iter = m_pressed_keys.begin(); iter != m_pressed_keys.end();)
        {
            if (!iter->is_modifier && now - iter->down_tick > 300000)
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
