#pragma once
#include <string>
#include <map>
class CChapterParser
{
public:
    CChapterParser(const std::wstring& contents);
    ~CChapterParser();
    void Parse();
    //解析一段文本中的章节，返回章节表。纯函数不修改成员，供在锁外预先解析用
    static std::map<int, std::wstring> ParseChapterData(const std::wstring& contents);
    //整体换入章节表
    void SetChapterData(std::map<int, std::wstring>&& data);
    const std::map<int, std::wstring>& GetChapterData() const;
    std::wstring GetChapterTitle(int chapter_index) const;
    std::wstring GetCurrentChapterTitle() const;
    int GetChapterIndexByPos(int pos) const;
    std::wstring GetChapterByPos(int pos) const;

private:
    const std::wstring& m_contents;
    std::map<int, std::wstring> m_chapter;      //章节标题。key中索引，value为章节标题
};
