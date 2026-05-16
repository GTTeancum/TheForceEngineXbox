#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine System Library
// System functionality, such as timers and logging.
//////////////////////////////////////////////////////////////////////

#include "types.h"

// ---------------------------------------------------------------------------
// TokenList
//
// PC builds:  std::vector<std::string>
// Xbox build: fixed-capacity array backed by XboxString proxy.
//   Max 64 tokens per line is generous for all TFE parsing needs.
// ---------------------------------------------------------------------------
#ifdef _XBOX

#include <string>

#ifndef XBOX_STRING_DEFINED
#define XBOX_STRING_DEFINED
// XboxString is also defined in fileutil.h; guard against double definition.
// Both definitions are identical - the guard ensures only one is compiled.
struct XboxString
{
    char buf[1024];  // Tokens can be long (paths, quoted strings)

    XboxString() { buf[0] = 0; }

    const char* c_str()  const { return buf; }
    size_t      length() const { return strlen(buf); }
    size_t      size()   const { return strlen(buf); }
    bool        empty()  const { return buf[0] == 0; }

    void operator=(const char* src)
    {
        strncpy(buf, src ? src : "", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
    }

    bool operator==(const char* rhs) const
    {
        return strcmp(buf, rhs ? rhs : "") == 0;
    }

    // Implicit conversion to std::string so that
    // 'std::string s = token;' and 's_msgMap[id] = tokens[2];' compile.
    operator std::string() const { return std::string(buf); }
};
#endif // XBOX_STRING_DEFINED

#define TOKENLIST_MAX 64

struct TokenList
{
    XboxString entries[TOKENLIST_MAX];
    int        count;

    TokenList() : count(0) {}

    void push_back(const char* s)
    {
        if (count < TOKENLIST_MAX)
        {
            entries[count] = s;
            count++;
        }
    }

    void   clear()  { count = 0; }
    int    size()   const { return count; }
    bool   empty()  const { return count == 0; }

    const XboxString& operator[](int i) const { return entries[i]; }
          XboxString& operator[](int i)       { return entries[i]; }
};

#else // PC build

#include <vector>
#include <string>
typedef std::vector<std::string> TokenList;

#endif // _XBOX

// ---------------------------------------------------------------------------
// TFE_Parser
// ---------------------------------------------------------------------------
class TFE_Parser
{
public:
    TFE_Parser();
    ~TFE_Parser();

    void init(const char* buffer, size_t len);

    void enableBlockComments();
    void enableColonSeperator();
    void addCommentString(const char* comment);
    void convertToUpperCase(bool enable);

    const char* readLine(size_t& bufferPos, bool skipLeadingWhitespace = false, bool commentOnlyAtBeginning = false);
    void tokenizeLine(const char* line, TokenList& tokens);

private:
    const char* m_buffer;
    size_t      m_bufferLen;
    TokenList   m_commentStrings;
    bool        m_enableBlockComments;
    bool        m_blockComment;
    bool        m_enableColonSeperator;
    bool        m_convertToUppercase;

private:
    bool isComment(const char* buffer);
};
