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
#include "AttackableCreature.h"
#include "CreatureObject.h"
#include "LairObject.h"
#include "NpcManager.h"
#include "PlayerObject.h"
#include "SpawnManager.h"
#include "SpawnRegion.h"
#include "WorldConfig.h"
#include "WorldManager.h"
#include "ZoneServer/NonPersistentNpcFactory.h"
#include "MessageLib/MessageLib.h"
#include "LogManager/LogManager.h"

#include "DatabaseManager/Database.h"
#include "DatabaseManager/DatabaseResult.h"
#include "DatabaseManager/DataBinding.h"

#include "Utils/utils.h"
#include "Utils/clock.h"
#include "Utils/rand.h"

#include <cassert>

//=============================================================================

// Basic lair data when loading from DB.

class SpawnLairEntity
{
	public:
		SpawnLairEntity(){}

		uint64	mLairsId;
		uint64	mLairTemplateId;
		uint32	mNumberOfLairs;
};


//=============================================================================

SpawnManager* SpawnManager::mInstance  = NULL;


//======================================================================================================================

SpawnManager* SpawnManager::Instance(void)
{
	if (!mInstance)
	{
		mInstance = new SpawnManager(WorldManager::getSingletonPtr()->getDatabase());
	}
	return mInstance;
}


// This constructor prevents the default constructor to be used, as long as the constructor is kept private.

SpawnManager::SpawnManager()
{
}

//=============================================================================


SpawnManager::SpawnManager(Database* database) :mDatabase(database)
{
	// _setupDatabindings();
		// Initialize the queues for NPC-Manager.
	mNpcManagerScheduler	= new Anh_Utils::Scheduler();

	mNpcManagerScheduler->addTask(fastdelegate::MakeDelegate(this,&SpawnManager::_handleDormantNpcs),5,2500,NULL);
	mNpcManagerScheduler->addTask(fastdelegate::MakeDelegate(this,&SpawnManager::_handleReadyNpcs),5,1000,NULL);
	mNpcManagerScheduler->addTask(fastdelegate::MakeDelegate(this,&SpawnManager::_handleActiveNpcs),5,250,NULL);

	mNpcManagerScheduler->addTask(fastdelegate::MakeDelegate(this,&SpawnManager::_handleGeneralObjectTimers),5,2000,NULL);
	
	this->loadSpawns();
}


//=============================================================================

SpawnManager::~SpawnManager()
{
	// _destroyDatabindings();
	mInstance = NULL;
	
	mNpcDormantHandlers.clear();
	mNpcReadyHandlers.clear();
	mNpcActiveHandlers.clear();

	CreatureSpawnRegionMap::iterator it = mCreatureSpawnRegionMap.begin();
	while (it != mCreatureSpawnRegionMap.end())
	{
		delete (*it).second;
		mCreatureSpawnRegionMap.erase(it++);
	}
	mCreatureSpawnRegionMap.clear();
	mCreatureObjectDeletionMap.clear();

	delete(mNpcManagerScheduler);
}

//=============================================================================

void SpawnManager::process()
{
	mNpcManagerScheduler->process();
}
//=============================================================================
//
//	Handle npc.
//
//	Main purposes are to get the npc in suitable timer queue and handling generall issues


void SpawnManager::handleObjectReady(Object* object)
{
	CreatureObject* creature = dynamic_cast<CreatureObject*>(object);
	if (creature)
	{
		creature->respawn();
	}
}

//void SpawnManager::addRegionToDespawnTimer(Object* object)
//{
	//mSpawnRegionScheduler->addTask(fastdelegate::MakeDelegate(this,&gSpawnManager::_handleDormantNpcs),5,2500,NULL);
//}

//=============================================================================================================================
//
// This part is where the natural lairs are loaded from DB.
//
//=============================================================================================================================

void SpawnManager::loadSpawns(void)
{
	// load spawn data

	mDatabase->ExecuteSqlAsync(this,new SpawnAsyncContainer(SpawnQuery_Spawns),
								"SELECT s.id, s.spawn_x, s.spawn_z, s.spawn_width, s.spawn_length, s.spawn_density "
								"FROM spawns s "
								"WHERE s.spawn_planet=%u;",gWorldManager->getZoneId());
}

