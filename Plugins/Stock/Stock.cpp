#include "pch.h"
#include "Stock.h"
#include "DataManager.h"
#include "OptionsDlg.h"
#include "ManagerDialog.h"
#include "Common.h"

Stock Stock::m_instance;

Stock::Stock() : m_pFloatingWnd(NULL)
{
    m_items = vector<StockItem>(Stock_ITEM_MAX);
    fill(m_items.begin(), m_items.end(), StockItem());
    for (int index = 0; index < m_items.size(); index++)
    {
        m_items[index].index = index;
    }
}

Stock::~Stock()
{
    DestroyFloatingWnd();
}

Stock &Stock::Instance()
{
    return m_instance;
}

UINT Stock::ThreadCallback(LPVOID dwUser)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    m_instance.m_is_thread_runing = true;

    //m_setting_data可能被UI线程整体赋值，后台线程读取字段前必须加锁
    bool stock_codes_empty;
    {
        std::lock_guard<std::mutex> lock(g_data.m_settings_mutex);
        stock_codes_empty = g_data.m_setting_data.m_stock_codes.empty();
    }
    if (stock_codes_empty)
    {
        g_data.ResetText();
        //提前返回的路径也要推进节流时间戳：本函数每3秒被监控线程调起一次，
        //不更新时间戳会让每次DataRequired都认为需要刷新而反复起线程空转
        m_instance.m_last_request_time = time(nullptr);
        m_instance.m_is_thread_runing = false;
        return 0;
    }

    time_t cur_time = time(nullptr);
    if (cur_time - m_instance.m_last_request_time.load() > 3)
    {
        m_instance.m_last_request_time = cur_time;

        bool not_full_day;
        {
            std::lock_guard<std::mutex> lock(g_data.m_settings_mutex);
            not_full_day = (g_data.m_setting_data.m_full_day != 1);
        }
        if (not_full_day)
        {
            SYSTEMTIME now_time;
            GetLocalTime(&now_time);
            if (now_time.wHour < 9 || now_time.wHour > 15 || (now_time.wHour == 15 && now_time.wMinute > 30))
            {
                CCommon::WriteLog(L"Not currently in trading time!", g_data.m_log_path.c_str());
                g_data.ResetText();
                //非交易时间同样是提前返回，也要推进节流时间戳，否则每秒都会起新线程空转
                m_instance.m_last_request_time = time(nullptr);
                m_instance.m_is_thread_runing = false;
                return 0;
            }
        }

        // 标记正在更新(菜单状态由UI线程在弹出菜单时刷新，不能在后台线程直接操作菜单)
        m_instance.m_update_in_progress = true;

        //非MFC异常(如std::bad_alloc)逃出线程函数会直接terminate宿主进程，
        //这里兜底保证m_update_in_progress必然复位，否则菜单会被永久置灰
        try
        {
            g_data.RequestRealtimeData();
        }
        catch (...)
        {
        }
        m_instance.m_update_in_progress = false;
    }
    m_instance.m_is_thread_runing = false;
    return 0;
}

void Stock::LoadContextMenu()
{
    if (m_menu.m_hMenu == NULL)
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        m_menu.LoadMenu(IDR_MENU1);
    }
}

IPluginItem *Stock::GetItem(int index)
{
    size_t item_size = m_items.size();
    if (g_data.m_setting_data.m_stock_codes.size() < item_size)
        item_size = g_data.m_setting_data.m_stock_codes.size();
    if (item_size == 0)
        item_size = 1;
    //负索引同样返回空指针(宿主契约要求index在[0, 数量)之外时返回nullptr)
    if (index < 0 || index >= static_cast<int>(item_size))
        return nullptr;
    return &(m_items[index]);
}

const wchar_t *Stock::GetTooltipInfo()
{
    return L"";
}

