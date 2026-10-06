#include "main.hpp"
#include "attempt_history.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "GlobalNamespace/GameplayCoreInstaller.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include <chrono>
#include <optional>
#include <thread>
#include <algorithm>
#include <vector>

namespace rankedpractice {
namespace {
struct RecordingProfile { std::string beatLeader, scoreSaber; };
std::optional<RecordingProfile> recordingProfile;
MAKE_HOOK_MATCH(RankedToolsBeginAttempt, &GlobalNamespace::GameplayCoreInstaller::InstallBindings,
                void, GlobalNamespace::GameplayCoreInstaller* self) {
    recordingProfile.reset();
    try {
        if (getModConfig().RecordLocalAttempts.GetValue()) {
            recordingProfile = RecordingProfile{getModConfig().BeatLeaderPlayerId.GetValue(),
                                                 getModConfig().ScoreSaberPlayerId.GetValue()};
        }
    } catch (const std::exception& ex) { PaperLogger.warn("Could not start attempt capture: {}", ex.what()); }
    catch (...) { PaperLogger.warn("Could not start attempt capture."); }
    RankedToolsBeginAttempt(self);
}
MAKE_HOOK_MATCH(RankedToolsEndAttempt, &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::Finish,
                void, GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
                GlobalNamespace::LevelCompletionResults* results) {
    using namespace GlobalNamespace;
    std::optional<Attempt> captured;
    auto profile = std::move(recordingProfile);
    recordingProfile.reset();
    try {
        if (profile && self && results && !results->invalidated && self->gameMode == "Solo") {
            auto key = self->beatmapKey;
            const std::string level = static_cast<std::string>(key.levelId);
            if (level.rfind("custom_level_", 0) == 0 && level.size() == 53 && key.beatmapCharacteristic) {
                Attempt a;
                a.source = "local";
                const auto now = std::chrono::system_clock::now().time_since_epoch();
                a.timestamp = std::chrono::duration<double>(now).count();
                a.id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
                a.hash = level.substr(13);
                a.characteristic = static_cast<std::string>(key.beatmapCharacteristic->serializedName);
                static constexpr const char* difficulties[]{"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};
                const int difficulty = static_cast<int>(key.difficulty);
                if (difficulty >= 0 && difficulty < 5) a.difficulty = difficulties[difficulty];
                a.endTime = std::max(0.0f, results->endSongTime);
                a.practice = self->practiceSettings != nullptr;
                auto* modifiers = results->gameplayModifiers;
                a.normal = modifiers && modifiers->IsWithoutModifiers();
                if (modifiers && !a.normal) {
                    std::vector<std::string> tokens;
                    auto add = [&](bool enabled, const char* token) { if (enabled) tokens.emplace_back(token); };
                    add(modifiers->noFailOn0Energy, "NF"); add(modifiers->zenMode, "ZEN");
                    add(modifiers->instaFail, "IF"); add(modifiers->failOnSaberClash, "SC");
                    add(modifiers->noBombs, "NB"); add(modifiers->noArrows, "NA");
                    add(modifiers->ghostNotes, "GN"); add(modifiers->disappearingArrows, "DA");
                    add(modifiers->fastNotes, "FN"); add(modifiers->proMode, "PM");
                    add(modifiers->smallCubes, "SCUB"); add(modifiers->strictAngles, "SA");
                    const int speed = static_cast<int>(modifiers->songSpeed);
                    add(speed == 1, "FS"); add(speed == 2, "SS"); add(speed == 3, "SF");
                    add(static_cast<int>(modifiers->energyType) != 0, "BE");
                    add(static_cast<int>(modifiers->enabledObstacleType) != 0, "NO");
                    std::sort(tokens.begin(), tokens.end());
                    for (const auto& token : tokens) { if (!a.modifiers.empty()) a.modifiers += ","; a.modifiers += token; }
                    if (a.modifiers.empty()) a.modifiers = "MODIFIED";
                }
                if (a.practice) a.outcome = AttemptOutcome::Practice;
                else if (results->levelEndStateType == LevelCompletionResults::LevelEndStateType::Failed) a.outcome = AttemptOutcome::Fail;
                else if (results->levelEndAction == LevelCompletionResults::LevelEndAction::Restart) a.outcome = AttemptOutcome::Restart;
                else if (results->levelEndAction == LevelCompletionResults::LevelEndAction::Quit) a.outcome = AttemptOutcome::Quit;
                else if (results->levelEndStateType == LevelCompletionResults::LevelEndStateType::Cleared) a.outcome = AttemptOutcome::Clear;
                captured = std::move(a);
            }
        }
    } catch (const std::exception& ex) { PaperLogger.warn("Could not capture attempt: {}", ex.what()); }
    catch (...) { PaperLogger.warn("Could not capture attempt."); }
    // Always chain the game/other mods' callback, including when capture fails.
    RankedToolsEndAttempt(self, results);
    if (!captured || !profile) return;
    try {
        std::thread([a = std::move(*captured), ids = std::move(*profile)] {
            try {
                for (const auto& [service, id] : {std::pair{"beatleader", ids.beatLeader}, std::pair{"scoresaber", ids.scoreSaber}}) {
                    if (id.empty()) continue;
                    std::string error; std::vector<Attempt> merged;
                    if (!SaveAttemptHistory(service, id, {a}, merged, error)) PaperLogger.warn("Could not store {} attempt: {}", service, error);
                }
            } catch (const std::exception& ex) { PaperLogger.warn("Attempt writer stopped: {}", ex.what()); }
            catch (...) { PaperLogger.warn("Attempt writer stopped."); }
        }).detach();
    } catch (const std::exception& ex) { PaperLogger.warn("Could not start attempt writer: {}", ex.what()); }
}
}
void InstallAttemptRecorder() {
    INSTALL_HOOK(PaperLogger, RankedToolsBeginAttempt);
    INSTALL_HOOK(PaperLogger, RankedToolsEndAttempt);
}
} // namespace rankedpractice
