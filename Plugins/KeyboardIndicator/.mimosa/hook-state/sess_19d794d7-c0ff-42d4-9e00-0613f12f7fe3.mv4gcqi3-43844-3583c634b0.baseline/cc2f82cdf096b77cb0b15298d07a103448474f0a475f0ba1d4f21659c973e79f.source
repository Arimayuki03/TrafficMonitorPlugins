#pragma once
#include "DataManager.h"
#include <memory>

class COptionsDlg;

//统计页子对话框：键位覆盖图与柱状图是owner-draw静态控件，
//WM_DRAWITEM发送给直接父窗口(本对话框)，在这里接收并委托主对话框绘制
class CStatsPageDlg : public CDialog
{
    DECLARE_DYNAMIC(CStatsPageDlg)
public:
    CStatsPageDlg(COptionsDlg* owner);
    COptionsDlg* m_owner;
protected:
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
    afx_msg BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);    //把统计页控件通知转发给主对话框
    afx_msg BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);    //把命令路由给主对话框
    DECLARE_MESSAGE_MAP()
};

// COptionsDlg 对话框：主页签切换"显示设置/按键统计"两个子页面
class COptionsDlg : public CDialog
{
    DECLARE_DYNAMIC(COptionsDlg)

public:
    COptionsDlg(CWnd* pParent = nullptr);   // 标准构造函数
    virtual ~COptionsDlg();

    SettingData m_data;

    //对话框数据
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_OPTIONS_DIALOG };
#endif

private:
    //把子页面对话框嵌入主对话框(位置对齐主页签显示区)，返回创建的页面对话框
    CDialog* CreateSubPage(UINT template_id, bool stats_page);
    //主页签切换：显示当前页对应的子页面对话框
    void SwitchMainTab();
    //填充"按键排行"列表(排名/按键/次数/占比)，按当前筛选范围聚合
    void UpdateRankList();
    //填充"每日统计"列表(日期/当日按键数)
    void UpdateHistoryList();
    //更新统计摘要文本(累计/当日/打字速度)
    void UpdateStatsSummary();
    //按当前页签显示对应的控件(排行列表+键位覆盖图 或 每日统计列表+柱状图)，并刷新内容
    void SwitchStatsTab();
    //应用统计开关状态：置灰/恢复统计页与显示页的相关控件
    void ApplyStatsEnabled();
    //读取"显示设置"页的全部设置到m_data
    void ReadDisplayPageSettings();

public:
    //键位覆盖图绘制(60%透明的小键盘布局，按过的键高亮)，由统计页owner-draw回调
    void DrawKeyboardMap(CDC* pDC, CRect rect);
    //每日统计柱状图绘制(最近若干天键入量趋势)，由统计页owner-draw回调
    void DrawDailyChart(CDC* pDC, CRect rect);

private:
    //排行当前筛选是否为"单日跳转"模式(由每日统计列表点击设置)
    bool m_single_day_mode{ false };
    std::wstring m_single_day_date;         //单日跳转选中的日期

    //显示设置子页面与按键统计子页面
    std::unique_ptr<CDialog> m_display_page;
    std::unique_ptr<CDialog> m_stats_page;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

public:
    virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);    //public: 供统计页子对话框转发通知

private:

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    afx_msg void OnDestroy();
    afx_msg void OnBnClickedEnableKeyStatsCheck();
    afx_msg void OnBnClickedResetStatsButton();
    afx_msg void OnStatsRangeChanged();
    afx_msg void OnRefreshStats();
    afx_msg void OnExportCsv();
};
