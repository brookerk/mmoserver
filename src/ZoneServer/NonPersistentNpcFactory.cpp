/*
---------------------------------------------------------------------------------------
This source file is part of SWG:ANH (Star Wars Galaxies - A New Hope - Server Emulator)

For more information, visit http://www.swganh.com

Copyright (c) 2006 - 2010 The SWG:ANH Team
---------------------------------------------------------------------------------------
Use of this source code is governed by the GPL v3 license that can be found
in the COPYING file or at http://www.gnu.org/licenses/gpl-3.0.html

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
---------------------------------------------------------------------------------------
*/

#include "NonPersistentNpcFactory.h"
#include "PlayerEnums.h"
#include "AttackableCreature.h"
#include "AttackableStaticNpc.h"
#include "FillerNPC.h"
#include "Inventory.h"
#include "LairObject.h"
#include "NPCObject.h"
#include "ObjectFactoryCallback.h"
#include "QuestGiver.h"
#include "SpawnRegion.h"
#include "Trainer.h"
#include "Weapon.h"
#include "WorldConfig.h"
#include "WorldManager.h"

#include "LogManager/LogManager.h"
#include "DatabaseManager/Database.h"
#include "DatabaseManager/DatabaseResult.h"
#include "DatabaseManager/DataBinding.h"

#include "Utils/utils.h"
#include "Utils/rand.h"
#include <cassert>

//=============================================================================

class QueryNonPersistentNpcFactory
{
	public:

		QueryNonPersistentNpcFactory(ObjectFactoryCallback* ofCallback,uint32 queryType, uint64 templateId = 0, uint64 id = 0) :
									mOfCallback(ofCallback),mQueryType(queryType), mTemplateId(templateId), mId(id) {}

		QueryNonPersistentNpcFactory(ObjectFactoryCallback* ofCallback,
									 uint32 queryType,
									 uint64 templateId,
									 uint64 id,
									 uint64 spawnCellId,
                                     const glm::vec3& spawnPosition,
                                     const glm::quat& spawnDirection,
									 uint64	respawnDelay,
									 uint64 parentLairId) :
									mOfCallback(ofCallback),
									mQueryType(queryType), 
									mTemplateId(templateId), 
									mId(id),
									mSpawnCellId(spawnCellId),
									mSpawnPosition(spawnPosition),
									mSpawnDirection(spawnDirection),
									mRespawnDelay(respawnDelay),
									mParentObjectId(parentLairId) {}

		ObjectFactoryCallback*	mOfCallback;
		uint32					mQueryType;
		uint64					mTemplateId;
		uint64					mId;
		uint64					mSpawnCellId;
		uint64					mSpawnRegionId;
        glm::vec3				mSpawnPosition; 
        glm::quat				mSpawnDirection;
		uint64					mRespawnDelay;
		bool					mFirstSpawn;
		uint64					mParentObjectId;
};

//=============================================================================

// Basic lair data when loading from DB.

class NpcLairEntityEx
{
	public:
		NpcLairEntityEx(){}
		
		uint64	mCreatureSpawnRegion;
		uint64	mTemplateId;
		uint32	mCreatureGroup;
		uint32	mNumberOfLairs;
		float	mSpawnPosX;
		float	mSpawnPosZ;
		float	mSpawnDirY;
		float	mSpawnDirW;
		uint32	mFamily;
		BString	mFaction;
		BString	mObjectString;
		BString	mStfName;
		BString	mStfFile;
};

//=============================================================================

NonPersistentNpcFactory*	NonPersistentNpcFactory::mSingleton  = NULL;

//======================================================================================================================

NonPersistentNpcFactory* NonPersistentNpcFactory::Instance(void)
{
	if (!mSingleton)
	{
		mSingleton = new NonPersistentNpcFactory(WorldManager::getSingletonPtr()->getDatabase());
	}
	return mSingleton;
}



// This constructor prevents the default constructor to be used, as long as the constructor is keept private.

NonPersistentNpcFactory::NonPersistentNpcFactory() : FactoryBase(NULL)
{
}

//=============================================================================
NonPersistentNpcFactory::NonPersistentNpcFactory(Database* database)  : FactoryBase(database)
{
	_setupDatabindings();
}

//=============================================================================

