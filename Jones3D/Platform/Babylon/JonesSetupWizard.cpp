// The setup wizard's model; see JonesSetupWizard.h. Everything the player can decide is here,
// including the licence and the two-phase GOG extraction, so the window is only a view.
#include <Jones3D/Platform/Babylon/JonesSetupWizard.h>

#include <Jones3D/Platform/Babylon/JonesUtf8Path.h>

#include <Jones3D/Main/JonesDataLocate.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <string>
#include <system_error>

namespace
{
    std::string JoinPath(JonesInstallPlatform platform, const std::string& base,
        const std::string& relative)
    {
        if ( base.empty() )
        {
            return relative;
        }

        const char separator = platform == JONESINSTALL_PLATFORM_WINDOWS ? '\\' : '/';
        std::string path = base;
        if ( path.back() != '/' && path.back() != '\\' )
        {
            path += separator;
        }

        return path + relative;
    }

    std::string StripTrailingSeparators(std::string path)
    {
        while ( path.size() > 1 && (path.back() == '/' || path.back() == '\\') )
        {
            path.pop_back();
        }

        return path;
    }

    std::string NormalizePath(JonesInstallPlatform platform, const std::string& path)
    {
        std::string normalized = StripTrailingSeparators(path);
        for ( char& chr : normalized )
        {
            if ( chr == '\\' )
            {
                chr = '/';
            }
            if ( platform == JONESINSTALL_PLATFORM_WINDOWS && chr >= 'A' && chr <= 'Z' )
            {
                chr = static_cast<char>(chr - 'A' + 'a');
            }
        }

        return normalized;
    }

    bool PathsEqual(JonesInstallPlatform platform, const std::string& left,
        const std::string& right)
    {
        return NormalizePath(platform, left) == NormalizePath(platform, right);
    }

    bool IsDirectory(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_directory(JonesUtf8Path_From(path), ec) && !ec;
    }

    bool IsRegularFile(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(JonesUtf8Path_From(path), ec) && !ec;
    }

    void CreateDirectories(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::create_directories(JonesUtf8Path_From(path), ec);
    }

    bool ReadWholeFile(const std::string& path, std::string& outText)
    {
        std::ifstream file(JonesUtf8Path_From(path), std::ios::binary);
        if ( !file )
        {
            return false;
        }

        outText.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }

    std::string Latin1ToUtf8(const std::string& text)
    {
        std::string utf8;
        utf8.reserve(text.size());
        for ( const char chr : text )
        {
            const unsigned char value = static_cast<unsigned char>(chr);
            if ( value == '\r' )
            {
                continue;
            }

            if ( value < 0x80 )
            {
                utf8.push_back(static_cast<char>(value));
            }
            else
            {
                utf8.push_back(static_cast<char>(0xC0 | (value >> 6)));
                utf8.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
        }

        return utf8;
    }

    bool CopyToBuffer(char* pDest, size_t destSize, const std::string& text)
    {
        if ( !pDest || destSize == 0 || text.size() + 1 > destSize )
        {
            return false;
        }

        std::copy(text.begin(), text.end(), pDest);
        pDest[text.size()] = '\0';
        return true;
    }

    std::string SourceLabel(const JonesInstallSource& source)
    {
        std::string label = JonesInstall_SourceKindName(source.kind);
        label += " - ";
        label += source.aPath;
        if ( !source.bComplete && source.kind != JONESINSTALL_SOURCE_DISC )
        {
            label += " (incomplete)";
        }

        return label;
    }

    bool HasNdsFiles(const std::string& dir)
    {
        std::error_code ec;
        std::filesystem::directory_iterator it(JonesUtf8Path_From(dir), ec);
        const std::filesystem::directory_iterator end;
        while ( !ec && it != end )
        {
            const std::filesystem::directory_entry& entry = *it;
            std::string extension = JonesUtf8Path_To(entry.path().extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char chr) { return static_cast<char>(std::tolower(chr)); });
            std::error_code fileEc;
            if ( extension == ".nds" && entry.is_regular_file(fileEc) )
            {
                return true;
            }

            it.increment(ec);
        }

        return false;
    }

