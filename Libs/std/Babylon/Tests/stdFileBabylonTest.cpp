// Added: Unity Fixture suite for the Babylon column's files adapter: stdBabylonPath,
// stdFileBabylon and stdFileUtilBabylon. It compiles the adapter files directly and drives them
// through a fresh StdBabylonContext whose pFileSystem is a babylon::files::NullFileSystem (current
// path "/"), so every value is exact and deterministic. Files are prepared through the
// NullFileSystem API. This file also carries the single main() for the stdBabylonTests executable,
// running the time suite of stdPlatformTimeBabylonTest.cpp and the control suite of
// stdControlBabylonTest.cpp together with these groups.
#include <unity_fixture.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/NullFileSystem.h>
#include <babylon/time/NullClock.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonPath.h>
#include <std/General/std.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdMemory.h>

TEST_GROUP(stdBabylonPath);
TEST_GROUP(stdFileBabylon);
TEST_GROUP(stdFileUtilBabylon);

// The stdBabylonTests executable does not link General/stdMemory.c; stdFileUtil_DisposeFind
// releases the FindFileData block through stdMemory_Free, which pairs here with the new the
// helpers below use.
extern "C" void J3DAPI stdMemory_Free(void* pBytes)
{
    ::operator delete(pBytes);
}

namespace
{
    babylon::time::NullClock*      pNullClock    = nullptr;
    babylon::time::NullClock*      pVirtualClock = nullptr;
    babylon::files::NullFileSystem* pFileSystem  = nullptr;
    babylon::platform::NullPlatform* pPlatform   = nullptr;
    // The context contract wants a registry; the file adapters read no cvar, so this one stays empty.
    babylon::cvars::Registry         cvars;
    StdBabylonContext              context;

    void InstallContext()
    {
        pNullClock    = new babylon::time::NullClock();
        pVirtualClock = new babylon::time::NullClock();
        pFileSystem   = new babylon::files::NullFileSystem();
        pPlatform     = new babylon::platform::NullPlatform();

        context.pClock          = pNullClock;
        context.pVirtualClock   = pVirtualClock;
        context.pFileSystem     = pFileSystem;
        context.pPlatform       = pPlatform;
        context.pScriptPlatform = pPlatform;
        context.pWindow         = nullptr;
        context.pCvars          = &cvars;
        stdBabylon_SetContext(&context);
    }

    void RemoveContext()
    {
        stdBabylon_SetContext(nullptr);
        delete pPlatform;
        delete pFileSystem;
        delete pVirtualClock;
        delete pNullClock;
        pPlatform     = nullptr;
        pFileSystem   = nullptr;
        pVirtualClock = nullptr;
        pNullClock    = nullptr;
    }

    // The NullFileSystem API takes bytes; the fixtures are spelled as text.
    std::span<const std::byte> Bytes(std::string_view aText)
    {
        return std::span<const std::byte>(reinterpret_cast<const std::byte*>(aText.data()), aText.size());
    }

    // Reads a whole stored file through the babylon::files::File interface, for checking what the
    // adapter wrote; false when the file cannot be opened or a read fails.
    bool ReadWholeFile(const char* pPath, std::string& out)
    {
        std::unique_ptr<babylon::files::File> pFile = pFileSystem->open(pPath, babylon::files::OpenMode::Read);
        if ( !pFile )
        {
            return false;
        }

        out.clear();
        std::byte aChunk[256];
        for (;;)
        {
            const std::optional<std::size_t> numRead = pFile->read(std::span(aChunk));
            if ( !numRead || *numRead == 0 )
            {
                return numRead.has_value();
            }

            out.append(reinterpret_cast<const char*>(aChunk), *numRead);
        }
    }

    // Stores pFilter in the search filter of pData, truncated and zero padded like strncpy of size - 1
    // characters (the whole field is cleared first, as buffers are reused), without the CRT call MSVC's /sdl rejects.
    void CopySearchFilter(FindFileData* pData, const char* pFilter)
    {
        const std::size_t numCopy = std::min(std::strlen(pFilter), sizeof(pData->aSearchFilter) - 1);
        std::memset(pData->aSearchFilter, 0, sizeof(pData->aSearchFilter));
        std::memcpy(pData->aSearchFilter, pFilter, numCopy);
    }

    // Runs one complete search through the adapter and copies the reported names; the count of
    // results is returned, entries beyond numMax are not collected.
    int CollectFind(const char* pFilter, tFoundFileInfo* pInfos, int numMax)
    {
        FindFileData* pData       = new FindFileData;
        std::memset(pData, 0, sizeof(*pData));
        CopySearchFilter(pData, pFilter);

        int numFound = 0;
        while ( numFound < numMax && stdFileUtil_FindNext(pData, &pInfos[numFound]) )
        {
            ++numFound;
        }

        stdFileUtil_DisposeFind(pData);
        return numFound;
    }

