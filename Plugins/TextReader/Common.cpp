#include "pch.h"
#include "Common.h"
#include <afxinet.h>    //用于支持使用网络相关的类
#include <algorithm>

std::wstring CCommon::StrToUnicode(const char* str, bool utf8)
{
    if (str == nullptr)
        return std::wstring();
    std::wstring result;
    int size;
    size = MultiByteToWideChar((utf8 ? CP_UTF8 : CP_ACP), 0, str, -1, NULL, 0);
    if (size <= 0) return std::wstring();
    wchar_t* str_unicode = new wchar_t[size + 1];
    MultiByteToWideChar((utf8 ? CP_UTF8 : CP_ACP), 0, str, -1, str_unicode, size);
    result.assign(str_unicode);
    delete[] str_unicode;
    return result;
}

std::string CCommon::UnicodeToStr(const wchar_t* wstr, bool utf8)
{
    if (wstr == nullptr)
        return std::string();
    std::string result;
    int size{ 0 };
    size = WideCharToMultiByte((utf8 ? CP_UTF8 : CP_ACP), 0, wstr, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    char* str = new char[size + 1];
    WideCharToMultiByte((utf8 ? CP_UTF8 : CP_ACP), 0, wstr, -1, str, size, NULL, NULL);
    result.assign(str);
    delete[] str;
    return result;
}

bool CCommon::IsUTF8Bytes(const char* data)
{
    int charByteCounter = 1;  //计算当前正分析的字符应还有的字节数
    unsigned char curByte; //当前分析的字节.
    bool ascii = true;
    int length = static_cast<int>(strlen(data));
    for (int i = 0; i < length; i++)
    {
        curByte = static_cast<unsigned char>(data[i]);
        if (charByteCounter == 1)
        {
            if (curByte >= 0x80)
            {
                ascii = false;
                //判断当前
                while (((curByte <<= 1) & 0x80) != 0)
                {
                    charByteCounter++;
                }
                //标记位首位若为非0 则至少以2个1开始 如:110XXXXX...........1111110X
                if (charByteCounter == 1 || charByteCounter > 6)
                {
                    return false;
                }
            }
        }
        else
        {
            //若是UTF-8 此时第一位必须为1
            if ((curByte & 0xC0) != 0x80)
            {
                return false;
            }
            charByteCounter--;
        }
    }
    if (ascii) return false;        //如果全是ASCII字符，返回false
    else return true;
}

std::wstring CCommon::ConvertToUnicode(const std::string& content)
{
    if (content.size() >= 2)
    {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(content.data());
        //UTF-16 LE BOM
        if (data[0] == 0xFF && data[1] == 0xFE)
        {
            std::wstring result((content.size() - 2) / 2, L'\0');
            if (!result.empty())
                memcpy(&result[0], content.data() + 2, result.size() * 2);
            return result;
        }
        //UTF-16 BE BOM
        if (data[0] == 0xFE && data[1] == 0xFF)
        {
            std::wstring result((content.size() - 2) / 2, L'\0');
            for (size_t i = 0; i < result.size(); i++)
                result[i] = static_cast<wchar_t>((data[2 + i * 2] << 8) | data[3 + i * 2]);
            return result;
        }
        //UTF-8 BOM，去除BOM后按UTF-8转换
        if (content.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        {
            return StrToUnicode(content.c_str() + 3, true);
        }
    }
    bool is_utf8 = IsUTF8Bytes(content.c_str());    //判断编码类型
    return StrToUnicode(content.c_str(), is_utf8);  //转换成Unicode
}

void CCommon::StringSplit(const std::wstring& str, const std::wstring& div_str, std::vector<std::wstring>& results, bool skip_empty)
{
    results.clear();
    size_t split_index = 0 - div_str.size();
    size_t last_split_index = 0 - div_str.size();
    while (true)
    {
        split_index = str.find(div_str, split_index + div_str.size());
        std::wstring split_str = str.substr(last_split_index + div_str.size(), split_index - last_split_index - div_str.size());
        if (!split_str.empty() || !skip_empty)
            results.push_back(split_str);
        if (split_index == std::wstring::npos)
            break;
        last_split_index = split_index;
    }
}

bool CCommon::GetFileLastModified(const std::wstring& file_path, unsigned __int64& modified_time)
{
    WIN32_FILE_ATTRIBUTE_DATA file_attributes{};
    if (GetFileAttributesEx(file_path.c_str(), GetFileExInfoStandard, &file_attributes))
    {
        ULARGE_INTEGER last_modified_time{};
        last_modified_time.HighPart = file_attributes.ftLastWriteTime.dwHighDateTime;
        last_modified_time.LowPart = file_attributes.ftLastWriteTime.dwLowDateTime;
        modified_time = last_modified_time.QuadPart;
        return true;
    }
    return false;
}


bool CCommon::GetURL(const std::wstring& url, std::string& result, bool utf8, const std::wstring& user_agent)
{
    bool succeed{ false };
    CInternetSession* pSession{};
    CHttpFile* pfile{};
    try
    {
        pSession = new CInternetSession(user_agent.c_str());
        pfile = (CHttpFile*)pSession->OpenURL(url.c_str());
        DWORD dwStatusCode;
        pfile->QueryInfoStatusCode(dwStatusCode);
        if (dwStatusCode == HTTP_STATUS_OK)
        {
            //按原始字节读取响应体。不能把Unicode构建的CString按char*重解释（见修复文档FIX-003）
            result.clear();
            char buff[4096];
            UINT read_count = 0;
            while ((read_count = pfile->Read(buff, sizeof(buff))) > 0)
            {
                result.append(buff, read_count);
            }
            //与原按行读取的行为保持一致：去掉换行符
            result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            succeed = true;
        }
        pfile->Close();
        delete pfile;
        pfile = nullptr;
        pSession->Close();
    }
    catch (CInternetException* e)
    {
        succeed = false;
        //Close()自身可能抛出CInternetException，异常逃出catch会直接终止宿主进程，必须再捕获一层
        try
        {
            if (pfile != nullptr)
                pfile->Close();
            if (pSession != nullptr)
                pSession->Close();
        }
        catch (CInternetException* e2)
        {
            e2->Delete();
        }
        delete pfile;       //析构函数会释放句柄，不会抛出
        pfile = nullptr;
        e->Delete();
        SAFE_DELETE(pSession);
    }
    SAFE_DELETE(pSession);
    return succeed;
}

bool CCommon::IsURL(const std::wstring& str)
{
    return (str.substr(0, 7) == L"http://" || str.substr(0, 8) == L"https://" || str.substr(0, 6) == L"ftp://");
}
