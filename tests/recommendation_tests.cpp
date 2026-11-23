// Tests use production internals directly; no network or Beat Saber runtime is required.
#include <fstream>
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>
#include <thread>
#define RANKEDTOOLS_ATTEMPT_DIRECTORY "tests/attempt-sandbox"
#define RANKEDTOOLS_CLAN_CURSOR_DIRECTORY "tests/clan-search-sandbox"
#include "../src/attempt_history.cpp"
#include "../src/beatleader_pp.cpp"
#include "../src/recommendation.cpp"
#include "../src/clan_recommendation.cpp"
#include "../src/ability_chart.cpp"
#include "../src/playlist_image.cpp"
#include "../src/account.cpp"
#include "../src/recommendation_api.cpp"
#include "refresh_settings.hpp"
#include "refresh_tasks.hpp"

using namespace rankedpractice;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string("Line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

std::string ReadFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::vector<MapEntry> Parse(const std::string& json) {
    std::vector<MapEntry> entries;
    std::string error;
    CHECK(ParseEntries(json, entries, error));
    return entries;
}
std::vector<MapEntry> Fixture(const std::string& name) { return Parse(ReadFile("tests/fixtures/" + name + ".json")); }
std::string Hash(int id) {
    std::ostringstream hash;
    hash << std::hex << std::setw(40) << std::setfill('0') << id;
    return hash.str();
}
MapEntry Score(int id, double pp = 100, const char* mode = "Standard") {
    MapEntry score;
    score.hash = Hash(id); score.songName = "Test"; score.pp = pp; score.accuracy = .95;
    score.stars = 7; score.difficulties = {{mode, "ExpertPlus", 7, 8, 8, 5}};
    return score;
}
std::string Chart(int id, double stars, bool score = false, const std::string& modifiers = "", const char* mode = "Standard") {
    std::ostringstream json;
    json << "{\"hash\":\"" << Hash(id) << "\",\"name\":\"Song " << id
         << "\",\"difficultyName\":\"ExpertPlus\",\"modeName\":\"" << mode << "\",\"stars\":" << stars
         << ",\"accRating\":8,\"passRating\":8,\"techRating\":5";
    if (score) json << ",\"baseScore\":95,\"maxScore\":100,\"accuracy\":0.95,\"pp\":100,\"modifiers\":\"" << modifiers << "\"";
    json << '}';
    return json.str();
}
std::string Page(int page, int total, const std::vector<std::string>& rows) {
    std::ostringstream json;
    json << "{\"metadata\":{\"page\":" << page << ",\"total\":" << total << "},\"data\":[";
    for (size_t i = 0; i < rows.size(); ++i) { if (i) json << ','; json << rows[i]; }
    json << "]}";
    return json.str();
}
double Time(const std::string& value) {
    rapidjson::Document json;
    json.Parse(value.c_str());
    CHECK(!json.HasParseError());
    return TimestampValue(json);
}

void DatesAndParsing() {
    CHECK(Time("\"2024-01-01T00:00:00Z\"") == 1704067200);
    CHECK(Time("\"2024-01-01T01:00:00.125+01:00\"") == 1704067200.125);
    CHECK(Time("\"2023-12-31T17:00:00-07:00\"") == 1704067200);
    CHECK(Time("\"1704067200\"") == 1704067200);
    CHECK(Time("1704067200") == 1704067200);
    for (const auto& invalid : {"\"2024-02-30T00:00:00Z\"", "\"2024-01-01T25:00:00Z\"",
                               "\"2024-01-01T00:00:00\"", "\"1704067200garbage\"", "null"}) CHECK(Time(invalid) == 0);
    auto scores = Fixture("scoresaber-scores");
    CHECK(scores.size() == 3);
    for (const auto& score : scores) CHECK(score.timepost > 1500000000);
    auto recent = scores.front(); recent.timepost = 1704067200;
    auto old = recent; old.timepost -= 365 * 86400;
    CHECK(ObservationWeight(recent, recent.timepost) == 1);
    CHECK(std::abs(ObservationWeight(old, recent.timepost) - std::exp(-1.0)) < 1e-12);
    CHECK(Fixture("scoresaber-legacy-maps").size() == 14);
    auto v2 = Fixture("scoresaber-v2-maps");
    CHECK(v2.size() == 3);
    CHECK(v2.front().stars == 7.8 && v2.front().difficulties.front().name == "Hard");
    CHECK(v2.front().difficulties.front().characteristic == "Standard");
    auto maps = Fixture("beatleader-maps");
    std::set<std::string> keys;
    for (const auto& entry : maps) CHECK(keys.insert(EntryKey(entry)).second);
    CHECK(!maps.empty());
    auto unrated = Parse(Chart(99, 0, true));
    CHECK(unrated.size() == 1 && unrated.front().stars == 0);
    std::cout << "PASS: ISO dates, recency, API schemas, one entry per chart, unrated history\n";
}

void AccuracyTrainingContract() {
    auto point = [](int id, double stars, double accuracy) {
        auto score = Score(id);
        score.stars = score.difficulties[0].stars = stars;
        score.accuracy = accuracy;
        return score;
    };
    auto close = [](double actual, double expected) { return std::abs(actual - expected) < 1e-10; };
    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber, RatingSystem::AccSaber}) {
        auto best = point(1, 6, .90), duplicate = point(1, 6, .99);
        best.pp = 200;
        AccuracyCurve deduplicated({best, duplicate}, system);
        CHECK(deduplicated.Observations().size() == 1 && close(deduplicated.Predict(6), .90));
        CHECK(close(deduplicated.Predict(3), .90) && close(deduplicated.Predict(12), .90));
        auto oneSaber = duplicate;
        oneSaber.difficulties[0].characteristic = "OneSaber";
        AccuracyCurve mixed({best, oneSaber}, system);
        if (system == RatingSystem::BeatLeader) {
            CHECK(mixed.Observations().size() == 2);
            CHECK(mixed.Predict(6) > .90 && mixed.Predict(6) < .99);
        } else {
            CHECK(mixed.Observations().size() == 1 && close(mixed.Predict(6), .90));
        }
        auto modified = point(2, 7, .90); modified.modifiedStars = 10;
        AccuracyCurve effectiveStars({modified, point(3, 9, .95)}, system);
        CHECK(effectiveStars.MinStars() == 9 && effectiveStars.MaxStars() == 10);
        std::vector<MapEntry> excluded;
        auto nf = point(10, 5, .99); nf.noFail = true; excluded.push_back(nf);
        auto unranked = point(11, 5, .99); unranked.ranked = false; excluded.push_back(unranked);
        auto otherMode = point(12, 5, .99); otherMode.difficulties[0].characteristic = "NoArrows"; excluded.push_back(otherMode);
        auto noHash = point(13, 5, .99); noHash.hash.clear(); excluded.push_back(noHash);
        auto noPp = point(14, 5, .99); noPp.pp = 0; excluded.push_back(noPp);
        excluded.push_back(point(15, -1, .99));
        excluded.push_back(point(16, 5, .54));
        excluded.push_back(point(17, 5, 1.01));
        excluded.push_back(point(18, std::numeric_limits<double>::infinity(), .99));
        excluded.push_back(point(19, 5, std::numeric_limits<double>::quiet_NaN()));
        CHECK(!AccuracyCurve(excluded, system).HasObservations());
        const auto zero = point(20, 0, .95);
        CHECK(AccuracyCurve({zero}, system).HasObservations() == (system == RatingSystem::AccSaber));
        CHECK(AccuracyCurve({}, system).Predict(7) == .85);
        CHECK(AccuracyCurve({point(21, 7, 1)}, system).Predict(7) == .999);
        CHECK(close(AccuracyCurve({point(22, 7, .55)}, system).Predict(9), .55));
    }
    // Personal selectors for both star-based services use the shared fit.
    const std::vector<MapEntry> profile{point(1, 5, .96), point(2, 6, .92), point(3, 7, .94)};
    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber}) {
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove}) {
            auto candidate = kind == ListKind::ToImprove ? profile[1] : point(4, 6, .60);
            candidate.pp = 1;
            candidate.difficulties[0].stars = 6;
            const auto result = SelectRecommendations({candidate}, kind, 10, 6, profile, system, 0.0);
            CHECK(result.size() == 1);
            CHECK(close(result[0].predictedAccuracy, AccuracyCurve(profile, system).Predict(6)));
        }
    }
    std::cout << "PASS: shared accuracy eligibility, deduplication, effective stars, zero complexity, sparse fits and both star-based selectors\n";
}

void SigmoidAccuracy() {
    rapidjson::Document reference;
    reference.Parse(ReadFile("tests/fixtures/beatleader-sigmoid30.json").c_str());
    CHECK(!reference.HasParseError() && reference["cases"].IsArray());
    const double now = reference["now"].GetDouble();
    for (const auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber, RatingSystem::AccSaber})
    for (const auto& sample : reference["cases"].GetArray()) {
        std::vector<MapEntry> history;
        for (const auto& row : sample["observations"].GetArray()) {
            auto score = Score(10000 + static_cast<int>(history.size()));
            score.stars = row["stars"].GetDouble();
            score.accuracy = row["accuracy"].GetDouble();
            score.timepost = row["time"].GetDouble();
            history.push_back(score);
        }
        const AccuracyCurve fit(history, system, false, now);
        CHECK(fit.HasObservations());
        for (const auto& expected : sample["predictions"].GetArray()) {
            CHECK(std::abs(fit.Predict(expected["stars"].GetDouble()) - expected["accuracy"].GetDouble()) < 1e-12);
        }
        std::reverse(history.begin(), history.end());
        const AccuracyCurve reversed(history, system, false, now);
        double previous = 1.0;
        for (int i = 0; i <= 200; ++i) {
            const double prediction = fit.Predict(i * .1);
            CHECK(std::isfinite(prediction) && prediction >= .55 && prediction <= .999);
            CHECK(prediction <= previous + 1e-12);
            CHECK(std::abs(prediction - reversed.Predict(i * .1)) < 1e-12);
            previous = prediction;
        }
    }
    auto recent = Score(11000); recent.stars = 5; recent.accuracy = .96; recent.timepost = now;
    auto old = Score(11001); old.stars = 8; old.accuracy = .88; old.timepost = now - 30 * 86400;
    const std::vector<MapEntry> history{recent, old};
    const AccuracyCurve current(history, RatingSystem::BeatLeader, false, now);
    CHECK(current.Observations()[0].weight == 1.0);
    CHECK(std::abs(current.Observations()[1].weight - std::exp(-1.0)) < 1e-12);
    // Advancing time with no new scores preserves the fit, even if absolute
    // weights underflow. Confidence/dot opacity still use absolute score ages.
    const AccuracyCurve inactive(history, RatingSystem::BeatLeader, false, now + 1000.0 * 365 * 86400);
    CHECK(inactive.HasObservations());
    for (int i = 0; i <= 200; ++i) CHECK(std::abs(current.Predict(i * .1) - inactive.Predict(i * .1)) < 1e-10);
    CHECK(std::abs(ObservationWeight(old, now) - std::exp(-30.0 / 365.0)) < 1e-12);

    auto nf = old; nf.hash = Hash(11002); nf.noFail = true; nf.accuracy = .99;
    auto lowerPb = recent; lowerPb.pp = 1; lowerPb.accuracy = .60;
    const AccuracyCurve filtered({recent, old, nf, lowerPb}, RatingSystem::BeatLeader, false, now);
    CHECK(filtered.Observations().size() == 2);
    CHECK(std::abs(filtered.Predict(6) - current.Predict(6)) < 1e-12);
    auto modified = old; modified.stars = 2; modified.modifiedStars = 8;
    CHECK(std::abs(AccuracyCurve({recent, modified}, RatingSystem::BeatLeader, false, now).Predict(6) - current.Predict(6)) < 1e-12);
    CHECK(AccuracyCurve({}).Predict(7) == .85);

    for (const auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber, RatingSystem::AccSaber}) {
        const auto chart = BuildAbilityChart(history, system == RatingSystem::AccSaber, system);
        const AccuracyCurve expected(history, system);
        for (const auto& p : chart.curve) CHECK(std::abs(p.accuracy - expected.Predict(p.stars)) < 1e-12);
        const AccuracyCurve dated(history, system, false, now);
        CHECK(std::abs(dated.Observations()[1].weight - std::exp(-1.0)) < 1e-12);
        for (double stars : {0., 3., 6., 9., 12.}) CHECK(std::abs(dated.Predict(stars) - current.Predict(stars)) < 1e-12);
    }
    std::cout << "PASS: all three providers match frozen sigmoid research outputs, 30-day decay, bounds, sparse fits, long inactivity and charts\n";
}