NonPersistentNpcFactory::~NonPersistentNpcFactory()
{
	_destroyDatabindings();
	mSingleton = NULL;
}

//=============================================================================

void NonPersistentNpcFactory::handleDatabaseJobComplete(void* ref,DatabaseResult* result)
{
	if(!result){//Crash bug; http://paste.swganh.org/viewp.php?id=20100627073558-0930186c997f6dae885bf5b9b0655b8f
		gLogger->log(LogManager::CRITICAL,"NonPersistentNpcFactory::handleDatabaseJobComplete() DatabaseResult object passed was invalid!");
		return;
	}
	QueryNonPersistentNpcFactory* asyncContainer = reinterpret_cast<QueryNonPersistentNpcFactory*>(ref);

	switch(asyncContainer->mQueryType)
	{
		case NonPersistentNpcQuery_Attributes:
		{
			Object* object = gWorldManager->getObjectById(asyncContainer->mId);
			if (object)
			{
				_buildAttributeMap(object,result);
				if (object->getLoadState() == LoadState_Loaded && asyncContainer->mOfCallback)
				{
					asyncContainer->mOfCallback->handleObjectReady(object);
				}
			}
			else
			{
				gLogger->log(LogManager::DEBUG,"NonPersistentNpcFactory::handleDatabaseJobComplete() Object is GONE");
			}
		}
		break;
//=============================================================================
//
//	Upgrade version for use of the correct DB.
//
//=============================================================================

		case NonPersistentNpcQuery_LairTemplate:
		{
			NpcLairEntityEx lair;
			

			DataBinding* lairSpawnBinding = mDatabase->CreateDataBinding(4);
			lairSpawnBinding->addField(DFT_uint64,offsetof(NpcLairEntityEx,mCreatureSpawnRegion),8,0);
			lairSpawnBinding->addField(DFT_uint64,offsetof(NpcLairEntityEx,mTemplateId),8,1);
			lairSpawnBinding->addField(DFT_uint32,offsetof(NpcLairEntityEx,mCreatureGroup),4,2);
			lairSpawnBinding->addField(DFT_uint32,offsetof(NpcLairEntityEx,mFamily),4,3);
			
			DataBinding* lairSpawnNpcBinding = mDatabase->CreateDataBinding(4);
			lairSpawnNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mModel),255,4);
			lairSpawnNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mSpecies),255,5);
			lairSpawnNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mSpeciesGroup),255,6);
			lairSpawnNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mFaction),32,7);
					
			uint64 count = result->getRowCount();

			result->GetNextRow(lairSpawnBinding,&lair);

			// Let's create the lair.
		
			// We save the lairs-type... that's kinda a template for the complete lair.
			LairObject* npc	= new LairObject(asyncContainer->mTemplateId);

			// Set the new if of this temp object.
			npc->setId(asyncContainer->mId);

			// Register object with WorldManager.
			gWorldManager->addObject(npc, true);

			// May need the height also, in case of pre set (fixed) spawn position.
			npc->mPosition = asyncContainer->mSpawnPosition;

			if (npc->getParentId() == 0)
			{
				// Heightmap only works outside.
				npc->mPosition.y = npc->getHeightAt2DPosition(lair.mSpawnPosX, lair.mSpawnPosZ, true);
			}
			else
			{
				// We do not have support for handling creatures inside.
				//assert(false && "NonPersistentNpcFactory::handleDatabaseJobComplete NonPersistentNpcQuery_LairTemplate No support for handling creatures inside");
				gLogger->log(LogManager::CRITICAL,"NonPersistentNpcFactory::handleDatabaseJobComplete NonPersistentNpcQuery_LairTemplate No support for handling creatures inside.");
				npc->mPosition.y = 0;
			}
			
			
			npc->mDirection.y = lair.mSpawnDirY;
			npc->mDirection.w = lair.mSpawnDirW;

			// Let's get the spawn area.
			SpawnRegion* spawnRegion = dynamic_cast<SpawnRegion*>(gWorldManager->getObjectById(asyncContainer->mSpawnRegionId));

			npc->mSpawnRegion = 0;
			if(spawnRegion)
			{
				spawnRegion->addLair(npc);
				npc->mSpawnRegion = spawnRegion->getId(); 
			}

			result->ResetRowIndex();
			result->GetNextRow(lairSpawnNpcBinding,(void*)npc );
			
			mDatabase->DestroyDataBinding(lairSpawnBinding);
			mDatabase->DestroyDataBinding(lairSpawnNpcBinding);

			Inventory*	npcInventory = new Inventory();
			npcInventory->setParent(npc);

			npc->mHam.mHealth.setCurrentHitPoints(500);
			npc->mHam.mAction.setCurrentHitPoints(500);
			npc->mHam.mMind.setCurrentHitPoints(500);
			npc->mHam.calcAllModifiedHitPoints();

			// inventory

			npcInventory->setId(npc->mId + INVENTORY_OFFSET);
			npcInventory->setParentId(npc->mId);
			npcInventory->setModelString("object/tangible/inventory/shared_creature_inventory.iff");
			
			npcInventory->setName("inventory");
			npcInventory->setNameFile("item_n");
			npcInventory->setTangibleGroup(TanGroup_Inventory);
			npcInventory->setTangibleType(TanType_CreatureInventory);
			npc->mEquipManager.addEquippedObject(CreatureEquipSlot_Inventory,npcInventory);

			npc->setType(ObjType_Creature);

			// This will ensure the use of the single H(am) bar.
			npc->setCreoGroup(CreoGroup_AttackableObject);	
			npc->mTypeOptions = 0x0;
			npc->togglePvPStateOn((CreaturePvPStatus)(CreaturePvPStatus_Attackable));

			QueryNonPersistentNpcFactory* asContainer = new QueryNonPersistentNpcFactory(asyncContainer->mOfCallback, NonPersistentNpcQuery_LairCreatureTemplates, asyncContainer->mTemplateId, asyncContainer->mId);
			// Do not transfer object refs, use the handle, i.e. asyncContainer->mId
			 //asContainer-> >mObject = npc;

			mDatabase->ExecuteSqlAsync(this, asContainer,
							"SELECT creature_groups.creature_id "
							"FROM creature_groups "
							"WHERE creature_groups.creature_group_id=%u;",lair.mCreatureGroup);

		}
		break;

		case NonPersistentNpcQuery_LairCreatureTemplates:
		{
			// Get the lair object.
			LairObject* npc = dynamic_cast<LairObject*>(gWorldManager->getObjectById(asyncContainer->mId));
			assert(npc && "NonPersistentNpcFactory::handleDatabaseJobComplete NonPersistentNpcQuery_LairCreatureTemplates WorldManager unable to find object id");

			uint64	creatureTemplateId;
			DataBinding* creatureTemplateBinding = mDatabase->CreateDataBinding(1);
			creatureTemplateBinding->addField(DFT_uint64,0,8,0);

			uint64 count = result->getRowCount();

			int32	spawnRateInc = 100/static_cast<uint32>(count);
			int32	spawnRate = -1;

			for (uint64 i = 0; i < count; i++)
			{
				result->GetNextRow(creatureTemplateBinding, &creatureTemplateId);
				npc->setCreatureTemplate((uint32)i, creatureTemplateId);

				spawnRate += spawnRateInc;
				npc->setCreatureSpawnRate((uint32)i, (uint32)spawnRate);
			}
			if (count > 0 && spawnRate < 99)
			{
				npc->setCreatureSpawnRate(static_cast<uint32>(count) - 1, 99);
			}
			mDatabase->DestroyDataBinding(creatureTemplateBinding);

			QueryNonPersistentNpcFactory* asContainer = new QueryNonPersistentNpcFactory(asyncContainer->mOfCallback,NonPersistentNpcQuery_Attributes, 0, npc->getId());

			mDatabase->ExecuteSqlAsync(this,asContainer,"SELECT attributes.name,lair_attributes.value,attributes.internal"
				" FROM lair_attributes"
				" INNER JOIN attributes ON (lair_attributes.attribute_id = attributes.id)"
				" WHERE lair_attributes.lair_id = %"PRIu64" ORDER BY lair_attributes.order", asyncContainer->mTemplateId);
		}
		break;

		case NonPersistentNpcQuery_CreatureTemplate:
		{
			NPCObject* npc = _createNonPersistentNpc(result, asyncContainer->mTemplateId, asyncContainer->mId, asyncContainer->mParentObjectId);
			//assert(npc);
			if(!npc)
			{
				delete asyncContainer;
				return;
			}

			// Spawn related data.
			npc->setCellIdForSpawn(asyncContainer->mSpawnCellId);
			npc->setSpawnPosition(asyncContainer->mSpawnPosition);
			npc->setSpawnDirection(asyncContainer->mSpawnDirection);
			npc->setRespawnDelay(asyncContainer->mRespawnDelay);
			npc->setFirstSpawn(asyncContainer->mFirstSpawn);

			if( npc->getLoadState() == LoadState_Loaded && asyncContainer->mOfCallback)
			{
				asyncContainer->mOfCallback->handleObjectReady(npc);
			}
			else if (npc->getLoadState() == LoadState_Attributes)
			{
				QueryNonPersistentNpcFactory* asContainer = new QueryNonPersistentNpcFactory(asyncContainer->mOfCallback,NonPersistentNpcQuery_Attributes, 0, npc->getId());

				mDatabase->ExecuteSqlAsync(this,asContainer,"SELECT attributes.name,c.value,attributes.internal"
					" FROM creature_attributes c"
					" INNER JOIN attributes ON (c.attribute_id = attributes.id)"
					" WHERE c.creature_id = %"PRIu64" ORDER BY c.order", asyncContainer->mTemplateId);
			}
		}
		break;

		case NonPersistentNpcQuery_NpcTemplate:
		{
			NPCObject* npc = _createNonPersistentNpc(result, asyncContainer->mTemplateId, asyncContainer->mId, asyncContainer->mParentObjectId);
			//we can't assert here, as not all npc types are implemented (yet)
			//assert(npc);
			if(!npc){
				gLogger->log(LogManager::DEBUG,"NonPersistentNpcFactory::handleDatabaseJobComplete() unable to _createNonPersistentNpc, see above message.");
				break;
			
			assert(npc);
			if(!npc)
			{
				return;
			}

			// Spawn related data.
			npc->setCellIdForSpawn(asyncContainer->mSpawnCellId);
			npc->setSpawnPosition(asyncContainer->mSpawnPosition);
			npc->setSpawnDirection(asyncContainer->mSpawnDirection);
			npc->setRespawnDelay(asyncContainer->mRespawnDelay);
			npc->setFirstSpawn(asyncContainer->mFirstSpawn);

			if( npc->getLoadState() == LoadState_Loaded && asyncContainer->mOfCallback)
			{
				asyncContainer->mOfCallback->handleObjectReady(npc);
			}
			else if (npc->getLoadState() == LoadState_Attributes)
			{
				QueryNonPersistentNpcFactory* asContainer = new QueryNonPersistentNpcFactory(asyncContainer->mOfCallback,NonPersistentNpcQuery_Attributes, 0, npc->getId());

				mDatabase->ExecuteSqlAsync(this,asContainer,"SELECT attributes.name,non_persistent_npc_attributes.value,attributes.internal"
					" FROM non_persistent_npc_attributes"
					" INNER JOIN attributes ON (non_persistent_npc_attributes.attribute_id = attributes.id)"
					" WHERE non_persistent_npc_attributes.npc_id = %"PRIu64" ORDER BY non_persistent_npc_attributes.order", asyncContainer->mTemplateId);
			}
			}
		break;
			}
		default:
		{
			gLogger->log(LogManager::DEBUG,"NonPersistentNpcFactory::handleDatabaseJobComplete() UNKNOWN query = %u\n", asyncContainer->mQueryType);
		}
		break;
	}

	delete asyncContainer;
}

