#include "pch.h"
#include "DataManager.h"
#include "Common.h"
#include <vector>
#include <sstream>
#include <fstream>
#include <memory>
#include "../utilities/bass64/base64.h"
#include "../utilities/FilePathHelper.h"
#include "../utilities/Common.h"
#include "HtmlToText.h"

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
    //初始化DPI
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);
}

CDataManager::~CDataManager()
{
    SaveConfig();
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

static void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
{
    wchar_t buff[16];
    swprintf_s(buff, L"%d", value);
    WritePrivateProfileString(app_name, key_name, buff, file_path);
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    //获取模块的路径
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hModule, path, MAX_PATH);
    std::wstring module_path = path;
    m_config_path = module_path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        //模块的文件名
        std::wstring module_file_name = module_path.substr(index + 1);
        m_config_path = config_dir + module_file_name;
    }
    m_config_path += L".ini";
    //TODO: 在此添加载入配置的代码
    TCHAR buff[MAX_PATH];
    GetPrivateProfileString(_T("config"), _T("file_path"), _T(""), buff, MAX_PATH, m_config_path.c_str());
    m_setting_data.file_path = buff;

    m_setting_data.current_position = GetPrivateProfileInt(_T("config"), _T("current_position"), 0, m_config_path.c_str());
    m_setting_data.window_width = GetPrivateProfileInt(_T("config"), _T("window_width"), 180, m_config_path.c_str());
    m_setting_data.show_in_tooltips = GetPrivateProfileInt(_T("config"), _T("show_in_tooltips"), 0, m_config_path.c_str());
    m_setting_data.enable_mulit_line = GetPrivateProfileInt(_T("config"), _T("enable_mulit_line"), 0, m_config_path.c_str());
    m_setting_data.hide_when_lose_focus = GetPrivateProfileInt(_T("config"), _T("hide_when_lose_focus"), 0, m_config_path.c_str());
    m_setting_data.auto_read = GetPrivateProfileInt(_T("config"), _T("auto_read"), 0, m_config_path.c_str());
    m_setting_data.auto_read_timer_interval = GetPrivateProfileInt(_T("config"), _T("auto_read_timer_interval"), 2000, m_config_path.c_str());
    m_setting_data.auto_decode_base64 = GetPrivateProfileInt(_T("config"), _T("auto_decode_base64"), 1, m_config_path.c_str());
    m_setting_data.use_own_context_menu = GetPrivateProfileInt(_T("config"), _T("use_own_context_menu"), 1, m_config_path.c_str());
    m_setting_data.restart_at_end = GetPrivateProfileInt(_T("config"), _T("restart_at_end"), 0, m_config_path.c_str());
    m_setting_data.auto_reload_when_file_changed = GetPrivateProfileInt(_T("config"), _T("auto_reload_when_file_changed"), 0, m_config_path.c_str());
    m_setting_data.mouse_wheel_read = GetPrivateProfileInt(_T("config"), _T("mouse_wheel_read"), 0, m_config_path.c_str());
    m_setting_data.mouse_wheel_charactors = GetPrivateProfileInt(_T("config"), _T("mouse_wheel_charactors"), 3, m_config_path.c_str());
    m_setting_data.mouse_wheel_read_page = GetPrivateProfileInt(_T("config"), _T("mouse_wheel_read_page"), 0, m_config_path.c_str());

    //钳制ini中可能保存的异常值(current_position的上限在文本载入后再钳制)
    if (m_setting_data.current_position < 0)
        m_setting_data.current_position = 0;
    if (m_setting_data.window_width < 1)
        m_setting_data.window_width = 1;
    else if (m_setting_data.window_width > 10000)
        m_setting_data.window_width = 10000;
    if (m_setting_data.auto_read_timer_interval < 50)       //间隔过小会导致高频空转，过大(如负值转UINT)近乎永不触发
        m_setting_data.auto_read_timer_interval = 50;
    else if (m_setting_data.auto_read_timer_interval > 3600000)
        m_setting_data.auto_read_timer_interval = 3600000;

    //载入文本。必须在读取auto_decode_base64开关之后调用，否则启动载入不会对Base64编码的文本解码
    LoadTextContents(m_setting_data.file_path.c_str());

    //载入书签
    m_bookmark_mgr.LoadFromConfig(m_config_path);
}