//=============================================================================================================================
//
//these are the lairs that are part of the spawn we want to load
//
//=============================================================================================================================

void SpawnManager::loadSpawnGroup(uint32 spawn)
{
	//load lair and creature spawn, and optionally heightmaps cache.
	// NpcFamily_NaturalLairs

	SpawnAsyncContainer* asyncContainer = new SpawnAsyncContainer(SpawnQuery_Group);
	asyncContainer->spawnGroup = spawn;

	mDatabase->ExecuteSqlAsync(this,asyncContainer,
								"SELECT sg.id, l.id, l.lair_template, l.creature_group "
								"FROM spawn_groups sg "
								"INNER JOIN  lairs l ON (l.creature_spawn_region = sg.id) "
								"WHERE sg.spawn_id=%u ORDER BY l.id;", spawn);
}

//======================================================================================================================
//
// Handle the queue of Ready npc's.
//

bool SpawnManager::_handleReadyNpcs(uint64 callTime, void* ref)
{
	NpcReadyHandlers::iterator it = mNpcReadyHandlers.begin();
	while (it != mNpcReadyHandlers.end())
	{
		//  The timer has expired?
		if (callTime >= ((*it).second))
		{
			// Yes, handle it.
			NPCObject* npc = dynamic_cast<NPCObject*>(gWorldManager->getObjectById((*it).first));
			if (npc)
			{
				// uint64 waitTime = NpcManager::Instance()->handleReadyNpc(creature, callTime - (*it).second);
				// gLogger->log(LogManager::DEBUG,"Ready...");
				// gLogger->log(LogManager::DEBUG,"Ready... ID = %"PRIu64"",  (*it).first);
				uint64 waitTime = NpcManager::Instance()->handleNpc(npc, callTime - (*it).second);
				if (waitTime)
				{
					// Set next execution time.
					(*it).second = callTime + waitTime;
				}
				else
				{
					// Requested to remove the handler.
					mNpcReadyHandlers.erase(it++);
					// gLogger->log(LogManager::DEBUG,"Removed ready NPC handler...");
				}
			}
			else
			{
				// Remove the expired object...
				mNpcReadyHandlers.erase(it++);
				// gLogger->log(LogManager::DEBUG,"Removed ready NPC handler...");
			}
		}
		else
		{
			++it;
		}
	}
	return true;
}

//======================================================================================================================
//
// Handle the queue of Dormant npc's.
//

bool SpawnManager::_handleDormantNpcs(uint64 callTime, void* ref)
{
	NpcDormantHandlers::iterator it = mNpcDormantHandlers.begin();
	while (it != mNpcDormantHandlers.end())
	{
		//  The timer has expired?
		if (callTime >= ((*it).second))
		{
			// Yes, handle it.
			NPCObject* npc = dynamic_cast<NPCObject*>(gWorldManager->getObjectById((*it).first));
			if (npc)
			{
				// uint64 waitTime = NpcManager::Instance()->handleDormantNpc(creature, callTime - (*it).second);
				// gLogger->log(LogManager::DEBUG,"Dormant... ID = %"PRIu64"",  (*it).first);
				uint64 waitTime = NpcManager::Instance()->handleNpc(npc, callTime - (*it).second);

				if (waitTime)
				{
					// Set next execution time.
					(*it).second = callTime + waitTime;
				}
				else
				{
					// gLogger->log(LogManager::DEBUG,"Removed dormant NPC handler... %"PRIu64"",  (*it).first);

					// Requested to remove the handler.
					mNpcDormantHandlers.erase(it++);
				}
			}
			else
			{
				// Remove the expired object...
				mNpcDormantHandlers.erase(it++);
				gLogger->log(LogManager::DEBUG,"Removed expired dormant NPC handler...");
			}
		}
		else
		{
			++it;
		}
	}
	return true;
}

//======================================================================================================================
//
//	Add a npc to the Dormant queue.
//

void SpawnManager::addDormantNpc(uint64 creature, uint64 when)
{
	 gLogger->log(LogManager::DEBUG,"Adding dormant NPC handler... %"PRIu64"",  creature);

	uint64 expireTime = Anh_Utils::Clock::getSingleton()->getLocalTime();
	mNpcDormantHandlers.insert(std::make_pair(creature, expireTime + when));
}

