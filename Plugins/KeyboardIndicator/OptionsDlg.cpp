#include "pch.h"
#include "OptionsDlg.h"
#include "afxdialogex.h"
#include "DataManager.h"
#include "KeyboardHook.h"
#include <commdlg.h>

// COptionsDlg 对话框

IMPLEMENT_DYNAMIC(COptionsDlg, CDialog)

namespace
{
    //统计列表最多显示的行数，防止异常数据撑爆列表
    const size_t MAX_STATS_ROWS = 256;
    //主页签索引
    const int MAIN_TAB_DISPLAY = 0;
    const int MAIN_TAB_STATS = 1;
    //统计页内页签索引
    const int STATS_TAB_RANK = 0;
    const int STATS_TAB_HISTORY = 1;
    //排行时间范围(与下拉框顺序对应)
    const StatsRange RANGES[] = { StatsRange::Today, StatsRange::Days7, StatsRange::Days30, StatsRange::All };
    //键位覆盖图绘制的键码顺序：修饰键排+数字排+三排主键区+空格，覆盖常用按键。
    //修饰键显示名与GetKeyName一致(Ctrl/Shift/Alt/Win)，计数已归并所以每行各画一个
    const UINT KEYMAP_ROWS[][12] = {
        { VK_TAB, VK_CAPITAL, VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_ESCAPE, VK_BACK, VK_RETURN, VK_SPACE, VK_OEM_5, VK_DELETE },
        { '1','2','3','4','5','6','7','8','9','0', VK_OEM_MINUS, VK_OEM_PLUS },
        { 'Q','W','E','R','T','Y','U','I','O','P', VK_OEM_1, VK_OEM_7 },
        { 'A','S','D','F','G','H','J','K','L', VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2 },
        { 'Z','X','C','V','B','N','M', VK_LEFT, VK_UP, VK_DOWN, VK_RIGHT, VK_HOME },
    };
    const int KEYMAP_ROW_COUNT = 5;

    //把页面对话框定位到指定页签控件的显示区内。
    //子页面对话框实际位置以页签客户区为准，避免DPI缩放后错位
    void PlacePageInTab(CWnd* pPage, CTabCtrl* pTab)
    {
        if (pPage == nullptr || pPage->GetSafeHwnd() == nullptr || pTab == nullptr)
            return;
        CRect rc;
        pTab->GetClientRect(&rc);
        pTab->AdjustRect(FALSE, &rc);       //页签显示区
        rc.DeflateRect(2, 2);               //显示区与边框之间留出间隙
        POINT tl = rc.TopLeft();
        POINT br = rc.BottomRight();
        pTab->ClientToScreen(&tl);
        pTab->ClientToScreen(&br);
        CWnd* pParent = pPage->GetParent();
        if (pParent != nullptr)
        {
            pParent->ScreenToClient(&tl);
            pParent->ScreenToClient(&br);
        }
        rc.left = tl.x;
        rc.top = tl.y;
        rc.right = br.x;
        rc.bottom = br.y;
        pPage->MoveWindow(&rc);
    }

    //设置排行列表的列宽：排名/次数/占比紧凑固定宽，按键列吃掉剩余空间。
    //总宽严格等于客户区宽度(留出竖向滚动条位)，否则会出水平滚动条并截断占比列
    //探测ListView对传入列宽的DPI缩放系数：高DPI下系统会把LVM_SETCOLUMNWIDTH
    //的值按比例放大，直接累计传入值会低估实际总宽而出现横向滚动条。
    //返回 实际/传入 比值(异常时返回1.0)
    double ProbeColumnScale(CListCtrl* pList)
    {
        if (pList == nullptr)
            return 1.0;
        const int probe = 100;
        pList->SetColumnWidth(0, probe);
        const int got = pList->GetColumnWidth(0);
        if (got > 10)
        {
            const double scale = static_cast<double>(got) / probe;
            if (scale >= 0.5 && scale <= 4.0)
                return scale;
        }
        return 1.0;
    }