void CDataManager::SaveConfig() const
{
    if (!m_config_path.empty())
    {
        //TODO: 在此添加保存配置的代码
        WritePrivateProfileString(_T("config"), _T("file_path"), m_setting_data.file_path.c_str(), m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("current_position"), m_setting_data.current_position, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("window_width"), m_setting_data.window_width, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("show_in_tooltips"), m_setting_data.show_in_tooltips, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("enable_mulit_line"), m_setting_data.enable_mulit_line, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("hide_when_lose_focus"), m_setting_data.hide_when_lose_focus, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("auto_read"), m_setting_data.auto_read, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("auto_read_timer_interval"), m_setting_data.auto_read_timer_interval, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("auto_decode_base64"), m_setting_data.auto_decode_base64, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("use_own_context_menu"), m_setting_data.use_own_context_menu, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("restart_at_end"), m_setting_data.restart_at_end, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("auto_reload_when_file_changed"), m_setting_data.auto_reload_when_file_changed, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("mouse_wheel_read"), m_setting_data.mouse_wheel_read, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("mouse_wheel_charactors"), m_setting_data.mouse_wheel_charactors, m_config_path.c_str());
        WritePrivateProfileInt(_T("config"), _T("mouse_wheel_read_page"), m_setting_data.mouse_wheel_read_page, m_config_path.c_str());

        //保存书签
        m_bookmark_mgr.SaveToConfig(m_config_path);
    }
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

void CDataManager::DPIFromWindow(CWnd* pWnd)
{
    CWindowDC dc(pWnd);
    HDC hDC = dc.GetSafeHdc();
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
}

int CDataManager::DPI(int pixel)
{
    return m_dpi * pixel / 96;
}

float CDataManager::DPIF(float pixel)
{
    return m_dpi * pixel / 96;
}

int CDataManager::RDPI(int pixel)
{
    return pixel * 96 / m_dpi;
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
        m_icons[id] = hIcon;
        return hIcon;
    }
}


bool CDataManager::LoadTextContents(LPCTSTR file_path)
{
    //打开url
    if (CCommon::IsURL(file_path))
    {
        bool start_thread = false;
        {
            std::lock_guard<std::mutex> lock(m_data_mutex);
            //记录最新请求：运行中的下载线程结束后会接续处理新请求，ApplyPendingText也以此校验下载结果的归属
            m_requested_url = file_path;
            //置位判定必须与m_requested_url的更新在同一把锁内完成，否则可能与下载线程的退出处理竞争导致请求丢失
            bool expected = false;
            start_thread = m_is_thread_runing.compare_exchange_strong(expected, true);
        }
        if (start_thread)
        {
            //线程参数在堆上分配，由线程内部释放，避免static变量在线程运行中被改写
            ThreadParam* param = new ThreadParam{ file_path };
            CWinThread* thread = AfxBeginThread(ThreadCallback, (LPVOID)param);
            if (thread == nullptr)
            {
                delete param;
                m_is_thread_runing = false;
            }
        }
    }
    //打开本地文件
    else
    {
        //当前选择已不是url，未换入的下载结果作废
        {
            std::lock_guard<std::mutex> lock(m_data_mutex);
            m_requested_url.clear();
        }

        std::ifstream file{ file_path, std::ios::binary };
        if (file.fail())
        {
            return false;
        }

        //保存文件的上次修改时间
        CCommon::GetFileLastModified(file_path, m_file_last_modified);

        //先读入局部变量，成功后再在锁内换入共享数据，失败时保持现有内容不变
        std::wstring text_contents;
        std::wstring ext = utilities::CFilePathHelper(file_path).GetFileExtension();
        utilities::StringHelper::StringTransform(ext, false);
        //打开html文件
        if (ext == L"html" || ext == L"htm")
        {
            CHtmlToText html_to_text;
            if (!html_to_text.ParseFromFile(file_path))
                return false;
            text_contents = html_to_text.GetText();
        }
        //打开文本文件
        else
        {
            //获取文件长度。tellg()失败时返回-1，必须按有符号数处理，否则会被钳到50MB白白分配内存
            file.seekg(0, file.end);
            std::streamoff length = file.tellg();
            file.seekg(0, file.beg);
            if (length <= 0)	//获取文件长度失败或空文件
                return false;
            if (length > MAX_FILE_SIZE)	//限制文件大小不超过MAX_FILE_SIZE
                length = MAX_FILE_SIZE;

            //读取数据
            char* buff = new char[length + 1];
            file.read(buff, length);
            auto read_length = file.gcount();
            file.close();
            if (read_length <= 0)
            {
                delete[] buff;
                return false;
            }
            std::string str_contents(buff, read_length);
            delete[] buff;

            //判断是否是base64编码
            const int BASE64_MAX_LENGTH = 1048576;
            if (g_data.m_setting_data.auto_decode_base64 && utilities::IsBase64Code(str_contents, BASE64_MAX_LENGTH))
            {
                str_contents = utilities::Base64Decode(str_contents);
            }

            //识别编码(BOM/UTF-16/UTF-8/ANSI)并转换成Unicode
            text_contents = CCommon::ConvertToUnicode(str_contents);
        }

        //换入共享数据并解析章节。m_text_contents与章节表被监控线程(DataRequired)和UI线程共同访问，必须互斥
        {
            std::lock_guard<std::mutex> lock(m_data_mutex);
            m_text_contents = std::move(text_contents);
            m_chapter_parser.Parse();
            ClampReadPosition();
        }
    }

    return true;
}