void BeatLeaderV4Contract() {
    Difficulty zeroRatings; zeroRatings.noteSpan = 120;
    CHECK(BeatLeaderPp(.98, zeroRatings) == 0);
    for (const char* fixture : {"tests/fixtures/beatleader-ppv4.json", "tests/fixtures/beatleader-ppv4.1-live.json"}) {
        rapidjson::Document reference;
        reference.Parse(ReadFile(fixture).c_str());
        CHECK(!reference.HasParseError() && reference["cases"].IsArray());
        for (const auto& sample : reference["cases"].GetArray()) {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            sample["difficulty"].Accept(writer);
            const std::string chart = std::string(R"({"hash":")") + Hash(400) +
                R"(","name":"PPv4","difficulties":[)" + buffer.GetString() + "]}";
            const auto entries = Parse(chart);
            CHECK(entries.size() == 1);
            const auto& d = entries[0].difficulties.front();
            CHECK(d.noteSpan.has_value());
            CHECK(d.predictedAcc == sample["difficulty"]["predictedAcc"].GetDouble());
            CHECK(d.sliderPercentage == sample["difficulty"]["sliderPercentage"].GetDouble());
            CHECK(d.notes == sample["difficulty"]["notes"].GetDouble());
            for (const auto& score : sample["scores"].GetArray()) {
                const double expected = score[1].GetDouble();
                const double tolerance = sample.HasMember("live") ? 1e-5 : 1e-9;
                CHECK(std::abs(BeatLeaderPp(score[0].GetDouble(),d)-expected) <= tolerance*std::max(1.0,expected));
            }
            // The score and hash-lookup response shapes must retain the same inputs.
            const auto scored = Parse(std::string(R"({"baseScore":95,"accuracy":0.95,"pp":100,"leaderboard":{"song":{"hash":")") +
                Hash(400) + R"(","name":"PPv4"},"difficulty":)" + buffer.GetString() + "}}");
            CHECK(scored.size() == 1);
            CHECK(BeatLeaderPp(.98,scored[0].difficulties[0]) == BeatLeaderPp(.98,d));
            CHECK(BeatLeaderPp(0,d) == 0 && BeatLeaderPp(1.01,d) == 0);
        }
    }
    auto zeroSpan = Parse(Chart(401,7).insert(1,R"("noteSpan":0,"predictedAcc":0.98,)"));
    CHECK(zeroSpan.size() == 1 && zeroSpan[0].difficulties[0].noteSpan == 0);
    auto nullSpan = Parse(Chart(401,7).insert(1,R"("noteSpan":null,"predictedAcc":0.98,)"));
    auto absentSpan = Parse(Chart(401,7));
    CHECK(!nullSpan[0].difficulties[0].noteSpan && !absentSpan[0].difficulties[0].noteSpan);
    CHECK(BeatLeaderPp(.98,nullSpan[0].difficulties[0]) == BeatLeaderPp(.98,absentSpan[0].difficulties[0]));
    CHECK(BeatLeaderPp(.98,zeroSpan[0].difficulties[0]) != BeatLeaderPp(.98,nullSpan[0].difficulties[0]));
    const auto mixed = Parse(std::string(R"({"hash":")") + Hash(401) + R"(","name":"Mixed","difficulties":[
        {"difficultyName":"ExpertPlus","stars":7,"accRating":8,"passRating":8,"techRating":5,"noteSpan":120,"predictedAcc":0.98},
        {"difficultyName":"Expert","stars":7,"accRating":8,"passRating":8,"techRating":5,"noteSpan":null},
        {"difficultyName":"Hard","stars":7,"accRating":8,"passRating":8,"techRating":5}]})");
    CHECK(mixed.size() == 3 && mixed[0].difficulties[0].noteSpan &&
        !mixed[1].difficulties[0].noteSpan && !mixed[2].difficulties[0].noteSpan);

    auto easyAcc = Score(402,0), hardAcc = Score(403,0), training = Score(404);
    easyAcc.songName = "Easy acc"; hardAcc.songName = "Hard acc";
    for (auto* e : {&easyAcc,&hardAcc}) {
        auto& d = e->difficulties[0];
        d.noteSpan = 120; d.notes = 200;
    }
    easyAcc.difficulties[0].predictedAcc = .99;
    hardAcc.difficulties[0].predictedAcc = .96;
    training.accuracy = .98;
    const auto selected = SelectRecommendations({easyAcc,hardAcc},ListKind::NotPlayed,16,0,{training},RatingSystem::BeatLeader,0);
    CHECK(selected.size() == 2);
    CHECK(selected[0].predictedPp > selected[1].predictedPp);
    for (const auto& e : selected) CHECK(std::abs(e.predictedPp - BeatLeaderPp(e.predictedAccuracy,e.difficulties[0])) < 1e-9);
    auto improvement = easyAcc; improvement.pp = 100;
    const auto improved = SelectRecommendations({improvement},ListKind::ToImprove,16,0,{training,improvement},RatingSystem::BeatLeader,0);
    CHECK(improved.size() == 1 && improved[0].predictedPp == BeatLeaderPp(improved[0].predictedAccuracy,improvement.difficulties[0]));
    std::cout << "PASS: PPv4.1 upstream reference and live scores, per-map curves, mixed rollout, both recommendation modes\n";
}

void ModifiersAndPp() {
    auto scores = Fixture("beatleader-scores");
    CHECK(scores.size() == 8);
    CHECK(std::abs(scores[4].modifiedStars - 10.393583) < 1e-6);
    CHECK(std::abs(scores[6].modifiedStars - 10.205809) < 1e-6);
    rapidjson::Document chart;
    chart.Parse(R"({"modifiersRating":{"ssStars":5,"fsStars":9,"sfStars":11},"modifierValues":{"ss":-0.3,"fs":0.2,"sf":0.4,"gn":0.04,"nf":-1}})");
    for (const auto& [modifiers, expected] : {std::pair{"SS", 5.0}, {"FS", 9.0}, {"SF", 11.0}, {"FS,GN", 9.36}, {"FS,NF", 9.0}}) {
        Context context; context.stars = 7; context.modifiers = ModifierTokens(modifiers);
        DeriveModifiedStars(chart, context);
        CHECK(std::abs(context.modifiedStars - expected) < 1e-9);
    }
    chart.Parse(R"({"modifierValues":{"fs":0.2}})");
    Context legacy; legacy.stars = 7; legacy.modifiers = {"FS"};
    DeriveModifiedStars(chart, legacy);
    CHECK(std::abs(legacy.modifiedStars - 8.4) < 1e-9);
    auto nf = Fixture("beatleader-nf");
    CHECK(nf.size() == 2);
    for (const auto& entry : nf) CHECK(entry.noFail);
    CHECK(MakeWeightedProfile(nf).scores.empty());
    CHECK(SelectRecommendations(nf, ListKind::ToImprove, 16, 0, nf).empty());

    auto standard = Score(1, 100), oneSaber = Score(2, 500, "OneSaber"), candidate = Score(3, 0);
    auto profile = MakeWeightedProfile({standard, oneSaber, standard});
    CHECK(profile.scores.size() == 2);
    CHECK(std::abs(WeightedProfileChange(profile, ScoreKey(candidate, candidate.difficulties[0]), 300, true) - 286.1225) < 1e-6);
    CHECK(WeightedProfileChange(profile, ScoreKey(standard, standard.difficulties[0]), 300, true) == 0);
    CHECK(std::abs(WeightedProfileChange(profile, ScoreKey(standard, standard.difficulties[0]), 300, false) - 193) < 1e-6);
    CHECK(WeightedProfileChange(profile, ScoreKey(oneSaber, oneSaber.difficulties[0]), 400, false) == 0);
    oneSaber.ranked = false;
    CHECK(MakeWeightedProfile({standard, oneSaber}).scores.size() == 1);
    rapidjson::Document curve;
    curve.Parse(ReadFile("tests/fixtures/scoresaber-curve.json").c_str());
    for (const auto& point : curve["curve"].GetArray()) {
        CHECK(std::abs(ScoreSaberPp(1, point[0].GetDouble()) / 42.1168 - point[1].GetDouble()) < 1e-6);
    }
    for (size_t i : {0, 1, 2, 3, 5, 7}) {
        CHECK(std::abs(BeatLeaderPp(scores[i].accuracy, scores[i].difficulties.front()) - scores[i].pp) / scores[i].pp < 1e-5);
    }
    std::cout << "PASS: modifier-adjusted stars, NF exclusion, full profile weighting, public PP formulas\n";
}

void BeatLeaderSpeedRecommendations() {
    // Live PPv4.1 chart 2b3db71 and Faster Song score 14191264.
    const auto charts = Parse(R"({
        "hash":"1000000000000000000000000000000000000001","name":"Spoiled little brat",
        "difficultyName":"Expert","modeName":"Standard","stars":8.942231,
        "accRating":10.417408,"passRating":6.6918406,"techRating":5.0402036,
        "predictedAcc":0.9754327,"sliderPercentage":0.014176632,"notes":957,"noteSpan":156.27632,
        "modifierValues":{"fs":0.4},
        "modifiersRating":{
            "fsStars":10.234646,"fsAccRating":11.707053,"fsPassRating":8.335658,
            "fsTechRating":5.5636997,"fsPredictedAcc":0.970846,"fsSliderPercentage":0.014176715,
            "bsfStars":11,"bsfAccRating":20,"bsfPassRating":15,"bsfTechRating":8,"bsfPredictedAcc":0.970846
        }
    })");
    const auto faster = BeatLeaderSpeed::FasterSong;
    CHECK(charts.size() == 1);
    const auto& chart = charts.front().difficulties.front();
    const auto rated = BeatLeaderDifficultyAtSpeed(chart, faster);
    CHECK(rated && rated->stars == 10.234646 && rated->sliderPercentage == 0.014176715);
    CHECK(rated->noteSpan && std::abs(*rated->noteSpan - 156.27632 / 1.2) < 1e-10);
    CHECK(std::abs(PredictBeatLeaderPp(0.94026875, *rated) - 472.02783) < .001);
    // Posted No Arrows score 30119894 uses HE4VEN's normal-speed curve.
    const Difficulty noArrows{"Standard", "Expert", 9.865554, 11.5205555, 7.965426, 4.2994018,
                              0.97548926, 0.006586201, 1081.0, 119.53125};
    CHECK(std::abs(BeatLeaderPpv4(0.75360864, noArrows, .7) - 190.66856) < .001);
    CHECK(!BeatLeaderDifficultyAtSpeed(chart, BeatLeaderSpeed::SuperFastSong));
    auto incomplete = chart;
    incomplete.speedRatings[1].techRating = -1;
    CHECK(!BeatLeaderDifficultyAtSpeed(incomplete, faster));

    auto player = Score(10);
    player.accuracy = 0.94026875;
    player.stars = player.difficulties.front().stars = 9;
    auto previous = charts.front();
    previous.pp = 440;
    previous.accuracy = player.accuracy;
    const std::vector profile{player, previous};
    auto normal = SelectRecommendations(charts, ListKind::ToImprove, 16, 0, profile, RatingSystem::BeatLeader, 0);
    CHECK(normal.empty());
    const auto improvement = SelectRecommendations(charts, ListKind::ToImprove, 16, 0, profile,
        RatingSystem::BeatLeader, 0, false, {}, true);
    CHECK(improvement.size() == 1);
    CHECK(improvement[0].recommendedModifier == "Faster Song (1.20×)" && improvement[0].currentPp == 440);
    CHECK(improvement[0].stars == rated->stars && improvement[0].difficulties.front().stars == chart.stars);
    CHECK(std::abs(improvement[0].weightedPpGain - (improvement[0].predictedPp - 440)) < 1e-8);
    CHECK(improvement[0].clearChance < .50);
    CHECK(SelectRecommendations(charts, ListKind::ToImprove, 16, 0, profile,
        RatingSystem::BeatLeader, .50, false, {}, true).empty());
    const auto easierThreshold = SelectRecommendations(charts, ListKind::ToImprove, 16, 0, profile,
        RatingSystem::BeatLeader, improvement[0].clearChance / 2, false, {}, true);
    CHECK(easierThreshold.size() == 1 && easierThreshold[0].recommendedModifier == "Faster Song (1.20×)");

    auto beatable = previous;
    beatable.pp = 400;
    const auto fallback = SelectRecommendations(charts, ListKind::ToImprove, 16, 0, {player, beatable},
        RatingSystem::BeatLeader, .50, false, {}, true);
    CHECK(fallback.size() == 1 && fallback[0].recommendedModifier.empty() && fallback[0].currentPp == 400);

    auto competing = charts;
    auto& options = competing.front().difficulties.front();
    options.speedRatings[0] = {9.0, 13.0, 9.0, 5.0, .97, std::nullopt};
    options.speedRatings[2] = {10.2, 11.0, 10.0, 5.0, .965, std::nullopt};
    options.modifierValues[0] = .04;
    options.modifierValues[1] = .02;
    options.modifierValues[3] = -.2;
    const auto best = SelectRecommendations(competing, ListKind::ToImprove, 16, 0, profile,
        RatingSystem::BeatLeader, 0, false, {}, true);
    CHECK(best.size() == 1);
    CHECK(best[0].recommendedModifier == "Slower Song (0.85×) + Disappearing Arrows");

    auto catalogPlayer = player;
    catalogPlayer.pp = 600;
    ScoreHistory history{true, "123", RatingSystem::BeatLeader, {catalogPlayer}};
    auto incoming = Chart(24, 7);
    incoming.pop_back();
    incoming += R"(,"modifiersRating":{"fsStars":9,"fsAccRating":12,"fsPassRating":9,"fsTechRating":6}})";
    int pages = 0;
    auto catalog = [&](const std::string& url, std::string& body, std::string&) {
        CHECK(url.find("stars_from=") == std::string::npos);
        ++pages;
        body = Page(pages, 2, {pages == 1 ? Chart(25, 12) : incoming});
        return true;
    };
    std::string error;
    std::vector<MapEntry> result;
    CHECK(FetchServiceRecommendations(RatingSystem::BeatLeader, "123", ListKind::Combined, 16,
        result, error, 0, nullptr, &history, catalog, false, false, true));
    CHECK(pages == 2 && result.size() == 1 && result[0].hash == Hash(24));
    CHECK(result[0].recommendedModifier == "Faster Song (1.20×)" && result[0].stars == 9);
    std::cout << "PASS: live speed-modifier PP parity, effective stars, and minimum clear rate on harder replays\n";
}