    //列表控件所在DPI
    int ListDpi(CListCtrl* pList)
    {
        if (pList == nullptr || pList->GetSafeHwnd() == nullptr)
            return 96;
        HDC hdc = ::GetDC(pList->GetSafeHwnd());
        int dpi = hdc != nullptr ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc != nullptr)
            ::ReleaseDC(pList->GetSafeHwnd(), hdc);
        return dpi > 0 ? dpi : 96;
    }

    //列表可用宽度 = 客户区 - (未显示滚动条时为可能出现的竖向滚动条预留) - 边距。
    //注意：竖向滚动条已显示时GetClientRect已扣除其宽度，不能重复扣减
    int ListAvailWidth(CListCtrl* pList, int dpi)
    {
        if (pList == nullptr)
            return 200;
        CRect rc;
        pList->GetClientRect(&rc);
        bool vscroll_visible = false;
        if (pList->GetSafeHwnd() != nullptr)
        {
            SCROLLBARINFO sbi{};
            sbi.cbSize = sizeof(sbi);
            if (::GetScrollBarInfo(pList->GetSafeHwnd(), OBJID_VSCROLL, &sbi))
            {
                vscroll_visible = (sbi.rgstate[0] & STATE_SYSTEM_INVISIBLE) == 0
                    && (sbi.rgstate[0] & STATE_SYSTEM_UNAVAILABLE) == 0;
            }
        }
        int avail = rc.Width() - 2;
        if (!vscroll_visible)
        {
            //滚动条尚未出现：为可能出现的滚动条预留(按DPI取较宽值)
            int scrollbar = ::GetSystemMetrics(SM_CXVSCROLL);
            const int scaled = 22 * dpi / 96;
            if (scrollbar < scaled)
                scrollbar = scaled;
            avail -= scrollbar;
        }
        if (avail < 140)
            avail = 140;
        return avail;
    }

    //设置排行列表的列宽：窄列按表头自适应(控件按真实字体度量，天然适配DPI与缩放)，
    //回读实际像素宽后把剩余宽度给按键列，最后钳制总宽杜绝横向滚动条
    //用控件字体测量一段文本的像素宽度(物理像素，天然适配DPI)，失败返回0
    int MeasureTextWidth(CListCtrl* pList, const wchar_t* text)
    {
        if (pList == nullptr || pList->GetSafeHwnd() == nullptr)
            return 0;
        CDC* pDC = pList->GetDC();
        if (pDC == nullptr)
            return 0;
        CFont* pOldFont = nullptr;
        CFont* pFont = pList->GetFont();
        if (pFont != nullptr)
            pOldFont = pDC->SelectObject(pFont);
        const int width = pDC->GetTextExtent(text).cx;
        if (pOldFont != nullptr)
            pDC->SelectObject(pOldFont);
        pList->ReleaseDC(pDC);
        return width;
    }

    //设置排行列表的列宽。
    //注意不能用LVSCW_AUTOSIZE_USEHEADER：该常量作用于"最后一列"时会让列自动
    //撑满剩余宽度，导致列宽错乱。这里改为：用控件字体精确测量各列所需像素宽，
    //再按探测到的系统DPI放大系数反算下发值，最后回读实际宽度做钳制校验。
    void LayoutRankColumns(CListCtrl* pList)
    {
        if (pList == nullptr)
            return;
        //1) 目标像素宽：窄列取"表头文字"与"常见数值"的较大者
        const int pad = 24;         //表头文字两侧余量，避免出现省略号
        int t_rank = MeasureTextWidth(pList, L"排名") + pad;
        const int t_count_head = MeasureTextWidth(pList, L"次数") + pad;
        const int t_count_text = MeasureTextWidth(pList, L"99999") + pad;
        int t_count = t_count_head > t_count_text ? t_count_head : t_count_text;
        const int t_pct_head = MeasureTextWidth(pList, L"占比") + 20;
        const int t_pct_text = MeasureTextWidth(pList, L"100.0%") + 16;
        int t_pct = t_pct_head > t_pct_text ? t_pct_head : t_pct_text;
        //测量失败(无DC等)时的按DPI兜底
        const int dpi = ListDpi(pList);
        if (t_rank <= 0) t_rank = 42 * dpi / 96;
        if (t_count <= 0) t_count = 62 * dpi / 96;
        if (t_pct <= 0) t_pct = 58 * dpi / 96;
        //2) 按键列吃剩余宽度
        const int avail = ListAvailWidth(pList, dpi);
        int t_key = avail - t_rank - t_count - t_pct;
        if (t_key < 60)
            t_key = 60;
        //3) 按系统放大系数反算下发值(探测后回读即为目标像素)
        const double k = ProbeColumnScale(pList);
        pList->SetColumnWidth(0, static_cast<int>(t_rank / k + 0.5));
        pList->SetColumnWidth(1, static_cast<int>(t_key / k + 0.5));
        pList->SetColumnWidth(2, static_cast<int>(t_count / k + 0.5));
        pList->SetColumnWidth(3, static_cast<int>(t_pct / k + 0.5));
        //4) 回读校验：超过可用宽度时按溢出量收窄按键列(最多3轮)，杜绝横向滚动条
        for (int i = 0; i < 3; ++i)
        {
            const int total = pList->GetColumnWidth(0) + pList->GetColumnWidth(1)
                + pList->GetColumnWidth(2) + pList->GetColumnWidth(3);
            if (total <= avail)
                break;
            int key_now = pList->GetColumnWidth(1) - (total - avail) - 4;
            if (key_now < 40)
                key_now = 40;
            pList->SetColumnWidth(1, static_cast<int>(key_now / k + 0.5));
        }
    }

    //统计页内布局：页签显示区上部放键位覆盖图(排行页)/柱状图(每日页，高约1/3)，下部放对应列表。
    //两个图与两个列表重叠摆放，由SwitchStatsTab按页签切换可见性
    void LayoutStatsTabArea(CWnd* pStats, CTabCtrl* pTab)
    {
        if (pStats == nullptr || pTab == nullptr)
            return;
        //子页被PlacePageInTab拉伸到主页签显示区后，页内页签控件仍停在对话框模板
        //尺寸，导致列表区偏小、页面底部留大片空白。先把页内页签右/下边贴到页客户区
        CRect rc_page;
        pStats->GetClientRect(&rc_page);
        CRect rc_tab;
        pTab->GetWindowRect(&rc_tab);
        pStats->ScreenToClient(&rc_tab);
        const int page_margin = 8;
        rc_tab.right = rc_page.right - page_margin;
        rc_tab.bottom = rc_page.bottom - page_margin;
        if (rc_tab.Width() > 60 && rc_tab.Height() > 60)
            pTab->MoveWindow(&rc_tab);
        CRect rc;
        pTab->GetClientRect(&rc);
        pTab->AdjustRect(FALSE, &rc);       //页签显示区
        rc.DeflateRect(2, 2);
        POINT tl = rc.TopLeft();
        POINT br = rc.BottomRight();
        pTab->ClientToScreen(&tl);
        pTab->ClientToScreen(&br);
        pStats->ScreenToClient(&tl);
        pStats->ScreenToClient(&br);
        rc.left = tl.x;
        rc.top = tl.y;
        rc.right = br.x;
        rc.bottom = br.y;
        //上区(图)占1/3高度，下区(列表)占其余
        const int top_h = rc.Height() / 3;
        CRect rc_top(rc.left, rc.top, rc.right, rc.top + top_h);
        CRect rc_bottom(rc.left, rc.top + top_h + 4, rc.right, rc.bottom);
        CWnd* pMap = pStats->GetDlgItem(IDC_KEYBOARD_MAP_CTRL);
        CWnd* pChart = pStats->GetDlgItem(IDC_CHART_CTRL);
        CWnd* pRank = pStats->GetDlgItem(IDC_RANK_LIST);
        CWnd* pHistory = pStats->GetDlgItem(IDC_HISTORY_LIST);
        if (pMap != nullptr)
            pMap->MoveWindow(&rc_top);
        if (pChart != nullptr)
            pChart->MoveWindow(&rc_top);
        if (pRank != nullptr)
            pRank->MoveWindow(&rc_bottom);
        if (pHistory != nullptr)
            pHistory->MoveWindow(&rc_bottom);
    }

    //设置每日统计列表的列宽：日期列占满剩余宽度，总宽不超客户区
    //设置每日统计列表的列宽(算法同LayoutRankColumns：测量+反算+回读校验)
    void LayoutHistoryColumns(CListCtrl* pList)
    {
        if (pList == nullptr)
            return;
        const int dpi = ListDpi(pList);
        const int pad = 24;         //表头文字两侧余量，避免出现省略号
        int t_count_head = MeasureTextWidth(pList, L"次数") + pad;
        const int t_count_text = MeasureTextWidth(pList, L"99999") + pad;
        int t_count = t_count_head > t_count_text ? t_count_head : t_count_text;
        const int t_date = MeasureTextWidth(pList, L"2026-10-12") + pad;
        if (t_count <= 0) t_count = 62 * dpi / 96;
        const int avail = ListAvailWidth(pList, dpi);
        int t_date_final = t_date > 0 ? t_date : (100 * dpi / 96);
        if (t_date_final < 90)
            t_date_final = 90;
        const double k = ProbeColumnScale(pList);
        pList->SetColumnWidth(0, static_cast<int>(t_date_final / k + 0.5));
        pList->SetColumnWidth(1, static_cast<int>(t_count / k + 0.5));
        for (int i = 0; i < 3; ++i)
        {
            const int total = pList->GetColumnWidth(0) + pList->GetColumnWidth(1);
            if (total <= avail)
                break;
            int date_now = pList->GetColumnWidth(0) - (total - avail) - 4;
            if (date_now < 80)
                date_now = 80;
            pList->SetColumnWidth(0, static_cast<int>(date_now / k + 0.5));
        }
    }

    //按键显示名(键位覆盖图用，简单键直接用字符)
    std::wstring KeyMapLabel(UINT vk)
    {
        //等宽小格子内用短名保证可读(长名如CapsLk/Backspace会溢出)
        switch (vk)
        {
        case VK_SPACE:      return L"Space";
        case VK_TAB:        return L"Tab";
        case VK_CAPITAL:    return L"Caps";
        case VK_DELETE:     return L"Del";
        case VK_OEM_5:      return L"、";        //中文布局下"、"位于反斜杠键位
        case VK_HOME:       return L"Home";
        case VK_SHIFT:      return L"Shift";
        case VK_CONTROL:    return L"Ctrl";
        case VK_MENU:       return L"Alt";
        case VK_LWIN:       return L"Win";
        case VK_ESCAPE:     return L"Esc";
        case VK_BACK:       return L"Bksp";
        case VK_RETURN:     return L"Enter";
        case VK_OEM_MINUS:  return L"-";
        case VK_OEM_PLUS:   return L"=";
        case VK_OEM_1:      return L";";
        case VK_OEM_7:      return L"'";
        case VK_OEM_COMMA:  return L",";
        case VK_OEM_PERIOD: return L".";
        case VK_OEM_2:      return L"/?";       //同一物理键，提示 / 与 ? 共用
        default:
            if (vk >= 'A' && vk <= 'Z')
                return std::wstring(1, static_cast<wchar_t>(vk));
            if (vk >= '0' && vk <= '9')
                return std::wstring(1, static_cast<wchar_t>(vk));
            return CKeyboardHook::GetKeyName(vk, 0, false);
        }
    }
}