    bool IsWritableDirectory(const std::string& dir)
    {
        if ( !IsDirectory(dir) )
        {
            return false;
        }

        const std::filesystem::path probe = JonesUtf8Path_From(dir)
            / JonesUtf8Path_From(".jones3dex-write-test");
        std::ofstream file(probe, std::ios::binary | std::ios::trunc);
        if ( !file )
        {
            return false;
        }

        file.close();
        std::error_code ec;
        std::filesystem::remove(probe, ec);
        return !ec;
    }
}

JonesSetupWizard::JonesSetupWizard(babylon::platform::Platform& platform,
    babylon::process::Process& process, const JonesInstallHost& host,
    std::string userDir, std::string exeDir)
    : m_platform(platform)
    , m_process(process)
    , m_host(host)
    , m_userDir(StripTrailingSeparators(std::move(userDir)))
    , m_exeDir(StripTrailingSeparators(std::move(exeDir)))
{
    m_innoextractPath = JonesGogInstaller_FindInnoextract(m_process, m_exeDir,
        m_host.platform == JONESINSTALL_PLATFORM_WINDOWS);
}

void JonesSetupWizard::Start()
{
    m_message.clear();
    m_step = JonesSetupStep::Welcome;
    RefreshSources();
}

JonesSetupStep JonesSetupWizard::Step() const
{
    return m_step;
}

const std::vector<JonesSetupRow>& JonesSetupWizard::Rows() const
{
    return m_rows;
}

const JonesInstallSource& JonesSetupWizard::Source() const
{
    return m_source;
}

bool JonesSetupWizard::HasSource() const
{
    return m_bHasSource;
}

const std::string& JonesSetupWizard::UserDir() const
{
    return m_userDir;
}

std::string JonesSetupWizard::UserDirForDisplay() const
{
    if ( !m_userDir.empty() )
    {
        return m_userDir;
    }

    std::error_code ec;
    const std::filesystem::path working = std::filesystem::current_path(ec);
    if ( ec )
    {
        return std::string(".");
    }

    return JonesUtf8Path_To(working);
}

const std::string& JonesSetupWizard::ExeDir() const
{
    return m_exeDir;
}

std::string JonesSetupWizard::TargetBase() const
{
    return m_bNextToGame && !m_exeDir.empty() ? m_exeDir : m_userDir;
}

std::string JonesSetupWizard::TargetDataDir() const
{
    return JoinPath(m_host.platform, TargetBase(), JONESDATALOCATE_GAMEDATA_DIR);
}

std::string JonesSetupWizard::SelectedUserDir() const
{
    return TargetBase();
}

std::string JonesSetupWizard::InnoextractPath() const
{
    return m_innoextractPath;
}

const std::string& JonesSetupWizard::Message() const
{
    return m_message;
}

const std::string& JonesSetupWizard::LicenceText() const
{
    return m_licenceText;
}

const JonesDataReport& JonesSetupWizard::Report() const
{
    return m_report;
}

std::string JonesSetupWizard::ReleaseText() const
{
    if ( !m_report.bComplete )
    {
        return std::string();
    }

    if ( m_report.pRelease )
    {
        return std::string("Release: ") + m_report.pRelease;
    }

    return "Release: unknown, the files are complete";
}

std::string JonesSetupWizard::MissingText() const
{
    std::string missing;
    for ( size_t i = 0; i < m_report.numFiles; ++i )
    {
        const JonesDataFile& file = m_report.aFiles[i];
        if ( !file.bRequired || file.bPresent )
        {
            continue;
        }

        if ( !missing.empty() )
        {
            missing += ", ";
        }

        missing += file.pName;
    }

    return missing;
}

const JonesSetupProgressState& JonesSetupWizard::Progress() const
{
    return m_progress;
}

bool JonesSetupWizard::CopySaves() const
{
    return m_bCopySaves;
}

bool JonesSetupWizard::CanCopySaves() const
{
    return m_bHasSource && m_source.kind != JONESINSTALL_SOURCE_GOG_INSTALLER
        && HasSourceSaves(m_source.aPath);
}

void JonesSetupWizard::SetCopySaves(bool bCopySaves)
{
    m_bCopySaves = bCopySaves;
}

bool JonesSetupWizard::NextToGame() const
{
    return m_bNextToGame;
}