void PaginationAndCache() {
    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber}) {
        ScoreHistory cache;
        int calls = 0;
        HttpGetter get = [&](const std::string& url, std::string& body, std::string&) {
            ++calls;
            CHECK(url.find("page=" + std::to_string(calls)) != std::string::npos);
            // Include an unrated score and late NF evidence beyond both previous page caps.
            body = Page(calls, 55, {Chart(calls, calls == 2 ? 0 : 7, true, calls == 55 ? "NF" : "")});
            return true;
        };
        std::string error;
        CHECK(LoadScoreHistory("123", system, cache, error, get, false, false));
        CHECK(calls == 55 && cache.loaded && cache.scores.size() == 55 && cache.scores.back().noFail);
        CHECK(LoadScoreHistory("123", system, cache, error, get, false, false));
        CHECK(calls == 55);
        std::vector<MapEntry> result;
        CHECK(FetchServiceRecommendations(system, "123", ListKind::ToImprove, 16, result, error,
              0.50, nullptr, &cache, get, false));
        CHECK(calls == 55);
    }
    std::vector<MapEntry> entries;
    std::string error;
    int calls = 0;
    HttpGetter skipEmptyParsed = [&](const std::string&, std::string& body, std::string&) {
        ++calls; body = Page(calls, 2, {calls == 1 ? "{}" : Chart(2, 7)}); return true;
    };
    CHECK(FetchAllPages([](int) { return "test"; }, entries, error, skipEmptyParsed, false));
    CHECK(calls == 2 && entries.size() == 1);
    for (const auto& invalid : {std::string("broken"), std::string("{}"), Page(2, 1, {Chart(1, 7)}), Page(1, 1, {}),
                               std::string(R"({"metadata":{"page":1,"total":2,"totalPages":1},"data":[{}]})")}) {
        ScoreHistory cache;
        HttpGetter get = [&](const std::string&, std::string& body, std::string&) { body = invalid; return true; };
        CHECK(!LoadScoreHistory("123", RatingSystem::BeatLeader, cache, error, get, false, false));
        CHECK(!cache.loaded && cache.scores.empty());
    }
    ScoreHistory cache;
    calls = 0;
    HttpGetter fail = [&](const std::string&, std::string& body, std::string& message) {
        if (++calls == 2) { message = "network failure"; return false; }
        body = Page(1, 2, {Chart(1, 7, true)}); return true;
    };
    CHECK(!LoadScoreHistory("123", RatingSystem::ScoreSaber, cache, error, fail, false, false));
    CHECK(!cache.loaded && cache.scores.empty());
    HttpGetter repeated = [&](const std::string&, std::string& body, std::string&) { body = Page(0, 2, {Chart(1, 7, true)}); return true; };
    CHECK(!LoadScoreHistory("123", RatingSystem::BeatLeader, cache, error, repeated, false, false));
    HttpGetter empty = [&](const std::string&, std::string& body, std::string&) { body = Page(1, 0, {}); return true; };
    CHECK(LoadScoreHistory("123", RatingSystem::BeatLeader, cache, error, empty, false, false));
    CHECK(cache.loaded && cache.scores.empty());
    std::cout << "PASS: complete long histories, raw page termination, cache reuse, incomplete/error responses\n";
}

void CompleteCandidateBand() {
    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber}) {
        ScoreHistory cache{true, "123", system, {Score(999)}};
        std::vector<MapEntry> result;
        std::string error;
        int calls = 0;
        HttpGetter get = [&](const std::string& url, std::string& body, std::string&) {
            ++calls;
            if (system == RatingSystem::ScoreSaber) {
                CHECK(url.find("/api/v2/leaderboards?") != std::string::npos);
                CHECK(url.find("limit=100&minStars=") != std::string::npos && url.find("&maxStars=") != std::string::npos);
            } else CHECK(url.find("type=ranked&count=100&stars_from=") != std::string::npos);
            const auto* mode = system == RatingSystem::BeatLeader && calls == 2 ? "OneSaber" : "Standard";
            body = Page(calls, 2, {Chart(calls, calls == 1 ? 8.5 : 6, false, "", mode)}); return true;
        };
        CHECK(FetchServiceRecommendations(system, "123", ListKind::NotPlayed, 16, result, error,
              0.50, nullptr, &cache, get, false));
        CHECK(calls == 2 && result.size() == 1 && result.front().hash == Hash(2));
        CHECK(result.front().difficulties.front().characteristic ==
              (system == RatingSystem::BeatLeader ? "OneSaber" : "Standard"));
        calls = 0;
        HttpGetter laterBest = [&](const std::string&, std::string& body, std::string&) {
            ++calls; body = Page(calls, 2, {Chart(calls, calls == 1 ? 6 : 8)}); return true;
        };
        CHECK(FetchServiceRecommendations(system, "123", ListKind::NotPlayed, 1, result, error,
              0.0, nullptr, &cache, laterBest, false));
        CHECK(calls == 2);
        // SS PP rises with stars; identical BL component ratings tie, resolved by song name.
        if (system == RatingSystem::ScoreSaber) CHECK(result.front().hash == Hash(2));
    }
    ScoreHistory cache{true, "123", RatingSystem::BeatLeader, {Score(999)}};
    int calls = 0;
    HttpGetter get = [&](const std::string&, std::string& body, std::string&) {
        std::vector<std::string> rows;
        for (int id = (calls == 0 ? 1 : 12); id <= (calls == 0 ? 11 : 17); ++id) {
            std::ostringstream row;
            row << "{\"hash\":\"" << Hash(id) << "\",\"name\":\"Song " << id << "\",\"difficulties\":[";
            for (int diff = 0; diff < 4; ++diff) {
                if (diff) row << ',';
                row << "{\"difficultyName\":\"" << std::array{"Normal", "Hard", "Expert", "ExpertPlus"}[diff]
                    << "\",\"modeName\":\"Standard\",\"stars\":7,\"accRating\":8,\"passRating\":8,\"techRating\":5}";
            }
            row << "]}"; rows.push_back(row.str());
        }
        body = Page(++calls, 17, rows); return true;
    };
    std::vector<MapEntry> result;
    std::string error;
    CHECK(FetchServiceRecommendations(RatingSystem::BeatLeader, "123", ListKind::NotPlayed, 16, result, error,
          0.0, nullptr, &cache, get, false));
    CHECK(calls == 2 && result.size() == 16);
    std::set<std::string> songs;
    for (const auto& song : result) CHECK(songs.insert(song.hash).second);
    std::cout << "PASS: later clearable/better candidates, supported SS endpoint, full distinct-song limit\n";
}

void BeatLeaderOneSaberGain() {
    const auto system = RatingSystem::BeatLeader;
    ScoreHistory cache{true, "123", system, {Score(1, 900), Score(2, 50, "OneSaber")}};
    auto unranked = Chart(3, 7, false, "", "OneSaber");
    unranked.insert(1, "\"status\":2,");
    const HttpGetter get = [&](const std::string& url, std::string& body, std::string&) {
        CHECK(url.find("/maps?type=ranked&count=100") != std::string::npos);
        body = Page(1, 4, {Chart(1, 7, false, "", "OneSaber"), Chart(2, 7, false, "", "OneSaber"),
                           unranked, Chart(4, 7, false, "", "NoArrows")});
        return true;
    };
    std::string error;
    auto check = [&](ListKind kind, bool newChart, bool replay) {
        std::vector<MapEntry> result;
        CHECK(FetchServiceRecommendations(system, "123", kind, 16, result, error, 0.0, nullptr, &cache, get, false));
        CHECK(result.size() == static_cast<size_t>(newChart + replay));
        for (const auto& entry : result) {
            CHECK(entry.difficulties.front().characteristic == "OneSaber");
            CHECK(entry.weightedPpGain > 0);
            if (entry.isImprovement) CHECK(replay && entry.hash == Hash(2) && entry.currentPp == 50);
            else CHECK(newChart && entry.hash == Hash(1) && entry.currentPp == 0);
        }
    };
    check(ListKind::NotPlayed, true, false); // A Standard PB on this exact song and difficulty is independent.
    check(ListKind::ToImprove, false, true);
    check(ListKind::Combined, true, true);
}

ScoreHistory ClanTestHistory() {
    ScoreHistory history; history.loaded = true; history.playerId = "123";
    history.scores = {Score(90000)}; return history;
}
bool FetchTestClans(const std::string& id, bool all, int limit,
                    std::vector<ClanPlaylistRecommendations>& clans, std::string& error,
                    const HttpGetter& get, const HttpGetter& icon) {
    auto history = ClanTestHistory();
    HttpGetter wrapped = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/maps?type=ranked") != std::string::npos) { body = R"({"data":[],"metadata":{"total":0}})"; return true; }
        if (url.find("/clans/globalmap") != std::string::npos) { body = R"({"points":[]})"; return true; }
        return get(url, body, failure);
    };
    return FetchClanToConquerInternal(id, all, limit, clans, error, wrapped, icon, .5, &history, false);
}

