// OptionsDlg.cpp: 实现文件
//

#include "pch.h"
#include "KeyboardIndicator.h"
#include "OptionsDlg.h"
#include "afxdialogex.h"
#include "DataManager.h"

// COptionsDlg 对话框

IMPLEMENT_DYNAMIC(COptionsDlg, CDialog)

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


BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
END_MESSAGE_MAP()


// COptionsDlg 消息处理程序


BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetIcon(g_data.GetIcon(IDI_ICON1), FALSE);

    CheckDlgButton(IDC_SHOW_CAPS_LOCK_CHECK, m_data.show_caps_lock);
    CheckDlgButton(IDC_SHOW_NUM_LOCK_CHECK, m_data.show_num_lock);
    CheckDlgButton(IDC_SHOW_SCROLL_LOCK_CHECK, m_data.show_scroll_lock);
    CheckDlgButton(IDC_DRAW_ROUND_RECT_CHECK, m_data.draw_round_rect);
    CheckDlgButton(IDC_SHOW_PRESSED_KEY_CHECK, m_data.show_pressed_key);
    SetDlgItemInt(IDC_PRESSED_KEY_SHOW_TIME_EDIT, m_data.pressed_key_show_time, FALSE);
    SetDlgItemInt(IDC_PRESSED_KEY_RESERVED_EDIT, m_data.pressed_key_reserved_width, TRUE);

    return TRUE;  // return TRUE unless you set the focus to a control
                  // 异常: OCX 属性页应返回 FALSE
}


void COptionsDlg::OnOK()
{
    m_data.show_caps_lock = (IsDlgButtonChecked(IDC_SHOW_CAPS_LOCK_CHECK) != 0);
    m_data.show_num_lock = (IsDlgButtonChecked(IDC_SHOW_NUM_LOCK_CHECK) != 0);
    m_data.show_scroll_lock = (IsDlgButtonChecked(IDC_SHOW_SCROLL_LOCK_CHECK) != 0);
    m_data.draw_round_rect = (IsDlgButtonChecked(IDC_DRAW_ROUND_RECT_CHECK) != 0);
    m_data.show_pressed_key = (IsDlgButtonChecked(IDC_SHOW_PRESSED_KEY_CHECK) != 0);
    //读取显示时长，限制在0~10000毫秒
    BOOL success = FALSE;
    UINT show_time = GetDlgItemInt(IDC_PRESSED_KEY_SHOW_TIME_EDIT, &success, FALSE);
    if (!success)
        show_time = 1000;
    if (show_time > 10000)
        show_time = 10000;
    m_data.pressed_key_show_time = static_cast<int>(show_time);
    //读取按键区预留宽度，限制在0~300像素
    UINT reserved_width = GetDlgItemInt(IDC_PRESSED_KEY_RESERVED_EDIT, &success, TRUE);
    if (!success)
        reserved_width = 90;
    if (reserved_width > 300)
        reserved_width = 300;
    m_data.pressed_key_reserved_width = static_cast<int>(reserved_width);

    //所有显示项都被关闭时给出警告
    if (!m_data.show_caps_lock && !m_data.show_num_lock && !m_data.show_scroll_lock && !m_data.show_pressed_key)
    {
        MessageBox(g_data.StringRes(IDS_SELECT_AT_LEAST_WARNING), NULL, MB_ICONWARNING | MB_OK);
        return;
    }

    CDialog::OnOK();
}
