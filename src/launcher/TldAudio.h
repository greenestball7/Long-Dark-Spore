// Reads The Long Dark's own Wwise audio from the player's install and converts the clips
// chosen in design/sheets/tld_content.json to WAV files in the mod's cache.
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace TldAudio {

struct Result {
	int contentIndex;        // row in Sheets::kContent
	std::wstring wavPath;    // empty when nothing usable was found
	std::string source;      // where it came from, for the log
};

// tldFolder: The Long Dark install folder. cacheDir: where WAVs go (created if missing).
// log: receives one line per step. Returns one Result per tld_content row.
// Reuses an earlier conversion when the install hasn't changed.
std::vector<Result> PrepareCache(const std::wstring& tldFolder, const std::wstring& cacheDir,
                                 const std::wstring& codebooksPath,
                                 const std::function<void(const std::string&)>& log);

}  // namespace TldAudio
