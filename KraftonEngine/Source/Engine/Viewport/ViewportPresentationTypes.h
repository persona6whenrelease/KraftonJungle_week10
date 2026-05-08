#pragma once

struct FViewportPresentationRect
{
	float X = 0.0f;
	float Y = 0.0f;
	float Width = 0.0f;
	float Height = 0.0f;

	FViewportPresentationRect() = default;

	FViewportPresentationRect(float InX, float InY, float InWidth, float InHeight)
		: X(InX)
		, Y(InY)
		, Width(InWidth)
		, Height(InHeight)
	{
	}

	bool IsValid() const
	{
		return Width > 1.0f && Height > 1.0f;
	}

	float Right() const
	{
		return X + Width;
	}

	float Bottom() const
	{
		return Y + Height;
	}

	bool Contains(float ScreenX, float ScreenY) const
	{
		return IsValid()
			&& ScreenX >= X
			&& ScreenY >= Y
			&& ScreenX < Right()
			&& ScreenY < Bottom();
	}

	bool ScreenToLocal(float ScreenX, float ScreenY, float& OutX, float& OutY) const
	{
		if (!Contains(ScreenX, ScreenY))
		{
			return false;
		}

		OutX = ScreenX - X;
		OutY = ScreenY - Y;
		return true;
	}
};
