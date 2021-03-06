/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2010 The swgANH Team

---------------------------------------------------------------------------------------
*/

#include "ObjectController.h"
#include "ObjectControllerOpcodes.h"
#include "ObjectControllerCommandMap.h"
#include "PlayerObject.h"
#include "SpawnPoint.h"
#include "MessageLib/MessageLib.h"
#include "LogManager/LogManager.h"
#include "DatabaseManager/Database.h"
#include "DatabaseManager/DatabaseResult.h"
#include "DatabaseManager/DataBinding.h"

//#include "Common/Message.h"
//#include "Common/MessageFactory.h"

//=============================================================================
//
// database callback
//
void ObjectController::handleDatabaseJobComplete(void* ref,DatabaseResult* result)
{
	ObjControllerAsyncContainer* asyncContainer = reinterpret_cast<ObjControllerAsyncContainer*>(ref);

	switch(asyncContainer->mQueryType)
	{
		case OCQuery_StatRead:
		{
			if(!asyncContainer->playerObject)
				break;

			Ham* ourHam = asyncContainer->playerObject->getHam();

			StatTargets theTargets;

			DataBinding* binding = mDatabase->CreateDataBinding(9);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetHealth),4,0);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetStrength),4,1);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetConstitution),4,2);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetAction),4,3);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetQuickness),4,4);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetStamina),4,5);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetMind),4,6);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetFocus),4,7);
			binding->addField(DFT_uint32,offsetof(StatTargets,TargetWillpower),4,8);

			uint64	count	= result->getRowCount();
			//make sure we have some values set in case the statmigration table wont hold anything on us
			if(count != 1)
			{
				ourHam->setTargetStatValue(HamBar_Health,ourHam->getPropertyValue(HamBar_Health,HamProperty_MaxHitpoints));
				ourHam->setTargetStatValue(HamBar_Strength,ourHam->getPropertyValue(HamBar_Strength,HamProperty_MaxHitpoints));
				ourHam->setTargetStatValue(HamBar_Constitution,ourHam->getPropertyValue(HamBar_Constitution,HamProperty_MaxHitpoints));

				ourHam->setTargetStatValue(HamBar_Action,ourHam->getPropertyValue(HamBar_Action,HamProperty_MaxHitpoints));
				ourHam->setTargetStatValue(HamBar_Quickness,ourHam->getPropertyValue(HamBar_Quickness,HamProperty_MaxHitpoints));
				ourHam->setTargetStatValue(HamBar_Stamina,ourHam->getPropertyValue(HamBar_Stamina,HamProperty_MaxHitpoints));

				ourHam->setTargetStatValue(HamBar_Mind,ourHam->getPropertyValue(HamBar_Mind,HamProperty_MaxHitpoints));
				ourHam->setTargetStatValue(HamBar_Focus,ourHam->getPropertyValue(HamBar_Focus,HamProperty_MaxHitpoints));
				ourHam->setTargetStatValue(HamBar_Willpower,ourHam->getPropertyValue(HamBar_Willpower,HamProperty_MaxHitpoints));

			}
			else
			{
				result->GetNextRow(binding,&theTargets);
				ourHam->setTargetStatValue(HamBar_Health,theTargets.TargetHealth);
				ourHam->setTargetStatValue(HamBar_Strength,theTargets.TargetStrength);
				ourHam->setTargetStatValue(HamBar_Constitution,theTargets.TargetConstitution);

				ourHam->setTargetStatValue(HamBar_Action,theTargets.TargetAction);
				ourHam->setTargetStatValue(HamBar_Quickness,theTargets.TargetQuickness);
				ourHam->setTargetStatValue(HamBar_Stamina,theTargets.TargetStamina);

				ourHam->setTargetStatValue(HamBar_Mind,theTargets.TargetMind);
				ourHam->setTargetStatValue(HamBar_Focus,theTargets.TargetFocus);
				ourHam->setTargetStatValue(HamBar_Willpower,theTargets.TargetWillpower);

			}

			mDatabase->DestroyDataBinding(binding);
			gMessageLib->sendStatMigrationStartMessage(asyncContainer->playerObject);
		}
		break;

		case OCQuery_FindFriend:
		{
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint64,0,8);

			uint64	ret		= 0;
			uint64	count	= result->getRowCount();

			result->GetNextRow(binding,&ret);

			mDatabase->DestroyDataBinding(binding);
			if(count == 0)
				ret = 0;

			_handleFindFriendDBReply(ret,asyncContainer->mString);

		}
		break;

		case OCQuery_AddFriend:
		{
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint32	ret		= 0;
			//uint64	count	= result->getRowCount();

			result->GetNextRow(binding,&ret);

			mDatabase->DestroyDataBinding(binding);

			_handleAddFriendDBReply(ret,asyncContainer->mString);
		}
		break;

		case OCQuery_RemoveFriend:
		{
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint32	ret		= 0;
			//uint64	count	= result->getRowCount();

			result->GetNextRow(binding,&ret);

			mDatabase->DestroyDataBinding(binding);

			_handleRemoveFriendDBReply(ret,asyncContainer->mString);
		}
		break;

		case OCQuery_AddIgnore:
		{
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint32	ret		= 0;
			//uint64	count	= result->getRowCount();

			result->GetNextRow(binding,&ret);

			mDatabase->DestroyDataBinding(binding);

			_handleAddIgnoreDBReply(ret,asyncContainer->mString);
		}
		break;

		case OCQuery_RemoveIgnore:
		{
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint32	ret		= 0;
			//uint64	count	= result->getRowCount();

			result->GetNextRow(binding,&ret);

			mDatabase->DestroyDataBinding(binding);

			_handleRemoveIgnoreDBReply(ret,asyncContainer->mString);
		}
		break;

		case OCQuery_CloneAtPreDes:
		{
			if (!asyncContainer->playerObject)
				break;

			DataBinding* binding = mDatabase->CreateDataBinding(9);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mHealth.mWounds),4,0);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mStrength.mWounds),4,1);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mConstitution.mWounds),4,2);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mAction.mWounds),4,3);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mQuickness.mWounds),4,4);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mStamina.mWounds),4,5);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mMind.mWounds),4,6);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mFocus.mWounds),4,7);
			binding->addField(DFT_uint32,offsetof(PlayerObject,mHam.mWillpower.mWounds),4,8);

			uint64	count = result->getRowCount();

			result->GetNextRow(binding,asyncContainer->playerObject);

			// Update HAM
			// asyncContainer->playerObject->getHam()->calcAllModifiedHitPoints();
			asyncContainer->playerObject->getHam()->updateAllWounds(0);

			// Invoke the actual cloning process.
			SpawnPoint* sp = reinterpret_cast<SpawnPoint*>(asyncContainer->anyPtr);
			if (sp)
			{
				asyncContainer->playerObject->clone(sp->mCellId,sp->mDirection,sp->mPosition,true);
			}

			mDatabase->DestroyDataBinding(binding);
		}
		break;

		default:
		{
		}
		break;
	}

	mDBAsyncContainerPool.free(asyncContainer);
}



//=============================================================================