bool JonesSetupWizard::NextToGameAvailable() const
{
    if ( m_exeDir.empty() || PathsEqual(m_host.platform, m_exeDir, m_userDir) )
    {
        return false;
    }

    return IsWritableDirectory(m_exeDir);
}

void JonesSetupWizard::SetNextToGame(bool bNextToGame)
{
    if ( bNextToGame && !NextToGameAvailable() )
    {
        return;
    }

    m_bNextToGame = bNextToGame;
}

void JonesSetupWizard::Continue()
{
    if ( m_step == JonesSetupStep::Welcome )
    {
        RefreshSources();
        m_step = JonesSetupStep::Source;
    }
}

void JonesSetupWizard::AcceptLicence()
{
    if ( m_step == JonesSetupStep::Licence )
    {
        m_step = JonesSetupStep::Location;
    }
}

bool JonesSetupWizard::Back()
{
    switch ( m_step )
    {
        case JonesSetupStep::Welcome:
            return false;

        case JonesSetupStep::Source:
            m_step = JonesSetupStep::Welcome;
            return true;

        case JonesSetupStep::Licence:
            m_step = JonesSetupStep::Source;
            m_message.clear();
            RefreshSources();
            return true;

        case JonesSetupStep::Location:
            if ( m_bLicenceShown )
            {
                m_step = JonesSetupStep::Licence;
            }
            else
            {
                m_step = JonesSetupStep::Source;
                m_message.clear();
                RefreshSources();
            }
            return true;

        case JonesSetupStep::Disc2:
            m_step = JonesSetupStep::Source;
            m_message.clear();
            RefreshSources();
            return true;

        case JonesSetupStep::Done:
            if ( !m_report.bComplete )
            {
                m_step = JonesSetupStep::Source;
                m_message.clear();
                RefreshSources();
                return true;
            }
            return false;

        case JonesSetupStep::Cancelled:
        case JonesSetupStep::Error:
            m_step = JonesSetupStep::Source;
            m_message.clear();
            RefreshSources();
            return true;

        case JonesSetupStep::Progress:
        default:
            return false;
    }
}

bool JonesSetupWizard::SelectRow(size_t index)
{
    if ( m_step == JonesSetupStep::Disc2 )
    {
        return PrepareDisc2Row(index);
    }

    if ( m_step != JonesSetupStep::Source || index >= m_rows.size() )
    {
        return false;
    }

    const JonesSetupRow row = m_rows[index];
    switch ( row.kind )
    {
        case JonesSetupRowKind::Source:
            static_cast<void>(SelectSource(row.source));
            return true;

        case JonesSetupRowKind::ChooseFolder:
        {
            const babylon::platform::PathChoice choice =
                m_platform.chooseFolder("Choose a game folder", m_userDir);
            if ( choice.result == babylon::platform::PathChoiceResult::Unavailable )
            {
                m_message = "No file dialog is available on this system.";
                return true;
            }

            if ( choice.result == babylon::platform::PathChoiceResult::Chosen )
            {
                ChooseFolder(choice.path);
            }
            return true;
        }

        case JonesSetupRowKind::ChooseInstaller:
        {
            const babylon::platform::FileFilter filters[] = {
                { "GOG installer", "exe" },
                { "All files", "*" },
            };
            const babylon::platform::PathChoice choice = m_platform.chooseFile(
                "Choose a GOG installer file", m_userDir, filters);
            if ( choice.result == babylon::platform::PathChoiceResult::Unavailable )
            {
                m_message = "No file dialog is available on this system.";
                return true;
            }

            if ( choice.result == babylon::platform::PathChoiceResult::Chosen )
            {
                ChooseInstaller(choice.path);
            }
            return true;
        }

        case JonesSetupRowKind::SearchAgain:
            m_message.clear();
            RefreshSources();
            return true;

        default:
            return false;
    }
}

bool JonesSetupWizard::ChooseFolder(const std::string& path)
{
    if ( m_step == JonesSetupStep::Disc2 )
    {
        return PrepareDisc2Folder(path);
    }

    JonesInstallSource source = {};
    if ( !JonesInstall_GetFolderSource(&m_host, path.c_str(), &source) )
    {
        m_message = "This folder doesn't contain the game files.";
        return false;
    }

    return SelectSource(source);
}

