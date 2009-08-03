/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2008 The swgANH Team

---------------------------------------------------------------------------------------
*/

#include "MathLib/Quaternion.h"
#include "MissionTerminal.h"
#include "WorldConfig.h"
#include "PlayerObject.h"
#include "Tutorial.h"

//=============================================================================

MissionTerminal::MissionTerminal() : Terminal()
{
	mRadialMenu = RadialMenuPtr(new RadialMenu());
}

//=============================================================================

MissionTerminal::~MissionTerminal()
{
}

//=============================================================================

void MissionTerminal::prepareCustomRadialMenu(CreatureObject* creatureObject, uint8 itemCount)
{
	if (gWorldConfig->isTutorial())
	{
		if (this->getId() == 4294968328)
		{
			// This is the Misson Terminal in the tutorial.
			PlayerObject* player = dynamic_cast<PlayerObject*>(creatureObject);
			if (player)
			{
				Tutorial* tutorial = player->getTutorial();

				// We will not display this when tutorial is in state 21 or 22. LOL...
				if ((tutorial->getSubState() != 21) && (tutorial->getSubState() != 22))
				{
					// "You cannot take a mission from this terminal.  To take a mission, you will need to find a mission terminal on a planet."
					tutorial->scriptSystemMessage("@newbie_tutorial/system_messages:mission_terminal");
					tutorial->scriptPlayMusic(3397);		// sound/tut_00_mission_terminal.snd
				}
			}
		}
	}
}


//=============================================================================