//======================================================================================================================
//
//	Remove a npc from the Dormant queue.
//

void SpawnManager::removeDormantNpc(uint64 creature)
{
	NpcDormantHandlers::iterator it = mNpcDormantHandlers.find(creature);
	if (it != mNpcDormantHandlers.end())
	{
		// Remove creature.
		mNpcDormantHandlers.erase(it);
	}
}

//======================================================================================================================
//
//	Force a npc from the Dormant queue to be handled at next tick.
//

void SpawnManager::forceHandlingOfDormantNpc(uint64 creature)
{
	NpcDormantHandlers::iterator it = mNpcDormantHandlers.find(creature);
	if (it != mNpcDormantHandlers.end())
	{
		// Change the event time to NOW.
		uint64 now = Anh_Utils::Clock::getSingleton()->getLocalTime();
		(*it).second = now;
	}
}

void SpawnManager::handleDatabaseJobComplete(void* ref, DatabaseResult* result)
{
	SpawnAsyncContainer* asyncContainer = reinterpret_cast<SpawnAsyncContainer*>(ref);
	switch(asyncContainer->mQuery)
	{
		//lair data gets loaded and matched to the relevant spawns
		case SpawnQuery_Group:
		{
			LairData	data;

			DataBinding* lairBinding = mDatabase->CreateDataBinding(4);
			lairBinding->addField(DFT_uint32,offsetof(LairData,spawnGroupId),4,0);
			lairBinding->addField(DFT_uint32,offsetof(LairData,lairId),4,1);
			lairBinding->addField(DFT_uint32,offsetof(LairData,templateId),4,2);
			lairBinding->addField(DFT_uint32,offsetof(LairData,creatureGroupId),4,3);

			uint64 count = result->getRowCount();

			uint32 group = asyncContainer->spawnGroup;

			SpawnMap::iterator spawnIt = mSpawnListMap.find(group);
			if(spawnIt != mSpawnListMap.end())
			{
				for (uint64 i = 0;i < count;i++)
				{
					result->GetNextRow(lairBinding,&data);

					(*spawnIt).second.lairTypeList.push_back(data);
		
				}

				//now create the spawnregion
				SpawnRegion* spawnRegion = new(SpawnRegion);
				spawnRegion->mPosition.x = (*spawnIt).second.posX;
				spawnRegion->mPosition.z = (*spawnIt).second.posZ;
				spawnRegion->setWidth((float)(*spawnIt).second.width);
				spawnRegion->setHeight((float)(*spawnIt).second.height);

				spawnRegion->setId(gWorldManager->getRandomNpNpcIdSequence());

				spawnRegion->setSpawnData(&(*spawnIt).second);

				gWorldManager->addObject(spawnRegion);

			}
			else
			{
				assert(false && "spawnmanager::nospawngroup???");
			}

			
		}
		break;

		//these are the spawndatasets
		case SpawnQuery_Spawns:
		{
			SpawnDataStruct	data;

			DataBinding* spawnBinding = mDatabase->CreateDataBinding(6);
			spawnBinding->addField(DFT_uint32,offsetof(SpawnDataStruct,spawnId),4,0);
			spawnBinding->addField(DFT_float,offsetof(SpawnDataStruct,posX),4,1);
			spawnBinding->addField(DFT_float,offsetof(SpawnDataStruct,posZ),4,2);
			spawnBinding->addField(DFT_uint32,offsetof(SpawnDataStruct,width),4,3);
			spawnBinding->addField(DFT_uint32,offsetof(SpawnDataStruct,height),4,4);
			spawnBinding->addField(DFT_uint32,offsetof(SpawnDataStruct,density),4,5);

			uint64 count = result->getRowCount();

			for (uint64 i = 0;i < count;i++)
			{
				result->GetNextRow(spawnBinding,&data);

				mSpawnListMap.insert(std::make_pair(data.spawnId,data));
				loadSpawnGroup(data.spawnId);

			}

			mDatabase->DestroyDataBinding(spawnBinding);

		}
		break;

		default:
		{
		}
		break;
	}
	delete(asyncContainer );
}


//======================================================================================================================
//
//	Add a npc to the Active queue.
//