bool JonesSetupWizard::ChooseInstaller(const std::string& path)
{
    if ( !IsRegularFile(path) )
    {
        m_message = "That file does not exist.";
        return false;
    }

    JonesInstallSource source = {};
    source.kind = JONESINSTALL_SOURCE_GOG_INSTALLER;
    source.discNumber = 0;
    source.bComplete = false;
    if ( !CopyToBuffer(source.aPath, sizeof(source.aPath), path) )
    {
        m_message = "The installer's path is too long.";
        return false;
    }

    return SelectSource(source);
}

bool JonesSetupWizard::PrepareDisc2Row(size_t index)
{
    if ( index >= m_rows.size() )
    {
        return false;
    }

    const JonesSetupRow row = m_rows[index];
    switch ( row.kind )
    {
        case JonesSetupRowKind::Source:
            m_source = row.source;
            m_bHasSource = true;
            m_bDisc2Ready = true;
            m_message.clear();
            return true;

        case JonesSetupRowKind::ChooseFolder:
        {
            const babylon::platform::PathChoice choice =
                m_platform.chooseFolder("Choose the disc 2 folder", m_userDir);
            if ( choice.result == babylon::platform::PathChoiceResult::Unavailable )
            {
                m_message = "No file dialog is available on this system.";
                return true;
            }

            if ( choice.result == babylon::platform::PathChoiceResult::Chosen )
            {
                PrepareDisc2Folder(choice.path);
            }
            return true;
        }

        case JonesSetupRowKind::SearchAgain:
            m_message.clear();
            BuildDisc2Rows();
            return true;

        default:
            return false;
    }
}

bool JonesSetupWizard::PrepareDisc2Folder(const std::string& path)
{
    JonesInstallSource source = {};
    if ( !JonesInstall_GetFolderSource(&m_host, path.c_str(), &source) )
    {
        m_message = "This folder doesn't contain the game files.";
        return false;
    }

    m_source = source;
    m_bHasSource = true;
    m_bDisc2Ready = true;
    m_message.clear();
    return true;
}

bool JonesSetupWizard::TakeDisc2Run()
{
    const bool bReady = m_bDisc2Ready;
    m_bDisc2Ready = false;
    return bReady;
}

void JonesSetupWizard::BeginInstall()
{
    if ( !m_bHasSource )
    {
        SetErrorOutcome("No game source was selected.");
        return;
    }

    m_bCancel = false;
    m_bDisc2Ready = false;
    m_progress = JonesSetupProgressState();
    m_progress.phase = m_source.kind == JONESINSTALL_SOURCE_GOG_INSTALLER
        ? "Unpacking the installer" : "Copying the game files";
    m_step = JonesSetupStep::Progress;
}

void JonesSetupWizard::RunInstall()
{
    if ( m_step != JonesSetupStep::Progress || !m_bHasSource )
    {
        return;
    }

    if ( m_source.kind == JONESINSTALL_SOURCE_GOG_INSTALLER )
    {
        RunGogInstall();
    }
    else
    {
        RunCopyInstall();
    }
}

void JonesSetupWizard::StartInstall()
{
    BeginInstall();
    RunInstall();
}

void JonesSetupWizard::RequestCancel()
{
    if ( m_step == JonesSetupStep::Progress )
    {
        m_bCancel = true;
    }
}

bool JonesSetupWizard::IsCancelled() const
{
    return m_bCancel;
}

void JonesSetupWizard::SetPump(std::function<void()> pump)
{
    m_pump = std::move(pump);
}

#ifdef J3D_TEST
void JonesSetupWizard::SetTestHooks(TestHooks hooks)
{
    m_testHooks = std::move(hooks);
}
#endif