    // A file system that forwards everything to an inner NullFileSystem, reports another current
    // path, records the exists() probes and counts the listNames() calls, to check that the
    // resolver trusts the current path's segments and lists parents without metadata.
    class cCountingFileSystem final : public babylon::files::FileSystem
    {
    public:
        std::unique_ptr<babylon::files::File> open(std::string_view aPath, babylon::files::OpenMode eMode) override
        {
            return m_fs.open(aPath, eMode);
        }

        bool exists(std::string_view aPath) override
        {
            m_aProbed.emplace_back(aPath);
            return m_fs.exists(aPath);
        }

        bool createDirectory(std::string_view aPath) override
        {
            return m_fs.createDirectory(aPath);
        }

        bool remove(std::string_view aPath) override
        {
            return m_fs.remove(aPath);
        }

        std::optional<std::vector<babylon::files::DirectoryEntry>> listDirectory(std::string_view aPath) override
        {
            return m_fs.listDirectory(aPath);
        }

        std::optional<std::vector<std::string>> listNames(std::string_view aPath) override
        {
            ++m_numListNames;
            return m_fs.listNames(aPath);
        }

        std::string basePath() override
        {
            return m_fs.basePath();
        }

        std::string currentPath() override
        {
            return m_aCurrentPath;
        }

        std::string userPath(std::string_view aOrganization, std::string_view aApplication) override
        {
            return m_fs.userPath(aOrganization, aApplication);
        }

        // True when exists() saw exactly this path.
        bool Probed(std::string_view aPath) const
        {
            return std::find(m_aProbed.begin(), m_aProbed.end(), aPath) != m_aProbed.end();
        }

        babylon::files::NullFileSystem m_fs;
        std::vector<std::string>       m_aProbed;
        size_t                         m_numListNames = 0;
        std::string                    m_aCurrentPath = "/work/dir/"; // what currentPath() reports
    };
}

TEST_SETUP(stdBabylonPath)
{
    InstallContext();
}

TEST_TEAR_DOWN(stdBabylonPath)
{
    RemoveContext();
}

TEST(stdBabylonPath, ResolvesExistingEntriesCaseInsensitively)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/data/sub/file.txt", Bytes("text")));

    std::string out;
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "Data\\Sub\\File.TXT", out));
    TEST_ASSERT_EQUAL_STRING("/data/sub/file.txt", out.c_str());

    // A leading backslash has no drive to root it against, so it is relative (the current path is
    // "/" here, the same root the absolute spelling gets).
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "\\data\\x", out));
    TEST_ASSERT_EQUAL_STRING("/data/x", out.c_str());

    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "/data/sub/file.txt", out));
    TEST_ASSERT_EQUAL_STRING("/data/sub/file.txt", out.c_str());

    // Among case-folded equal names the byte order picks: "AB" before "ab".
    TEST_ASSERT_TRUE(pFileSystem->addFile("/d/ab", Bytes("1")));
    TEST_ASSERT_TRUE(pFileSystem->addFile("/d/AB", Bytes("2")));
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "/d/Ab", out));
    TEST_ASSERT_EQUAL_STRING("/d/AB", out.c_str());
}

TEST(stdBabylonPath, NormalizesDotsAndStopsAtMissingSegments)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/data/sub/file.txt", Bytes("text")));

    std::string out;
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "a/./b/../c", out));
    TEST_ASSERT_EQUAL_STRING("/a/c", out.c_str());

    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "../x", out));
    TEST_ASSERT_EQUAL_STRING("/x", out.c_str());

    // A missing segment stops resolving: it and everything after it stay as given, while the
    // earlier segments keep their resolved spelling.
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "/Data/NEW/Thing", out));
    TEST_ASSERT_EQUAL_STRING("/data/NEW/Thing", out.c_str());

    // The root survives an all-dots path.
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "..", out));
    TEST_ASSERT_EQUAL_STRING("/", out.c_str());

    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "/", out));
    TEST_ASSERT_EQUAL_STRING("/", out.c_str());
}

TEST(stdBabylonPath, KeepsTrailingSeparatorsAndRejectsEmptyInput)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/data/sub/file.txt", Bytes("text")));

    std::string out;
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "/data/sub/", out));
    TEST_ASSERT_EQUAL_STRING("/data/sub/", out.c_str());

    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(*pFileSystem, "Data\\Sub\\", out));
    TEST_ASSERT_EQUAL_STRING("/data/sub/", out.c_str());

    out = "keep";
    TEST_ASSERT_FALSE(stdBabylonPath_Resolve(*pFileSystem, NULL, out));
    TEST_ASSERT_EQUAL_STRING("keep", out.c_str());

    TEST_ASSERT_FALSE(stdBabylonPath_Resolve(*pFileSystem, "", out));
    TEST_ASSERT_EQUAL_STRING("keep", out.c_str());
}

