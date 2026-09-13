#pragma once
#include "Stock.h"
#include "StockItem.h"
#include <string>
#include "PluginInterface.h"
#include "ManagerDialog.h"
#include <map>
#include <vector>
#include <mutex>
#include <atomic>

constexpr auto kSH = L"sh";    // 上海
constexpr auto kSZ = L"sz";    // 深圳
constexpr auto kHK = L"rt_hk"; // 香港
constexpr auto kMG = L"gb_";   // 美国
constexpr auto kBJ = L"bj";    // 北京
constexpr auto kNF = L"nf";    // 国内期货
constexpr auto kHF = L"hf";    // 海外期货

const std::vector<CString> StockTypeSet{kSH, kSZ, kHK, kMG, kBJ};

#define Stock_ITEM_MAX 10

class Stock : public ITMPlugin
{
private:
    Stock();
    virtual ~Stock();

public:
    static Stock &Instance();

    virtual IPluginItem *GetItem(int index) override;
    virtual const wchar_t *GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void *hParent) override;
    virtual const wchar_t *GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t *data) override;
    virtual int GetCommandCount() override;
    virtual const wchar_t *GetCommandName(int command_index) override;
    virtual void OnPluginCommand(int command_index, void *hWnd, void *para) override;
    virtual void *GetPluginIcon() override;

    INT_PTR ShowStockManageDlg(CWnd *pWnd);
    void SendStockInfoRequest();
    void ShowContextMenu(CWnd *pWnd);
    void DisableUpdateCommand();
    void EnableUpdateCommand();

    void ShowFloatingWnd(void *hWnd, CPoint ptScreen, std::wstring stock_id);
    void DestroyFloatingWnd();
    void UpdateKLine();

public:
    //保护股票数据(stocks map和StockInfo)的互斥量。
    //后台刷新线程与UI线程都会访问这些数据，必须用recursive_mutex保护(部分锁内操作会再次加锁)
    std::recursive_mutex m_stockDataMutex;
    //分时数据(悬浮窗K线)下载线程是否正在运行
    std::atomic<bool> m_timeline_thread_running{ false };
    //保护悬浮窗对象生命周期(m_pFloatingWnd的创建与销毁)。悬浮窗网络线程销毁前检查时也要加锁
    std::mutex m_wndMutex;

private:
    static UINT ThreadCallback(LPVOID dwUser);
    void LoadContextMenu();
    void updateItems();

private:
    static Stock m_instance;
    vector<StockItem> m_items;

    std::atomic<bool> m_is_thread_runing{ false };
    CManagerDialog *m_option_dlg{};         // 保存选项设置对话框的句柄
    std::atomic<unsigned __int64> m_last_request_time{ 0 }; // 上次请求的时间
    std::atomic<bool> m_update_in_progress{ false };        // 是否正在更新行情(用于在UI线程中刷新菜单状态)
    CMenu m_menu;

    CFloatingWnd *m_pFloatingWnd;
};

#ifdef __cplusplus
extern "C"
{
#endif
    __declspec(dllexport) ITMPlugin *TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
