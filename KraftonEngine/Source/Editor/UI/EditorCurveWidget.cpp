#include "UI/EditorCurveWidget.h"

#include "Serialization/CurveSaveManager.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include <algorithm>
#include <ctime>
#include <cstring>

namespace ImGui
{
	template<int Steps>
	void BezierTable(const ImVec2 P[4], ImVec2 Results[Steps + 1])
	{
		static float Coefficients[(Steps + 1) * 4];
		static float* K = nullptr;

		if (!K)
		{
			K = Coefficients;
			for (unsigned Step = 0; Step <= Steps; ++Step)
			{
				const float T = static_cast<float>(Step) / static_cast<float>(Steps);
				Coefficients[Step * 4 + 0] = (1.0f - T) * (1.0f - T) * (1.0f - T);
				Coefficients[Step * 4 + 1] = 3.0f * (1.0f - T) * (1.0f - T) * T;
				Coefficients[Step * 4 + 2] = 3.0f * (1.0f - T) * T * T;
				Coefficients[Step * 4 + 3] = T * T * T;
			}
		}

		for (unsigned Step = 0; Step <= Steps; ++Step)
		{
			Results[Step] = ImVec2(
				K[Step * 4 + 0] * P[0].x + K[Step * 4 + 1] * P[1].x + K[Step * 4 + 2] * P[2].x + K[Step * 4 + 3] * P[3].x,
				K[Step * 4 + 0] * P[0].y + K[Step * 4 + 1] * P[1].y + K[Step * 4 + 2] * P[2].y + K[Step * 4 + 3] * P[3].y
			);
		}
	}

	float BezierValue(float Delta01, float P[4])
	{
		constexpr int STEPS = 256;

		const ImVec2 ControlPoints[4] = { { 0.0f, 0.0f }, { P[0], P[1] }, { P[2], P[3] }, { 1.0f, 1.0f } };
		ImVec2 Results[STEPS + 1];
		BezierTable<STEPS>(ControlPoints, Results);

		const float ClampedDelta = std::clamp(Delta01, 0.0f, 1.0f);
		return Results[static_cast<int>(ClampedDelta * STEPS)].y;
	}