TEST(stdBabylonPath, TrustsTheCurrentPathSegments)
{
    cCountingFileSystem counting;
    TEST_ASSERT_TRUE(counting.m_fs.createDirectory("/work/dir/"));

    // A relative probe for a missing loose file: the walk starts at the first segment the input
    // contributes and never probes the current path's own segments.
    std::string out;
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "missing\\file.txt", out));
    TEST_ASSERT_EQUAL_STRING("/work/dir/missing/file.txt", out.c_str());
    TEST_ASSERT_FALSE(counting.Probed("/work"));
    TEST_ASSERT_FALSE(counting.Probed("/work/"));
    TEST_ASSERT_FALSE(counting.Probed("/work/dir"));
    TEST_ASSERT_FALSE(counting.Probed("/work/dir/"));
    TEST_ASSERT_TRUE(counting.Probed("/work/dir/missing"));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(counting.m_numListNames));

    // A ".." that pops into the current path untrusts exactly the popped segment: /work stays
    // trusted, Dir resolves to the real dir through a listing of /work, and /work itself is
    // still never passed to exists().
    counting.m_aProbed.clear();
    counting.m_numListNames = 0;
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "..\\Dir\\x", out));
    TEST_ASSERT_EQUAL_STRING("/work/dir/x", out.c_str());
    TEST_ASSERT_FALSE(counting.Probed("/work"));
    TEST_ASSERT_FALSE(counting.Probed("/work/"));
    TEST_ASSERT_TRUE(counting.Probed("/work/Dir"));
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(counting.m_numListNames));
}

TEST(stdBabylonPath, KeepsTheDriveRootOfTheCurrentPath)
{
    // The SDL backend reports a Windows working directory as "C:/...": relative input keeps that
    // drive as its root, never climbs above it and never probes the current path's segments.
    cCountingFileSystem counting;
    counting.m_aCurrentPath = "C:/work/dir/";

    std::string out;
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "missing\\file.txt", out));
    TEST_ASSERT_EQUAL_STRING("C:/work/dir/missing/file.txt", out.c_str());
    TEST_ASSERT_FALSE(counting.Probed("C:/work"));
    TEST_ASSERT_FALSE(counting.Probed("C:/work/dir"));
    TEST_ASSERT_TRUE(counting.Probed("C:/work/dir/missing"));

    counting.m_aProbed.clear();
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "..\\Dir\\x", out));
    TEST_ASSERT_EQUAL_STRING("C:/work/Dir/x", out.c_str());
    TEST_ASSERT_FALSE(counting.Probed("C:/work"));
    TEST_ASSERT_TRUE(counting.Probed("C:/work/Dir"));

    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "..\\..\\..\\x", out));
    TEST_ASSERT_EQUAL_STRING("C:/x", out.c_str());

    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "../../..", out));
    TEST_ASSERT_EQUAL_STRING("C:/", out.c_str());
}

TEST(stdBabylonPath, DrivePathsAreAbsoluteOnlyOnWindows)
{
    cCountingFileSystem counting;

    std::string out;
#ifdef _WIN32
    // A drive path roots at its drive, whatever the current path is.
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "D:\\Data\\File.txt", out));
    TEST_ASSERT_EQUAL_STRING("D:/Data/File.txt", out.c_str());
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "d:/a/../b", out));
    TEST_ASSERT_EQUAL_STRING("d:/b", out.c_str());
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "D:\\..\\x\\", out));
    TEST_ASSERT_EQUAL_STRING("D:/x/", out.c_str());
    // "D:" without a separator is drive-relative, not absolute: it stays a plain segment.
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "D:x", out));
    TEST_ASSERT_EQUAL_STRING("/work/dir/D:x", out.c_str());
#else
    // Elsewhere "C:" is an ordinary name, so the path stays relative.
    TEST_ASSERT_TRUE(stdBabylonPath_Resolve(counting, "C:\\x", out));
    TEST_ASSERT_EQUAL_STRING("/work/dir/C:/x", out.c_str());
#endif
}

TEST_GROUP_RUNNER(stdBabylonPath)
{
    RUN_TEST_CASE(stdBabylonPath, ResolvesExistingEntriesCaseInsensitively);
    RUN_TEST_CASE(stdBabylonPath, NormalizesDotsAndStopsAtMissingSegments);
    RUN_TEST_CASE(stdBabylonPath, KeepsTrailingSeparatorsAndRejectsEmptyInput);
    RUN_TEST_CASE(stdBabylonPath, TrustsTheCurrentPathSegments);
    RUN_TEST_CASE(stdBabylonPath, KeepsTheDriveRootOfTheCurrentPath);
    RUN_TEST_CASE(stdBabylonPath, DrivePathsAreAbsoluteOnlyOnWindows);
}

