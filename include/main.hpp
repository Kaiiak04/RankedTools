#pragma once

#include "scotland2/shared/modloader.h"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "paper2_scotland2/shared/logger.hpp"
#include "modconfig.hpp"
#include "_config.hpp"

#include <atomic>
#include <string>

constexpr auto PaperLogger = Paper::ConstLoggerContext("beatleaderpp");

namespace rankedpractice {
extern std::atomic<bool> refreshInProgress;
void StartRefresh();
void SetStatus(const std::string& status);
}
