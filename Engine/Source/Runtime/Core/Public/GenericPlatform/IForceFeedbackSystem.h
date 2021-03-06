// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "IInputInterface.h"

// Abstract interface for the force feedback system
// class is deprecated and will be removed in favor of IInputInterface
class IForceFeedbackSystem : public IInputInterface
{
public:
	virtual ~IForceFeedbackSystem() {};
};