void SpawnManager::addActiveNpc(uint64 creature, uint64 when)
{
	uint64 expireTime = Anh_Utils::Clock::getSingleton()->getLocalTime();

	mNpcActiveHandlers.insert(std::make_pair(creature, expireTime + when));
}

//======================================================================================================================
//
//	Remove a npc from the Active queue.
//

void SpawnManager::removeActiveNpc(uint64 creature)
{
	NpcActiveHandlers::iterator it = mNpcActiveHandlers.find(creature);
	if (it != mNpcActiveHandlers.end())
	{
		// Remove creature.
		mNpcActiveHandlers.erase(it);
	}
}

//======================================================================================================================
//
// Handle the queue of Active npc's.
//
bool SpawnManager::_handleActiveNpcs(uint64 callTime, void* ref)
{
	NpcActiveHandlers::iterator it = mNpcActiveHandlers.begin();
	while (it != mNpcActiveHandlers.end())
	{
		//  The timer has expired?
		if (callTime >= ((*it).second))
		{
			// Yes, handle it.
			
			NPCObject* npc = dynamic_cast<NPCObject*>(gWorldManager->getObjectById((*it).first));
			if (npc)
			{
				// uint64 waitTime = NpcManager::Instance()->handleActiveNpc(creature, callTime - (*it).second);
				// gLogger->log(LogManager::DEBUG,"Active...");
				// gLogger->log(LogManager::DEBUG,"Active... ID = %"PRIu64"",  (*it).first);
				uint64 waitTime = NpcManager::Instance()->handleNpc(npc, callTime - (*it).second);
				if (waitTime)
				{
					// Set next execution time.
					(*it).second = callTime + waitTime;
				}
				else
				{
					// Requested to remove the handler.
					mNpcActiveHandlers.erase(it++);
					// gLogger->log(LogManager::DEBUG,"Removed active NPC handler...");
				}
			}
			else
			{
				// Remove the expired object...
				mNpcActiveHandlers.erase(it++);
				// gLogger->log(LogManager::DEBUG,"Removed active NPC handler...");
			}
		}
		else
		{
			++it;
		}
	}
	return true;
}

//======================================================================================================================
//
//	Force a npc from the Ready queue to be handled at next tick.
//

void SpawnManager::forceHandlingOfReadyNpc(uint64 creature)
{
	NpcReadyHandlers::iterator it = mNpcReadyHandlers.find(creature);
	if (it != mNpcReadyHandlers.end())
	{
		// Change the event time to NOW.
		uint64 now = Anh_Utils::Clock::getSingleton()->getLocalTime();
		(*it).second = now;
	}
}

//======================================================================================================================
//
//	Add a npc to the Ready queue.
//

void SpawnManager::addReadyNpc(uint64 creature, uint64 when)
{
	uint64 expireTime = Anh_Utils::Clock::getSingleton()->getLocalTime();

	mNpcReadyHandlers.insert(std::make_pair(creature, expireTime + when));
}

//======================================================================================================================
//
//	Remove a npc from the Ready queue.
//

void SpawnManager::removeReadyNpc(uint64 creature)
{
	NpcReadyHandlers::iterator it = mNpcReadyHandlers.find(creature);
	if (it != mNpcReadyHandlers.end())
	{
		// Remove creature.
		mNpcReadyHandlers.erase(it);
	}
}

//======================================================================================================================
//
//	Add a timed entry for deletion of inactive spawn regions
//
void SpawnManager::addSpawnRegionForTimedUnSpawn(uint64 RegionId, uint64 when)
{
	uint64 expireTime = Anh_Utils::Clock::getSingleton()->getLocalTime();

	 gLogger->log(LogManager::DEBUG,"WorldManager::addSpawnRegionForTimedUnSpawn Adding new region %I64u at %"PRIu64"", RegionId, expireTime + when);

	CreatureObjectDeletionMap::iterator it = mLairObjectDeletionMap.find(RegionId);
	if (it != mLairObjectDeletionMap.end())
	{
		return;
	}
	// gLogger->log(LogManager::DEBUG,"Adding new object with id %"PRIu64"", creatureId);
	mLairObjectDeletionMap.insert(std::make_pair(RegionId, expireTime + when));
}