void Stock::DataRequired()
{
    time_t cur_time = time(nullptr);
    if (cur_time - m_instance.m_last_request_time.load() > 3)
    {
        SendStockInfoRequest();
    }
    std::lock_guard<std::mutex> lock(m_wndMutex);
    if (m_pFloatingWnd != NULL && ::IsWindow(m_pFloatingWnd->GetSafeHwnd()))
    {
        //本函数在宿主监控线程中调用。持锁期间SendMessage会等待UI线程处理消息，而UI线程
        //可能正阻塞在本锁上(ShowFloatingWnd/DestroyFloatingWnd)，会形成互等死锁，
        //必须用PostMessage异步通知(OnRequestData无返回值依赖)
        m_pFloatingWnd->PostMessage(FWND_MSG_REQUEST_DATA, cur_time, 0);
    }
}

ITMPlugin::OptionReturn Stock::ShowOptionsDialog(void *hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd *pParent = CWnd::FromHandle((HWND)hParent);
    if (ShowStockManageDlg(pParent) == IDOK)
    {
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t *Stock::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return g_data.StringRes(IDS_PLUGIN_NAME).GetString();
    case TMI_DESCRIPTION:
        return g_data.StringRes(IDS_PLUGIN_DESCRIPTION).GetString();
    case TMI_AUTHOR:
        return L"CListery";
    case TMI_COPYRIGHT:
        return L"Copyright (C) by CListery 2022";
    case ITMPlugin::TMI_URL:
        return L"https://github.com/zhongyang219/TrafficMonitorPlugins";
    case TMI_VERSION:
        return L"1.14";
    default:
        break;
    }
    return L"";
}

void Stock::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t *data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        // 从配置文件读取配置
        g_data.LoadConfig(std::wstring(data));
        updateItems();
        break;
    case ITMPlugin::EI_TASKBAR_WND_VALUE_RIGHT_ALIGN:
        // 获取TrafficMonitor任务栏窗口中“数值右对齐”设置
        g_data.m_right_align = (_wtoi(data) != 0);
        break;
    default:
        break;
    }
}

int Stock::GetCommandCount()
{
    return 1;
}

const wchar_t *Stock::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0:
        return g_data.StringRes(IDS_MENU_UPDATE_STOCK).GetString();
    }
    return nullptr;
}

void Stock::OnPluginCommand(int command_index, void *hWnd, void *para)
{
    switch (command_index)
    {
    case 0:
        SendStockInfoRequest();
        break;
    }
}

void *Stock::GetPluginIcon()
{
    return g_data.GetIcon(IDI_STOCK);
}

void Stock::updateItems()
{
    //enable成员已删除(除赋值外全仓无读取点，纯死代码)。stock_id的赋值保留：
    //DrawItem/OnMouseEvent等读路径依赖它，LoadConfig后据此刷新各项的股票代码
    for (int index = 0; index < static_cast<int>(g_data.m_setting_data.m_stock_codes.size()); index++)
    {
        if (index > static_cast<int>(m_items.size()) - 1)
        {
            break;
        }
        m_items[index].stock_id = g_data.m_setting_data.m_stock_codes[index];
    }
}

INT_PTR Stock::ShowStockManageDlg(CWnd *pWnd)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CManagerDialog dlg(pWnd);
    dlg.m_data = g_data.m_setting_data;
    INT_PTR rtn = dlg.DoModal();
    if (rtn == IDOK)
    {
        //整体赋值与后台线程读取字段(ThreadCallback/RequestRealtimeData)并发，必须加锁
        std::lock_guard<std::mutex> lock(g_data.m_settings_mutex);
        g_data.m_setting_data = dlg.m_data;
        updateItems();
        g_data.SaveConfig();
    }
    return rtn;
}

void Stock::SendStockInfoRequest()
{
    //原子地检查并置位，避免两次快速触发时启动两个并发刷新线程
    bool expected = false;
    if (m_is_thread_runing.compare_exchange_strong(expected, true))
    {
        if (AfxBeginThread(ThreadCallback, nullptr) == NULL)
        {
            m_is_thread_runing = false;
        }
    }
}