void ClansAndSettings() {
    std::string error;
    std::vector<ClanPlaylistRecommendations> clans;
    for (const auto& profile : {R"({"clans":[],"clanOrder":"OLD"})", R"({"clans":null})"}) {
        int calls = 0;
        HttpGetter get = [&](const std::string&, std::string& body, std::string&) { ++calls; body = profile; return true; };
        CHECK(FetchTestClans("123", true, 16, clans, error, get, get));
        CHECK(clans.empty() && calls == 1);
    }
    for (const auto& profile : {"{}", R"({"clans":false})", R"({"clans":[{}]})", "broken"}) {
        HttpGetter get = [&](const std::string&, std::string& body, std::string&) { body = profile; return true; };
        CHECK(!FetchTestClans("123", true, 16, clans, error, get, get));
    }
    int calls = 0;
    HttpGetter joined = [&](const std::string& url, std::string& body, std::string&) {
        if (++calls == 1) body = R"({"clans":[{"tag":"A","id":1},{"tag":"B","id":2}],"clanOrder":"OLD,B"})";
        else { CHECK(url.find("/clan/B/maps") != std::string::npos); body = R"({"data":[]})"; }
        return true;
    };
    CHECK(FetchTestClans("123", false, 16, clans, error, joined, joined));
    CHECK(calls == 2 && clans.size() == 1 && clans.front().clanTag == "B");
    auto entry = Score(1); entry.leaderboardId = "1";
    entry.difficulties.push_back({"Standard", "Expert", 9, 8, 8, 5});
    auto selected = EvaluatePlayerPerformance({entry}, {Score(20)});
    CHECK(selected.size() == 2 && selected.back().stars == 9 && selected.back().difficulties.front().name == "Expert");

    RefreshSettings settings; settings.beatLeaderPlayerId = "123"; settings.limit = 16;
    RefreshSettings snapshot = settings;
    settings.beatLeaderPlayerId = "456"; settings.limit = 50;
    std::thread worker([snapshot] { CHECK(snapshot.beatLeaderPlayerId == "123" && snapshot.limit == 16); });
    worker.join();
    const auto main = ReadFile("src/main.cpp");
    const auto workerStart = main.find("static void RefreshWorker");
    const auto launch = main.find("void StartRefresh()");
    CHECK(workerStart != std::string::npos && launch > workerStart);
    CHECK(main.substr(workerStart, launch - workerStart).find("getModConfig()") == std::string::npos);
    CHECK(ReadFile("src/refresh_engine.cpp").find("getModConfig()") == std::string::npos);
    CHECK(main.find("std::thread(RefreshWorker, std::move(settings))") != std::string::npos);
    std::cout << "PASS: empty clan cleanup, invalid membership protection, selected chart, immutable worker settings\n";
}

void ClearRateAndCombinedPlaylists() {
    CHECK(NormalizeMinClearRatePercent(-10) == 0);
    CHECK(NormalizeMinClearRatePercent(100) == 95);
    CHECK(NormalizeMinClearRatePercent(52) == 50);
    CHECK(NormalizeMinClearRatePercent(53) == 55);
    for (int percent = 0; percent <= 95; percent += 5) {
        CHECK(NormalizeMinClearRatePercent(percent) == percent);
        const double threshold = percent / 100.0;
        CHECK(RecommendationPriority(100, threshold, threshold) == (percent == 0 ? 100 : 100 * threshold));
        if (percent > 0) CHECK(RecommendationPriority(100, threshold - 0.0001, threshold) == 0);
    }
    CHECK(RecommendationPriority(100, .01, 0) == 100);
    CHECK(RecommendationPriority(100, .60, .50) == 60);
    CHECK(RecommendationPriority(100, .60, .50, true) == 100);
    CHECK(RecommendationPriority(100, .49, .50, true) == 0);
    RefreshSettings defaults;
    CHECK(defaults.minClearRate == .50 && !defaults.separateNewAndRepeatTracks);
    auto training = Score(999);
    ClearabilityModel model({training});
    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber}) {
        auto cautious = EstimateRecommendationTargetStars({training}, system, .95);
        CHECK(cautious < 7 && model.Predict(cautious) >= .95);
        CHECK(EstimateRecommendationTargetStars({training}, system, 0) == 7);
    }
    auto first = Score(1, 0), second = Score(2, 0), sameSong = Score(1, 100);
    first.weightedPpGain = 100; first.priority = 60; first.clearChance = .6;
    second.weightedPpGain = 80; second.priority = 76; second.clearChance = .95;
    sameSong.weightedPpGain = 110; sameSong.difficulties[0].name = "Expert";
    auto merged = MergeRecommendations({first, second}, {sameSong}, 2);
    CHECK(merged.size() == 2 && merged.front().hash == first.hash && merged.front().isImprovement);
    CHECK(merged.front().priority == 110 && merged.front().difficulties.front().name == "Expert");
    CHECK(!merged.back().isImprovement && merged.back().priority == 80);
    merged = MergeRecommendations({first, second}, {}, 1);
    CHECK(merged.size() == 1 && merged.front().hash == first.hash);
    CHECK(MergeRecommendations({first}, {sameSong}, 0).empty());

    // The same song has two eligible charts. Raw-gain ranking must select the
    // higher theoretical gain even when the easier chart wins normal ranking.
    auto easy = Score(10, 0), hard = easy;
    easy.stars = easy.difficulties[0].stars = 6;
    hard.difficulties[0].name = "Expert"; hard.stars = hard.difficulties[0].stars = 7.4;
    auto normal = SelectRecommendations({easy, hard}, ListKind::NotPlayed, 1, 7,
                  {training}, RatingSystem::ScoreSaber, .50);
    auto raw = SelectRecommendations({easy, hard}, ListKind::NotPlayed, 1, 7,
               {training}, RatingSystem::ScoreSaber, .50, true);
    CHECK(normal.size() == 1 && raw.size() == 1);
    CHECK(normal.front().stars == 6 && raw.front().stars == 7.4);
    CHECK(raw.front().weightedPpGain > normal.front().weightedPpGain);

    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber}) {
        ScoreHistory cache{true, "123", system, {training}};
        std::string error;
        std::vector<MapEntry> result;
        int calls = 0;
        HttpGetter get = [&](const std::string& url, std::string& body, std::string&) {
            CHECK(url.find("/scores?") == std::string::npos); // reuse the loaded history
            ++calls;
            body = Page(1, 2, {Chart(999, 7), Chart(123, 6)}); return true;
        };
        CHECK(FetchServiceRecommendations(system, "123", ListKind::Combined, 2, result, error,
              .50, nullptr, &cache, get, false));
        CHECK(calls == 1 && result.size() == 2);
        CHECK(result[0].weightedPpGain >= result[1].weightedPpGain);
        CHECK(std::count_if(result.begin(), result.end(), [](const auto& entry) { return entry.isImprovement; }) == 1);
        const auto improvement = std::find_if(result.begin(), result.end(), [](const auto& entry) { return entry.isImprovement; });
        CHECK(improvement->hash == Hash(999) && improvement->currentPp == 100);
        const auto unplayed = std::find_if(result.begin(), result.end(), [](const auto& entry) { return !entry.isImprovement; });
        CHECK(unplayed->hash == Hash(123) && unplayed->clearChance >= .50);
        for (const auto& entry : result) CHECK(entry.priority == entry.weightedPpGain);
        HttpGetter fail = [](const std::string&, std::string&, std::string& message) { message = "offline"; return false; };
        CHECK(!FetchServiceRecommendations(system, "123", ListKind::Combined, 2, result, error,
              .50, nullptr, &cache, fail, false));
        CHECK(result.size() == 2); // no partial combined result on failure
    }
    CHECK(PlaylistFileName("BeatLeader", ListKind::Combined) == "RankedPractice_BeatLeader_PPGain.bplist");
    CHECK(PlaylistTitle("ScoreSaber", ListKind::Combined) == "ScoreSaber - PP Gain");
    std::cout << "PASS: 0-95% thresholds, cautious search, merged gain sorting and deduplication, combined API flow\n";
}

