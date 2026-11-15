#pragma once

#include "imgui.h"

constexpr ImVec4 colBg(0.09f, 0.12f, 0.16f, 1.0f);
constexpr ImVec4 colBar(0.13f, 0.15f, 0.20f, 1.0f);
constexpr ImVec4 colText(0.80f, 0.80f, 0.83f, 1.0f);

extern float uiScale;
float px(float value);
void applyTheme();
void loadFont();
