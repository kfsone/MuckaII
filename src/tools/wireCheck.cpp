#include "muckaii/mud/golden/golden.hpp"
#include "muckaii/persistence/wireReader.hpp"

#include "wireAudit.hpp"
#include "wireCheckOptions.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// muckaiiWireCheck: a manual tool that replays a recorded wire log through the decoder, and cuts
// parts of it out as capture files (docs/evidence/captures/README.md). It never writes the database.
namespace
{
using namespace muckaii;

constexpr int kExitOk { 0 };
constexpr int kExitFindings { 1 };
constexpr int kExitFailure { 2 };

// resolveDatabase returns the database to read: the one given, or the standard wire log.
std::optional<std::filesystem::path> resolveDatabase(const tools::WireCheckOptions& options)
{
    if (options.mDatabase.has_value())
        return options.mDatabase;
    return persistence::findDefaultDatabasePath();
}

// printRuns lists `runs` one per line.
void printRuns(const persistence::RunSummaries& runs)
{
    for (const persistence::RunSummary& run : runs)
    {
        std::cout << "run " << run.mId << ": " << run.mHost << " " << run.mClientVersion << ", "
                  << run.mRecords << " records" << (run.mEndedMs.has_value() ? "" : ", not closed") << "\n";
    }
}

// checkRuns audits the selected runs, prints each report, and returns the exit code.
int checkRuns(persistence::WireReader& reader, const tools::WireCheckOptions& options)
{
    int  exitCode { kExitOk };
    bool isRunFound { ! options.mRun.has_value() };
    for (const persistence::RunSummary& run : reader.listRuns())
    {
        if (options.mRun.has_value() && *options.mRun != run.mId)
            continue;
        isRunFound = true;
        const persistence::WireRecords records { reader.readRecords(run.mId) };
        if (! reader.getError().empty())
        {
            std::cerr << "run " << run.mId << ": " << reader.getError() << "\n";
            return kExitFailure;
        }
        const tools::RunAudit audit { tools::auditRun(run.mId, records) };
        for (const std::string& line : tools::formatAudit(audit))
            std::cout << line << "\n";
        if (! audit.isClean())
            exitCode = kExitFindings;
    }
    if (! isRunFound)
    {
        std::cerr << "no run " << *options.mRun << "\n";
        return kExitFailure;
    }
    return exitCode;
}

// exportRun writes the selected records of the run as a capture file that does not yet exist.
int exportRun(persistence::WireReader& reader, const tools::WireCheckOptions& options)
{
    if (std::filesystem::exists(options.mExportPath))
    {
        std::cerr << options.mExportPath.string() << " exists; captures are immutable, pick a new name\n";
        return kExitFailure;
    }
    const persistence::SeqRange    range { .mFirst = options.mFirstSeq, .mLast = options.mLastSeq };
    const persistence::WireRecords records { reader.readRecords(*options.mRun, range) };
    if (! reader.getError().empty())
    {
        std::cerr << reader.getError() << "\n";
        return kExitFailure;
    }
    const mud::golden::CaptureRecords capture { tools::toCaptureRecords(records, options.mIsIncludingSent) };
    if (capture.empty())
    {
        std::cerr << "no records to export\n";
        return kExitFailure;
    }
    const types::Bytes bytes { mud::golden::serializeCapture(capture) };
    std::ofstream      file { options.mExportPath, std::ios::binary };
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (! file)
    {
        std::cerr << "cannot write " << options.mExportPath.string() << "\n";
        return kExitFailure;
    }
    std::cout << "wrote " << capture.size() << " records to " << options.mExportPath.string()
              << "; check it for account ids and passwords before sharing\n";
    return kExitOk;
}

// runTool opens the database and performs the requested action.
int runTool(const tools::WireCheckOptions& options)
{
    const std::optional<std::filesystem::path> path { resolveDatabase(options) };
    if (! path.has_value())
    {
        std::cerr << "no home directory; pass --db\n";
        return kExitFailure;
    }
    persistence::WireReader reader { *path };
    if (! reader.isOpen())
    {
        std::cerr << path->string() << ": " << reader.getError() << "\n";
        return kExitFailure;
    }
    if (options.mAction == tools::WireCheckAction::kList)
    {
        printRuns(reader.listRuns());
        return kExitOk;
    }
    if (options.mAction == tools::WireCheckAction::kExport)
        return exportRun(reader, options);
    return checkRuns(reader, options);
}
} // namespace

// main parses the command line and runs the tool; it exits 1 when a run needs a look and 2 on failure.
int main(int argc, char** argv)
{
    const std::vector<std::string> args { argv + 1, argv + argc };
    std::string                    error;
    const auto                     options { tools::parseWireCheckArguments(args, error) };
    if (! options.has_value())
    {
        std::cerr << error << "\n\n" << tools::getWireCheckUsage();
        return kExitFailure;
    }
    if (options->mAction == tools::WireCheckAction::kHelp)
    {
        std::cout << tools::getWireCheckUsage();
        return kExitOk;
    }
    return runTool(*options);
}