Attempt Run(int chart, AttemptOutcome outcome, int event = 1) {
    Attempt a;
    a.id = std::to_string(event); a.source = "local"; a.hash = Hash(chart);
    a.difficulty = "ExpertPlus"; a.stars = 7; a.outcome = outcome; a.endTime = 30;
    a.timestamp = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    return a;
}
void AttemptEvidence() {
    const auto fail = Run(1, AttemptOutcome::Fail);
    auto clear = Run(1, AttemptOutcome::Clear, 2);
    const auto score = Score(1);
    ClearabilityModel baseline({score}), failed({score}, {fail}), completed({score}, {fail, clear});
    const auto key = AttemptChartKey(fail);
    CHECK(failed.Predict(7, key) < baseline.Predict(7, key));
    CHECK(completed.Predict(7, key) > failed.Predict(7, key));
    CHECK(failed.Predict(7, key) < failed.Predict(7, AttemptChartKey(Run(2, AttemptOutcome::Fail))));
    CHECK(completed.NearbyFailures(7) == 1); // A later clear retains earlier failure evidence.
    CHECK(completed.NearbyCounts(7).first == 1); // PB is not counted again alongside a recorded clear.
    for (auto outcome : {AttemptOutcome::Quit, AttemptOutcome::Restart, AttemptOutcome::Practice, AttemptOutcome::Unknown}) {
        auto ignored = fail; ignored.outcome = outcome;
        CHECK(ClearabilityModel({score}, {ignored}).Predict(7) == baseline.Predict(7));
    }
    for (auto modifier : {"NF", "SS", "FS", "ZEN", "GN"}) {
        auto ignored = fail; ignored.modifiers = modifier;
        CHECK(!IsTrainingAttempt(ignored));
        CHECK(ClearabilityModel({score}, {ignored}).Predict(7) == baseline.Predict(7));
    }
    auto practice = fail; practice.practice = true; CHECK(!IsTrainingAttempt(practice));
    auto oneSaber = fail; oneSaber.characteristic = "OneSaber";
    const auto oneSaberKey = AttemptChartKey(oneSaber);
    const ClearabilityModel mixed({score}, {oneSaber});
    CHECK(mixed.Predict(7, oneSaberKey) < mixed.Predict(7, key));
    CHECK(mixed.NearbyFailures(7) == 1);
    for (const auto system : {RatingSystem::ScoreSaber, RatingSystem::AccSaber})
        CHECK(ClearabilityModel({score}, {oneSaber}, system).Predict(7) == baseline.Predict(7));
    auto old = fail; old.timestamp -= 5 * 365 * 86400;
    CHECK(ClearabilityModel({score}, {old}).Predict(7) > failed.Predict(7));
    std::vector<Attempt> grind;
    for (int i = 0; i < 400; ++i) grind.push_back(Run(1, AttemptOutcome::Fail, i + 10));
    CHECK(ClearabilityModel({score}, grind).LocalSupport(7) < 5.01);
    CHECK(std::abs(ClearabilityModel({score}, grind).Predict(7) -
                   ClearabilityModel({score}, {grind[0], grind[1], grind[2], grind[3]}).Predict(7)) < .001);
    auto candidate = Score(1, 0);
    const auto normal = SelectRecommendations({candidate}, ListKind::NotPlayed, 10, 7, {Score(2)}, RatingSystem::ScoreSaber, 0);
    const auto withFailure = SelectRecommendations({candidate}, ListKind::NotPlayed, 10, 7, {Score(2)}, RatingSystem::ScoreSaber, 0, false, {fail});
    CHECK(normal.size() == 1 && withFailure.size() == 1); // Failed chart stays Not Played.
    CHECK(normal[0].weightedPpGain == withFailure[0].weightedPpGain);
    CHECK(normal[0].predictedAccuracy == withFailure[0].predictedAccuracy);
    CHECK(withFailure[0].clearChance < normal[0].clearChance && withFailure[0].nearbyFailures == 1);
    auto unrated = fail; unrated.stars = 0;
    std::vector<Attempt> resolved{unrated}; ResolveAttemptRatings(resolved, {score});
    CHECK(resolved[0].stars == 7 && IsTrainingAttempt(resolved[0]));

    std::vector<Attempt> merged{fail};
    auto remote = fail; remote.source = "beatleader"; remote.id = "100"; remote.timestamp += 12;
    MergeAttempts(merged, {remote}); CHECK(merged.size() == 1 && merged[0].source == "beatleader");
    MergeAttempts(merged, {remote, fail}); CHECK(merged.size() == 1);
    auto delayedLocal = fail; delayedLocal.timestamp += 300;
    MergeAttempts(merged, {delayedLocal}); CHECK(merged.size() == 1); // Persistent alias survives timing changes.
    auto distinct = remote; distinct.id = "101";
    MergeAttempts(merged, {distinct}); CHECK(merged.size() == 2); // Distinct server IDs are distinct attempts.
    auto another = fail; another.id = "local2"; another.timestamp += 60;
    MergeAttempts(merged, {another}); CHECK(merged.size() == 3);
    std::vector<Attempt> quick{Run(1, AttemptOutcome::Fail, 20), Run(1, AttemptOutcome::Fail, 21)};
    auto firstUpload = quick[0]; firstUpload.source = "beatleader"; firstUpload.id = "200"; firstUpload.timestamp += 5;
    auto secondUpload = quick[1]; secondUpload.source = "beatleader"; secondUpload.id = "201"; secondUpload.timestamp += 6;
    MergeAttempts(quick, {firstUpload, secondUpload});
    CHECK(quick.size() == 2 && quick[0].source == "beatleader" && quick[1].source == "beatleader");
    std::vector<Attempt> bounded;
    for (int i = 0; i < 20001; ++i) { auto a = fail; a.id = std::to_string(i); a.timestamp += i; bounded.push_back(a); }
    std::vector<Attempt> newest; MergeAttempts(newest, bounded);
    CHECK(newest.size() == 20000 && newest.front().id == "20000" && newest.back().id == "1");
    std::cout << "PASS: real failures, repeated clears, exclusions, recency, per-chart influence, independent PP/accuracy, deduplication\n";
}
void BeatLeaderAttemptReconciliation() {
    auto local = Run(1, AttemptOutcome::Fail);
    local.timestamp = 1700000000.25;
    auto remote = local;
    remote.source = "beatleader";
    remote.id = "10";
    remote.timestamp = 1700000001;
    remote.endTime += 1.685;
    std::vector<Attempt> merged{local};
    MergeAttempts(merged, {remote});
    CHECK(merged.size() == 1 && merged[0].source == "beatleader" && merged[0].localId == local.id);
    auto retry = local;
    retry.id = "2";
    retry.timestamp += 2;
    MergeAttempts(merged, {retry});
    CHECK(merged.size() == 2); // An existing link cannot consume a different local run.
    ReconcileSavedBeatLeaderAttempts(merged);
    CHECK(merged.size() == 2);

    for (const char* custom : {"DNC", "BFS", "BSF"}) {
        auto modified = remote;
        modified.normal = false;
        modified.modifiers = custom;
        modified.ratingChecked = local.timestamp;
        modified.stars = 0;
        std::vector<Attempt> saved{modified, local};
        ReconcileSavedBeatLeaderAttempts(saved);
        CHECK(saved.size() == 1 && saved[0].modifiers == custom && saved[0].stars == 0 && !IsTrainingAttempt(saved[0]));
    }
    auto different = remote;
    different.modifiers = "DA";
    different.normal = false;
    auto delayedFailure = remote;
    delayedFailure.timestamp += 5;
    auto scoresaber = remote;
    scoresaber.source = "scoresaber";
    for (const auto* nonmatch : {&different, &delayedFailure, &scoresaber}) {
        std::vector<Attempt> saved{local};
        MergeAttempts(saved, {*nonmatch});
        CHECK(saved.size() == 2);
    }

    const std::string player = "999984";
    const auto file = "tests/attempt-sandbox/beatleader_" + player + ".json";
    const std::string chart = Hash(1);
    const auto oldCache = "{\"version\":1,\"attempts\":["
        "{\"source\":\"local\",\"id\":\"1\",\"hash\":\"" + chart +
        "\",\"difficulty\":\"ExpertPlus\",\"timestamp\":1700000000.25,\"endTime\":30,\"stars\":7,\"outcome\":2},"
        "{\"source\":\"beatleader\",\"id\":\"10\",\"hash\":\"" + chart +
        "\",\"difficulty\":\"ExpertPlus\",\"timestamp\":1700000001,\"endTime\":31.685,\"stars\":7,\"outcome\":2}]}";
    std::filesystem::create_directories(std::filesystem::path(file).parent_path());
    std::ofstream(file) << oldCache;
    std::string error;
    CHECK(ReadAttemptHistory("beatleader", player, merged, error));
    CHECK(merged.size() == 1 && merged[0].localId == local.id);
    CHECK(ClearabilityModel({}, merged).NearbyFailures(7) == 1);
    CHECK(ReadFile(file) == oldCache);
    CHECK(SaveAttemptHistory("beatleader", player, {remote}, merged, error) && merged.size() == 1);
    CHECK(ReadAttemptHistory("beatleader", player, merged, error) && merged.size() == 1 && merged[0].localId == local.id);
    std::filesystem::remove(file);
    std::cout << "PASS: BeatLeader local/API failure timing, custom modifiers, distinct retries, and old-cache repair\n";
}
void AttemptImportAndStorage() {
    const std::string player = "999991";
    const auto now = static_cast<long long>(Run(1, AttemptOutcome::Fail).timestamp);
    for (auto system : {RatingSystem::BeatLeader, RatingSystem::ScoreSaber}) {
        const auto service = system == RatingSystem::BeatLeader ? "beatleader" : "scoresaber";
        const auto path = std::string("tests/attempt-sandbox/") + service + "_" + player + ".json";
        std::filesystem::remove(path);
        ScoreHistory history;
        int scoreCalls = 0, attemptCalls = 0;
        HttpGetter get = [&](const std::string& url, std::string& body, std::string&) {
            const bool attemptFeed = url.find("scoresstats") != std::string::npos || url.find("personalBest=all") != std::string::npos;
            if (!attemptFeed) { ++scoreCalls; body = Page(1, 1, {Chart(2, 7, true)}); return true; }
            ++attemptCalls;
            if (system == RatingSystem::BeatLeader) {
                body = Page(attemptCalls, 2, {"{\"id\":" + std::to_string(attemptCalls) +
                    ",\"endType\":" + std::to_string(attemptCalls == 1 ? 2 : 1) +
                    ",\"attemptsCount\":1000,\"timepost\":" + std::to_string(now) +
                    ",\"time\":30,\"baseScore\":20,\"leaderboard\":{" +
                    "\"song\":{\"hash\":\"" + Hash(1) + "\",\"name\":\"Song\"}," +
                    "\"difficulty\":{\"difficultyName\":\"ExpertPlus\",\"modeName\":\"Standard\",\"stars\":7,\"status\":3}}}"});
            } else {
                CHECK(url.find("realmId=1") != std::string::npos);
                body = Page(attemptCalls, 2, {"{\"score\":{\"id\":" + std::to_string(attemptCalls) +
                    ",\"playOutcome\":\"" + (attemptCalls == 1 ? std::string("FAIL") : std::string("CLEAR")) +
                    "\",\"playOutcomeTime\":30,\"createdAt\":\"" + AttemptUtcDate(now) + "\",\"mods\":[]}," +
                    "\"leaderboard\":{\"songHash\":\"" + Hash(1) + "\",\"songName\":\"Song\"," +
                    "\"difficulty\":{\"rawDifficulty\":\"_ExpertPlus_SoloStandard\",\"gameMode\":\"SoloStandard\"}," +
                    "\"realm\":{\"stars\":7,\"leaderboardStatus\":\"RANKED\"}}}"});
            }
            return true;
        };
        std::string error;
        CHECK(LoadScoreHistory(player, system, history, error, get, false));
        CHECK(scoreCalls == 1 && attemptCalls == 2 && history.attempts.size() == 2 && history.attemptWarning.empty());
        CHECK(history.scores.size() == 1 && history.scores[0].hash == Hash(2));
        CHECK(history.attempts[0].stars == 7 && history.attempts[0].timestamp > 0);
        CHECK(ClearabilityModel(history.scores, history.attempts).NearbyFailures(7) == 1); // attemptsCount is not a row multiplier.
        CHECK(LoadScoreHistory(player, system, history, error, get, false)); CHECK(attemptCalls == 2);
        std::vector<Attempt> disk;
        CHECK(ReadAttemptHistory(service, player, disk, error) && disk.size() == 2);
        auto local = Run(1, AttemptOutcome::Fail); local.timestamp = now; local.stars = 0;
        CHECK(SaveAttemptHistory(service, player, {local}, disk, error) && disk.size() == 2);
        ScoreHistory offline;
        HttpGetter failed = [&](const std::string& url, std::string& body, std::string& message) {
            if (url.find("scoresstats") != std::string::npos || url.find("personalBest=all") != std::string::npos) {
                CHECK(url.find("from=") != std::string::npos); message = "HTTP 401"; return false;
            }
            body = Page(1, 1, {Chart(2, 7, true)}); return true;
        };
        CHECK(LoadScoreHistory(player, system, offline, error, failed, false));
        CHECK(offline.attempts.size() == 2 && offline.attemptWarning.find("401") != std::string::npos);
        CHECK(offline.loaded && offline.scores.size() == 1);
        if (system == RatingSystem::BeatLeader) {
            CHECK(offline.attemptWarning.find("public statistics") != std::string::npos);
            CHECK(offline.attemptWarning.find("cached/local attempts") != std::string::npos);
        }
        ScoreHistory interrupted;
        int partialPages = 0;
        HttpGetter partial = [&](const std::string& url, std::string& body, std::string& message) {
            if (url.find("scoresstats") != std::string::npos || url.find("personalBest=all") != std::string::npos) {
                if (++partialPages == 2) { message = "Disconnected on page two"; return false; }
                // A valid first page must not move the cursor past an unseen second page.
                body = Page(1, 2, {"{}"}); return true;
            }
            body = Page(1, 1, {Chart(2, 7, true)}); return true;
        };
        CHECK(LoadScoreHistory(player, system, interrupted, error, partial, false));
        CHECK(partialPages == 2 && interrupted.attempts.size() == 2 && !interrupted.attemptWarning.empty());
        CHECK(ReadAttemptHistory(service, player, disk, error) && disk.size() == 2);
        std::ofstream(path) << "broken";
        CHECK(!SaveAttemptHistory(service, player, {local}, disk, error));
        CHECK(ReadFile(path) == "broken"); // Corruption never silently overwrites the original cache.
        std::filesystem::remove(path);
    }
    std::string error; std::vector<Attempt> disk;
    CHECK(!ReadAttemptHistory("scoresaber", "../escape", disk, error));
    const std::string path = "tests/attempt-sandbox/beatleader_999992.json";
    std::filesystem::remove(path);
    CHECK(ReadAttemptHistory("beatleader", "999990", disk, error) && disk.empty());
    bool first = false, second = false;
    std::thread a([&] { std::vector<Attempt> saved; std::string e; first = SaveAttemptHistory("beatleader", "999992", {Run(1, AttemptOutcome::Fail, 1)}, saved, e); });
    std::thread b([&] { std::vector<Attempt> saved; std::string e; second = SaveAttemptHistory("beatleader", "999992", {Run(1, AttemptOutcome::Clear, 2)}, saved, e); });
    a.join(); b.join(); CHECK(first && second);
    CHECK(ReadAttemptHistory("beatleader", "999992", disk, error) && disk.size() == 2);
    std::filesystem::remove(path);
    std::cout << "PASS: both providers' attempt imports, balanced outcomes, persistent cache, outage fallback, player isolation, concurrent writes\n";
}
void ActualAttemptSchemas() {
    std::string error;
    PageInfo page;
    std::vector<Attempt> attempts;
    CHECK(ParseAttemptPage(ReadFile("tests/fixtures/beatleader-attempts-live.json"), attempts, error, &page, RatingSystem::BeatLeader));
    CHECK(page.rawRows == 2 && attempts.size() == 2 && attempts[0].stars > 0 && attempts[0].timestamp > 0);
    attempts.clear();
    CHECK(ParseAttemptPage(ReadFile("tests/fixtures/scoresaber-attempts-live.json"), attempts, error, &page, RatingSystem::ScoreSaber));
    CHECK(page.rawRows == 20 && attempts.size() == 20);
    CHECK(std::count_if(attempts.begin(), attempts.end(), [](const auto& a) { return a.outcome == AttemptOutcome::Fail; }) == 11);
    const auto charts = Parse(ReadFile("tests/fixtures/beatleader-attempt-chart-live.json"));
    CHECK(!charts.empty() && charts.front().hash.size() == 40 && charts.front().stars > 0);
    auto local = Run(1, AttemptOutcome::Fail); local.stars = 0;
    local.hash = charts.front().hash; local.difficulty = charts.front().difficulties.front().name;
    std::vector<Attempt> resolved{local}; ResolveAttemptRatings(resolved, charts);
    CHECK(resolved[0].stars == charts.front().stars);
    auto ssBody = ReadFile("tests/fixtures/scoresaber-attempts-live.json");
    rapidjson::Document json; json.Parse(ssBody.c_str());
    json["data"][0]["leaderboard"]["realm"]["realmId"].SetInt(2);
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); json.Accept(writer);
    attempts.clear();
    CHECK(!ParseAttemptPage(buffer.GetString(), attempts, error, &page, RatingSystem::ScoreSaber));

    const std::string path = "tests/attempt-sandbox/scoresaber_999993.json";
    std::filesystem::remove(path);
    ScoreHistory history;
    int explicitCalls = 0, fallbackCalls = 0;
    HttpGetter get = [&](const std::string& url, std::string& body, std::string& message) {
        if (url.find("personalBest=all") == std::string::npos) { body = Page(1, 1, {Chart(1, 7, true)}); return true; }
        if (url.find("realmId=1") != std::string::npos) { ++explicitCalls; message = "Leaderboard API returned HTTP 400."; return false; }
        ++fallbackCalls;
        auto sample = ReadFile("tests/fixtures/scoresaber-attempts-live.json");
        rapidjson::Document doc; doc.Parse(sample.c_str());
        doc["metadata"]["totalItems"].SetInt(20); doc["metadata"]["totalPages"].SetInt(1);
        rapidjson::StringBuffer output; rapidjson::Writer<rapidjson::StringBuffer> serializer(output); doc.Accept(serializer);
        body = output.GetString(); return true;
    };
    CHECK(LoadScoreHistory("999993", RatingSystem::ScoreSaber, history, error, get, false));
    CHECK(explicitCalls == 1 && fallbackCalls == 1 && history.attempts.size() == 20 && history.attemptWarning.empty());
    std::filesystem::remove(path);
    // A rejected realm query needs one retry for the whole import, not one
    // rejected request on every page. Returned chart realms stay validated.
    history = {};
    explicitCalls = fallbackCalls = 0;
    HttpGetter multiplePages = [&](const std::string& url, std::string& body, std::string& message) {
        if (url.find("personalBest=all") == std::string::npos) { body = Page(1, 1, {Chart(1, 7, true)}); return true; }
        if (url.find("realmId=1") != std::string::npos) { ++explicitCalls; message = "Leaderboard API returned HTTP 400."; return false; }
        ++fallbackCalls;
        rapidjson::Document doc; doc.Parse(ssBody.c_str());
        const int page = fallbackCalls;
        doc["metadata"]["page"].SetInt(page);
        doc["metadata"]["totalItems"].SetInt(40);
        doc["metadata"]["totalPages"].SetInt(2);
        for (auto& row : doc["data"].GetArray()) row["score"]["id"].SetInt(10000 * page + row["score"]["id"].GetInt() % 10000);
        rapidjson::StringBuffer output; rapidjson::Writer<rapidjson::StringBuffer> serializer(output); doc.Accept(serializer);
        body = output.GetString();
        return true;
    };
    CHECK(LoadScoreHistory("999993", RatingSystem::ScoreSaber, history, error, multiplePages, false));
    CHECK(explicitCalls == 1 && fallbackCalls == 2 && history.attempts.size() == 40 && history.attemptWarning.empty());
    std::filesystem::remove(path);
    const auto blCharts = Parse(ReadFile("tests/fixtures/beatleader-attempt-chart-live.json"));
    Attempt localChart = Run(1, AttemptOutcome::Fail);
    localChart.hash = blCharts.front().hash; localChart.difficulty = blCharts.front().difficulties.front().name;
    localChart.stars = 0;
    const std::string localPath = "tests/attempt-sandbox/beatleader_999994.json";
    std::filesystem::remove(localPath);
    std::vector<Attempt> disk;
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    ScoreHistory localOnly; localOnly.importAttempts = false;
    int lookups = 0;
    HttpGetter chartLookup = [&](const std::string& url, std::string& body, std::string&) {
        CHECK(url.find("scoresstats") == std::string::npos);
        if (url.find("leaderboards/hash/") != std::string::npos) { ++lookups; body = ReadFile("tests/fixtures/beatleader-attempt-chart-live.json"); }
        else body = Page(1, 1, {Chart(2, 7, true)});
        return true;
    };
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, chartLookup, false));
    CHECK(lookups == 1 && localOnly.attempts.size() == 1 && localOnly.attempts[0].stars > 0 && localOnly.attemptWarning.empty());
    CHECK(ReadAttemptHistory("beatleader", "999994", disk, error) && disk[0].stars > 0);
    std::filesystem::remove(localPath);
    // A failed run without a PB must not keep PPv3 stars indefinitely.
    localChart.stars = 99;
    localChart.ratingChecked = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    localChart.ratingEpoch = 4; // Even a recent PPv4 cache needs immediate PPv4.1 migration.
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    localOnly = {}; localOnly.importAttempts = false; lookups = 0;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, chartLookup, false));
    CHECK(lookups == 1 && localOnly.attempts[0].stars == blCharts.front().stars && localOnly.attempts[0].ratingChecked > 0);
    CHECK(ReadAttemptHistory("beatleader", "999994", disk, error) && disk[0].ratingEpoch == kBeatLeaderAttemptRatingEpoch);
    localOnly = {}; localOnly.importAttempts = false; lookups = 0;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, chartLookup, false));
    CHECK(lookups == 0 && localOnly.attempts[0].stars == blCharts.front().stars);
    std::filesystem::remove(localPath);
    localChart.ratingChecked = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() - 8*86400;
    localChart.ratingEpoch = kBeatLeaderAttemptRatingEpoch;
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    HttpGetter unavailableRating = [&](const std::string& url, std::string& body, std::string& message) {
        if (url.find("leaderboards/hash/") != std::string::npos) { message = "Offline"; return false; }
        body = Page(1,1,{Chart(2,7,true)}); return true;
    };
    localOnly = {}; localOnly.importAttempts = false;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, unavailableRating, false));
    CHECK(localOnly.attempts.size() == 1 && localOnly.attempts[0].stars == 0);
    CHECK(localOnly.attemptWarning.find("outdated BeatLeader ratings") != std::string::npos);
    CHECK(ReadAttemptHistory("beatleader", "999994", disk, error) && disk[0].stars == 99);
    std::filesystem::remove(localPath);
    localChart.ratingChecked += 8*86400;
    localChart.ratingEpoch = 0;
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    localOnly = {}; localOnly.importAttempts = false;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, unavailableRating, false));
    CHECK(localOnly.attempts[0].stars == 0 && localOnly.attemptWarning.find("outdated BeatLeader ratings") != std::string::npos);
    std::filesystem::remove(localPath);
    // A successful lookup that no longer includes the ranked chart retires its
    // old stars on disk too; the cache merge must not restore that rating.
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    HttpGetter removedChart = [&](const std::string& url, std::string& body, std::string&) {
        body = url.find("leaderboards/hash/") != std::string::npos ? Page(1,0,{}) : Page(1,1,{Chart(2,7,true)});
        return true;
    };
    localOnly = {}; localOnly.importAttempts = false;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, removedChart, false));
    CHECK(localOnly.attempts[0].stars == 0);
    CHECK(ReadAttemptHistory("beatleader", "999994", disk, error) && disk[0].stars == 0 && disk[0].ratingEpoch == kBeatLeaderAttemptRatingEpoch);
    std::filesystem::remove(localPath);
    // Fresh PB data resolves the cached attempt without a redundant hash query.
    localChart.hash = Hash(2); localChart.difficulty = "ExpertPlus";
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    localOnly = {}; localOnly.importAttempts = false;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, unavailableRating, false));
    CHECK(localOnly.attempts[0].stars == 7 && localOnly.attemptWarning.empty());
    std::filesystem::remove(localPath);
    localChart.characteristic = "OneSaber";
    localChart.stars = 0;
    localChart.ratingEpoch = 0;
    CHECK(SaveAttemptHistory("beatleader", "999994", {localChart}, disk, error));
    HttpGetter mixedRatings = [&](const std::string& url, std::string& body, std::string&) {
        if (url.find("leaderboards/hash/") != std::string::npos) {
            body = Page(1, 2, {Chart(2, 7), Chart(2, 9, false, "", "OneSaber")});
        } else body = Page(1, 1, {Chart(2, 7, true)});
        return true;
    };
    localOnly = {}; localOnly.importAttempts = false;
    CHECK(LoadScoreHistory("999994", RatingSystem::BeatLeader, localOnly, error, mixedRatings, false));
    CHECK(localOnly.attempts.size() == 1 && localOnly.attempts.front().stars == 9);
    CHECK(localOnly.attempts.front().ratingEpoch == kBeatLeaderAttemptRatingEpoch);
    std::filesystem::remove(localPath);
    localChart.characteristic = "Standard";
    const auto ssCharts = Parse(ReadFile("tests/fixtures/scoresaber-attempt-chart-live.json"));
    CHECK(ssCharts.size() == 1 && ssCharts[0].stars > 0);
    localChart.hash = ssCharts[0].hash; localChart.difficulty = ssCharts[0].difficulties[0].name;
    localChart.stars = 0; localChart.ratingChecked = 0; localChart.ratingEpoch = 0;
    const std::string ssLocalPath = "tests/attempt-sandbox/scoresaber_999995.json";
    std::filesystem::remove(ssLocalPath);
    CHECK(SaveAttemptHistory("scoresaber", "999995", {localChart}, disk, error));
    ScoreHistory ssLocalOnly; ssLocalOnly.importAttempts = false;
    lookups = 0;
    HttpGetter ssChartLookup = [&](const std::string& url, std::string& body, std::string&) {
        CHECK(url.find("personalBest=all") == std::string::npos);
        if (url.find("leaderboards/hash/") != std::string::npos) {
            CHECK(url.find("/SoloStandard/9?realmId=1") != std::string::npos);
            ++lookups; body = ReadFile("tests/fixtures/scoresaber-attempt-chart-live.json");
        } else body = Page(1, 1, {Chart(2, 7, true)});
        return true;
    };
    CHECK(LoadScoreHistory("999995", RatingSystem::ScoreSaber, ssLocalOnly, error, ssChartLookup, false));
    CHECK(lookups == 1 && ssLocalOnly.attempts.size() == 1 && ssLocalOnly.attempts[0].stars == ssCharts[0].stars && ssLocalOnly.attemptWarning.empty());
    CHECK(ReadAttemptHistory("scoresaber", "999995", disk, error) && disk[0].stars == ssCharts[0].stars);
    std::filesystem::remove(ssLocalPath);
    std::cout << "PASS: real API schemas, sibling song/chart lookup, ScoreSaber realm validation and live 400 compatibility fallback\n";
}
void ClanIcons() {
    const auto pngA = ReadFile("tests/fixtures/clan-icon-live.png");
    const auto pngB = ReadFile("assets/scoresaber_ppgain.png");
    CHECK(DownloadedPlaylistImage(pngA).starts_with("data:image/png;base64,"));
    CHECK(DownloadedPlaylistImage(pngA) != DownloadedPlaylistImage(pngB));
    CHECK(EncodePlaylistImage("M", "image/png") == "data:image/png;base64,TQ==");
    CHECK(EncodePlaylistImage("Ma", "image/png") == "data:image/png;base64,TWE=");
    CHECK(EncodePlaylistImage("Man", "image/png") == "data:image/png;base64,TWFu");
    CHECK(DownloadedPlaylistImage("").empty());
    CHECK(DownloadedPlaylistImage("<html>Not found</html>").empty());
    CHECK(DownloadedPlaylistImage(pngA.substr(0, pngA.size() - 12)).empty());
    std::string bounded;
    CurlResponse response{bounded, 3};
    char data[] = "1234";
    CHECK(CurlWrite(data, 1, 2, &response) == 2 && bounded == "12");
    CHECK(CurlWrite(data + 2, 1, 2, &response) == 0 && bounded == "12");
    CHECK(CurlWrite(data + 2, 1, 1, &response) == 1 && bounded == "123");
    CHECK(CurlWrite(data, std::numeric_limits<size_t>::max(), 2, &response) == 0);

    std::vector<ClanPlaylistRecommendations> playlists;
    std::string error;
    int mapCalls = 0;
    std::vector<std::string> iconCalls;
    HttpGetter api = [&](const std::string& url, std::string& body, std::string&) {
        if (url.find("/player/") != std::string::npos) {
            // Membership lacks icons, as on the live API; order is independent of array order.
            body = R"({"clans":[{"tag":"A","id":1},{"tag":"B","id":2}],"clanOrder":"B,A"})";
        } else {
            ++mapCalls;
            const std::string tag = url.find("/clan/B/maps") != std::string::npos ? "B" : "A";
            body = "{\"data\":[],\"container\":{\"tag\":\"" + tag + "\",\"icon\":\"https://example.com/" + tag + ".png\"}}";
        }
        return true;
    };
    HttpGetter icons = [&](const std::string& url, std::string& body, std::string&) {
        iconCalls.push_back(url);
        body = url.ends_with("A.png") ? pngA : pngB;
        return true;
    };
    CHECK(FetchTestClans("123", true, 16, playlists, error, api, icons));
    CHECK(mapCalls == 2 && iconCalls.size() == 2 && playlists.size() == 2);
    CHECK(playlists[0].clanTag == "B" && playlists[0].image == DownloadedPlaylistImage(pngB));
    CHECK(playlists[1].clanTag == "A" && playlists[1].image == DownloadedPlaylistImage(pngA));
    CHECK(playlists[0].iconWarning.empty() && playlists[1].iconWarning.empty());
    mapCalls = 0; iconCalls.clear();
    CHECK(FetchTestClans("123", false, 16, playlists, error, api, icons));
    CHECK(mapCalls == 1 && iconCalls.size() == 1 && iconCalls[0].ends_with("B.png"));
    CHECK(playlists.size() == 1 && playlists[0].image == DownloadedPlaylistImage(pngB));

    // Failed/invalid/oversized downloads degrade independently and never block song data.
    for (int failure : {0, 1, 2}) {
        HttpGetter brokenIcon = [&](const std::string& url, std::string& body, std::string& message) {
            if (url.ends_with("A.png")) { body = pngA; return true; }
            if (failure == 0) { message = "HTTP 404"; return false; }
            body = failure == 1 ? "<html>Not an image</html>" : std::string(kMaxClanIconBytes + 1, 'x');
            return true;
        };
        CHECK(FetchTestClans("123", true, 16, playlists, error, api, brokenIcon));
        CHECK(playlists[0].image.empty() && !playlists[0].iconWarning.empty());
        CHECK(playlists[1].image == DownloadedPlaylistImage(pngA) && playlists[1].iconWarning.empty());
    }
    for (const std::string icon : {"", "file:///icon.png", "http://example.com/icon.png"}) {
        HttpGetter missing = [&](const std::string& url, std::string& body, std::string&) {
            if (url.find("/player/") != std::string::npos) body = R"({"clans":[{"tag":"A","id":1}]})";
            else body = "{\"data\":[],\"container\":{\"tag\":\"A\",\"icon\":\"" + icon + "\"}}";
            return true;
        };
        HttpGetter forbidden = [&](const std::string&, std::string&, std::string&) { CHECK(false); return false; };
        CHECK(FetchTestClans("123", true, 16, playlists, error, missing, forbidden));
        CHECK(playlists.size() == 1 && playlists[0].image.empty() && !playlists[0].iconWarning.empty());
    }
    HttpGetter mismatched = [&](const std::string& url, std::string& body, std::string&) {
        body = url.find("/player/") != std::string::npos ? R"({"clans":[{"tag":"A","id":1}]})" :
            R"({"data":[],"container":{"tag":"B","icon":"https://example.com/B.png"}})";
        return true;
    };
    iconCalls.clear();
    CHECK(FetchTestClans("123", true, 16, playlists, error, mismatched, icons));
    CHECK(iconCalls.empty() && playlists[0].image.empty());
    std::cout << "PASS: clan covers follow membership order, stay independent per clan and safely fall back on image failures\n";
}

