#pragma once
#include <string>
#include <map>
#include <atomic>
#include <mutex>
#include "resource.h"
#include "ChapterParser.h"
#include "BookmarkMgr.h"

#define g_data CDataManager::Instance()


struct SettingData
{
    //TODO: 在此添加选项设置的数据
    std::wstring file_path;     //文本文件路径
    int window_width;           //窗口宽度
    int current_position;       //当前阅读位置（字）
    bool show_in_tooltips;         //在鼠标提示中显示
    bool enable_mulit_line;     //允许多行显示
    bool hide_when_lose_focus;    //失去焦点时不显示
    bool auto_read;             //自动阅读
    int auto_read_timer_interval;   //自动阅读的时间间隔
    bool auto_decode_base64{};  //自动对base64编码的文本文件解码
    bool use_own_context_menu{};    //使用独立的右键菜单
    bool restart_at_end{};      //阅读到结束时自动返回开始
    bool auto_reload_when_file_changed{};   //文件更改时自动重新加载
    bool mouse_wheel_read{};    //使用鼠标滚轮阅读
    int mouse_wheel_charactors{ 1 };    //鼠标滚轮一次阅读字数
    bool mouse_wheel_read_page{};   //鼠标滚轮一次阅读整页
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
    void DPIFromWindow(CWnd* pWnd);
    int DPI(int pixel);
    float DPIF(float pixel);
    int RDPI(int pixel);
    HICON GetIcon(UINT id);

    bool LoadTextContents(LPCTSTR file_path);
    //以下读取接口都会加锁返回文本/章节表的快照拷贝。DataRequired(宿主监控线程)可能随时换入新文本，
    //UI线程不得直接持有m_text_contents的引用或指针跨调用使用
    size_t GetTextLength();        //文本总字数
    std::wstring GetTextSubstr(int position, size_t max_length);    //从position处取最多max_length个字，越界时返回空串
    std::wstring GetCurrentChapterTitle();
    std::wstring GetChapterTitle(int chapter_index);
    std::wstring GetChapterByPos(int pos);
    int GetChapterIndexByPos(int pos);
    std::map<int, std::wstring> GetChapterDataSnapshot();

    int GetPageStep(CDC* dc);      //获取翻页步长，并保存在m_page_step中

    void PageUp(int step = 0);
    void PageDown(int step = 0);
    void AddBookmark();

    bool IsMultiLine() const;
    bool HasFocus() const;

    CChapterParser& GetChapter();
    const std::set<int>& GetBookmark();

    void SaveReadPosition();

    //把后台线程下载并解析好的文本换入共享数据。
    //注意：此函数由宿主的DataRequired在后台监控线程中调用(不是UI线程)，换入操作必须与UI线程的读取互斥
    void ApplyPendingText();

    SettingData m_setting_data;
    int m_page_step{ 1 };
    bool m_boss_key_pressed{ false };
    bool m_multi_line{ false };
    int m_draw_width{};
    HWND m_wnd{};
    int m_position_modified{ false };

    void CheckFileChange();

private:
    static UINT ThreadCallback(LPVOID dwUser);
    void ClampReadPosition();       //把current_position钳制到[0,文本长度]内(调用方须已持有m_data_mutex)

    //传给下载线程的参数(在堆上分配，由线程内部释放，避免static变量在线程运行中被改写)
    struct ThreadParam
    {
        std::wstring url;
    };

private:
    unsigned __int64 m_file_last_modified{};    //打开文件的上次修改时间

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;
    int m_dpi{ 96 };
    std::wstring m_text_contents;
    CChapterParser m_chapter_parser{ m_text_contents };
    CBookmarkMgr m_bookmark_mgr;
    std::atomic<bool> m_is_thread_runing{ false };
    //m_data_mutex保护所有跨线程共享的文本数据：m_text_contents、章节表(m_chapter_parser.m_chapter)以及下面几个pending/请求变量。
    //后台下载线程只写pending，DataRequired(宿主监控线程)通过ApplyPendingText换入，UI线程通过上面的快照接口读取
    std::mutex m_data_mutex;
    std::wstring m_pending_text;
    std::wstring m_pending_url;     //待换入文本对应的url
    bool m_has_pending_text{ false };
    std::wstring m_requested_url;   //最新一次请求下载的url(选择本地文件时置空)，用于丢弃过期下载结果并让运行中的线程接续新请求
};
