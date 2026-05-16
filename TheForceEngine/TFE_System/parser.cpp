#include <cstring>
#include "parser.h"
#include <assert.h>
#include <ctype.h>

namespace
{
    static char s_line[4096];

    bool isWhitespace(const char c)
    {
        return (c <= 32 || c >= 127);
    }

    bool isSeparator(const char c)
    {
        return (c == '=' || c == ',');
    }
}

TFE_Parser::TFE_Parser()
    : m_buffer(NULL), m_bufferLen(0u)
    , m_enableBlockComments(false), m_blockComment(false)
    , m_enableColonSeperator(false), m_convertToUppercase(false)
{
}

TFE_Parser::~TFE_Parser() {}

void TFE_Parser::init(const char* buffer, size_t len)
{
    m_buffer    = buffer;
    m_bufferLen = len;
}

void TFE_Parser::enableBlockComments()
{
    m_enableBlockComments = true;
}

void TFE_Parser::enableColonSeperator()
{
    m_enableColonSeperator = true;
}

void TFE_Parser::addCommentString(const char* comment)
{
    m_commentStrings.push_back(comment);
}

void TFE_Parser::convertToUpperCase(bool enable)
{
    m_convertToUppercase = enable;
}

bool TFE_Parser::isComment(const char* buffer)
{
    const int commentCount = (int)m_commentStrings.size();
    for (int c = 0; c < commentCount; c++)
    {
        const char* cstr = m_commentStrings[c].c_str();
        size_t      clen = m_commentStrings[c].length();
        if (strncmp(cstr, buffer, clen) == 0)
            return true;
    }
    return false;
}

const char* TFE_Parser::readLine(size_t& bufferPos, bool skipLeadingWhitespace, bool commentOnlyAtBeginning)
{
    if (bufferPos >= m_bufferLen || m_bufferLen < 1) return NULL;

    bool lineHasContent = false;
    s32  skip = -1;

    while (!lineHasContent && bufferPos < m_bufferLen)
    {
        s_line[0] = 0;
        size_t linePos   = 0;
        bool   inComment = false;

        for (size_t i = bufferPos; i < m_bufferLen; i++)
        {
            bufferPos = i + 1;

            if (m_enableBlockComments && i > 0 && m_buffer[i-1] == '*' && m_buffer[i] == '/')
            {
                m_blockComment = false;
            }
            else if (m_enableBlockComments && m_buffer[i] == '/' && m_buffer[i+1] == '*')
            {
                m_blockComment = true;
            }
            else if (m_buffer[i] == '\n' || m_buffer[i] == '\r')
            {
                for (size_t ii = i + 1; ii < m_bufferLen; ii++)
                {
                    if (m_buffer[ii] != '\n' && m_buffer[ii] != '\r')
                    {
                        bufferPos = ii;
                        break;
                    }
                }
                break;
            }
            else if (!inComment && !m_blockComment)
            {
                if (!commentOnlyAtBeginning)
                    inComment = isComment(m_buffer + i);

                if (!inComment)
                {
                    s_line[linePos++] = m_convertToUppercase
                        ? (char)toupper((unsigned char)m_buffer[i])
                        : m_buffer[i];
                    assert(linePos <= 4096);
                }
            }
        }
        s_line[linePos] = 0;

        lineHasContent = false;
        skip = -1;
        for (size_t i = 0; i < linePos; i++)
        {
            if (!isWhitespace(s_line[i]))
            {
                if (commentOnlyAtBeginning && isComment(&s_line[i]))
                    break;
                if (skip < 0) skip = (s32)i;
                lineHasContent = true;
                break;
            }
        }
    }

    if (lineHasContent && skipLeadingWhitespace && skip > 0)
        return &s_line[skip];

    return (s_line[0] != 0) ? s_line : NULL;
}

void TFE_Parser::tokenizeLine(const char* line, TokenList& tokens)
{
    tokens.clear();

    const size_t len = strlen(line);
    size_t start = 0, end = 0;
    for (size_t c = 0; c < len; c++)
    {
        if (!isWhitespace(line[c]))
        {
            if (start == 0 && end == 0) { start = c; }
            end = c + 1;
        }
    }

    bool   inQuote     = false;
    char   curToken[1024];
    size_t curTokenPos = 0;

    for (size_t c = start; c < end; c++)
    {
        if (line[c] == '"')
        {
            if (inQuote && curTokenPos == 0)
                tokens.push_back("");
            inQuote = !inQuote;
        }
        else if (!inQuote && (isWhitespace(line[c]) || isSeparator(line[c])))
        {
            curToken[curTokenPos] = 0;
            if (curTokenPos)
                tokens.push_back(curToken);
            curTokenPos = 0;
            curToken[0] = 0;
        }
        else if (!inQuote && m_enableColonSeperator && line[c] == ':')
        {
            curToken[curTokenPos] = 0;
            if (curTokenPos)
                tokens.push_back(curToken);
            curTokenPos = 0;
            curToken[0] = 0;
        }
        else
        {
            if (curTokenPos < sizeof(curToken) - 1)
                curToken[curTokenPos++] = line[c];
        }
    }

    if (curTokenPos)
    {
        curToken[curTokenPos] = 0;
        tokens.push_back(curToken);
    }
}