void AbilityChartContract() {
    CHECK(BuildAbilityChart({}).curve.empty());
    CHECK(BuildAbilityChart({}).scores.empty());
    auto low = Score(9001); low.stars = 2; low.accuracy = .60;
    auto middle = Score(9002); middle.stars = 6; middle.accuracy = .95;
    auto high = Score(9003); high.stars = 9; high.modifiedStars = 10.5; high.accuracy = .80;
    auto nf = Score(9004); nf.noFail = true;
    auto invalid = Score(9005); invalid.accuracy = .54;
    auto nonStandard = Score(9006, 100, "OneSaber");
    auto duplicate = middle; duplicate.pp = 50; duplicate.accuracy = .70;
    auto unranked = Score(9007); unranked.ranked = false;
    const std::vector<MapEntry> history{low, middle, high, nf, invalid, nonStandard, duplicate, unranked};
    const auto chart = BuildAbilityChart(history);
    const AccuracyCurve production(history);
    CHECK(chart.scores.size() == 4);
    CHECK(BuildAbilityChart(history, false, RatingSystem::ScoreSaber).scores.size() == 3);
    CHECK(chart.minObservedStars == 2 && chart.maxObservedStars == 10.5);
    CHECK(chart.maxStars >= chart.maxObservedStars + 2);
    CHECK(chart.curve.front().stars == 0 && chart.curve.back().stars == chart.maxStars);
    bool knotSeen = false;
    for (size_t i = 0; i < chart.curve.size(); ++i) {
        const auto& p = chart.curve[i];
        CHECK(std::abs(p.accuracy - production.Predict(p.stars)) < 1e-12);
        CHECK(p.accuracy >= chart.minAccuracy && p.accuracy <= 1);
        if (i) CHECK(p.stars > chart.curve[i - 1].stars && p.accuracy <= chart.curve[i - 1].accuracy + 1e-12);
        knotSeen = knotSeen || p.stars == 10.5;
    }
    CHECK(knotSeen);
    CHECK(chart.curve.front().accuracy > .60); // The global fit can smooth a low-star outlier.
    const auto bitmap = RenderAbilityChart(chart);
    CHECK(bitmap.pixels.size() == bitmap.width * bitmap.height * 4);
    bool cyan = false;
    for (size_t i = 0; i < bitmap.pixels.size(); i += 4) {
        if (bitmap.pixels[i] == 0 && bitmap.pixels[i + 1] == 211 && bitmap.pixels[i + 2] == 255) cyan = true;
    }
    CHECK(cyan);
    const auto single = BuildAbilityChart({high});
    CHECK(single.scores.size() == 1 && single.minObservedStars == single.maxObservedStars);
    CHECK(std::all_of(single.curve.begin(), single.curve.end(), [](auto p) { return std::isfinite(p.accuracy); }));
    CHECK(RenderAbilityChart(single).pixels.size() == bitmap.pixels.size());
    std::cout << "PASS: ability chart shares training eligibility, exact predictions, monotonicity and sparse-history behavior\n";
}