TEST_SETUP(stdFileBabylon)
{
    InstallContext();
}

TEST_TEAR_DOWN(stdFileBabylon)
{
    RemoveContext();
}

TEST(stdFileBabylon, OpenReadEofTellAcrossBufferSizes)
{
    std::string aBig;
    for ( int cur = 0; cur < 20000; ++cur )
    {
        aBig.push_back(static_cast<char>(cur & 0xFF));
    }
    TEST_ASSERT_TRUE(pFileSystem->addFile("/big.bin", Bytes(aBig)));

    // Relative and spelled with backslashes and another case: the resolver finds the file.
    tFileHandle fh = stdFileOpen("Big.bin", "rb");
    TEST_ASSERT_NOT_EQUAL(0u, fh);

    char aSmall[4];
    TEST_ASSERT_EQUAL_size_t(3u, stdFileRead(fh, aSmall, 3u));
    TEST_ASSERT_EQUAL_MEMORY(aBig.data(), aSmall, 3u);
    TEST_ASSERT_EQUAL_INT(3, stdFileTell(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(fh));

    static char aMid[5000];
    TEST_ASSERT_EQUAL_size_t(5000u, stdFileRead(fh, aMid, 5000u));
    TEST_ASSERT_EQUAL_MEMORY(aBig.data() + 3, aMid, 5000u);
    TEST_ASSERT_EQUAL_INT(5003, stdFileTell(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(fh));

    static char aRest[14997];
    TEST_ASSERT_EQUAL_size_t(14997u, stdFileRead(fh, aRest, 14997u));
    TEST_ASSERT_EQUAL_MEMORY(aBig.data() + 5003, aRest, 14997u);
    TEST_ASSERT_EQUAL_INT(20000, stdFileTell(fh));

    // EOF only after a read hits the end.
    char aOne;
    TEST_ASSERT_EQUAL_size_t(0u, stdFileRead(fh, &aOne, 1u));
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(20000, stdFileTell(fh));

    // A read on a Write handle returns 0.
    tFileHandle fhWrite = stdFileOpen("/write.bin", "wb");
    TEST_ASSERT_NOT_EQUAL(0u, fhWrite);
    TEST_ASSERT_EQUAL_size_t(0u, stdFileRead(fhWrite, aSmall, sizeof(aSmall)));
    TEST_ASSERT_EQUAL_INT(0, stdFileFlush(fhWrite));

    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fhWrite));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(0));
}

