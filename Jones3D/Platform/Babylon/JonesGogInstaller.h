// The Babylon column's GOG offline installer driver. It finds the bundled innoextract program,
// reads the installer's licence text and extracts the game data with progress and cancel. The
// setup wizard calls it; this file has no UI of its own.
#ifndef JONES3D_PLATFORM_BABYLON_JONESGOGINSTALLER_H
#define JONES3D_PLATFORM_BABYLON_JONESGOGINSTALLER_H
#ifndef __cplusplus
#error "JonesGogInstaller.h is C++ only: it names babylon::process and std::string."
#endif

#include <babylon/process/Process.h>

#include <functional>
#include <string>
#include <string_view>

// Every path in this API is a UTF-8 string. The implementation converts it to and from
// std::filesystem::path with the platform's path encoding.

// The innoextract program to run, or "" when none can be run. Looks first for
// "<exeDir>/innoextract" ("innoextract.exe" when bWindows), using process.exists() on the full path,
// then for the bare name "innoextract" in the search path (process.exists("innoextract")).
// exeDir may be "" (skips the first step). Never throws.
std::string JonesGogInstaller_FindInnoextract(babylon::process::Process& process,
    const std::string& exeDir, bool bWindows);

// Extracts the installer's licence into tempDir and reads it.
// Runs: <innoPath> --color=0 --progress=0 -d <tempDir> -I tmp/EULA_en-US.txt -I tmp/EULA.txt <installerPath>
// then reads <tempDir>/tmp/EULA_en-US.txt, else <tempDir>/tmp/EULA.txt. The text is UTF-8: a leading UTF-8 BOM
// (EF BB BF) is removed and "\r\n" becomes "\n". Creates tempDir if missing.
// Returns true with outText filled. Returns false with outError set (English, one sentence, names the
// installer file name, not the full path) when innoextract can't start, exits non-zero, or neither file
// exists afterwards, or the file is larger than 1 MiB. outText is cleared on failure.
bool JonesGogInstaller_ReadEula(babylon::process::Process& process, const std::string& innoPath,
    const std::string& installerPath, const std::string& tempDir, std::string& outText,
    std::string& outError);

// What one line of innoextract output says. ESC '[' ... <final byte 0x40-0x7E> sequences are removed
// first, then leading spaces.
// - A line starting with `- "` names the file being extracted: bHasFile, file = the text between the
//   first two '"' characters (e.g. `Resource/CD1.GOB`).
// - A line containing a number followed by '%' (e.g. `[=====>      ]  8.1%   188 MiB/s`) carries progress:
//   bHasPercent, percent = that number (0..100, clamped). Use the last such number on the line.
// - A line may carry neither (e.g. `Extracting "..." - setup data version 5.6.2 (unicode)`, `Done.`).
struct JonesGogProgressLine
{
    bool bHasFile = false;
    std::string file;
    bool bHasPercent = false;
    double percent = 0.0;
};
JonesGogProgressLine JonesGogInstaller_ParseLine(std::string_view line);

enum class JonesGogExtractResult
{
    Ok,         // innoextract exited 0
    Cancelled,  // the cancel check asked to stop; extractDir is removed
    NotStarted, // innoextract could not be started
    Failed,     // innoextract exited non-zero; extractDir is removed
};

// Called for each progress line: percent 0..100 of the whole installer (last known value) and the
// file being extracted ("" until the first file line). Runs on the calling thread.
using JonesGogProgressFunc = std::function<void(double percent, const std::string& currentFile)>;

// Extracts the game data from the installer into extractDir (created if missing).
// Runs: <innoPath> --color=0 --progress=1 -d <extractDir> -I /Resource -I /KeySets -I /Jones3D.chm <installerPath>
// Every output line goes through JonesGogInstaller_ParseLine; onProgress (may be empty) is called whenever a
// line carries a percent or a file. cancelled (may be empty) is passed to Process::run.
// The last 20 non-progress lines are kept; on Failed outError is "innoextract failed (exit code N): " plus the
// last of those lines. On NotStarted outError says innoextract could not be started. On Ok outError is "".
JonesGogExtractResult JonesGogInstaller_Extract(babylon::process::Process& process,
    const std::string& innoPath, const std::string& installerPath, const std::string& extractDir,
    const JonesGogProgressFunc& onProgress, const babylon::process::CancelCheck& cancelled,
    std::string& outError);

// Removes dir and everything below it (std::filesystem::remove_all with an error_code). "" is a no-op.
// Never throws.
void JonesGogInstaller_RemoveDir(const std::string& dir);

#endif // JONES3D_PLATFORM_BABYLON_JONESGOGINSTALLER_H