//======================================================================================================================
//
//	Add a timed entry for deletion of inactive spawn regions
//
void SpawnManager::addSpawnRegionHandling(uint64 RegionId)
{
	uint64 expireTime = Anh_Utils::Clock::getSingleton()->getLocalTime();

	 gLogger->log(LogManager::DEBUG,"WorldManager::addSpawnRegionHandling Adding new region %I64u at %"PRIu64"", RegionId, expireTime + 5000);

	CreatureObjectDeletionMap::iterator it = mSpawnRegionHandlingMap.find(RegionId);
	if (it != mSpawnRegionHandlingMap.end())
	{
		return;
	}
	// gLogger->log(LogManager::DEBUG,"Adding new object with id %"PRIu64"", creatureId);
	mSpawnRegionHandlingMap.insert(std::make_pair(RegionId, expireTime + 5000));
}

//======================================================================================================================
//
//	stop an empty spawnregion from updating its entities
//
void SpawnManager::removeSpawnRegionHandling(uint64 RegionId)
{
	CreatureObjectDeletionMap::iterator it = mSpawnRegionHandlingMap.find(RegionId);
	if (it != mSpawnRegionHandlingMap.end())
	{
		gLogger->log(LogManager::DEBUG,"SpawnManager::removeSpawnRegionHandling :: region  %"PRIu64"",RegionId);
		mSpawnRegionHandlingMap.erase(it++);
		return;
	}
	else
		gLogger->log(LogManager::DEBUG,"SpawnManager::removeSpawnRegionHandling :: Region  %"PRIu64" Not Found!!!",RegionId);
}

//======================================================================================================================
//
//	remove a timed entry for deletion of  inactive spawn regions
//
void SpawnManager::removeSpawnRegionFromTimedUnSpawn(uint64 RegionId)
{
	CreatureObjectDeletionMap::iterator it = mLairObjectDeletionMap.find(RegionId);
	if (it != mLairObjectDeletionMap.end())
	{
		gLogger->log(LogManager::DEBUG,"SpawnManager::removeSpawnRegionFromTimedUnSpawn :: UnSpawned region  %"PRIu64"",RegionId);
		mLairObjectDeletionMap.erase(it++);
		return;
	}
	else
		gLogger->log(LogManager::DEBUG,"SpawnManager::removeSpawnRegionFromTimedUnSpawn :: Region  %"PRIu64" Not Found!!!",RegionId);
}

//======================================================================================================================
//
//	Add a timed entry for deletion of dead creature objects.
//
void SpawnManager::addCreatureObjectForTimedDeletion(uint64 creatureId, uint64 when)
{
	uint64 expireTime = Anh_Utils::Clock::getSingleton()->getLocalTime();

	// gLogger->log(LogManager::DEBUG,"WorldManager::addCreatureObjectForTimedDeletion Adding new at %"PRIu64"", expireTime + when);

	CreatureObjectDeletionMap::iterator it = mCreatureObjectDeletionMap.find(creatureId);
	if (it != mCreatureObjectDeletionMap.end())
	{
		// Only remove object if new expire time is earlier than old. (else people can use "lootall" to add 10 new seconds to a corpse forever).
		if (expireTime + when < (*it).second)
		{
			// gLogger->log(LogManager::DEBUG,"Removing object with id %"PRIu64"", creatureId);
			mCreatureObjectDeletionMap.erase(it);
		}
		else
		{
			return;
		}

	}
	// gLogger->log(LogManager::DEBUG,"Adding new object with id %"PRIu64"", creatureId);
	mCreatureObjectDeletionMap.insert(std::make_pair(creatureId, expireTime + when));
}

