#pragma once
#include "UnityEngine/Transform.hpp"

namespace rankedpractice {
void BuildAccountSettings(UnityEngine::Transform* parent);
// Called on settings re-entry to refresh names for previously saved numeric IDs.
void RefreshAccountSettings();
}
