// The setup wizard's model: the steps, the selected source, the target, the licence text, the
// progress state and the messages. It owns every decision and calls the install engine; the
// window only shows this state and forwards the player's choices. No window or Babylon ui type
// appears here, so tests drive it with NullPlatform, NullProcess and temporary folders.
#ifndef JONES3D_PLATFORM_BABYLON_JONESSETUPWIZARD_H
#define JONES3D_PLATFORM_BABYLON_JONESSETUPWIZARD_H
#ifndef __cplusplus
#error "JonesSetupWizard.h is C++ only: it names babylon::platform and babylon::process types."
#endif

#include <Jones3D/Main/JonesData.h>
#include <Jones3D/Main/JonesInstall.h>
#include <Jones3D/Platform/Babylon/JonesGogInstaller.h>

#include <babylon/platform/Platform.h>
#include <babylon/process/Process.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class JonesSetupStep
{
    Welcome,
    Source,
    Licence,
    Location,
    Progress,
    Disc2,
    Done,
    Cancelled,
    Error
};

enum class JonesSetupRowKind
{
    Source,
    ChooseFolder,
    ChooseInstaller,
    SearchAgain
};

struct JonesSetupRow
{
    JonesSetupRowKind kind = JonesSetupRowKind::Source;
    JonesInstallSource source = {};
    std::string label;
};

struct JonesSetupProgressState
{
    double percent = 0.0;
    std::string phase;
    std::string file;
    int64_t bytesDone = 0;
    int64_t bytesTotal = 0;
};

class JonesSetupWizard
{
public:
    JonesSetupWizard(babylon::platform::Platform& platform,
        babylon::process::Process& process, const JonesInstallHost& host,
        std::string userDir, std::string exeDir);

    // Starts on the welcome screen and collects the sources the finder can see.
    void Start();

    JonesSetupStep Step() const;
    const std::vector<JonesSetupRow>& Rows() const;

    const JonesInstallSource& Source() const;
    bool HasSource() const;

    const std::string& UserDir() const;
    // The user directory as the Location screen shows it: the stored path, or the absolute
    // working directory when it is empty (the working directory itself).
    std::string UserDirForDisplay() const;
    const std::string& ExeDir() const;
    std::string TargetBase() const;
    std::string TargetDataDir() const;
    std::string SelectedUserDir() const;
    std::string InnoextractPath() const;

    const std::string& Message() const;
    const std::string& LicenceText() const;
    const JonesDataReport& Report() const;
    std::string ReleaseText() const;
    std::string MissingText() const;
    const JonesSetupProgressState& Progress() const;

    bool CopySaves() const;
    bool CanCopySaves() const;
    void SetCopySaves(bool bCopySaves);

    bool NextToGame() const;
    bool NextToGameAvailable() const;
    void SetNextToGame(bool bNextToGame);

    void Continue();
    void AcceptLicence();
    bool Back();

    // Handles one row of the current screen. A source row selects it; a picker row opens the
    // native dialog; Search again rescans. Returns true when the model changed or a dialog ran.
    bool SelectRow(size_t index);

    bool ChooseFolder(const std::string& path);
    bool ChooseInstaller(const std::string& path);

    // Disc 2 rows: prepare the selected disc 2 source for a copy run. The caller then runs
    // BeginInstall and RunInstall, exactly as the Location screen does.
    bool PrepareDisc2Row(size_t index);
    bool PrepareDisc2Folder(const std::string& path);

    // True once a disc 2 row selected a source and the copy has not started yet.
    bool TakeDisc2Run();

    void BeginInstall();
    void RunInstall();
    void StartInstall();

    void RequestCancel();
    bool IsCancelled() const;

    // Called by the engine's progress and process callbacks while an install runs. The window
    // polls events and redraws from it.
    void SetPump(std::function<void()> pump);

#ifdef J3D_TEST
    struct TestHooks
    {
        std::function<bool(const std::string& installer, const std::string& tempDir,
            std::string& outText, std::string& outError)> readEula;
        std::function<JonesGogExtractResult(const std::string& extractDir,
            const JonesGogProgressFunc& onProgress,
            const babylon::process::CancelCheck& cancelled, std::string& outError)> extract;
    };

    void SetTestHooks(TestHooks hooks);
#endif

private:
    void RefreshSources();
    void BuildDisc2Rows();
    bool SelectSource(const JonesInstallSource& source);
    void EnterLicence(const JonesInstallSource& source);
    bool ReadDiscLicence(const std::string& sourceRoot, std::string& outText,
        std::string& outError) const;
    void FinishInstall();
    void SetCancelledOutcome();
    void SetErrorOutcome(const std::string& message);
    void RemoveSetupTemp();
    void Pump();
    void UpdateCopyProgress(int64_t done, int64_t total, const char* pFile, double low,
        double high);

    void RunGogInstall();
    void RunCopyInstall();
    bool BuildPlan(const std::string& sourceRoot, bool bCopySaves, JonesInstallPlan& plan);
    void RunPlan(const JonesInstallPlan& plan, double low, double high);

    bool HasSourceSaves(const std::string& sourceRoot) const;

    babylon::platform::Platform& m_platform;
    babylon::process::Process& m_process;
    JonesInstallHost m_host;
    std::string m_userDir;
    std::string m_exeDir;
    std::string m_innoextractPath;

    JonesSetupStep m_step = JonesSetupStep::Welcome;
    std::vector<JonesSetupRow> m_rows;
    JonesInstallSource m_source = {};
    bool m_bHasSource = false;
    bool m_bNextToGame = false;
    bool m_bLicenceShown = false;
    bool m_bCopySaves = true;

    std::string m_message;
    std::string m_licenceText;
    JonesDataReport m_report = {};
    JonesSetupProgressState m_progress;

    bool m_bCancel = false;
    std::function<void()> m_pump;
    bool m_bDisc2Ready = false;
    double m_phaseLow = 0.0;
    double m_phaseHigh = 1.0;

#ifdef J3D_TEST
    TestHooks m_testHooks;
#endif
};

#endif // JONES3D_PLATFORM_BABYLON_JONESSETUPWIZARD_H