//======================================================================================================================
//
// remove us from the world if we are a timed out corpse 
// or if we are a nonactive spawnregion 
//
bool SpawnManager::_handleGeneralObjectTimers(uint64 callTime, void* ref)
{
	CreatureObjectDeletionMap::iterator it = mCreatureObjectDeletionMap.begin();
	while (it != mCreatureObjectDeletionMap.end())
	{
		//  The timer has expired?
		if (callTime >= ((*it).second))
		{
			// Is it a valid object?
			CreatureObject* creature = dynamic_cast<CreatureObject*>(gWorldManager->getObjectById((*it).first));
			if (creature)
			{
				// Yes, handle it. We may put up a copy of this npc...
				NpcManager::Instance()->handleExpiredCreature((*it).first);
				gWorldManager->destroyObject(creature);
				mCreatureObjectDeletionMap.erase(it++);
				gWorldManager->removeNpId(creature->getId());
				gWorldManager->removeNpId(creature->getId()+1);//inventory
			}
			else
			{
				// Remove the invalid object...from this list.
				mCreatureObjectDeletionMap.erase(it++);
				assert(false && "SpawnManager::_handleGeneralObjectTimers creature invalid" );
			}
		}
		else
		{
			++it;
		}
	}


	it = mLairObjectDeletionMap.begin();
	while (it != mLairObjectDeletionMap.end())
	{
		//  The timer has expired?
		if (callTime >= ((*it).second))
		{
			// Is it a valid object?
			SpawnRegion* region = dynamic_cast<SpawnRegion*>(gWorldManager->getObjectById((*it).first));
			if (region)
			{
				region->despawnArea();
				mLairObjectDeletionMap.erase(it++);
			}
			else
			{
				// Remove the invalid object...from this list.
				mLairObjectDeletionMap.erase(it++);
				assert(false && "SpawnManager::_handleGeneralObjectTimers spawnregion invalid" );
			}
		}
		else
		{
			++it;
		}
	}

	/*
	it = mSpawnRegionHandlingMap.begin();
	while (it != mSpawnRegionHandlingMap.end())
	{
		//  The timer has expired?
		if (callTime >= ((*it).second))
		{
			// Is it a valid object?
			SpawnRegion* region = dynamic_cast<SpawnRegion*>(gWorldManager->getObjectById((*it).first));
			if (region)
			{
				(*it).second = callTime + 5000;
				region->spawnArea();
				//mLairObjectDeletionMap.erase(it++);
			}
			else
			{
				// Remove the invalid object...from this list.
				mSpawnRegionHandlingMap.erase(it++);
				assert(false && "SpawnManager::_handleGeneralObjectTimers spawnregion invalid" );
			}
		}
		else
		{
			++it;
		}
	}
	*/
	return (true);
}

//======================================================================================================================
//
//	removes an entity from the world and destroys it
//  use to remove entities manually without them generating a respawn
//  use this if we are not dead
void SpawnManager::unSpawnEntity(uint64 creatureId)
{
	//get rid of any lists
	
	AttackableCreature* creature = dynamic_cast<AttackableCreature*>(gWorldManager->getObjectById(creatureId));
	if (creature)
	{
		if (LairObject* lair = dynamic_cast<LairObject*>(gWorldManager->getObjectById(creature->getLairId())))
		{
			//we might need to report to the lair as removed
			lair->removeSpawn(creatureId);
			gLogger->log(LogManager::DEBUG,"SpawnManager::unSpawnEntity :: DeSpawned creature %"PRIu64" parent %"PRIu64"",creatureId,lair->getId());
			
		}
		else
			gLogger->log(LogManager::DEBUG,"SpawnManager::unSpawnEntity :: DeSpawned creature # %"PRIu64"",creatureId);

		removeActiveNpc(creatureId);
		removeReadyNpc(creatureId);
		removeDormantNpc(creatureId);
		gWorldManager->destroyObject(creature);
		gWorldManager->removeNpId(creature->getId());
		gWorldManager->removeNpId(creature->getId()+1);//inventory
	}
	else
	if (LairObject* lair = dynamic_cast<LairObject*>(gWorldManager->getObjectById(creatureId)))
	{
		//remove our spawned creatures
		lair->unSpawn();

		gLogger->log(LogManager::DEBUG,"SpawnManager::unSpawnEntity :: DeSpawned lair %"PRIu64"",creatureId);

		removeActiveNpc(creatureId);
		removeReadyNpc(creatureId);
		removeDormantNpc(creatureId);
		gWorldManager->destroyObject(lair);
		gWorldManager->removeNpId(lair->getId());
		gWorldManager->removeNpId(lair->getId()+1);//inventory
	}
	else
	{
		Object* object = dynamic_cast<Object*>(gWorldManager->getObjectById(creatureId));

		gLogger->log(LogManager::DEBUG,"SpawnManager::unSpawnEntity :: entity %"PRIu64" not found",creatureId);
	}
}