// CStatsPageDlg：统计页子对话框，接收owner-draw控件的WM_DRAWITEM并委托主对话框绘制
IMPLEMENT_DYNAMIC(CStatsPageDlg, CDialog)

CStatsPageDlg::CStatsPageDlg(COptionsDlg* owner)
    : CDialog(IDD_STATS_PAGE, owner)
    , m_owner(owner)
{
}

void CStatsPageDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CStatsPageDlg, CDialog)
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

//统计页内部控件(页签等)的通知发给本子对话框，这里原样转交主对话框处理，
//保证页签切换/列表事件统一在COptionsDlg::OnNotify里分流
BOOL CStatsPageDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
    NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
    if (header != nullptr && m_owner != nullptr)
    {
        LRESULT result = 0;
        if (m_owner->OnNotify(wParam, lParam, &result))
        {
            *pResult = result;
            return TRUE;        //owner已处理
        }
        //owner未处理时回落到默认处理，避免吞掉键盘导航/表头点击等默认行为
    }
    return CDialog::OnNotify(wParam, lParam, pResult);
}

//统计页内按钮(清零/导出/刷新)与下拉框的BN_CLICKED/CBN_SELCHANGE等命令消息
//的第一站是本子对话框；MFC不会自动把命令路由给"兄弟"主对话框的消息映射，
//这里重写OnCmdMsg把命令显式交给owner处理，未处理再回落默认路由
BOOL CStatsPageDlg::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
    if (m_owner != nullptr && m_owner->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
        return TRUE;
    return CDialog::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

void CStatsPageDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    //键位覆盖图与柱状图是SS_OWNERDRAW静态控件，WM_DRAWITEM发给直接父窗口
    if (lpDrawItemStruct != nullptr && lpDrawItemStruct->CtlType == ODT_STATIC && m_owner != nullptr)
    {
        CDC dc;
        dc.Attach(lpDrawItemStruct->hDC);
        CRect rect(lpDrawItemStruct->rcItem);
        if (lpDrawItemStruct->CtlID == IDC_KEYBOARD_MAP_CTRL)
            m_owner->DrawKeyboardMap(&dc, rect);
        else if (lpDrawItemStruct->CtlID == IDC_CHART_CTRL)
            m_owner->DrawDailyChart(&dc, rect);
        dc.Detach();
        return;
    }
    CDialog::OnDrawItem(nIDCtl, lpDrawItemStruct);
}

COptionsDlg::COptionsDlg(CWnd* pParent /*=nullptr*/)
    : CDialog(IDD_OPTIONS_DIALOG, pParent)
{
}

COptionsDlg::~COptionsDlg()
{
}

void COptionsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

CDialog* COptionsDlg::CreateSubPage(UINT template_id, bool stats_page)
{
    //以主对话框为父窗口创建子页面对话框(隐藏)，位置由PlacePageInTab调整。
    //两个子页都用CStatsPageDlg子类：它负责把owner-draw的WM_DRAWITEM、
    //控件通知(TCN_SELCHANGE等)与按钮命令(清零/导出/刷新等)路由给主对话框
    auto page = std::make_unique<CStatsPageDlg>(this);
    if (!page->Create(template_id, this))
        return nullptr;
    page->ShowWindow(SW_HIDE);
    CDialog* result = page.get();
    page.release();     //所有权转移给m_display_page/m_stats_page
    return result;
}

BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
    ON_BN_CLICKED(IDC_ENABLE_KEY_STATS_CHECK, &COptionsDlg::OnBnClickedEnableKeyStatsCheck)
    ON_BN_CLICKED(IDC_RESET_STATS_BUTTON, &COptionsDlg::OnBnClickedResetStatsButton)
    ON_CBN_SELCHANGE(IDC_STATS_RANGE_COMBO, &COptionsDlg::OnStatsRangeChanged)
    ON_BN_CLICKED(IDC_STATS_REFRESH_BUTTON, &COptionsDlg::OnRefreshStats)
    ON_BN_CLICKED(IDC_EXPORT_CSV_BUTTON, &COptionsDlg::OnExportCsv)
END_MESSAGE_MAP()


// COptionsDlg 消息处理程序


BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetIcon(g_data.GetIcon(IDI_ICON1), FALSE);

    //创建两个子页面并加入主页签
    CTabCtrl* pMainTab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
    if (pMainTab != nullptr)
    {
        pMainTab->InsertItem(MAIN_TAB_DISPLAY, g_data.StringRes(IDS_PAGE_DISPLAY));
        pMainTab->InsertItem(MAIN_TAB_STATS, g_data.StringRes(IDS_PAGE_STATS));
    }
    m_display_page.reset(CreateSubPage(IDD_DISPLAY_PAGE, false));
    m_stats_page.reset(CreateSubPage(IDD_STATS_PAGE, true));
    PlacePageInTab(m_display_page.get(), pMainTab);
    PlacePageInTab(m_stats_page.get(), pMainTab);

    //初始化"显示设置"页控件
    CWnd* pDisplay = m_display_page.get();
    if (pDisplay != nullptr)
    {
        pDisplay->CheckDlgButton(IDC_SHOW_CAPS_LOCK_CHECK, m_data.show_caps_lock);
        pDisplay->CheckDlgButton(IDC_SHOW_NUM_LOCK_CHECK, m_data.show_num_lock);
        pDisplay->CheckDlgButton(IDC_SHOW_SCROLL_LOCK_CHECK, m_data.show_scroll_lock);
        pDisplay->CheckDlgButton(IDC_DRAW_ROUND_RECT_CHECK, m_data.draw_round_rect);
        pDisplay->CheckDlgButton(IDC_SHOW_PRESSED_KEY_CHECK, m_data.show_pressed_key);
        pDisplay->SetDlgItemInt(IDC_PRESSED_KEY_SHOW_TIME_EDIT, m_data.pressed_key_show_time, FALSE);
        pDisplay->SetDlgItemInt(IDC_PRESSED_KEY_RESERVED_EDIT, m_data.pressed_key_reserved_width, TRUE);
        pDisplay->CheckDlgButton(IDC_SHOW_KEY_COUNT_CHECK, m_data.show_key_count);
        pDisplay->SetDlgItemInt(IDC_KEY_COUNT_RESERVED_EDIT, m_data.key_count_reserved_width, TRUE);
        //KPM开关与宽度(此前漏了初始化，导致打开对话框永远是未勾选、确定即被写回false)
        pDisplay->CheckDlgButton(IDC_SHOW_KPM_CHECK, m_data.show_kpm);
        pDisplay->SetDlgItemInt(IDC_KPM_RESERVED_EDIT, m_data.kpm_reserved_width, TRUE);
    }

    //初始化"按键统计"页：页签、筛选下拉框、键位覆盖图、两张列表、柱状图
    CWnd* pStats = m_stats_page.get();
    if (pStats != nullptr)
    {
        pStats->CheckDlgButton(IDC_ENABLE_KEY_STATS_CHECK, m_data.enable_key_stats);
        pStats->SetDlgItemInt(IDC_DAILY_GOAL_EDIT, m_data.daily_goal, TRUE);
        CTabCtrl* pTab = (CTabCtrl*)pStats->GetDlgItem(IDC_STATS_TAB);
        if (pTab != nullptr)
        {
            pTab->InsertItem(STATS_TAB_RANK, g_data.StringRes(IDS_STATS_TAB_RANK));
            pTab->InsertItem(STATS_TAB_HISTORY, g_data.StringRes(IDS_STATS_TAB_HISTORY));
        }
        //排行筛选下拉框：今日/近7天/近30天/全部
        CComboBox* pCombo = (CComboBox*)pStats->GetDlgItem(IDC_STATS_RANGE_COMBO);
        if (pCombo != nullptr)
        {
            pCombo->AddString(g_data.StringRes(IDS_RANGE_TODAY));
            pCombo->AddString(g_data.StringRes(IDS_RANGE_7DAYS));
            pCombo->AddString(g_data.StringRes(IDS_RANGE_30DAYS));
            pCombo->AddString(g_data.StringRes(IDS_RANGE_ALL));
            pCombo->SetCurSel(0);
        }
        //排行列表：排名/按键/次数/占比
        CListCtrl* pRankList = (CListCtrl*)pStats->GetDlgItem(IDC_RANK_LIST);
        if (pRankList != nullptr)
        {
            pRankList->SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            pRankList->InsertColumn(0, g_data.StringRes(IDS_STATS_COL_RANK), LVCFMT_RIGHT, 36);
            pRankList->InsertColumn(1, g_data.StringRes(IDS_STATS_COL_KEY), LVCFMT_LEFT, 80);
            pRankList->InsertColumn(2, g_data.StringRes(IDS_STATS_COL_COUNT), LVCFMT_RIGHT, 60);
            pRankList->InsertColumn(3, g_data.StringRes(IDS_STATS_COL_PERCENT), LVCFMT_RIGHT, 56);
        }
        //每日统计列表：日期/按键数
        CListCtrl* pHistoryList = (CListCtrl*)pStats->GetDlgItem(IDC_HISTORY_LIST);
        if (pHistoryList != nullptr)
        {
            pHistoryList->SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            pHistoryList->InsertColumn(0, g_data.StringRes(IDS_STATS_COL_DATE), LVCFMT_LEFT, 110);
            pHistoryList->InsertColumn(1, g_data.StringRes(IDS_STATS_COL_COUNT), LVCFMT_RIGHT, 90);
        }
        //统一布局：页签显示区上部放键位覆盖图/柱状图(高约1/3)，下部放列表
        LayoutStatsTabArea(pStats, pTab);
        LayoutRankColumns(pRankList);
        LayoutHistoryColumns(pHistoryList);
    }

    //默认显示"显示设置"页
    if (pMainTab != nullptr)
        pMainTab->SetCurSel(MAIN_TAB_DISPLAY);
    SwitchMainTab();
    //统计开关的置灰状态与统计页初始内容
    ApplyStatsEnabled();
    UpdateStatsSummary();
    SwitchStatsTab();

    return TRUE;  // return TRUE unless you set the focus to a control
                  // 异常: OCX 属性页应返回 FALSE
}

//处理通知与命令消息：页签切换(主/统计页)、排行列表双击跳转、自绘控件绘制
BOOL COptionsDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
    NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
    if (header != nullptr)
    {
        //主页签与统计页内页签切换
        if (header->code == TCN_SELCHANGE)
        {
            if (header->idFrom == IDC_MAIN_TAB)
            {
                SwitchMainTab();
                *pResult = 0;
                return TRUE;
            }
            if (header->idFrom == IDC_STATS_TAB)
            {
                SwitchStatsTab();
                *pResult = 0;
                return TRUE;
            }
        }
        //每日统计列表双击：跳转到该天的按键明细(单日排行)
        if (header->code == NM_DBLCLK && header->idFrom == IDC_HISTORY_LIST)
        {
            CWnd* pStats = m_stats_page.get();
            CListCtrl* pList = (pStats != nullptr) ? (CListCtrl*)pStats->GetDlgItem(IDC_HISTORY_LIST) : nullptr;
            if (pList != nullptr)
            {
                int row = ((NMITEMACTIVATE*)lParam)->iItem;
                if (row >= 0)
                {
                    //取行数据中保存的真实日期(显示文本"今日"是本地化标签，不是日期)
                    std::wstring* date_ptr = reinterpret_cast<std::wstring*>(pList->GetItemData(row));
                    if (date_ptr == nullptr)
                        return TRUE;
                    m_single_day_date = *date_ptr;
                    m_single_day_mode = true;
                    //切换排行页签并把下拉框切到"今日"(单日模式下按日期覆盖)
                    CTabCtrl* pTab = (CTabCtrl*)pStats->GetDlgItem(IDC_STATS_TAB);
                    if (pTab != nullptr)
                        pTab->SetCurSel(STATS_TAB_RANK);
                    SwitchStatsTab();
                    return TRUE;
                }
            }
        }
    }
    return CDialog::OnNotify(wParam, lParam, pResult);
}