void Stock::ShowContextMenu(CWnd *pWnd)
{
    LoadContextMenu();
    CMenu *context_menu = m_menu.GetSubMenu(0);
    if (context_menu != nullptr)
    {
        //“更新”菜单项的状态在UI线程中刷新(后台线程不能直接操作菜单)
        context_menu->EnableMenuItem(ID_UPDATE, MF_BYCOMMAND | (m_update_in_progress ? MF_GRAYED : MF_ENABLED));
        CPoint point1;
        GetCursorPos(&point1);
        DWORD id = context_menu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point1.x, point1.y, pWnd);
        // 点击了“管理”
        if (id == ID_OPTIONS)
        {
            ShowStockManageDlg(pWnd);
        }
        // 点击了“更新”
        else if (id == ID_UPDATE)
        {
            SendStockInfoRequest();
        }
    }
}

void Stock::ShowFloatingWnd(void *hWnd, CPoint ptScreen, std::wstring stock_id)
{
    // 如果已有悬浮窗，先销毁
    DestroyFloatingWnd();

    ClientToScreen((HWND)hWnd, &ptScreen);

    CWnd *pWnd = CWnd::FromHandle((HWND)hWnd);
    //主窗口模式下宿主传入的是无父窗口的顶层窗口，GetParent()返回NULL，必须判空回退；
    //任务栏模式下GetParent()是宿主任务栏窗口(可能为跨进程窗口)，其字体句柄不能直接持有
    CWnd *pParent = (pWnd != NULL) ? pWnd->GetParent() : NULL;
    CFont *font = (pParent != NULL) ? pParent->GetFont() : NULL;
    //复制LOGFONT为插件自有字体，取不到时传NULL，浮窗OnPaint里if(m_pfont)兜底用默认字体
    CFont *pFloatingFont = NULL;
    if (font != NULL)
    {
        LOGFONT logfont{};
        if (font->GetLogFont(&logfont))
        {
            m_floating_wnd_font.DeleteObject();
            if (m_floating_wnd_font.CreateFontIndirect(&logfont))
                pFloatingFont = &m_floating_wnd_font;
        }
    }

    std::lock_guard<std::mutex> lock(m_wndMutex);
    // 创建新的悬浮窗
    m_pFloatingWnd = new CFloatingWnd;
    if (!m_pFloatingWnd->Create(pFloatingFont, ptScreen, stock_id))
    {
        delete m_pFloatingWnd;
        m_pFloatingWnd = NULL;
    }
}

void Stock::DestroyFloatingWnd()
{
    std::lock_guard<std::mutex> lock(m_wndMutex);
    if (m_pFloatingWnd != NULL)
    {
        //浮窗HWND可能已被其内部的透明子窗口连带销毁(点击浮窗外区域)，此时IsWindow()为FALSE，
        //但C++对象仍然存活且没有其他释放点，不能因此跳过delete，否则每轮"关闭再打开"泄漏一个对象
        if (::IsWindow(m_pFloatingWnd->GetSafeHwnd()))
            m_pFloatingWnd->DestroyWindow();
        delete m_pFloatingWnd;
        m_pFloatingWnd = NULL;
    }
}

void Stock::UpdateKLine()
{
    //本函数在网络线程中调用。持锁期间不能SendMessage跨线程等待UI线程处理消息，
    //否则UI线程若正阻塞在m_wndMutex上(DataRequired)会互等死锁，必须用PostMessage异步通知
    std::lock_guard<std::mutex> lock(m_wndMutex);
    if (m_pFloatingWnd != NULL && ::IsWindow(m_pFloatingWnd->GetSafeHwnd()))
    {
        m_pFloatingWnd->PostMessage(FWND_MSG_UPDATE_STATUS, FALSE, 0);
    }
}

ITMPlugin *TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &Stock::Instance();
}