void ExportAbilityPreview() {
    std::vector<MapEntry> scores;
    for (int page = 1; page <= 2; ++page) {
        const auto parsed = Parse(ReadFile("analysis/grug-accuracy/scores-page-" + std::to_string(page) + ".json"));
        scores.insert(scores.end(), parsed.begin(), parsed.end());
    }
    const auto bitmap = RenderAbilityChart(BuildAbilityChart(scores));
    std::ofstream output(".review/ability-chart.ppm", std::ios::binary);
    CHECK(output.good());
    output << "P6\n" << bitmap.width << " " << bitmap.height << "\n255\n";
    for (int y = bitmap.height - 1; y >= 0; --y)
        for (int x = 0; x < bitmap.width; ++x)
            output.write(reinterpret_cast<const char*>(bitmap.pixels.data() + (y * bitmap.width + x) * 4), 3);
}

void AccountLookupContract() {
    const auto bl = AccountService::BeatLeader, ss = AccountService::ScoreSaber;
    AccountQuery query; std::string error;
    CHECK(ParseAccountInput(bl, "  https://www.BeatLeader.com/u/grug/ranked?x=1#scores  ", query, error));
    CHECK(query.kind == AccountInputKind::Profile && query.value == "grug");
    CHECK(AccountRequestUrl(bl, query, 1) == "https://api.beatleader.com/player/grug");
    CHECK(ParseAccountInput(ss, "scoresaber.com/u/76561198289934738?sort=recent", query, error));
    CHECK(query.kind == AccountInputKind::NumericId && query.value == "76561198289934738");
    CHECK(AccountRequestUrl(ss, query, 1) == "https://scoresaber.com/api/v2/players/76561198289934738");
    CHECK(ParseAccountInput(ss, "https://scoresaber.com/u/some-player", query, error));
    CHECK(AccountRequestUrl(ss, query, 1) == "https://scoresaber.com/api/v2/players/vanity/some-player");
    CHECK(ParseAccountInput(bl, "http://beatleader.xyz/u/my%20alias/", query, error));
    CHECK(query.value == "my alias");
    CHECK(AccountRequestUrl(bl, query, 1) == "https://api.beatleader.com/player/my%20alias");
    for (const auto& input : {"https://scoresaber.com/u/123", "https://beatleader.com.evil.test/u/123",
        "https://beatleader.com@evil.test/u/123", "https://beatleader.com/leaderboard/123",
        "https://beatleader.com/u/", "https://beatleader.com/u/%00", "https://beatleader.com/u/a%2Fb",
        "https://beatleader.com/u/a%5Cb", "https://beatleader.com/u/%XX", "file://beatleader.com/u/123"})
        CHECK(!ParseAccountInput(bl, input, query, error));
    CHECK(!ParseAccountInput(bl, "000000", query, error));
    CHECK(!ParseAccountInput(bl, std::string(33, '1'), query, error));
    CHECK(!ParseAccountInput(bl, std::string("abc\0def", 7), query, error));
    CHECK(!ParseAccountInput(ss, "ab", query, error));
    CHECK(!ParseAccountInput(ss, "\xc3\xa9\xc3\xa9", query, error)); // Two characters, four UTF-8 bytes.
    CHECK(ParseAccountInput(ss, "\xc3\xa9\xc3\xa9\xc3\xa9", query, error));
    CHECK(ParseAccountInput(ss, "A & B+?", query, error));
    CHECK(AccountRequestUrl(ss, query, 2) == "https://scoresaber.com/api/v2/players?search=A%20%26%20B%2B%3F&limit=5&page=2");
    CHECK(!ParseAccountInput(bl, std::string(129, 'a'), query, error));
    CHECK(IsCanonicalAccountId("76561198289934738"));
    CHECK(!IsCanonicalAccountId("grug"));

    const std::string exact = "76561198289934738";
    const std::string row = R"({"id":"76561198289934738","name":"grug","country":"US","rank":10597,"stats":{"rank":6004},"avatar":"https://cdn.scoresaber.com/avatar.jpg"})";
    AccountPage page;
    query = {AccountInputKind::NumericId, exact};
    CHECK(ParseAccountPage(bl, query, 1, row, page, error));
    CHECK(page.profiles.size() == 1 && page.profiles[0].id == exact && page.profiles[0].rank == 10597);
    CHECK(ParseAccountPage(ss, query, 1, row, page, error));
    CHECK(page.profiles[0].rank == 6004 && page.profiles[0].country == "US");
    query.value = "123";
    CHECK(!ParseAccountPage(bl, query, 1, row, page, error)); // Wrong ID must never be saved.
    CHECK(page.profiles.empty());
    CHECK(ParseAccountPage(bl, {AccountInputKind::NumericId, "358378"}, 1,
        R"({"id":"76561198289934738","name":"grug","linkedIds":{"questId":358378,"steamId":"76561198289934738"}})", page, error));
    CHECK(page.profiles.front().id == exact);
    CHECK(!ParseAccountPage(ss, {AccountInputKind::NumericId, "358378"}, 1,
        R"({"id":"76561198289934738","name":"grug","linkedIds":{"questId":358378}})", page, error));
    query = {AccountInputKind::Profile, "grug"};
    CHECK(ParseAccountPage(bl, query, 1, row, page, error));
    CHECK(page.profiles.front().id == exact); // Aliases become canonical IDs, never history namespaces.
    query = {AccountInputKind::Search, "grug"};
    CHECK(ParseAccountPage(bl, query, 1, Page(1, 23, {row, row,
        R"({"id":"456","name":"grug","avatar":"http://insecure/image","country":"<x>","rank":0})",
        R"({"id":"bad","name":"Invalid"})", R"({"id":"789","name":""})"}), page, error));
    CHECK(page.profiles.size() == 2 && page.hasNext);
    CHECK(page.profiles[1].avatar.empty() && page.profiles[1].country.empty());
    CHECK(page.profiles[0].name == page.profiles[1].name && page.profiles[0].id != page.profiles[1].id);
    CHECK(ParseAccountPage(ss, query, 2, "{\"metadata\":{\"page\":2,\"itemsPerPage\":5,\"totalItems\":6},\"data\":[" + row + "]}", page, error));
    CHECK(page.page == 2 && !page.hasNext);
    CHECK(!ParseAccountPage(ss, query, 1, "{\"metadata\":{\"page\":2},\"data\":[" + row + "]}", page, error));
    CHECK(!ParseAccountPage(bl, query, 1, "<html>Service unavailable</html>", page, error));
    CHECK(!ParseAccountPage(bl, query, 1, "{\"statusCode\":401}", page, error));
    CHECK(ParseAccountPage(bl, query, 1, Page(1, 0, {}), page, error));
    CHECK(page.profiles.empty() && !page.hasNext);
    query = {AccountInputKind::NumericId, exact};
    CHECK(ParseAccountPage(bl, query, 1, R"({"id":76561198289934738,"name":"Numeric JSON ID"})", page, error));
    CHECK(page.profiles.front().id == exact); // Never round through double precision.
    CHECK(!ParseAccountPage(bl, query, 1, R"({"id":76561198289934738.0,"name":"Rounded ID"})", page, error));

    int requests = 0;
    auto get = [&](const std::string& url, std::string& body, std::string&) {
        ++requests; CHECK(url == "https://api.beatleader.com/player/grug"); body = row; return true;
    };
    CHECK(FindAccountsInternal(bl, "https://beatleader.com/u/grug/ranked", 1, page, error, get));
    CHECK(requests == 1 && page.profiles.front().id == exact);
    CHECK(!FindAccountsInternal(ss, "ab", 1, page, error, get));
    CHECK(requests == 1); // Reject locally without spending an API request.
    CHECK(!FindAccountsInternal(bl, exact, 1, page, error,
        [](const std::string&, std::string&, std::string& e) { e = "HTTP 503"; return false; }));
    CHECK(page.profiles.empty() && error == "HTTP 503");

    AccountLookupState state, independent;
    AccountPage first; first.profiles = {{exact, "grug"}};
    auto oldRequest = state.Begin();
    CHECK(!state.Select(oldRequest, 0));
    state.Invalidate(); // Editing input or pressing Cancel invalidates older replies.
    CHECK(!state.Complete(oldRequest, first));
    auto current = state.Begin();
    CHECK(state.Complete(current, first));
    CHECK(state.Select(current, 0) && state.Select(current, 0)->id == exact);
    CHECK(!state.Select(oldRequest, 0) && !state.Select(current, 1));
    CHECK(!state.Complete(current, first)); // Duplicate completion cannot replace results.
    auto otherRequest = independent.Begin();
    CHECK(independent.Complete(otherRequest, first));
    auto nextPage = state.Begin();
    CHECK(!state.Select(current, 0));
    CHECK(independent.Select(otherRequest, 0)); // The other leaderboard remains selected independently.
    CHECK(!state.Complete(current, first));
    CHECK(state.Complete(nextPage, {})); // An error/empty page leaves no stale selectable result.
    CHECK(!state.Select(nextPage, 0));
    auto png = ReadFile("assets/scoresaber_ppgain.png");
    CHECK(IsAccountAvatarImage(png));
    png[16] = 0x7f;
    CHECK(!IsAccountAvatarImage(png)); // Small compressed bodies can still request huge textures.
    std::string jpeg = std::string("\xff\xd8\xff\xe0\x00\x04zz\xff\xc0\x00\x0b\x08\x00\x80\x01\x00\x01\x01\x11\x00\xff\xd9", 23);
    CHECK(IsAccountAvatarImage(jpeg));
    jpeg[15] = 0x10; // Width = 4096.
    CHECK(!IsAccountAvatarImage(jpeg));
    CHECK(!IsAccountAvatarImage("<html>Error</html>"));
    std::cout << "PASS: account links, aliases and linked IDs, exact identity, paginated searches, ambiguous names and stale-reply protection\n";
}