void JonesSetupWizard::RefreshSources()
{
    JonesInstallSourceList sources = {};
    static_cast<void>(JonesInstall_FindSources(&m_host, &sources));

    m_rows.clear();
    for ( size_t i = 0; i < sources.numSources; ++i )
    {
        const JonesInstallSource& source = sources.aSources[i];
        if ( source.kind == JONESINSTALL_SOURCE_GOG_INSTALLER && m_innoextractPath.empty() )
        {
            continue;
        }

        JonesSetupRow row;
        row.kind = JonesSetupRowKind::Source;
        row.source = source;
        row.label = SourceLabel(source);
        m_rows.push_back(std::move(row));
    }

    JonesSetupRow chooseFolder;
    chooseFolder.kind = JonesSetupRowKind::ChooseFolder;
    chooseFolder.label = "Choose a game folder...";
    m_rows.push_back(std::move(chooseFolder));

    if ( !m_innoextractPath.empty() )
    {
        JonesSetupRow chooseInstaller;
        chooseInstaller.kind = JonesSetupRowKind::ChooseInstaller;
        chooseInstaller.label = "Choose a GOG installer file...";
        m_rows.push_back(std::move(chooseInstaller));
    }

    JonesSetupRow searchAgain;
    searchAgain.kind = JonesSetupRowKind::SearchAgain;
    searchAgain.label = "Search again";
    m_rows.push_back(std::move(searchAgain));
}

void JonesSetupWizard::BuildDisc2Rows()
{
    JonesInstallSourceList sources = {};
    static_cast<void>(JonesInstall_FindSources(&m_host, &sources));

    m_rows.clear();
    for ( size_t i = 0; i < sources.numSources; ++i )
    {
        const JonesInstallSource& source = sources.aSources[i];
        if ( source.kind != JONESINSTALL_SOURCE_DISC || source.discNumber != 2 )
        {
            continue;
        }

        JonesSetupRow row;
        row.kind = JonesSetupRowKind::Source;
        row.source = source;
        row.label = SourceLabel(source);
        m_rows.push_back(std::move(row));
    }

    JonesSetupRow chooseFolder;
    chooseFolder.kind = JonesSetupRowKind::ChooseFolder;
    chooseFolder.label = "Choose a folder...";
    m_rows.push_back(std::move(chooseFolder));

    JonesSetupRow searchAgain;
    searchAgain.kind = JonesSetupRowKind::SearchAgain;
    searchAgain.label = "Search again";
    m_rows.push_back(std::move(searchAgain));
}

bool JonesSetupWizard::SelectSource(const JonesInstallSource& source)
{
    if ( !source.bComplete && source.kind != JONESINSTALL_SOURCE_DISC
        && source.kind != JONESINSTALL_SOURCE_GOG_INSTALLER )
    {
        JonesInstallPlan plan = {};
        static_cast<void>(JonesInstall_BuildPlan(m_host.platform, source.aPath,
            TargetDataDir().c_str(), false, &plan));

        m_message = "This folder doesn't contain the game files.";
        if ( plan.aMissing[0] )
        {
            m_message += " Missing: ";
            m_message += plan.aMissing;
            m_message += ".";
        }
        return false;
    }

    m_source = source;
    m_bHasSource = true;
    m_bCopySaves = true;
    m_message.clear();
    EnterLicence(m_source);
    return true;
}

void JonesSetupWizard::EnterLicence(const JonesInstallSource& source)
{
    m_bLicenceShown = false;
    m_licenceText.clear();

    if ( source.kind == JONESINSTALL_SOURCE_GOG_INSTALLER )
    {
        const std::string tempDir = JoinPath(m_host.platform,
            JoinPath(m_host.platform, TargetBase(), "setup-tmp"), "eula");
        CreateDirectories(tempDir);

        std::string text;
        std::string error;
        bool bRead = false;
#ifdef J3D_TEST
        if ( m_testHooks.readEula )
        {
            bRead = m_testHooks.readEula(source.aPath, tempDir, text, error);
        }
        else
#endif
        {
            bRead = JonesGogInstaller_ReadEula(m_process, m_innoextractPath, source.aPath,
                tempDir, text, error);
        }

        RemoveSetupTemp();

        if ( !bRead )
        {
            m_step = JonesSetupStep::Source;
            m_message = error.empty()
                ? "The GOG installer's licence could not be read." : error;
            return;
        }

        m_licenceText = std::move(text);
        m_bLicenceShown = true;
        m_step = JonesSetupStep::Licence;
        return;
    }

    if ( source.kind == JONESINSTALL_SOURCE_DISC )
    {
        const std::string licencePath = JoinPath(m_host.platform,
            JoinPath(m_host.platform, source.aPath, "install"), "license.txt");
        if ( IsRegularFile(licencePath) )
        {
            std::string text;
            std::string error;
            if ( !ReadDiscLicence(source.aPath, text, error) )
            {
                m_step = JonesSetupStep::Source;
                m_message = error;
                return;
            }

            m_licenceText = std::move(text);
            m_bLicenceShown = true;
            m_step = JonesSetupStep::Licence;
            return;
        }
    }

    m_step = JonesSetupStep::Location;
}

