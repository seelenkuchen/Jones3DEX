#include "JonesCapture.h"

#include <ctype.h>
#include <string.h>

#include <std/General/std.h>

#define JONECAPTURE_OPTION        "-capture"
#define JONECAPTURE_OPTION_LENGTH (sizeof(JONECAPTURE_OPTION) - 1)

// Added: the parser also runs before stdStartup on Windows, so the logging host services may not
// exist yet. Log through them only when they do.
static void JonesCapture_LogError(const char* pText)
{
    if ( std_g_pHS )
    {
        STDLOG_ERROR("%s\n", pText);
    }
}

// Returns the next whitespace-separated token without changing the input. A token may contain
// quoted whitespace; the quotes stay part of the token.
static bool JonesCapture_NextToken(const char* pCmdLine, size_t* pPos, size_t* pTokenStart, size_t* pTokenEnd)
{
    size_t cur = *pPos;
    while ( pCmdLine[cur] && isspace((unsigned char)pCmdLine[cur]) )
    {
        ++cur;
    }

    if ( !pCmdLine[cur] )
    {
        *pPos = cur;
        return false;
    }

    *pTokenStart = cur;

    bool bInQuotes = false;
    while ( pCmdLine[cur] )
    {
        if ( pCmdLine[cur] == '"' )
        {
            bInQuotes = !bInQuotes;
        }
        else if ( !bInQuotes && isspace((unsigned char)pCmdLine[cur]) )
        {
            break;
        }

        ++cur;
    }

    *pTokenEnd = cur;
    *pPos      = cur;
    return true;
}

bool JonesCapture_ParseCommandLine(const char* pCmdLine, char* pPath, size_t pathSize, char* pRest, size_t restSize)
{
    if ( pPath && pathSize )
    {
        pPath[0] = '\0';
    }

    if ( pRest && restSize )
    {
        pRest[0] = '\0';
    }

    if ( !pCmdLine || !pPath || pathSize == 0 || !pRest || restSize == 0 )
    {
        JonesCapture_LogError("Render capture option: missing command line or output buffer.");
        return false;
    }

    size_t tokenStart  = 0;
    size_t tokenEnd    = 0;
    size_t optionStart = 0;
    size_t optionEnd   = 0;
    size_t valueStart  = 0;
    size_t valueEnd    = 0;
    size_t cur         = 0;
    bool bFound        = false;

    while ( JonesCapture_NextToken(pCmdLine, &cur, &tokenStart, &tokenEnd) )
    {
        if ( !bFound
          && tokenEnd - tokenStart == JONECAPTURE_OPTION_LENGTH
          && memcmp(&pCmdLine[tokenStart], JONECAPTURE_OPTION, JONECAPTURE_OPTION_LENGTH) == 0 )
        {
            bFound      = true;
            optionStart = tokenStart;
            optionEnd   = tokenEnd;

            if ( !JonesCapture_NextToken(pCmdLine, &cur, &valueStart, &valueEnd) )
            {
                JonesCapture_LogError("Render capture option: missing file name.");
                return false;
            }

            break;
        }
    }

    if ( !bFound )
    {
        return false;
    }

    const char* pValue = pCmdLine + valueStart;
    size_t valueSize   = valueEnd - valueStart;

    if ( valueSize > 0 && pValue[0] == '"' )
    {
        if ( valueSize < 2 || pValue[valueSize - 1] != '"' )
        {
            JonesCapture_LogError("Render capture option: unterminated quoted file name.");
            return false;
        }

        ++pValue;
        valueSize -= 2;
    }

    if ( valueSize == 0 )
    {
        JonesCapture_LogError("Render capture option: empty file name.");
        return false;
    }

    if ( valueSize >= pathSize )
    {
        JonesCapture_LogError("Render capture option: file name is too long.");
        return false;
    }

    memcpy(pPath, pValue, valueSize);
    pPath[valueSize] = '\0';

    size_t restUsed = 0;
    bool bFirst     = true;
    cur             = 0;

    while ( JonesCapture_NextToken(pCmdLine, &cur, &tokenStart, &tokenEnd) )
    {
        if ( (tokenStart == optionStart && tokenEnd == optionEnd)
          || (tokenStart == valueStart && tokenEnd == valueEnd) )
        {
            continue;
        }

        size_t tokenSize  = tokenEnd - tokenStart;
        size_t neededSize = tokenSize + (bFirst ? 0 : 1);
        if ( neededSize >= restSize - restUsed )
        {
            JonesCapture_LogError("Render capture option: the remaining command line is too long.");
            pPath[0] = '\0';
            pRest[0] = '\0';
            return false;
        }

        if ( !bFirst )
        {
            pRest[restUsed++] = ' ';
        }

        memcpy(&pRest[restUsed], &pCmdLine[tokenStart], tokenSize);
        restUsed += tokenSize;
        bFirst    = false;
    }

    pRest[restUsed] = '\0';
    return true;
}