//=============================================================================

// Just a placeholder o make compiler happy.
void NonPersistentNpcFactory::requestObject(ObjectFactoryCallback* ofCallback,uint64 id,uint16 subGroup,uint16 subType,DispatchClient* client)
{
	assert(false);
	// requestObject(ofCallback,id, 0);
}

//=============================================================================

NPCObject* NonPersistentNpcFactory::_createNonPersistentNpc(DatabaseResult* result, uint64 templateId, uint64 npcNewId, uint64 controllingObject)
{
	NpcIdentifier	npcIdentifier;

	uint64 count = result->getRowCount();
	if(!count)
	{
		return NULL;
	}

	result->GetNextRow(mNpcIdentifierBinding,(void*)&npcIdentifier);
	result->ResetRowIndex();

	return this->createNonPersistentNpc(result, templateId, npcNewId, npcIdentifier.mFamilyId, controllingObject);
}

NPCObject* NonPersistentNpcFactory::createNonPersistentNpc(DatabaseResult* result, uint64 templateId, uint64 npcNewId, uint32 familyId, uint64 controllingObject)
{
	NPCObject*		npc;

	switch(familyId)
	{
		case NpcFamily_Trainer:
		{
			npc	= new Trainer();
		}
		break;		

		case NpcFamily_Filler:
		{
			npc	= new FillerNPC();
		}
		break;

		case NpcFamily_QuestGiver:
		{
			npc	= new QuestGiver();
		}
		break;

		case NpcFamily_AttackableObject:
		{
			// Stuff like Debris.
			npc	= new AttackableStaticNpc();
		}
		break;

		case NpcFamily_AttackableCreatures:
		{
			// Stuff like npc's and womp rats :).
			npc	= new AttackableCreature(templateId);
		}
		break;

		case NpcFamily_NaturalLairs:
		{
			// First time lairs.

			//Lairs are not supported here, at least not yet.
			gLogger->log(LogManager::WARNING,"NonPersistentNpcFactory::createNonPersistent NpcFamily_NaturalLairs Family(%u), but this is not implemented.",familyId);
			return NULL;
			//npc	= new LairObject(templateId);
		}
		break;

		default:
		{
			gLogger->log(LogManager::WARNING,"NonPersistentNpcFactory::createNonPersistent unknown Family(%u).",familyId);
			return NULL;
			//npc = new NPCObject();
		}
		break;
	}

	// Set the new temporarily id.
	npc->setId(npcNewId);

	// Register object with WorldManager. dont add to si yet!!
	gWorldManager->addObject(npc, true);

	// inventory
	Inventory*	npcInventory = new Inventory();
	npcInventory->setCapacity(50);//we want to be able to fill something in our inventory
	npcInventory->setParent(npc);
	npcInventory->setId(npc->mId + 1);
	npcInventory->setParentId(npc->mId);
	npcInventory->setModelString("object/tangible/inventory/shared_creature_inventory.iff");
	npcInventory->setName("inventory");
	npcInventory->setNameFile("item_n");
	npcInventory->setTangibleGroup(TanGroup_Inventory);
	npcInventory->setTangibleType(TanType_CreatureInventory);
	npc->mEquipManager.addEquippedObject(CreatureEquipSlot_Inventory,npcInventory);

	uint64 count = result->getRowCount();

	result->GetNextRow(mNonPersistentNpcBinding,(void*)npc);

	// The template for this creature, in case of a respawn.
	npc->mNpcTemplateId = templateId;

	// Should bet fetched from attributes, these will do as defaults.
	npc->mHam.mHealth.setCurrentHitPoints(500);
	npc->mHam.mAction.setCurrentHitPoints(500);
	npc->mHam.mMind.setCurrentHitPoints(500);
	npc->mHam.calcAllModifiedHitPoints();
	

	if (npc->getNpcFamily() == NpcFamily_AttackableObject)
	{
		// Dynamic spawned pve-enabled "static" creatures like debris.
		npc->setType(ObjType_Creature);
		npc->setCreoGroup(CreoGroup_AttackableObject);

		npc->mTypeOptions = 0x0;
		
		// Let's start as non-attackable. 
		// npc->togglePvPStateOn((CreaturePvPStatus)(CreaturePvPStatus_Attackable));
	}
	else if (npc->getNpcFamily() == NpcFamily_NaturalLairs)
	{
		//Lairs are not supported here, at least not yet.
		gLogger->log(LogManager::WARNING,"NonPersistentNpcFactory::createNonPersistent NpcFamily_NaturalLairs Family(%u), but this is not implemented.",familyId);
		return NULL;

		// Dynamic spawned pve-enabled "static" creatures like lairs.
		npc->setType(ObjType_Creature);

		// This will ensure the use of the single H(am) bar.
		npc->setCreoGroup(CreoGroup_AttackableObject);	
		npc->mTypeOptions = 0x0;
		npc->togglePvPStateOn((CreaturePvPStatus)(CreaturePvPStatus_Attackable));

		// npc->mHam.mHealth.setCurrentHitPoints(5000);
		// npc->mHam.mHealth.setMaxHitPoints(5000);
		// npc->mHam.mHealth.setBaseHitPoints(5000);
		// npc->mHam.calcAllModifiedHitPoints();

		// Let's put some credits in the inventory.
		// npcInventory->setCredits((gRandom->getRand()%25) + 10);
	}
	else if (npc->getNpcFamily() == NpcFamily_AttackableCreatures)
	{
		// Dynamic spawned pve-enabled creatures.
		npc->setType(ObjType_Creature);
		npc->setCreoGroup(CreoGroup_Creature);

		npc->mTypeOptions = 0x0;

		if (gWorldConfig->isTutorial())
		{
			npc->togglePvPStateOn((CreaturePvPStatus)(CreaturePvPStatus_Attackable + CreaturePvPStatus_Aggressive + CreaturePvPStatus_Enemy ));
		}
		else
		{
			npc->togglePvPStateOn((CreaturePvPStatus)(CreaturePvPStatus_Attackable));
		}

		AttackableCreature* attackableNpc = dynamic_cast<AttackableCreature*>(npc);
		assert(attackableNpc && "NonPersistentNpcFactory::createNonPersistent unable to cast npc to AttackableCreature instance");

		// Fix this later
		// Also set the owner (lair) who's controlling this creature.
		attackableNpc->setLairId(controllingObject);

		Weapon* defaultWeapon = new Weapon();
		defaultWeapon->setId(gWorldManager->getRandomNpId());

		defaultWeapon->setParentId(npc->mId);
		defaultWeapon->setModelString("object/weapon/melee/unarmed/shared_unarmed_default_player.iff");
		defaultWeapon->setGroup(WeaponGroup_Unarmed);
		defaultWeapon->setEquipSlotMask(CreatureEquipSlot_Hold_Left);
		defaultWeapon->addInternalAttribute("weapon_group","1");
 
		npc->mEquipManager.setDefaultWeapon(defaultWeapon);
		npc->mEquipManager.equipDefaultWeapon();

// Weapon to use should be gotten from attibutes or whereever we find that kind of info.
		// This little fellow may need a gun.
		Weapon* pistol = new Weapon();
		pistol->setId(gWorldManager->getRandomNpId());

		pistol->setParentId(npc->mId);
		pistol->setModelString("object/weapon/ranged/pistol/shared_pistol_cdef.iff");
		pistol->setGroup(WeaponGroup_Pistol);
		pistol->setEquipSlotMask(CreatureEquipSlot_Hold_Left);
		pistol->addInternalAttribute("weapon_group","32");
		attackableNpc->setPrimaryWeapon(pistol);

		
		// A saber can be handy, too.
		Weapon* saber = new Weapon();
		saber->setId(gWorldManager->getRandomNpId());

		saber->setParentId(npc->mId);
		saber->setModelString("object/weapon/melee/sword/shared_sword_lightsaber_vader.iff");
		saber->setGroup(WeaponGroup_2h);
		saber->setEquipSlotMask(CreatureEquipSlot_Hold_Left);
		saber->addInternalAttribute("weapon_group","4");
		attackableNpc->setSecondaryWeapon(saber);

		if (gWorldConfig->isTutorial())
		{
			attackableNpc->equipPrimaryWeapon();
		}
		else
		{
			// attackableNpc->equipSecondaryWeapon();
		}

// Should be handle by "loot manager"
		// Let's put some credits in the inventory.
		npcInventory->setCredits((gRandom->getRand()%25) + 10);
	}
	else
	{
		npc->mTypeOptions = 0x108;
	}
	npc->setLoadState(LoadState_Attributes);

	return npc;
}