size_t CDataManager::GetTextLength()
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_text_contents.size();
}

std::wstring CDataManager::GetTextSubstr(int position, size_t max_length)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    if (position < 0 || static_cast<size_t>(position) >= m_text_contents.size())
        return std::wstring();
    return m_text_contents.substr(static_cast<size_t>(position), max_length);
}

std::wstring CDataManager::GetCurrentChapterTitle()
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_chapter_parser.GetCurrentChapterTitle();
}

std::wstring CDataManager::GetChapterTitle(int chapter_index)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_chapter_parser.GetChapterTitle(chapter_index);
}

std::wstring CDataManager::GetChapterByPos(int pos)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_chapter_parser.GetChapterByPos(pos);
}

int CDataManager::GetChapterIndexByPos(int pos)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_chapter_parser.GetChapterIndexByPos(pos);
}

std::map<int, std::wstring> CDataManager::GetChapterDataSnapshot()
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_chapter_parser.GetChapterData();
}

int CDataManager::GetPageStep(CDC* dc)
{
    if (dc == nullptr)
        return 1;
    //在锁内测量，避免测量期间后台线程换入文本导致指针失效
    int step = 1;
    bool width_exceeded = false;
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        while (true)
        {
            if (m_setting_data.current_position + step >= static_cast<int>(m_text_contents.size()))
                break;
            int text_width = dc->GetTextExtent(m_text_contents.c_str() + m_setting_data.current_position, step).cx;
            if (text_width > m_draw_width)
            {
                width_exceeded = true;
                break;
            }
            step++;
        }
        if (width_exceeded)
            step--;
    }
    //步长最小为1，否则绘制区过窄时翻页/滚轮/自动翻页会全部失效
    if (step < 1)
        step = 1;
    return step;
}

void CDataManager::PageUp(int step)
{
    if (step <= 0)
        step = m_page_step;
    if (m_boss_key_pressed)
        return;
    if (m_setting_data.current_position > 0)
        m_setting_data.current_position -= step;
    if (m_setting_data.current_position < 0)
        m_setting_data.current_position = 0;
    m_position_modified = true;
}

void CDataManager::PageDown(int step)
{
    if (step <= 0)
        step = m_page_step;
    if (m_boss_key_pressed)
        return;
    {
        //在锁内读取文本长度，避免与后台线程的文本换入竞争
        std::lock_guard<std::mutex> lock(m_data_mutex);
        //文本不足2字符时size()-2会无符号下溢，先做守卫(空文本时MAX_POS取0，翻页动作被钳制在开头)
        const int text_size = static_cast<int>(m_text_contents.size());
        const int MAX_POS = text_size >= 2 ? text_size - 2 : 0;
        if (m_setting_data.current_position < MAX_POS || m_setting_data.restart_at_end)
            m_setting_data.current_position += step;
        if (m_setting_data.current_position > MAX_POS)
        {
            if (m_setting_data.restart_at_end)
                m_setting_data.current_position = 0;
            else
                m_setting_data.current_position = MAX_POS;
        }
        if (m_setting_data.current_position < 0)
            m_setting_data.current_position = 0;
    }
    m_position_modified = true;
}