void ConcurrentRefreshContract() {
    std::promise<void> firstStarted, secondStarted;
    auto firstReady = firstStarted.get_future();
    auto secondReady = secondStarted.get_future();
    // Each task must start while the other is still running. A timeout gives a
    // useful failure rather than hanging if orchestration becomes sequential.
    const auto parallel = RunRefreshTasks([&] {
        firstStarted.set_value();
        return secondReady.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    }, [&] {
        secondStarted.set_value();
        return firstReady.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    });
    CHECK(parallel[0].succeeded && parallel[1].succeeded);
    CHECK(parallel[0].error.empty() && parallel[1].error.empty());
    bool otherFinished = false;
    const auto failure = RunRefreshTasks([]() -> bool { throw std::runtime_error("first failed"); }, [&] {
        otherFinished = true; return true;
    });
    CHECK(!failure[0].succeeded && failure[0].error == "first failed");
    CHECK(failure[1].succeeded && otherFinished);
    const auto unknown = RunRefreshTasks([]() -> bool { throw 42; }, [] { return false; });
    CHECK(!unknown[0].succeeded && !unknown[0].error.empty());
    CHECK(!unknown[1].succeeded && unknown[1].error.empty());
    int calls = 0;
    const auto single = RunRefreshTasks({}, [&] { ++calls; return true; });
    CHECK(calls == 1 && single[0].succeeded && single[1].succeeded);
    const auto disabled = RunRefreshTasks({}, {});
    CHECK(disabled[0].succeeded && disabled[1].succeeded);
    std::cout << "PASS: concurrent providers, independent failures, joined completion and disabled services\n";
}

void DefaultRealmCompatibility() {
    for (const auto& url : {"https://example.test/maps?realmId=1&limit=100", "https://example.test/maps?limit=100&realmId=1",
                           "https://example.test/maps?realmId=1"}) {
        int explicitCalls = 0, defaultCalls = 0;
        const auto compatible = DefaultRealmCompatibleGet([&](const std::string& request, std::string& body, std::string& error) {
            if (request.find("realmId=1") != std::string::npos) {
                ++explicitCalls; error = "Leaderboard API returned HTTP 400."; return false;
            }
            ++defaultCalls;
            CHECK(request.find("?&") == std::string::npos);
            CHECK(request.find("?limit=100") != std::string::npos || request == "https://example.test/maps");
            body = "{}"; return true;
        });
        std::string body, error;
        CHECK(compatible(url, body, error));
        CHECK(compatible(url, body, error));
        CHECK(explicitCalls == 1 && defaultCalls == 2 && error.empty());
    }
    int calls = 0;
    auto failure = DefaultRealmCompatibleGet([&](const std::string&, std::string&, std::string& error) {
        ++calls; error = "Leaderboard API returned HTTP 429."; return false;
    });
    std::string body, error;
    CHECK(!failure("https://example.test/maps?realmId=1", body, error) && calls == 1);
    const auto sample = ReadFile("tests/fixtures/scoresaber-v2-maps.json");
    std::vector<MapEntry> maps;
    CHECK(ParseScoreSaberEntries(sample, maps, error));
    CHECK(maps.size() == 3);
    for (int realmId : {0, 2}) {
        rapidjson::Document document; document.Parse(sample.c_str());
        document["data"][1]["realm"]["realmId"].SetInt(realmId);
        rapidjson::StringBuffer output; rapidjson::Writer<rapidjson::StringBuffer> writer(output); document.Accept(writer);
        maps.clear();
        CHECK(!ParseScoreSaberEntries(output.GetString(), maps, error) && maps.empty());
    }
    ScoreHistory history{true, "123", RatingSystem::ScoreSaber, {Score(999)}};
    int explicitCalls = 0, pages = 0;
    auto candidates = [&](const std::string& request, std::string& body, std::string& error) {
        if (request.find("realmId=1") != std::string::npos) { ++explicitCalls; error = "Leaderboard API returned HTTP 400."; return false; }
        ++pages;
        body = Page(pages, 2, {Chart(pages, 7)}); return true;
    };
    CHECK(FetchServiceRecommendations(RatingSystem::ScoreSaber, "123", ListKind::NotPlayed, 16, maps,
        error, 0.0, nullptr, &history, candidates, false));
    CHECK(explicitCalls == 1 && pages == 2 && !maps.empty());
    std::cout << "PASS: one realm fallback per operation, accepted query formats and default rating validation\n";
}

#include "clan_recommendation_tests.hpp"
#include "sync_playlist_tests.hpp"
#include "accsaber_tests.hpp"
#include "clan_api_audit.hpp"
#include "clan_refresh_tests.hpp"
#include "clan_search_tests.hpp"
#include "clan_legacy_tests.hpp"

int main(int argc, char** argv) {
    try {
        if (argc == 4 && (std::string(argv[1]) == "--validate-sync-catalog" || std::string(argv[1]) == "--validate-sync-scores")) {
            std::vector<MapEntry> charts;
            std::string error;
            const auto system = Lower(argv[2]) == "scoresaber" ? RatingSystem::ScoreSaber : RatingSystem::BeatLeader;
            const bool scores = std::string(argv[1]) == "--validate-sync-scores";
            if (!ParseSyncPage(ReadFile(argv[3]), system, scores, charts, error, nullptr)) throw std::runtime_error(error);
            std::cout << "Validated " << charts.size() << " custom chart identities.\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--audit-clan-profile") return AuditClanProfile(argv[2], argv[3]);
        if (argc == 4 && std::string(argv[1]) == "--benchmark-clan-profile") return AuditClanProfile(argv[2], argv[3], true);
        if (argc == 4 && std::string(argv[1]) == "--benchmark-paced-clan-profile") return AuditClanProfile(argv[2], argv[3], true, true);
        DatesAndParsing(); AccuracyTrainingContract(); SigmoidAccuracy(); ModifiersAndPp(); BeatLeaderSpeedRecommendations(); BeatLeaderV4Contract(); PaginationAndCache(); CompleteCandidateBand(); ClansAndSettings(); PersonalizedClanContract(); ClanSearchContract(); ClanRefreshPerformance(); ClanModifierRefresh(); ClanSearchLearning(); ConsecutiveClanRefreshes(); ConsecutiveClanRefreshes(true); LearnedSecondaryClans(); LegacyClanSelection(); LegacyClanRefresh(); LegacyClearRateCap(); ClearRateAndCombinedPlaylists();
        BeatLeaderOneSaberGain(); AttemptEvidence(); BeatLeaderAttemptReconciliation(); AttemptImportAndStorage();
        ActualAttemptSchemas();
        AbilityChartContract();
        ClanIcons();
        AccountLookupContract();
        ConcurrentRefreshContract();
        DefaultRealmCompatibility();
        SyncPlaylistContract(); AccSaberContract(); ThreeServiceSyncContract();
        if (argc > 1 && std::string(argv[1]) == "--export-ability-chart") ExportAbilityPreview();
        std::cout << "All " << checks << " regression checks passed.\n";
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n'; return 1;
    }
}