void COptionsDlg::SwitchMainTab()
{
    //主页签切换：只显示当前页对应的子页面对话框
    CTabCtrl* pMainTab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
    if (pMainTab == nullptr)
        return;
    const int sel = pMainTab->GetCurSel();
    if (m_display_page != nullptr)
        m_display_page->ShowWindow(sel == MAIN_TAB_DISPLAY ? SW_SHOW : SW_HIDE);
    if (m_stats_page != nullptr)
        m_stats_page->ShowWindow(sel == MAIN_TAB_STATS ? SW_SHOW : SW_HIDE);
}

void COptionsDlg::SwitchStatsTab()
{
    //统计页内页签切换：排行页显示键位覆盖图+排行列表，每日统计页显示柱状图+每日列表
    CWnd* pStats = m_stats_page.get();
    CTabCtrl* pTab = (pStats != nullptr) ? (CTabCtrl*)pStats->GetDlgItem(IDC_STATS_TAB) : nullptr;
    CWnd* pRank = (pStats != nullptr) ? pStats->GetDlgItem(IDC_RANK_LIST) : nullptr;
    CWnd* pHistory = (pStats != nullptr) ? pStats->GetDlgItem(IDC_HISTORY_LIST) : nullptr;
    CWnd* pMap = (pStats != nullptr) ? pStats->GetDlgItem(IDC_KEYBOARD_MAP_CTRL) : nullptr;
    CWnd* pChart = (pStats != nullptr) ? pStats->GetDlgItem(IDC_CHART_CTRL) : nullptr;
    if (pTab == nullptr)
        return;
    const int sel = pTab->GetCurSel();
    if (sel == STATS_TAB_RANK)
    {
        UpdateRankList();
        if (pRank != nullptr) pRank->ShowWindow(SW_SHOW);
        if (pMap != nullptr) pMap->ShowWindow(SW_SHOW);
        if (pHistory != nullptr) pHistory->ShowWindow(SW_HIDE);
        if (pChart != nullptr) pChart->ShowWindow(SW_HIDE);
    }
    else
    {
        UpdateHistoryList();
        if (pRank != nullptr) pRank->ShowWindow(SW_HIDE);
        if (pMap != nullptr) pMap->ShowWindow(SW_HIDE);
        if (pHistory != nullptr) pHistory->ShowWindow(SW_SHOW);
        if (pChart != nullptr) pChart->ShowWindow(SW_SHOW);
        //柱状图重绘
        if (pChart != nullptr)
            pChart->Invalidate();
    }
}

void COptionsDlg::ApplyStatsEnabled()
{
    //按统计开关状态置灰/恢复相关控件
    CWnd* pDisplay = m_display_page.get();
    CWnd* pStats = m_stats_page.get();
    if (pDisplay != nullptr)
    {
        CWnd* pShowCount = pDisplay->GetDlgItem(IDC_SHOW_KEY_COUNT_CHECK);
        CWnd* pCountWidth = pDisplay->GetDlgItem(IDC_KEY_COUNT_RESERVED_EDIT);
        CWnd* pShowKpm = pDisplay->GetDlgItem(IDC_SHOW_KPM_CHECK);
        CWnd* pKpmWidth = pDisplay->GetDlgItem(IDC_KPM_RESERVED_EDIT);
        const BOOL stats_on = m_data.enable_key_stats;
        if (pShowCount != nullptr)
            pShowCount->EnableWindow(stats_on);
        if (pCountWidth != nullptr)
            pCountWidth->EnableWindow(stats_on);
        if (pShowKpm != nullptr)
            pShowKpm->EnableWindow(stats_on);
        if (pKpmWidth != nullptr)
            pKpmWidth->EnableWindow(stats_on);
    }
    if (pStats != nullptr)
    {
        const BOOL enable = m_data.enable_key_stats;
        CWnd* controls[] = {
            pStats->GetDlgItem(IDC_RESET_STATS_BUTTON),
            pStats->GetDlgItem(IDC_STATS_TAB),
            pStats->GetDlgItem(IDC_RANK_LIST),
            pStats->GetDlgItem(IDC_HISTORY_LIST),
            pStats->GetDlgItem(IDC_STATS_RANGE_COMBO),
            pStats->GetDlgItem(IDC_STATS_REFRESH_BUTTON),
            pStats->GetDlgItem(IDC_EXPORT_CSV_BUTTON),
            pStats->GetDlgItem(IDC_DAILY_GOAL_EDIT),
            pStats->GetDlgItem(IDC_KEYBOARD_MAP_CTRL),
            pStats->GetDlgItem(IDC_CHART_CTRL),
        };
        for (CWnd* ctrl : controls)
        {
            if (ctrl != nullptr)
                ctrl->EnableWindow(enable);
        }
    }
}

void COptionsDlg::UpdateStatsSummary()
{
    //摘要行：累计/当日总数与打字速度(当前/峰值/平均KPM)；统计未启用时显示提示
    CWnd* pSummary = (m_stats_page != nullptr) ? m_stats_page->GetDlgItem(IDC_STATS_SUMMARY) : nullptr;
    if (pSummary == nullptr)
        return;
    if (!m_data.enable_key_stats)
    {
        pSummary->SetWindowTextW(g_data.StringRes(IDS_STATS_DISABLED_HINT));
        return;
    }
    unsigned long current_kpm = 0, peak_kpm = 0, avg_kpm = 0;
    CKeyboardHook::Instance().GetTypingSpeed(current_kpm, peak_kpm, avg_kpm);
    wchar_t buff[192];
    swprintf_s(buff, g_data.StringRes(IDS_STATS_SUMMARY_FMT).GetString(),
        g_data.GetStatsTotal(), g_data.GetStatsToday());
    wchar_t kpm_buff[96];
    swprintf_s(kpm_buff, g_data.StringRes(IDS_KPM_FMT).GetString(),
        current_kpm, peak_kpm, avg_kpm);
    const std::wstring text = std::wstring(buff) + L"\r\n" + kpm_buff;
    pSummary->SetWindowTextW(text.c_str());
}