void CDataManager::AddBookmark()
{
    m_bookmark_mgr.AddBookmark(m_setting_data.file_path, m_setting_data.current_position);
    g_data.SaveConfig();
}

bool CDataManager::IsMultiLine() const
{
    return m_multi_line && m_setting_data.enable_mulit_line;
}

bool CDataManager::HasFocus() const
{
    return GetForegroundWindow() == m_wnd;
}

CChapterParser & CDataManager::GetChapter()
{
    return m_chapter_parser;
}

const std::set<int>& CDataManager::GetBookmark()
{
    return m_bookmark_mgr.GetBookmark(m_setting_data.file_path);
}

void CDataManager::SaveReadPosition()
{
    if (m_position_modified)
    {
        SaveConfig();
        m_position_modified = false;
    }
}

void CDataManager::CheckFileChange()
{
    if (m_setting_data.auto_reload_when_file_changed && !m_setting_data.file_path.empty() && !CCommon::IsURL(m_setting_data.file_path))
    {
        //检查文件的最后修改时间
        unsigned __int64 last_modified{};
        if (CCommon::GetFileLastModified(m_setting_data.file_path, last_modified))
        {
            //如果文件的最后修改时间比打开的时候新，则重新打开文件
            if (last_modified > m_file_last_modified)
            {
                m_file_last_modified = last_modified;
                LoadTextContents(m_setting_data.file_path.c_str());
            }
        }
    }
}

UINT CDataManager::ThreadCallback(LPVOID dwUser)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    //线程参数在堆上分配，使用完后由本线程释放
    std::unique_ptr<ThreadParam> param(reinterpret_cast<ThreadParam*>(dwUser));
    std::wstring url = std::move(param->url);
    for (;;)
    {
        CHtmlToText html_to_text;
        //只把下载成功的结果写入临时变量，由DataRequired通过ApplyPendingText换入共享数据。
        //解析失败时保持现有内容不变(不置pending)，否则下次换入会把已加载的正文和章节表清空
        if (html_to_text.ParseFromUrl(url))
        {
            std::lock_guard<std::mutex> lock(m_instance.m_data_mutex);
            m_instance.m_pending_text = html_to_text.GetText();
            m_instance.m_pending_url = url;
            m_instance.m_has_pending_text = true;
        }
        //检查下载期间是否有新的请求，有则接续处理(否则运行期间发起的新请求要等到重启才能加载)
        std::wstring next_url;
        {
            std::lock_guard<std::mutex> lock(m_instance.m_data_mutex);
            if (m_instance.m_requested_url != url)
            {
                next_url = m_instance.m_requested_url;
            }
            else
            {
                //清除运行标志与检查新请求必须在同一把锁内完成，避免与LoadTextContents的置位竞争丢失请求
                m_instance.m_is_thread_runing = false;
                break;
            }
        }
        url = std::move(next_url);
    }
    return 0;
}

void CDataManager::ApplyPendingText()
{
    std::wstring pending;
    std::wstring pending_url;
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        if (!m_has_pending_text)
            return;
        pending.swap(m_pending_text);
        pending_url = m_pending_url;
        m_has_pending_text = false;
    }
    //章节解析是耗时操作，在锁外用局部变量完成，避免长时间阻塞UI线程的读取
    std::map<int, std::wstring> chapters = CChapterParser::ParseChapterData(pending);
    {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        //归属校验：下载期间用户可能已切换到其他文件，此时丢弃过期的下载结果，避免覆盖当前选择
        if (pending_url != m_requested_url)
            return;
        //m_chapter_parser持有m_text_contents的引用，这里只赋值不重新构造，引用保持有效
        m_text_contents = std::move(pending);
        m_chapter_parser.SetChapterData(std::move(chapters));
        //重新载入的文本可能比已保存的阅读位置短，将阅读位置钳制回文本范围内
        ClampReadPosition();
    }
}

void CDataManager::ClampReadPosition()
{
    //前置条件：调用方已持有m_data_mutex
    if (m_setting_data.current_position < 0)
        m_setting_data.current_position = 0;
    else if (m_setting_data.current_position > static_cast<int>(m_text_contents.size()))
        m_setting_data.current_position = static_cast<int>(m_text_contents.size());
}