	bool Bezier(const char* Label, float P[5])
	{
		constexpr int SMOOTHNESS = 64;
		constexpr int CURVE_WIDTH = 4;
		constexpr int LINE_WIDTH = 1;
		constexpr int GRAB_RADIUS = 8;
		constexpr int GRAB_BORDER = 2;
		constexpr bool AREA_CONSTRAINED = true;
		constexpr int AREA_WIDTH = 128;

		static struct
		{
			const char* Name;
			float Points[4];
		} Presets[] = {
			{ "Linear", { 0.000f, 0.000f, 1.000f, 1.000f } },

			{ "In Sine", { 0.470f, 0.000f, 0.745f, 0.715f } },
			{ "In Quad", { 0.550f, 0.085f, 0.680f, 0.530f } },
			{ "In Cubic", { 0.550f, 0.055f, 0.675f, 0.190f } },
			{ "In Quart", { 0.895f, 0.030f, 0.685f, 0.220f } },
			{ "In Quint", { 0.755f, 0.050f, 0.855f, 0.060f } },
			{ "In Expo", { 0.950f, 0.050f, 0.795f, 0.035f } },
			{ "In Circ", { 0.600f, 0.040f, 0.980f, 0.335f } },
			{ "In Back", { 0.600f, -0.280f, 0.735f, 0.045f } },

			{ "Out Sine", { 0.390f, 0.575f, 0.565f, 1.000f } },
			{ "Out Quad", { 0.250f, 0.460f, 0.450f, 0.940f } },
			{ "Out Cubic", { 0.215f, 0.610f, 0.355f, 1.000f } },
			{ "Out Quart", { 0.165f, 0.840f, 0.440f, 1.000f } },
			{ "Out Quint", { 0.230f, 1.000f, 0.320f, 1.000f } },
			{ "Out Expo", { 0.190f, 1.000f, 0.220f, 1.000f } },
			{ "Out Circ", { 0.075f, 0.820f, 0.165f, 1.000f } },
			{ "Out Back", { 0.175f, 0.885f, 0.320f, 1.275f } },

			{ "InOut Sine", { 0.445f, 0.050f, 0.550f, 0.950f } },
			{ "InOut Quad", { 0.455f, 0.030f, 0.515f, 0.955f } },
			{ "InOut Cubic", { 0.645f, 0.045f, 0.355f, 1.000f } },
			{ "InOut Quart", { 0.770f, 0.000f, 0.175f, 1.000f } },
			{ "InOut Quint", { 0.860f, 0.000f, 0.070f, 1.000f } },
			{ "InOut Expo", { 1.000f, 0.000f, 0.000f, 1.000f } },
			{ "InOut Circ", { 0.785f, 0.135f, 0.150f, 0.860f } },
			{ "InOut Back", { 0.680f, -0.550f, 0.265f, 1.550f } },
		};

		bool bReload = false;
		ImGui::PushID(Label);
		if (ImGui::ArrowButton("##lt", ImGuiDir_Left))
		{
			if (--P[4] >= 0.0f)
			{
				bReload = true;
			}
			else
			{
				++P[4];
			}
		}
		ImGui::SameLine();

		if (ImGui::Button("Presets"))
		{
			ImGui::OpenPopup("!Presets");
		}
		if (ImGui::BeginPopup("!Presets"))
		{
			for (int Index = 0; Index < IM_ARRAYSIZE(Presets); ++Index)
			{
				if (Index == 1 || Index == 9 || Index == 17)
				{
					ImGui::Separator();
				}
				if (ImGui::MenuItem(Presets[Index].Name, nullptr, static_cast<int>(P[4]) == Index))
				{
					P[4] = static_cast<float>(Index);
					bReload = true;
				}
			}
			ImGui::EndPopup();
		}
		ImGui::SameLine();

		if (ImGui::ArrowButton("##rt", ImGuiDir_Right))
		{
			if (++P[4] < IM_ARRAYSIZE(Presets))
			{
				bReload = true;
			}
			else
			{
				--P[4];
			}
		}
		ImGui::SameLine();
		ImGui::PopID();

		if (bReload)
		{
			std::memcpy(P, Presets[static_cast<int>(P[4])].Points, sizeof(float) * 4);
		}
		const bool bPresetChanged = bReload;

		const ImGuiStyle& Style = GetStyle();
		ImDrawList* DrawList = GetWindowDrawList();
		ImGuiWindow* Window = GetCurrentWindow();
		if (Window->SkipItems)
		{
			return false;
		}

		bool bChanged = SliderFloat4(Label, P, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_None);
		bChanged |= bPresetChanged;
		bool bHovered = IsItemActive() || IsItemHovered();
		Dummy(ImVec2(0.0f, 3.0f));

		const float Avail = GetContentRegionAvail().x;
		const float Dim = AREA_WIDTH > 0 ? static_cast<float>(AREA_WIDTH) : Avail;
		const ImVec2 Canvas(Dim, Dim);

		ImRect Bb(Window->DC.CursorPos, Window->DC.CursorPos + Canvas);
		ItemSize(Bb);
		if (!ItemAdd(Bb, 0))
		{
			return bChanged;
		}

		const ImGuiID Id = Window->GetID(Label);
		bHovered |= ItemHoverable(ImRect(Bb.Min, Bb.Min + ImVec2(Avail, Dim)), Id, ImGuiItemFlags_None);

		RenderFrame(Bb.Min, Bb.Max, GetColorU32(ImGuiCol_FrameBg, 1.0f), true, Style.FrameRounding);

		for (int Index = 0; Index <= 4; ++Index)
		{
			const float X = Bb.Min.x + Canvas.x * (static_cast<float>(Index) / 4.0f);
			DrawList->AddLine(ImVec2(X, Bb.Min.y), ImVec2(X, Bb.Max.y), GetColorU32(ImGuiCol_TextDisabled));

			const float Y = Bb.Min.y + Canvas.y * (static_cast<float>(Index) / 4.0f);
			DrawList->AddLine(ImVec2(Bb.Min.x, Y), ImVec2(Bb.Max.x, Y), GetColorU32(ImGuiCol_TextDisabled));
		}

		const ImVec2 ControlPoints[4] = { { 0.0f, 0.0f }, { P[0], P[1] }, { P[2], P[3] }, { 1.0f, 1.0f } };
		ImVec2 Results[SMOOTHNESS + 1];
		BezierTable<SMOOTHNESS>(ControlPoints, Results);

		{
			const ImVec2 Mouse = GetIO().MousePos;
			ImVec2 Pos[2];
			float Distance[2];

			for (int Index = 0; Index < 2; ++Index)
			{
				Pos[Index] = ImVec2(P[Index * 2 + 0], 1.0f - P[Index * 2 + 1]) * (Bb.Max - Bb.Min) + Bb.Min;
				Distance[Index] = (Pos[Index].x - Mouse.x) * (Pos[Index].x - Mouse.x) + (Pos[Index].y - Mouse.y) * (Pos[Index].y - Mouse.y);
			}

			const int Selected = Distance[0] < Distance[1] ? 0 : 1;
			if (bHovered && Distance[Selected] < (4 * GRAB_RADIUS * 4 * GRAB_RADIUS))
			{
				SetTooltip("(%4.3f, %4.3f)", P[Selected * 2 + 0], P[Selected * 2 + 1]);

				if (IsMouseClicked(0) || IsMouseDragging(0))
				{
					float& Px = (P[Selected * 2 + 0] += GetIO().MouseDelta.x / Canvas.x);
					float& Py = (P[Selected * 2 + 1] -= GetIO().MouseDelta.y / Canvas.y);

					if (AREA_CONSTRAINED)
					{
						Px = std::clamp(Px, 0.0f, 1.0f);
						Py = std::clamp(Py, 0.0f, 1.0f);
					}

					bChanged = true;
				}
			}
		}

		{
			const ImColor Color(GetStyle().Colors[ImGuiCol_PlotLines]);
			for (int Index = 0; Index < SMOOTHNESS; ++Index)
			{
				const ImVec2 Point0 = { Results[Index + 0].x, 1.0f - Results[Index + 0].y };
				const ImVec2 Point1 = { Results[Index + 1].x, 1.0f - Results[Index + 1].y };
				const ImVec2 R(Point0.x * (Bb.Max.x - Bb.Min.x) + Bb.Min.x, Point0.y * (Bb.Max.y - Bb.Min.y) + Bb.Min.y);
				const ImVec2 S(Point1.x * (Bb.Max.x - Bb.Min.x) + Bb.Min.x, Point1.y * (Bb.Max.y - Bb.Min.y) + Bb.Min.y);
				DrawList->AddLine(R, S, Color, CURVE_WIDTH);
			}
		}

		static std::clock_t Epoch = std::clock();
		const ImVec4 White(GetStyle().Colors[ImGuiCol_Text]);
		for (int Index = 0; Index < 3; ++Index)
		{
			const double Now = (std::clock() - Epoch) / static_cast<double>(CLOCKS_PER_SEC);
			float Delta = (static_cast<int>(Now * 1000.0) % 1000) / 1000.0f;
			Delta += Index / 3.0f;
			if (Delta > 1.0f)
			{
				Delta -= 1.0f;
			}

			const int ResultIndex = static_cast<int>(Delta * SMOOTHNESS);
			const float EvalX = Results[ResultIndex].x;
			const float EvalY = Results[ResultIndex].y;
			const ImVec2 PreviewX = ImVec2(EvalX, 1.0f) * (Bb.Max - Bb.Min) + Bb.Min;
			const ImVec2 PreviewY = ImVec2(0.0f, 1.0f - EvalY) * (Bb.Max - Bb.Min) + Bb.Min;
			const ImVec2 PreviewCurve = ImVec2(EvalX, 1.0f - EvalY) * (Bb.Max - Bb.Min) + Bb.Min;
			DrawList->AddCircleFilled(PreviewX, GRAB_RADIUS / 2.0f, ImColor(White));
			DrawList->AddCircleFilled(PreviewY, GRAB_RADIUS / 2.0f, ImColor(White));
			DrawList->AddCircleFilled(PreviewCurve, GRAB_RADIUS / 2.0f, ImColor(White));
		}

		const float Luma = IsItemActive() || IsItemHovered() ? 0.5f : 1.0f;
		const ImVec4 Pink(1.00f, 0.00f, 0.75f, Luma);
		const ImVec4 Cyan(0.00f, 0.75f, 1.00f, Luma);
		const ImVec2 P1 = ImVec2(P[0], 1.0f - P[1]) * (Bb.Max - Bb.Min) + Bb.Min;
		const ImVec2 P2 = ImVec2(P[2], 1.0f - P[3]) * (Bb.Max - Bb.Min) + Bb.Min;
		DrawList->AddLine(ImVec2(Bb.Min.x, Bb.Max.y), P1, ImColor(White), LINE_WIDTH);
		DrawList->AddLine(ImVec2(Bb.Max.x, Bb.Min.y), P2, ImColor(White), LINE_WIDTH);
		DrawList->AddCircleFilled(P1, GRAB_RADIUS, ImColor(White));
		DrawList->AddCircleFilled(P1, GRAB_RADIUS - GRAB_BORDER, ImColor(Pink));
		DrawList->AddCircleFilled(P2, GRAB_RADIUS, ImColor(White));
		DrawList->AddCircleFilled(P2, GRAB_RADIUS - GRAB_BORDER, ImColor(Cyan));

		return bChanged;
	}