void COptionsDlg::UpdateRankList()
{
    CWnd* pStats = m_stats_page.get();
    CListCtrl* pList = (pStats != nullptr) ? (CListCtrl*)pStats->GetDlgItem(IDC_RANK_LIST) : nullptr;
    if (pList == nullptr)
        return;
    //每次填充前重算列宽：此时列表已处最终尺寸，上一次填充产生的竖向滚动条
    //也已计入客户区，可保证四列总宽恰好不触发横向滚动条
    LayoutRankColumns(pList);
    pList->SetRedraw(FALSE);
    pList->DeleteAllItems();
    if (m_data.enable_key_stats)
    {
        g_data.UpdateKeyStats();
        std::vector<std::pair<std::wstring, unsigned long long>> detail;
        if (m_single_day_mode)
        {
            //单日跳转模式：显示指定日期的明细
            detail = g_data.GetStatsRankByDay(m_single_day_date);
        }
        else
        {
            //按筛选下拉框选中的时间范围聚合排行
            StatsRange range = StatsRange::Today;
            CComboBox* pCombo = (CComboBox*)pStats->GetDlgItem(IDC_STATS_RANGE_COMBO);
            int sel = (pCombo != nullptr) ? pCombo->GetCurSel() : 0;
            if (sel >= 0 && sel < static_cast<int>(_countof(RANGES)))
                range = RANGES[sel];
            detail = g_data.GetStatsRankByRange(range);
        }
        if (detail.empty())
        {
            int row = pList->InsertItem(0, L"");
            pList->SetItemText(row, 1, g_data.StringRes(IDS_STATS_EMPTY));
        }
        else
        {
            //占比的分母为筛选范围内的总按键数
            unsigned long long range_total = 0;
            for (const auto& item : detail)
                range_total += item.second;
            for (size_t i = 0; i < detail.size() && i < MAX_STATS_ROWS; ++i)
            {
                wchar_t rank_buff[16];
                wchar_t count_buff[24];
                wchar_t percent_buff[24];
                swprintf_s(rank_buff, L"%zu", i + 1);
                swprintf_s(count_buff, L"%llu", detail[i].second);
                double percent = 0.0;
                if (range_total > 0)
                    percent = static_cast<double>(detail[i].second) * 100.0 / static_cast<double>(range_total);
                swprintf_s(percent_buff, L"%.1f%%", percent);
                int row = pList->InsertItem(pList->GetItemCount(), rank_buff);
                pList->SetItemText(row, 1, detail[i].first.c_str());
                pList->SetItemText(row, 2, count_buff);
                pList->SetItemText(row, 3, percent_buff);
            }
        }
    }
    pList->SetRedraw(TRUE);
    pList->Invalidate();
    //键位覆盖图与排行数据同步刷新(显示当前筛选范围的按键覆盖情况)
    CWnd* pStats2 = m_stats_page.get();
    if (pStats2 != nullptr)
    {
        CWnd* pMap = pStats2->GetDlgItem(IDC_KEYBOARD_MAP_CTRL);
        if (pMap != nullptr)
            pMap->Invalidate();
    }
}

void COptionsDlg::UpdateHistoryList()
{
    CWnd* pStats = m_stats_page.get();
    CListCtrl* pList = (pStats != nullptr) ? (CListCtrl*)pStats->GetDlgItem(IDC_HISTORY_LIST) : nullptr;
    if (pList == nullptr)
        return;
    //释放上次填充时挂在行数据上的日期字符串
    for (int row = pList->GetItemCount() - 1; row >= 0; --row)
    {
        std::wstring* date_ptr = reinterpret_cast<std::wstring*>(pList->GetItemData(row));
        delete date_ptr;
    }
    //每次填充前重算列宽(与排行表同理)
    LayoutHistoryColumns(pList);
    pList->SetRedraw(FALSE);
    pList->DeleteAllItems();
    if (m_data.enable_key_stats)
    {
        g_data.UpdateKeyStats();
        //历史按日期升序存储，展示时最新在前；今日数据单独列在最前
        const unsigned long long today = g_data.GetStatsToday();
        if (today > 0)
        {
            wchar_t count_buff[24];
            swprintf_s(count_buff, L"%llu", today);
            int row = pList->InsertItem(0, g_data.StringRes(IDS_RANGE_TODAY));
            pList->SetItemText(row, 1, count_buff);
            //"今日"行显示的是本地化标签，lParam保存真实日期供单日跳转查询
            pList->SetItemData(row, reinterpret_cast<DWORD_PTR>(new std::wstring(g_data.GetTodayDate())));
        }
        const std::vector<DailyKeyStat> history = g_data.GetStatsHistory();
        for (size_t i = history.size(); i > 0; --i)
        {
            wchar_t count_buff[24];
            swprintf_s(count_buff, L"%llu", history[i - 1].total);
            int row = pList->InsertItem(pList->GetItemCount(), history[i - 1].date.c_str());
            pList->SetItemText(row, 1, count_buff);
            pList->SetItemData(row, reinterpret_cast<DWORD_PTR>(new std::wstring(history[i - 1].date)));
        }
        if (pList->GetItemCount() == 0)
        {
            int row = pList->InsertItem(0, L"");
            pList->SetItemText(row, 0, g_data.StringRes(IDS_STATS_EMPTY));
        }
    }
    pList->SetRedraw(TRUE);
    pList->Invalidate();
    CWnd* pStats2 = m_stats_page.get();
    if (pStats2 != nullptr)
    {
        CWnd* pChart = pStats2->GetDlgItem(IDC_CHART_CTRL);
        if (pChart != nullptr)
            pChart->Invalidate();
    }
}

void COptionsDlg::ReadDisplayPageSettings()
{
    //从"显示设置"页读取全部设置
    CWnd* pDisplay = m_display_page.get();
    if (pDisplay == nullptr)
        return;
    m_data.show_caps_lock = (pDisplay->IsDlgButtonChecked(IDC_SHOW_CAPS_LOCK_CHECK) != 0);
    m_data.show_num_lock = (pDisplay->IsDlgButtonChecked(IDC_SHOW_NUM_LOCK_CHECK) != 0);
    m_data.show_scroll_lock = (pDisplay->IsDlgButtonChecked(IDC_SHOW_SCROLL_LOCK_CHECK) != 0);
    m_data.draw_round_rect = (pDisplay->IsDlgButtonChecked(IDC_DRAW_ROUND_RECT_CHECK) != 0);
    m_data.show_pressed_key = (pDisplay->IsDlgButtonChecked(IDC_SHOW_PRESSED_KEY_CHECK) != 0);
    m_data.show_key_count = (pDisplay->IsDlgButtonChecked(IDC_SHOW_KEY_COUNT_CHECK) != 0);
    BOOL success = FALSE;
    //带符号读取，负值按0处理(无符号读取时负值会回绕成巨大的数)
    int show_time = pDisplay->GetDlgItemInt(IDC_PRESSED_KEY_SHOW_TIME_EDIT, &success, TRUE);
    if (!success)
        show_time = 1000;
    if (show_time < 0)
        show_time = 0;
    if (show_time > 10000)
        show_time = 10000;
    m_data.pressed_key_show_time = show_time;
    int reserved_width = pDisplay->GetDlgItemInt(IDC_PRESSED_KEY_RESERVED_EDIT, &success, TRUE);
    if (!success)
        reserved_width = 90;
    if (reserved_width < 0)
        reserved_width = 0;
    if (reserved_width > 300)
        reserved_width = 300;
    m_data.pressed_key_reserved_width = reserved_width;
    int count_width = pDisplay->GetDlgItemInt(IDC_KEY_COUNT_RESERVED_EDIT, &success, TRUE);
    if (!success)
        count_width = 60;
    if (count_width < 0)
        count_width = 0;
    if (count_width > 300)
        count_width = 300;
    m_data.key_count_reserved_width = count_width;
    m_data.show_kpm = (pDisplay->IsDlgButtonChecked(IDC_SHOW_KPM_CHECK) != 0);
    int kpm_width = pDisplay->GetDlgItemInt(IDC_KPM_RESERVED_EDIT, &success, TRUE);
    if (!success)
        kpm_width = 70;
    if (kpm_width < 0)
        kpm_width = 0;
    if (kpm_width > 300)
        kpm_width = 300;
    m_data.kpm_reserved_width = kpm_width;
}

