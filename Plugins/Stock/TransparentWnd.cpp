#include "pch.h"
#include "TransparentWnd.h"
#include "FloatingWnd.h"
#include <Stock.h>

BEGIN_MESSAGE_MAP(CTransparentWnd, CWnd)
ON_WM_LBUTTONDOWN()
ON_WM_ERASEBKGND()
ON_WM_CREATE()
END_MESSAGE_MAP()

int CTransparentWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    // 添加调试输出
    TRACE(L"CTransparentWnd Created\n");
    return 0;
}

CTransparentWnd::CTransparentWnd() : m_pParent(nullptr)
{
}

void CTransparentWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
    // 添加调试输出
    TRACE(L"CTransparentWnd OnLButtonDown\n");

    // 获取鼠标位置
    CPoint ptScreen;
    GetCursorPos(&ptScreen);

    // 获取浮动窗口区域
    CRect rcFloat;
    if (m_pParent && m_pParent->GetSafeHwnd())
    {
        m_pParent->GetWindowRect(rcFloat);
        if (!rcFloat.PtInRect(ptScreen))
        {
            //点击在浮窗外区域。不能在这里同步销毁窗口：DestroyWindow会连带销毁浮窗，
            //随后DestroyFloatingWnd在"本消息处理函数仍在执行的透明窗对象"栈帧内
            //delete其所属CFloatingWnd，构成对象存活期UB。改为向浮窗投递关闭消息，
            //在浮窗OnCloseRequest里只销毁两个HWND，C++对象延迟到下次显示/析构时统一delete
            TRACE(L"Requesting floating window close\n");
            m_pParent->PostMessage(FWND_MSG_CLOSE, 0, 0);
        }
        else
        {
            // 如果点击在浮动窗口内部，将消息传递给浮动窗口
            m_pParent->SendMessage(WM_LBUTTONDOWN, nFlags, MAKELPARAM(point.x, point.y));
        }
    }
}

BOOL CTransparentWnd::OnEraseBkgnd(CDC *pDC)
{
    return TRUE; // 不擦除背景
}