//=============================================================================
//
// 

void NonPersistentNpcFactory::_setupDatabindings()
{
	mNonPersistentNpcBinding = mDatabase->CreateDataBinding(11);
	mNonPersistentNpcBinding->addField(DFT_uint64,offsetof(NPCObject,mSpeciesId),8,0);
	mNonPersistentNpcBinding->addField(DFT_uint64,offsetof(NPCObject,mLootGroupId),8,1);
	mNonPersistentNpcBinding->addField(DFT_uint8,offsetof(NPCObject,mPosture),1,2);
	mNonPersistentNpcBinding->addField(DFT_uint64,offsetof(NPCObject,mState),8,3);
	mNonPersistentNpcBinding->addField(DFT_uint16,offsetof(NPCObject,mCL),2,4);
	mNonPersistentNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mModel),255,5);
	mNonPersistentNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mSpecies),255,6);
	mNonPersistentNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mSpeciesGroup),255,7);
	mNonPersistentNpcBinding->addField(DFT_bstring,offsetof(NPCObject,mFaction),32,8);
	mNonPersistentNpcBinding->addField(DFT_uint8,offsetof(NPCObject,mMoodId),1,9);
	mNonPersistentNpcBinding->addField(DFT_float,offsetof(NPCObject,mScale),4,10);

	mNpcIdentifierBinding = mDatabase->CreateDataBinding(1);
	mNpcIdentifierBinding->addField(DFT_uint32,offsetof(NpcIdentifier,mFamilyId),4,11);
}