bool JonesSetupWizard::ReadDiscLicence(const std::string& sourceRoot, std::string& outText,
    std::string& outError) const
{
    const std::string licencePath = JoinPath(m_host.platform,
        JoinPath(m_host.platform, sourceRoot, "install"), "license.txt");
    std::string bytes;
    if ( !ReadWholeFile(licencePath, bytes) )
    {
        outError = "The disc's licence file could not be read.";
        return false;
    }

    outText = Latin1ToUtf8(bytes);
    return true;
}

void JonesSetupWizard::FinishInstall()
{
    m_progress.percent = 1.0;
    m_progress.phase = "Finished";
    m_progress.file.clear();

    // Added: check the target with the same cache file JonesMain_Startup uses, so the first game
    // start reuses these hashes instead of hashing every GOB again.
    char aResourceDir[JONESINSTALL_MAX_PATH] = { 0 };
    const bool bFound = JonesData_FindResourceDir(TargetDataDir().c_str(), aResourceDir,
        sizeof(aResourceDir));
    const std::string cachePath = JoinPath(m_host.platform, SelectedUserDir(),
        "datacheck.txt");

    m_report = JonesDataReport();
    static_cast<void>(JonesData_CheckDir(bFound ? aResourceDir : nullptr,
        cachePath.c_str(), &m_report));
    m_step = JonesSetupStep::Done;
    m_message.clear();
    Pump();
}

void JonesSetupWizard::SetCancelledOutcome()
{
    m_message = "Setup was cancelled.";
    m_step = JonesSetupStep::Cancelled;
}

void JonesSetupWizard::SetErrorOutcome(const std::string& message)
{
    m_message = message.empty() ? "Setup failed." : message;
    m_step = JonesSetupStep::Error;
}

void JonesSetupWizard::RemoveSetupTemp()
{
    const std::string tempDir = JoinPath(m_host.platform, TargetBase(), "setup-tmp");
    if ( m_source.kind == JONESINSTALL_SOURCE_GOG_INSTALLER )
    {
        JonesGogInstaller_RemoveDir(tempDir);
    }
    else
    {
        std::error_code ec;
        std::filesystem::remove_all(JonesUtf8Path_From(tempDir), ec);
    }
}

void JonesSetupWizard::Pump()
{
    if ( m_pump )
    {
        m_pump();
    }
}

void JonesSetupWizard::UpdateCopyProgress(int64_t done, int64_t total, const char* pFile,
    double low, double high)
{
    double fraction = 0.0;
    if ( total > 0 )
    {
        fraction = static_cast<double>(done) / static_cast<double>(total);
    }
    fraction = std::clamp(fraction, 0.0, 1.0);

    m_progress.percent = low + (high - low) * fraction;
    m_progress.bytesDone = done;
    m_progress.bytesTotal = total;
    if ( pFile )
    {
        m_progress.file = pFile;
    }
}