void COptionsDlg::OnOK()
{
    ReadDisplayPageSettings();
    //统计开关与每日目标在"按键统计"页
    CWnd* pStats = m_stats_page.get();
    if (pStats != nullptr)
    {
        m_data.enable_key_stats = (pStats->IsDlgButtonChecked(IDC_ENABLE_KEY_STATS_CHECK) != 0);
        BOOL success = FALSE;
        int daily_goal = pStats->GetDlgItemInt(IDC_DAILY_GOAL_EDIT, &success, TRUE);
        if (!success)
            daily_goal = 0;
        if (daily_goal < 0)
            daily_goal = 0;
        if (daily_goal > 1000000)
            daily_goal = 1000000;
        m_data.daily_goal = daily_goal;
    }

    //所有显示项都被关闭时给出警告(任务栏计数或KPM显示开启且统计启用时也算有显示内容)
    if (!m_data.show_caps_lock && !m_data.show_num_lock && !m_data.show_scroll_lock
        && !m_data.show_pressed_key
        && !(m_data.enable_key_stats && (m_data.show_key_count || m_data.show_kpm)))
    {
        MessageBox(g_data.StringRes(IDS_SELECT_AT_LEAST_WARNING), NULL, MB_ICONWARNING | MB_OK);
        return;
    }

    CDialog::OnOK();
}

void COptionsDlg::OnBnClickedEnableKeyStatsCheck()
{
    //统计开关在"按键统计"页切换；同步状态并刷新。
    //立即写全局开关：UpdateKeyStats读取的是全局值，不同步会导致勾选后到确定前
    //跨天归档被跳过、摘要与表格读到旧数据
    CWnd* pStats = m_stats_page.get();
    if (pStats != nullptr)
        m_data.enable_key_stats = (pStats->IsDlgButtonChecked(IDC_ENABLE_KEY_STATS_CHECK) != 0);
    g_data.m_setting_data.enable_key_stats.store(m_data.enable_key_stats, std::memory_order_relaxed);
    if (m_data.enable_key_stats)
        g_data.UpdateKeyStats();
    ApplyStatsEnabled();
    UpdateStatsSummary();
    SwitchStatsTab();
}