TEST(stdFileBabylon, Gets)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/lines.txt", Bytes("alpha\nbravo")));

    tFileHandle fh = stdFileOpen("/lines.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);

    char aBuf[16];
    TEST_ASSERT_EQUAL_PTR(aBuf, stdFileGets(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_STRING("alpha\n", aBuf);
    TEST_ASSERT_EQUAL_INT(6, stdFileTell(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(fh));

    TEST_ASSERT_EQUAL_PTR(aBuf, stdFileGets(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_STRING("bravo", aBuf);

    TEST_ASSERT_NULL(stdFileGets(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // A line longer than size - 1 splits across calls.
    TEST_ASSERT_TRUE(pFileSystem->addFile("/long.txt", Bytes("abcdefgh\n")));
    fh = stdFileOpen("/long.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);

    char aFour[5];
    TEST_ASSERT_EQUAL_PTR(aFour, stdFileGets(fh, aFour, sizeof(aFour)));
    TEST_ASSERT_EQUAL_STRING("abcd", aFour);
    TEST_ASSERT_EQUAL_PTR(aFour, stdFileGets(fh, aFour, sizeof(aFour)));
    TEST_ASSERT_EQUAL_STRING("efgh", aFour);
    TEST_ASSERT_EQUAL_PTR(aFour, stdFileGets(fh, aFour, sizeof(aFour)));
    TEST_ASSERT_EQUAL_STRING("\n", aFour);
    TEST_ASSERT_NULL(stdFileGets(fh, aFour, sizeof(aFour)));
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // size 1 terminates without reading, size 0 fails like fgets.
    fh = stdFileOpen("/long.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    char aOneByte[1];
    TEST_ASSERT_EQUAL_PTR(aOneByte, stdFileGets(fh, aOneByte, 1u));
    TEST_ASSERT_EQUAL_STRING("", aOneByte);
    TEST_ASSERT_EQUAL_INT(0, stdFileTell(fh));
    TEST_ASSERT_NULL(stdFileGets(fh, aOneByte, 0u));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // Nothing read at the end of an empty file: NULL with EOF set.
    TEST_ASSERT_TRUE(pFileSystem->addFile("/empty.txt", Bytes("")));
    fh = stdFileOpen("/empty.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_NULL(stdFileGets(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
}

TEST(stdFileBabylon, GetwsClocale)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/wide.txt", Bytes("wide\n")));

    tFileHandle fh = stdFileOpen("/wide.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);

    wchar_t aWide[16];
    TEST_ASSERT_EQUAL_PTR(aWide, stdFileGetws(fh, aWide, std::size(aWide)));
    TEST_ASSERT_EQUAL_INT(0, std::wcscmp(L"wide\n", aWide));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // In the "C" locale a byte of 0x80 or more is not a wide character: the call returns NULL.
    std::string aHigh;
    aHigh.push_back(static_cast<char>(0x80));
    aHigh.push_back('x');
    TEST_ASSERT_TRUE(pFileSystem->addFile("/high.txt", Bytes(aHigh)));

    fh = stdFileOpen("/high.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_NULL(stdFileGetws(fh, aWide, std::size(aWide)));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
}

// Text-mode reads ("r", "rt") translate the way the Microsoft C runtime does: "\r\n" is "\n",
// also when the pair straddles the 8 KiB read-ahead buffer, a lone '\r' stays, Ctrl-Z ends the
// data until a seek. Positions stay byte offsets in the file (this adapter's contract; the
// Microsoft ftell of a text stream differs and no engine caller asks for it). Binary reads are
// byte-exact.
TEST(stdFileBabylon, TextModeReadsCrLfAsLf)
{
    // "\r" is the last byte of the first buffer fill, "\n" the first of the second.
    std::string aBoundary(8191, 'x');
    aBoundary += "\r\nyz\r\n";
    TEST_ASSERT_TRUE(pFileSystem->addFile("/boundary.txt", Bytes(aBoundary)));

    tFileHandle fh = stdFileOpen("/boundary.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    static char aLine[8300];
    TEST_ASSERT_EQUAL_PTR(aLine, stdFileGets(fh, aLine, sizeof(aLine)));
    TEST_ASSERT_EQUAL_size_t(8192u, std::strlen(aLine));
    TEST_ASSERT_EQUAL_CHAR('x', aLine[8190]);
    TEST_ASSERT_EQUAL_CHAR('\n', aLine[8191]);
    TEST_ASSERT_EQUAL_INT(8193, stdFileTell(fh));
    TEST_ASSERT_EQUAL_PTR(aLine, stdFileGets(fh, aLine, sizeof(aLine)));
    TEST_ASSERT_EQUAL_STRING("yz\n", aLine);
    TEST_ASSERT_EQUAL_INT(8197, stdFileTell(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(fh));
    TEST_ASSERT_NULL(stdFileGets(fh, aLine, sizeof(aLine)));
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // fread of the whole file in text mode: the direct read path for large requests translates too.
    fh = stdFileOpen("/boundary.txt", "rt");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(8195u, stdFileRead(fh, aLine, sizeof(aLine)));
    TEST_ASSERT_EQUAL_MEMORY("\nyz\n", aLine + 8191, 4u);
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // The same file in binary: every byte, through the direct read path.
    fh = stdFileOpen("/boundary.txt", "rb");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(aBoundary.size(), stdFileRead(fh, aLine, sizeof(aLine)));
    TEST_ASSERT_EQUAL_MEMORY(aBoundary.data(), aLine, aBoundary.size());
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // Ctrl-Z ends the data and is not consumed; a seek clears it.
    TEST_ASSERT_TRUE(pFileSystem->addFile("/ctrlz.txt", Bytes(std::string_view("ab\x1A" "cd\r\n", 7))));
    fh = stdFileOpen("/ctrlz.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    char aBuf[16];
    TEST_ASSERT_EQUAL_PTR(aBuf, stdFileGets(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_STRING("ab", aBuf);
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(2, stdFileTell(fh));
    TEST_ASSERT_EQUAL_size_t(0u, stdFileRead(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_INT(0, stdFileSeek(fh, 3, 0));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(fh));
    TEST_ASSERT_EQUAL_PTR(aBuf, stdFileGets(fh, aBuf, sizeof(aBuf)));
    TEST_ASSERT_EQUAL_STRING("cd\n", aBuf);
    TEST_ASSERT_EQUAL_INT(7, stdFileTell(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // Getws on a text handle reads the same translated characters.
    TEST_ASSERT_TRUE(pFileSystem->addFile("/widecrlf.txt", Bytes("wide\r\n")));
    fh = stdFileOpen("/widecrlf.txt", "r");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    wchar_t aWide[16];
    TEST_ASSERT_EQUAL_PTR(aWide, stdFileGetws(fh, aWide, std::size(aWide)));
    TEST_ASSERT_EQUAL_INT(0, std::wcscmp(L"wide\n", aWide));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
}

TEST(stdFileBabylon, SeekClearsEofAndDropsTheBuffer)
{
    std::string aBig;
    for ( int cur = 0; cur < 20000; ++cur )
    {
        aBig.push_back(static_cast<char>(cur & 0xFF));
    }
    TEST_ASSERT_TRUE(pFileSystem->addFile("/big.bin", Bytes(aBig)));

    tFileHandle fh = stdFileOpen("/big.bin", "rb");
    TEST_ASSERT_NOT_EQUAL(0u, fh);

    char aSmall[16];
    TEST_ASSERT_EQUAL_size_t(3u, stdFileRead(fh, aSmall, 3u)); // fills the read-ahead buffer

    // A failing seek restores the logical position: the read-ahead filled the buffer to 8192,
    // seeking past the end is refused, Tell still reports 3 and the next read continues at
    // byte 3. (fseek may go past the end of a read file; the Babylon interface refuses that.)
    TEST_ASSERT_EQUAL_INT(-1, stdFileSeek(fh, 19999, 1)); // logical 3 + 19999 > 20000
    TEST_ASSERT_EQUAL_INT(3, stdFileTell(fh));
    TEST_ASSERT_EQUAL_size_t(1u, stdFileRead(fh, aSmall, 1u));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(static_cast<unsigned char>(aBig[3])),
        static_cast<int>(static_cast<unsigned char>(aSmall[0])));

    // Current is turned into Begin from the logical position, not the buffered file position.
    TEST_ASSERT_EQUAL_INT(0, stdFileSeek(fh, 99, 1)); // logical 4 + 99
    TEST_ASSERT_EQUAL_INT(103, stdFileTell(fh));
    TEST_ASSERT_EQUAL_size_t(4u, stdFileRead(fh, aSmall, 4u));
    TEST_ASSERT_EQUAL_MEMORY(aBig.data() + 103, aSmall, 4u);

    TEST_ASSERT_EQUAL_INT(0, stdFileSeek(fh, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, stdFileTell(fh));
    TEST_ASSERT_EQUAL_size_t(2u, stdFileRead(fh, aSmall, 2u));
    TEST_ASSERT_EQUAL_MEMORY(aBig.data(), aSmall, 2u);

    TEST_ASSERT_EQUAL_INT(0, stdFileSeek(fh, -10, 2));
    TEST_ASSERT_EQUAL_INT(19990, stdFileTell(fh));
    TEST_ASSERT_EQUAL_size_t(10u, stdFileRead(fh, aSmall, 10u));
    TEST_ASSERT_EQUAL_MEMORY(aBig.data() + 19990, aSmall, 10u);

    // fseek can move a read file past the end; the Babylon interface refuses it, so this is -1.
    TEST_ASSERT_EQUAL_INT(-1, stdFileSeek(fh, 10, 2));
    TEST_ASSERT_EQUAL_INT(20000, stdFileTell(fh));
    TEST_ASSERT_EQUAL_INT(-1, stdFileSeek(fh, -5, 0));
    TEST_ASSERT_EQUAL_INT(-1, stdFileSeek(fh, 0, 5));

    // Seek clears the EOF flag on success.
    char aOne;
    TEST_ASSERT_EQUAL_size_t(0u, stdFileRead(fh, &aOne, 1u));
    TEST_ASSERT_EQUAL_INT(1, stdFileEof(fh));
    TEST_ASSERT_EQUAL_INT(0, stdFileSeek(fh, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(fh));

    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));

    // Handle 0 behaves like the failure paths of the C runtime calls.
    TEST_ASSERT_EQUAL_INT(-1, stdFileTell(0));
    TEST_ASSERT_EQUAL_INT(-1, stdFileSeek(0, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, stdFileEof(0));
}

TEST(stdFileBabylon, WriteAndAppendModes)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/trunc.txt", Bytes("oldcontent")));

    // w truncates.
    tFileHandle fh = stdFileOpen("/Trunc.txt", "wb");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(5u, stdFileWrite(fh, "12345", 5u));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
    TEST_ASSERT_EQUAL_size_t(5u, stdFileSize("/trunc.txt"));

    std::string aContents;
    TEST_ASSERT_TRUE(ReadWholeFile("/trunc.txt", aContents));
    TEST_ASSERT_EQUAL_STRING("12345", aContents.c_str());

    // a appends.
    TEST_ASSERT_TRUE(pFileSystem->addFile("/app.txt", Bytes("AB")));
    fh = stdFileOpen("/app.txt", "ab");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(2u, stdFileWrite(fh, "CD", 2u));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
    TEST_ASSERT_EQUAL_size_t(4u, stdFileSize("/app.txt"));
    TEST_ASSERT_TRUE(ReadWholeFile("/app.txt", aContents));
    TEST_ASSERT_EQUAL_STRING("ABCD", aContents.c_str());

    // wb+ and wt+ open for writing; a read on such a handle returns 0.
    fh = stdFileOpen("/plus.bin", "wb+");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(1u, stdFileWrite(fh, "x", 1u));
    char aRead[2];
    TEST_ASSERT_EQUAL_size_t(0u, stdFileRead(fh, aRead, sizeof(aRead)));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
    TEST_ASSERT_EQUAL_size_t(1u, stdFileSize("/plus.bin"));

    fh = stdFileOpen("/plus.bin", "wt+");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(1u, stdFileWrite(fh, "y", 1u));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
    TEST_ASSERT_TRUE(ReadWholeFile("/plus.bin", aContents));
    TEST_ASSERT_EQUAL_STRING("y", aContents.c_str());

    fh = stdFileOpen("/app.txt", "at+");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(1u, stdFileWrite(fh, "E", 1u));
    TEST_ASSERT_EQUAL_size_t(0u, stdFileRead(fh, aRead, sizeof(aRead)));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
    TEST_ASSERT_EQUAL_size_t(5u, stdFileSize("/app.txt"));

    // Open of a missing file for reading returns 0 (the probe never logs); Size says 0 too.
    TEST_ASSERT_EQUAL(0u, stdFileOpen("/missing.bin", "rb"));
    TEST_ASSERT_EQUAL_size_t(0u, stdFileSize("/missing.bin"));
    TEST_ASSERT_EQUAL(0u, stdFileSize("/missing.bin"));

    // Null name, null or unknown mode: 0.
    TEST_ASSERT_EQUAL(0u, stdFileOpen(NULL, "rb"));
    TEST_ASSERT_EQUAL(0u, stdFileOpen("/trunc.txt", NULL));
    TEST_ASSERT_EQUAL(0u, stdFileOpen("/trunc.txt", ""));
    TEST_ASSERT_EQUAL(0u, stdFileOpen("/trunc.txt", "x"));
}

TEST_GROUP_RUNNER(stdFileBabylon)
{
    RUN_TEST_CASE(stdFileBabylon, OpenReadEofTellAcrossBufferSizes);
    RUN_TEST_CASE(stdFileBabylon, Gets);
    RUN_TEST_CASE(stdFileBabylon, GetwsClocale);
    RUN_TEST_CASE(stdFileBabylon, TextModeReadsCrLfAsLf);
    RUN_TEST_CASE(stdFileBabylon, SeekClearsEofAndDropsTheBuffer);
    RUN_TEST_CASE(stdFileBabylon, WriteAndAppendModes);
}

TEST_SETUP(stdFileUtilBabylon)
{
    InstallContext();
}

TEST_TEAR_DOWN(stdFileUtilBabylon)
{
    RemoveContext();
}

TEST(stdFileUtilBabylon, FindNextReportsDotEntriesInSortedOrder)
{
    TEST_ASSERT_TRUE(pFileSystem->createDirectory("/fx/child/"));
    TEST_ASSERT_TRUE(pFileSystem->addFile("/fx/a.txt", Bytes("1")));
    TEST_ASSERT_TRUE(pFileSystem->addFile("/fx/AbC.txt", Bytes("2")));
    TEST_ASSERT_TRUE(pFileSystem->addFile("/fx/b.txt", Bytes("3")));
    TEST_ASSERT_TRUE(pFileSystem->addFile("/fx/c.bin", Bytes("4")));

    // The POSIX listing reports "." and ".."; the search adds them back as directories. Order:
    // case-insensitive name order with the byte order breaking ties.
    tFoundFileInfo aInfos[16];
    TEST_ASSERT_EQUAL_INT(7, CollectFind("/fx/*.*", aInfos, 16));
    TEST_ASSERT_EQUAL_STRING(".", aInfos[0].aName);
    TEST_ASSERT_EQUAL_INT(1, aInfos[0].bIsDirectory);
    TEST_ASSERT_EQUAL_STRING("..", aInfos[1].aName);
    TEST_ASSERT_EQUAL_INT(1, aInfos[1].bIsDirectory);
    TEST_ASSERT_EQUAL_STRING("a.txt", aInfos[2].aName);
    TEST_ASSERT_EQUAL_INT(0, aInfos[2].bIsDirectory);
    TEST_ASSERT_EQUAL_STRING("AbC.txt", aInfos[3].aName);
    TEST_ASSERT_EQUAL_STRING("b.txt", aInfos[4].aName);
    TEST_ASSERT_EQUAL_STRING("c.bin", aInfos[5].aName);
    TEST_ASSERT_EQUAL_STRING("child", aInfos[6].aName);
    TEST_ASSERT_EQUAL_INT(1, aInfos[6].bIsDirectory);

    // Babylon reports no times for directory entries.
    for ( int cur = 0; cur < 7; ++cur )
    {
        TEST_ASSERT_EQUAL_UINT32(0u, aInfos[cur].lastChanged);
    }

    TEST_ASSERT_EQUAL_INT(3, CollectFind("/fx/*.txt", aInfos, 16));
    TEST_ASSERT_EQUAL_STRING("a.txt", aInfos[0].aName);
    TEST_ASSERT_EQUAL_STRING("AbC.txt", aInfos[1].aName);
    TEST_ASSERT_EQUAL_STRING("b.txt", aInfos[2].aName);

    TEST_ASSERT_EQUAL_INT(1, CollectFind("/fx/A?C.*", aInfos, 16));
    TEST_ASSERT_EQUAL_STRING("AbC.txt", aInfos[0].aName);

    // No match and a missing directory: no cursor, FindNext returns 0.
    TEST_ASSERT_EQUAL_INT(0, CollectFind("/fx/*.xyz", aInfos, 16));
    TEST_ASSERT_EQUAL_INT(0, CollectFind("/missing/*.*", aInfos, 16));
}

TEST(stdFileUtilBabylon, FindNextRetriesAfterAFailedSearch)
{
    TEST_ASSERT_TRUE(pFileSystem->addFile("/fx/a.txt", Bytes("1")));

    FindFileData* pData = new FindFileData;
    std::memset(pData, 0, sizeof(*pData));
    CopySearchFilter(pData, "/missing/*.*");

    tFoundFileInfo info;
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FindNext(pData, &info));
    TEST_ASSERT_EQUAL_INT(0, pData->nFoundFiles);

    // Only successful results are counted, so the same FindFileData can be searched again.
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FindNext(pData, &info));
    TEST_ASSERT_EQUAL_INT(0, pData->nFoundFiles);

    CopySearchFilter(pData, "/FX/*.txt");
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_FindNext(pData, &info));
    TEST_ASSERT_EQUAL_STRING("a.txt", info.aName);
    TEST_ASSERT_EQUAL_INT(1, pData->nFoundFiles);

    stdFileUtil_DisposeFind(pData);

#ifdef J3D_RUNTIME_GUARDS
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FindNext(NULL, &info));
    FindFileData* pGuardData = new FindFileData;
    std::memset(pGuardData, 0, sizeof(*pGuardData));
    CopySearchFilter(pGuardData, "/FX/*.*");
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FindNext(pGuardData, NULL));
    stdFileUtil_DisposeFind(pGuardData);
#endif

    stdFileUtil_DisposeFind(NULL);
}

TEST(stdFileUtilBabylon, MkDirFileExistsRmDirDelFile)
{
    // MkDir: 1 only when the parent exists as a directory and nothing exists at the path.
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_MkDir("/fx2"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_MkDir("/fx2"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_MkDir("/missing/x"));

    // Relative and backslash spellings resolve like every other path.
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_MkDir("fx2\\sub"));
    TEST_ASSERT_TRUE(pFileSystem->exists("/fx2/sub/"));

    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_FileExists("/fx2"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FileExists("/fx2/nope"));

    // RmDir fails on a not-empty directory and on a file; DelFile fails on a directory.
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_RmDir("/fx2"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_DelFile("/fx2"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_RmDir("/fx2/nope"));

    TEST_ASSERT_TRUE(pFileSystem->addFile("/fx2/f.txt", Bytes("x")));
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_FileExists("/FX2/F.TXT")); // case-insensitive resolve
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_RmDir("/fx2/f.txt"));
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_DelFile("/fx2/f.txt"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_DelFile("/fx2/f.txt")); // missing after a delete
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_RmDir("/fx2/sub"));
    TEST_ASSERT_EQUAL_INT(1, stdFileUtil_RmDir("/fx2"));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FileExists("/fx2"));

    // Any resolve failure: 0.
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_MkDir(NULL));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_FileExists(NULL));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_RmDir(NULL));
    TEST_ASSERT_EQUAL_INT(0, stdFileUtil_DelFile(NULL));
}

TEST_GROUP_RUNNER(stdFileUtilBabylon)
{
    RUN_TEST_CASE(stdFileUtilBabylon, FindNextReportsDotEntriesInSortedOrder);
    RUN_TEST_CASE(stdFileUtilBabylon, FindNextRetriesAfterAFailedSearch);
    RUN_TEST_CASE(stdFileUtilBabylon, MkDirFileExistsRmDirDelFile);
}

static void runAllTests(void)
{
    RUN_TEST_GROUP(stdPlatformTimeBabylon);
    RUN_TEST_GROUP(stdBabylonPath);
    RUN_TEST_GROUP(stdFileBabylon);
    RUN_TEST_GROUP(stdFileUtilBabylon);
    RUN_TEST_GROUP(stdControlBabylon);
    RUN_TEST_GROUP(stdBabylonEvents);
    RUN_TEST_GROUP(stdCvarBabylon);
}

int main(int argc, const char* argv[])
{
    return UnityMain(argc, argv, runAllTests);
}
