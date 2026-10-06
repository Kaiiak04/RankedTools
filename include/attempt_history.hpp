#pragma once
#include <string>
#include <vector>

namespace rankedpractice {
enum class AttemptOutcome { Unknown, Clear, Fail, Quit, Restart, Practice };
struct Attempt {
    std::string id; // Provider record ID, or a locally generated event ID.
    std::string source; // "local", "beatleader", or "scoresaber".
    std::string localId; // Preserve the local identity after matching a provider record.
    std::string hash;
    std::string characteristic{"Standard"};
    std::string difficulty;
    std::string modifiers;
    AttemptOutcome outcome{AttemptOutcome::Unknown};
    double timestamp{0.0};
    double endTime{0.0};
    double stars{0.0};
    double ratingChecked{0.0};
    bool normal{true};
    bool practice{false};
};
std::string AttemptChartKey(const Attempt& attempt);
bool IsTrainingAttempt(const Attempt& attempt);
void MergeAttempts(std::vector<Attempt>& history, const std::vector<Attempt>& incoming);
bool ReadAttemptHistory(const std::string& service, const std::string& playerId,
                        std::vector<Attempt>& attempts, std::string& error);
bool SaveAttemptHistory(const std::string& service, const std::string& playerId,
                        const std::vector<Attempt>& incoming, std::vector<Attempt>& merged,
                        std::string& error);
void InstallAttemptRecorder();
} // namespace rankedpractice