void COptionsDlg::OnBnClickedResetStatsButton()
{
    //清零统计数据需确认，操作不可恢复
    if (MessageBox(g_data.StringRes(IDS_RESET_STATS_CONFIRM), NULL, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;
    g_data.ClearAllStats();
    m_single_day_mode = false;
    UpdateStatsSummary();
    UpdateRankList();
    UpdateHistoryList();
}

void COptionsDlg::OnStatsRangeChanged()
{
    //筛选范围变化时退出单日模式并重刷排行
    m_single_day_mode = false;
    UpdateRankList();
}

void COptionsDlg::OnRefreshStats()
{
    //手动刷新：重新同步统计并重刷当前页签内容
    if (m_data.enable_key_stats)
        g_data.UpdateKeyStats();
    UpdateStatsSummary();
    SwitchStatsTab();
}

void COptionsDlg::OnExportCsv()
{
    //导出每日统计CSV：弹保存对话框选择位置
    OPENFILENAMEW ofn{};
    wchar_t file_buff[MAX_PATH] = L"KeyboardStats.csv";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetSafeHwnd();
    ofn.lpstrFilter = L"CSV文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file_buff;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"csv";
    if (GetSaveFileNameW(&ofn))
    {
        if (!g_data.ExportStatsCsv(file_buff))
            MessageBox(L"导出CSV失败，无法写入所选文件。", NULL, MB_ICONWARNING | MB_OK);
    }
}

void COptionsDlg::DrawKeyboardMap(CDC* pDC, CRect rect)
{
    //键位覆盖图：60%透明底色的小键盘布局，统计范围内按过的键高亮显示次数
    //无论是否有数据都画出完整键盘底图：没按过的键浅灰底灰字(60%透明观感)
    pDC->FillSolidRect(rect, ::GetSysColor(COLOR_WINDOW));
    //取当前筛选的排行数据(与排行列表同源)
    std::map<std::wstring, unsigned long long> counts;
    if (m_data.enable_key_stats)
    {
        if (m_single_day_mode)
        {
            for (const auto& item : g_data.GetStatsRankByDay(m_single_day_date))
                counts[item.first] = item.second;
        }
        else
        {
            CComboBox* pCombo = (CComboBox*)m_stats_page->GetDlgItem(IDC_STATS_RANGE_COMBO);
            int sel = (pCombo != nullptr) ? pCombo->GetCurSel() : 0;
            StatsRange range = (sel >= 0 && sel < static_cast<int>(_countof(RANGES))) ? RANGES[sel] : StatsRange::Today;
            for (const auto& item : g_data.GetStatsRankByRange(range))
                counts[item.first] = item.second;
        }
    }
    unsigned long long max_count = 0;
    for (const auto& item : counts)
    {
        if (item.second > max_count)
            max_count = item.second;
    }
    const int rows = KEYMAP_ROW_COUNT;
    const int cols = 12;       //数字排含 -/=/Backspace 共12键
    const int gap = 2;
    int key_w = (rect.Width() - gap * (cols - 1)) / cols;
    int key_h = (rect.Height() - gap * (rows - 1)) / rows;
    if (key_w < 8 || key_h < 8)
        return;
    CFont* old_font = nullptr;
    CFont small_font;
    //键帽字号占格高62%(原50%在高DPI下过小难以辨认)，并保底12px
    int font_h = key_h * 62 / 100;
    if (font_h < 12)
        font_h = 12;
    if (small_font.CreateFont(-font_h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, _T("Segoe UI")))
        old_font = pDC->SelectObject(&small_font);
    pDC->SetBkMode(TRANSPARENT);
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            UINT vk = KEYMAP_ROWS[r][c];
            if (vk == 0)
                continue;
            //所有键一律等宽等距，避免长名键挤占邻键；每行恰好铺满一行格子
            CRect key_rect(rect.left + c * (key_w + gap), rect.top + r * (key_h + gap),
                rect.left + c * (key_w + gap) + key_w, rect.top + r * (key_h + gap) + key_h);
            //统计名与键帽显示名分开：counts的键是GetKeyName产生的统计名
            //(如CapsLk/Backspace)，键帽上显示短名(如Caps/Bksp)，
            //查找必须用统计名，否则这些键有数据却永远不高亮
            const std::wstring stat_name = CKeyboardHook::GetKeyName(vk, 0, false);
            const std::wstring label = KeyMapLabel(vk);
            auto iter = counts.find(stat_name);
            if (iter != counts.end() && iter->second > 0)
            {
                //按过的键：蓝色高亮，强度随次数加深
                const double intensity = 0.35 + 0.65 * static_cast<double>(iter->second) / static_cast<double>(max_count);
                //GDI无alpha混合，用浅蓝到深蓝近似
                COLORREF fill = RGB(static_cast<int>(255 - 225 * intensity),
                    static_cast<int>(255 - 165 * intensity), 255);
                CBrush brush(fill);
                pDC->FillRect(key_rect, &brush);
                pDC->SetTextColor(RGB(255, 255, 255));
            }
            else
            {
                //没按过的键：浅灰底、灰字(模拟60%透明)
                CBrush brush(RGB(235, 236, 240));
                pDC->FillRect(key_rect, &brush);
                pDC->SetTextColor(RGB(120, 120, 120));
            }
            pDC->DrawText(label.c_str(), key_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    if (old_font != nullptr)
        pDC->SelectObject(old_font);
}

void COptionsDlg::DrawDailyChart(CDC* pDC, CRect rect)
{
    //每日统计柱状图：最近若干天(最多30天)键入量趋势。
    //owner-draw静态控件不自动擦背景，先填充背景避免统计关闭时残留旧图
    pDC->FillSolidRect(rect, ::GetSysColor(COLOR_WINDOW));
    if (!m_data.enable_key_stats)
        return;
    //数据：今日+历史(展示最多30条，最新在右)
    std::vector<std::pair<std::wstring, unsigned long long>> days;
    const unsigned long long today = g_data.GetStatsToday();
    const std::vector<DailyKeyStat> history = g_data.GetStatsHistory();
    for (const auto& day : history)
        days.emplace_back(day.date, day.total);
    if (today > 0)
        days.emplace_back(std::wstring(g_data.StringRes(IDS_RANGE_TODAY)), today);
    if (days.empty())
    {
        pDC->SetTextColor(RGB(120, 120, 120));
        pDC->DrawText(g_data.StringRes(IDS_STATS_EMPTY), rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (days.size() > 30)
        days.erase(days.begin(), days.end() - 30);
    unsigned long long max_value = 1;
    for (const auto& day : days)
    {
        if (day.second > max_value)
            max_value = day.second;
    }
    const int bar_count = static_cast<int>(days.size());
    //柱宽封顶40px(单日数据时不再铺满整块)，天数多时按可用宽度收窄；整组水平居中
    const int gap = 6;
    int bar_w = 40;
    if (bar_count > 1)
    {
        const int fit = (rect.Width() - 8 - gap * (bar_count - 1)) / bar_count;
        if (fit < bar_w)
            bar_w = fit;
    }
    if (bar_w < 3)
        bar_w = 3;
    //柱子较宽且天数不多时显示数值与日期标注
    const bool show_value = bar_w >= 22;
    const bool show_date = bar_w >= 30 && bar_count <= 15;
    //标注字号按绘图DC的DPI缩放(此前固定11-14px在175%缩放下偏小且行高不足)，
    //同时受控件高度约束
    const int dpi = GetDeviceCaps(pDC->GetSafeHdc(), LOGPIXELSY);
    int font_h = 12 * (dpi > 0 ? dpi : 96) / 96;
    if (font_h < 11)
        font_h = 11;
    const int max_font = rect.Height() / 5;
    if (font_h > max_font)
        font_h = max_font > 11 ? max_font : 11;
    //标注区高度与字号联动：日期标签需要完整的文字行高，避免被控件底边裁掉
    const int label_h = show_date ? font_h + 8 : 2;
    const int baseline = rect.bottom - label_h;
    const int draw_h = baseline - rect.top;
    if (draw_h < 12)
        return;
    //顶部余量：保证最高柱不贴顶，数值标注也有空间
    int top_pad = draw_h * 12 / 100;
    if (show_value && top_pad < font_h + 8)
        top_pad = font_h + 8;
    const int usable_h = draw_h - top_pad;
    if (usable_h < 10)
        return;

    //标注字体(数值/日期共用)
    CFont* pOldFont = nullptr;
    CFont label_font;
    if (label_font.CreateFont(-font_h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, _T("Segoe UI")))
        pOldFont = pDC->SelectObject(&label_font);
    pDC->SetBkMode(TRANSPARENT);

    //基线
    {
        CPen pen(PS_SOLID, 1, RGB(200, 200, 200));
        CPen* pOldPen = pDC->SelectObject(&pen);
        pDC->MoveTo(rect.left + 2, baseline);
        pDC->LineTo(rect.right - 2, baseline);
        pDC->SelectObject(pOldPen);
    }

    const int goal = m_data.daily_goal;
    const int total_w = bar_count * bar_w + gap * (bar_count - 1);
    int x = rect.left + (rect.Width() - total_w) / 2;
    for (int i = 0; i < bar_count; ++i)
    {
        int bar_h = static_cast<int>(static_cast<double>(days[i].second) * usable_h / static_cast<double>(max_value));
        if (bar_h < 2 && days[i].second > 0)
            bar_h = 2;
        CRect bar(x, baseline - bar_h, x + bar_w, baseline);
        COLORREF color = RGB(70, 130, 220);     //普通柱：蓝色
        if (goal > 0 && days[i].second >= static_cast<unsigned long long>(goal))
            color = RGB(60, 170, 80);           //达标柱：绿色
        CBrush brush(color);
        pDC->FillRect(bar, &brush);
        //柱顶数值标注(区高font_h+4，配合顶部余量不会被裁)
        if (show_value)
        {
            wchar_t value_text[24];
            swprintf_s(value_text, L"%llu", days[i].second);
            CRect value_rect(x - 10, bar.top - font_h - 4, x + bar_w + 10, bar.top);
            pDC->SetTextColor(RGB(90, 90, 90));
            pDC->DrawText(value_text, value_rect, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
        }
        //柱下日期标注(仅天数不多时显示；yyyy-MM-dd 缩为 MM-dd，今日显示"今日")
        if (show_date)
        {
            std::wstring date_label = days[i].first;
            if (date_label.size() == 10 && date_label[4] == L'-')
                date_label = date_label.substr(5);
            //使用完整标注区(label_h=font_h+8)并垂直居中，保证不被控件底边裁剪
            CRect date_rect(x - 10, baseline, x + bar_w + 10, rect.bottom);
            pDC->SetTextColor(RGB(120, 120, 120));
            pDC->DrawText(date_label.c_str(), date_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        x += bar_w + gap;
    }
    //目标线(启用每日目标且目标值在可视范围内时画绿色虚线)
    if (goal > 0 && static_cast<unsigned long long>(goal) <= max_value)
    {
        const int goal_y = baseline - static_cast<int>(static_cast<double>(goal) * usable_h / static_cast<double>(max_value));
        CPen pen(PS_DASH, 1, RGB(60, 170, 80));
        CPen* old_pen = pDC->SelectObject(&pen);
        pDC->MoveTo(rect.left + 2, goal_y);
        pDC->LineTo(rect.right - 2, goal_y);
        pDC->SelectObject(old_pen);
    }
    if (pOldFont != nullptr)
        pDC->SelectObject(pOldFont);
}