void JonesSetupWizard::RunGogInstall()
{
    const std::string base = TargetBase();
    const std::string setupTmp = JoinPath(m_host.platform, base, "setup-tmp");
    const std::string extractDir = JoinPath(m_host.platform, setupTmp, "extract");
    CreateDirectories(extractDir);

    m_progress.phase = "Unpacking the installer";
    m_progress.percent = 0.0;
    m_progress.file.clear();
    m_progress.bytesDone = 0;
    m_progress.bytesTotal = 0;
    Pump();

    const JonesGogProgressFunc onProgress = [this](double percent, const std::string& file)
    {
        m_progress.percent = std::clamp(percent / 200.0, 0.0, 0.5);
        if ( !file.empty() )
        {
            m_progress.file = file;
        }
        Pump();
    };
    const babylon::process::CancelCheck cancelled = [this]()
    {
        Pump();
        return m_bCancel;
    };

    std::string error;
    JonesGogExtractResult result;
#ifdef J3D_TEST
    if ( m_testHooks.extract )
    {
        result = m_testHooks.extract(extractDir, onProgress, cancelled, error);
    }
    else
#endif
    {
        result = JonesGogInstaller_Extract(m_process, m_innoextractPath, m_source.aPath,
            extractDir, onProgress, cancelled, error);
    }

    if ( m_bCancel || result == JonesGogExtractResult::Cancelled )
    {
        RemoveSetupTemp();
        SetCancelledOutcome();
        Pump();
        return;
    }

    if ( result != JonesGogExtractResult::Ok )
    {
        RemoveSetupTemp();
        SetErrorOutcome(error.empty()
            ? "The GOG installer could not be unpacked." : error);
        Pump();
        return;
    }

    m_progress.phase = "Copying the game files";
    m_progress.percent = 0.5;
    m_progress.file.clear();
    Pump();

    JonesInstallPlan plan = {};
    if ( !BuildPlan(extractDir, false, plan) )
    {
        RemoveSetupTemp();
        SetErrorOutcome(m_message);
        Pump();
        return;
    }

    RunPlan(plan, 0.5, 1.0);
    RemoveSetupTemp();
}

void JonesSetupWizard::RunCopyInstall()
{
    m_progress.phase = "Copying the game files";
    m_progress.percent = 0.0;
    m_progress.file.clear();
    Pump();

    JonesInstallPlan plan = {};
    if ( !BuildPlan(m_source.aPath, m_bCopySaves && CanCopySaves(), plan) )
    {
        RemoveSetupTemp();
        SetErrorOutcome(m_message);
        Pump();
        return;
    }

    RunPlan(plan, 0.0, 1.0);
}

bool JonesSetupWizard::BuildPlan(const std::string& sourceRoot, bool bCopySaves,
    JonesInstallPlan& plan)
{
    plan = JonesInstallPlan();
    if ( JonesInstall_BuildPlan(m_host.platform, sourceRoot.c_str(),
            TargetDataDir().c_str(), bCopySaves, &plan) )
    {
        return true;
    }

    if ( plan.aMissing[0] )
    {
        m_message = "The game files are missing: ";
        m_message += plan.aMissing;
        m_message += ".";
    }
    else
    {
        m_message = "No game files were found in the selected folder.";
    }

    return false;
}

void JonesSetupWizard::RunPlan(const JonesInstallPlan& plan, double low, double high)
{
    m_phaseLow = low;
    m_phaseHigh = high;

    JonesInstallRunOutcome outcome = {};
    const JonesInstallRunResult result = JonesInstall_Run(&plan, &m_host,
        [](int64_t done, int64_t total, const char* pFile, void* pUserData)
        {
            JonesSetupWizard* pWizard = static_cast<JonesSetupWizard*>(pUserData);
            pWizard->UpdateCopyProgress(done, total, pFile, pWizard->m_phaseLow,
                pWizard->m_phaseHigh);
            pWizard->Pump();
        },
        this, &m_bCancel, &outcome);

    if ( m_bCancel || result == JONESINSTALL_RUN_CANCELLED )
    {
        RemoveSetupTemp();
        SetCancelledOutcome();
        Pump();
        return;
    }

    if ( result == JONESINSTALL_RUN_ERROR )
    {
        RemoveSetupTemp();
        SetErrorOutcome(outcome.aMessage);
        Pump();
        return;
    }

    m_progress.percent = high;
    Pump();

    if ( outcome.bNeedsDisc2 )
    {
        BuildDisc2Rows();
        m_message.clear();
        m_step = JonesSetupStep::Disc2;
        Pump();
        return;
    }

    FinishInstall();
}

bool JonesSetupWizard::HasSourceSaves(const std::string& sourceRoot) const
{
    const std::string saves = JoinPath(m_host.platform, sourceRoot, "SaveGames");
    return IsDirectory(saves) && HasNdsFiles(saves);
}
