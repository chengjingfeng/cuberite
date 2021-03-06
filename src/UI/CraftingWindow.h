
// CraftingWindow.h

// Representing the UI window for the crafting block





#pragma once

#include "Window.h"





class cCraftingWindow:
	public cWindow
{
	using Super = cWindow;

public:

	cCraftingWindow();

	virtual void DistributeStack(cItem & a_ItemStack, int a_Slot, cPlayer & a_Player, cSlotArea * a_ClickedArea, bool a_ShouldApply) override;
};