	void ShowBezierDemo()
	{
		static float V[5] = { 0.950f, 0.050f, 0.795f, 0.035f, 6.0f };
		Bezier("easeInExpo", V);
	}
}

void FEditorCurveWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
}

bool FEditorCurveWidget::OpenCurveAsset(const FString& CurvePath)
{
	CurrentCurvePath = CurvePath;
	bHasOpenCurve = !CurrentCurvePath.empty();

	FCurveAsset Curve = FCurveSaveManager::MakeDefaultBezier();
	FString Error;
	if (bHasOpenCurve && FCurveSaveManager::LoadFromFile(CurrentCurvePath, Curve, &Error))
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			BezierPoints[Index] = Curve.ControlPoints[Index];
		}
		BezierPoints[4] = static_cast<float>(Curve.PresetIndex);
		StatusMessage = "Loaded: " + CurrentCurvePath;
		bDirty = false;
		return true;
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		BezierPoints[Index] = Curve.ControlPoints[Index];
	}
	BezierPoints[4] = static_cast<float>(Curve.PresetIndex);
	StatusMessage = Error.empty() ? "Curve load failed." : ("Curve load failed: " + Error);
	bDirty = bHasOpenCurve;
	return false;
}

bool FEditorCurveWidget::SaveCurrentCurve()
{
	if (!bHasOpenCurve || CurrentCurvePath.empty())
	{
		StatusMessage = "No curve file is open.";
		return false;
	}

	FCurveAsset Curve;
	Curve.Version = FCurveSaveManager::CurrentVersion;
	Curve.PresetIndex = static_cast<int32>(BezierPoints[4]);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Curve.ControlPoints[Index] = BezierPoints[Index];
	}

	if (!FCurveSaveManager::SaveToFile(Curve, CurrentCurvePath))
	{
		StatusMessage = "Curve save failed: " + CurrentCurvePath;
		return false;
	}

	StatusMessage = "Saved: " + CurrentCurvePath;
	bDirty = false;
	return true;
}

void FEditorCurveWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_Once);
	if (!ImGui::Begin("Curve Editor"))
	{
		ImGui::End();
		return;
	}

	if (bHasOpenCurve)
	{
		ImGui::TextUnformatted(CurrentCurvePath.c_str());
		if (bDirty)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted("*");
		}
	}
	else
	{
		ImGui::TextDisabled("No curve file open");
	}

	if (!bHasOpenCurve)
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Save"))
	{
		SaveCurrentCurve();
	}
	if (!bHasOpenCurve)
	{
		ImGui::EndDisabled();
	}

	if (!StatusMessage.empty())
	{
		ImGui::SameLine();
		ImGui::TextUnformatted(StatusMessage.c_str());
	}

	ImGui::Separator();
	if (ImGui::Bezier("Bezier", BezierPoints))
	{
		bDirty = true;
	}
	ImGui::End();
}