//=============================================================================

void NonPersistentNpcFactory::_destroyDatabindings()
{
	mDatabase->DestroyDataBinding(mNonPersistentNpcBinding);
	mDatabase->DestroyDataBinding(mNpcIdentifierBinding);
}

//=============================================================================
//
// request a lair
//
//=============================================================================
void NonPersistentNpcFactory::requestLairObject(ObjectFactoryCallback* ofCallback, uint64 lairsId, uint64 npcNewId, uint64 spawnRegion, glm::vec3 spawnPoint, bool firstSpawn)
{
	
	QueryNonPersistentNpcFactory* asynccontainer = new QueryNonPersistentNpcFactory(ofCallback, NonPersistentNpcQuery_LairTemplate, lairsId, npcNewId);

	if(spawnRegion == 0)
	{
		return;
	}

	asynccontainer->mSpawnRegionId = spawnRegion;
	asynccontainer->mFirstSpawn = firstSpawn;
	asynccontainer->mSpawnPosition = spawnPoint;
	mDatabase->ExecuteSqlAsync(this,asynccontainer,
								"SELECT lairs.creature_spawn_region, lairs.lair_template, lairs.creature_group, "
								"lairs.family, lair_templates.lair_object_string, lair_templates.stf_name, lair_templates.stf_file, "
								"faction.name "
								"FROM lairs "
								"INNER JOIN spawn_groups ON (lairs.creature_spawn_region = spawn_groups.id) "
								"INNER JOIN spawns ON (spawn_groups.spawn_id = spawns.id AND %u = spawns.spawn_planet) "
								"INNER JOIN lair_templates ON (lairs.lair_template = lair_templates.id) "
								"INNER JOIN faction ON (lairs.faction = faction.id) "
								"WHERE lairs.id=%u;",gWorldManager->getZoneId(), lairsId);
}

//===========================================================================================================================
//
//request a crearure Object

void NonPersistentNpcFactory::requestCreatureObject(ObjectFactoryCallback* ofCallback, 
											   uint64 creatureTemplateId, 
											   uint64 npcNewId,
											   uint64 spawnCellId,
                                               const glm::vec3& spawnPosition, 
											   const glm::quat&	spawnDirection,
											   uint64 respawnDelay,
											   uint64 parentLairId)
{

	mDatabase->ExecuteSqlAsync(this,new QueryNonPersistentNpcFactory(ofCallback, NonPersistentNpcQuery_CreatureTemplate, creatureTemplateId, npcNewId, spawnCellId, spawnPosition, spawnDirection, respawnDelay, parentLairId),
								"SELECT c.creature_species_id, c.loot_group_id, "
								"c.creature_posture, c.creature_state, c.creature_level, "
								"c.creature_type, c.stf_variable_id, c.stf_file_id, "
								"f.name, c.creature_moodID, c.creature_scale , c.creature_family "
								"FROM creatures c "
								"INNER JOIN faction f ON (c.creature_faction = f.id) "
								"WHERE c.id=%"PRIu64";", creatureTemplateId);
}
