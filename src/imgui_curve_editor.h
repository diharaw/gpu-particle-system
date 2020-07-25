#pragma once

namespace ImGui
{
extern float BezierValue(float dt01, float P[4]);
extern int   Bezier(const char* label, float P[5]);
extern void  ShowBezierDemo();
} // namespace ImGui