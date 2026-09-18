#pragma once

#include "Updater\update.h"
#include "User Settings/Macros/macro.h"
#include "User Settings/Game Profiles/gameProfile.h"

constexpr short defaultWindowWidth = 1280;
constexpr short defaultWindowHeigth = 720;

int GUI(controller& x360Controller, std::vector<Macros>& Macro, std::vector<gameProfile>& gameProfiles);

