#pragma once

#include "GameFramework/AActor.h"

class UCameraComponent;

class ACameraActor : public AActor
{
public:
	DECLARE_CLASS(ACameraActor, AActor)

	ACameraActor() = default;
	~ACameraActor() override = default;

	void InitDefaultComponents() override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }

private:
	UCameraComponent* CameraComponent = nullptr;
};
