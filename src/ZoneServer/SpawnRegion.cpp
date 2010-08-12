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

#include "SpawnRegion.h"
#include "PlayerObject.h"
#include "QTRegion.h"
#include "QuadTree.h"
#include "WorldManager.h"
#include "ZoneTree.h"
#include "utils/rand.h"
#include "LairObject.h"
#include "NpcManager.h"
//=============================================================================

SpawnRegion::SpawnRegion()
: RegionObject()
, mQTRegion(NULL)
, mSI(gWorldManager->getSI())
, mMission(0)
{
	mActive				= true;
	mRegionType			= Region_Spawn;
	mInactivityTimer	= 0;
	mSpawnStatus		= ESpawnStatus_Unspawned;
	mDensity			= 50;
}

//=============================================================================

SpawnRegion::~SpawnRegion()
{
}

//=============================================================================

void SpawnRegion::update()
{
	//this is very problematic - currently we assume an object entered us when it is in the same qtregion we are in

	//run about every 4.5 seconds
	if(!mSubZoneId)
	{
		mQTRegion	= mSI->getQTRegion(mPosition.x,mPosition.z);
		mSubZoneId	= (uint32)mQTRegion->getId();
		mQueryRect	= Anh_Math::Rectangle(mPosition.x - mWidth,mPosition.z - mHeight,mWidth * 2,mHeight * 2);
	}

	Object*		object;
	ObjectSet	objList;

	if(mParentId)
	{
		mSI->getObjectsInRange(this,&objList,ObjType_Player,mWidth);
	}

	if(mQTRegion)
	{
		mQTRegion->mTree->getObjectsInRange(this,&objList,ObjType_Player,&mQueryRect);
	}

	ObjectSet::iterator objIt = objList.begin();

	while(objIt != objList.end())
	{
		object = dynamic_cast<Object*>(*objIt);

		if(!(checkKnownObjects(object)))
		{
			onObjectEnter(object);
		}

		++objIt;
	}

	PlayerObjectSet oldKnownObjects = mKnownPlayers;
	PlayerObjectSet::iterator objSetIt = oldKnownObjects.begin();

	while(objSetIt != oldKnownObjects.end())
	{
		object = dynamic_cast<Object*>(*objSetIt);

		if(objList.find(object) == objList.end())
		{
			onObjectLeave(object);
		}

		++objSetIt;
	}
}


void SpawnRegion::setSpawnData(SpawnDataStruct*	spawnData)
{
	mSpawnData = spawnData;
	if(!mSpawnData)
	{
		return;
	}
	mDensity = spawnData->density;
	if(mDensity < 50)
		mDensity = 50;
}


//=============================================================================
void SpawnRegion::populateArea()
{
	SpawnLairList::iterator it = mSpawnData->lairTypeList.begin();
	

	uint32 sDensity = mDensity*mDensity;

	uint32 density = (uint32)(this->mWidth * this->mHeight)/(sDensity);

	uint32 lairvariety = mSpawnData->lairTypeList.size();
	if(!lairvariety)
	{
		return;
	}

	density /= lairvariety;
	while(it != mSpawnData->lairTypeList.end())
	{
		for(uint32 i = 0; i < density; i++)
		{
			glm::vec3 spawnPoint = getSpawnLocation();
			if((spawnPoint.x != 0) && (spawnPoint.z != 0))
			{
				gNpcManager->spawnLairs((*it).lairId,this->getId(),spawnPoint);
			}
		}
		it++;
	}
	
}

//====================================================================================0
//
//when we get empty we need to despawn
//

void SpawnRegion::despawnArea()
{
	gLogger->log(LogManager::DEBUG,"SpawnRegion::despawnArea() despawn area  %"PRIu64"",this->getId());
	LairObjectList::iterator it = mLairObjectList.begin();
	while(it != mLairObjectList.end())
	{
		gLogger->log(LogManager::DEBUG,"SpawnRegion::despawnArea() despawn lair  %"PRIu64"",(*it)->getId());
		gSpawnManager->unSpawnEntity((*it)->getId());

		it++;
	}
	mSpawnStatus = ESpawnStatus_Unspawned;
}

//====================================================================================0
//
// create the dormant lairs
//
void SpawnRegion::spawnArea()
{
	//handleEvents
	gLogger->log(LogManager::DEBUG,"SpawnRegion::spawnArea() spawn region   %"PRIu64"",this->getId());
	
	LairObjectList::iterator it = mLairObjectList.begin();
	while(it != mLairObjectList.end())
	{
		gLogger->log(LogManager::DEBUG,"SpawnRegion::despawnArea() despawn lair  %"PRIu64"",(*it)->getId());
		
		
		if((*it)->getAiState() == NpcIsDormant)
		{
			if((*it)->getFirstSpawn())
			{
				NpcManager::Instance()->handleNpc((*it),1);
				//(*it)->spawn();
			}
			//gSpawnManager->forceHandlingOfDormantNpc((*it)->getId());

		}

		it++;
	}

}


bool SpawnRegion::checkSpawnLocation(glm::vec3 location)
{
	LairObjectList::iterator it = mLairObjectList.begin();
	while(it != mLairObjectList.end())
	{
		float distanceFromLair = glm::distance((*it)->mPosition, location);
		
		if(distanceFromLair < (float) 10)
			return false;

		it++;
	}

	return true;

}

//====================================================================================0
//
// make sure we havnt got all the lairs in one lump
//
bool SpawnRegion::checkSpawnDensity(glm::vec3 location)
{
	uint32 count = 0;
	uint32 amount = (uint32) sqrt((float)mDensity)/2;
	LairObjectList::iterator it = mLairObjectList.begin();
	while(it != mLairObjectList.end())
	{
		float distanceFromLair = glm::distance((*it)->mPosition, location);
		
		if(distanceFromLair < (float) mDensity)
			count++;

		if(count > amount)
			return false;

		it++;
	}

	return true;

}

glm::vec3 SpawnRegion::getSpawnLocation()
{
	glm::vec3 spawnPoint;
	spawnPoint.x = 0;
	spawnPoint.z = 0;
			
	bool found = false;
	uint32 iteration = 0;
	while(!found)
	{
		iteration++;
		spawnPoint.x = this->mPosition.x + (gRandom->getRand()% ((uint32)this->getWidth()));
		spawnPoint.z = this->mPosition.z + (gRandom->getRand()% ((uint32)this->getHeight()));

		//now check already present lairs
		if(checkSpawnLocation(spawnPoint) && checkSpawnDensity(spawnPoint))
		{
			return(spawnPoint);
			found = true;
		}
		
		if(iteration>500)
		{
			spawnPoint.x = 0;
			spawnPoint.z = 0;
			return(spawnPoint);

		}
	}

	return(spawnPoint);
	
}

void SpawnRegion::onObjectEnter(Object* object)
{
	
	//we got company - if were not spawned start spawning now
	if(mSpawnStatus == ESpawnStatus_AwaitDespawn)//when we get empty we have a timer until we despawn
	{
		//
		gSpawnManager->removeSpawnRegionFromTimedUnSpawn(this->getId());
		mSpawnStatus = ESpawnStatus_Spawned;
	}
	else
	if(mSpawnStatus == ESpawnStatus_Unspawned)//when we get empty we have a timer until we despawn
	{
		//
		mSpawnStatus = ESpawnStatus_Spawned;
		populateArea();
		//gSpawnManager->addSpawnRegionHandling(this->getId());
		
	}

	if(object->getParentId() == mParentId)
	{
		//PlayerObject* player = (PlayerObject*)object;
		addKnownObjectSafe(object);
	}

}

//=============================================================================

void SpawnRegion::onObjectLeave(Object* object)
{
	//PlayerObject* player = (PlayerObject*)object;
	removeKnownObject(object);

	if((mSpawnStatus == ESpawnStatus_Spawned)&&(mKnownPlayers.size() == 0))//when we get empty we need a timer until we despawn
	{
		gSpawnManager->addSpawnRegionForTimedUnSpawn(this->getId(),10000);
		gSpawnManager->removeSpawnRegionHandling(this->getId());
		mSpawnStatus = ESpawnStatus_AwaitDespawn;
	}
}

//=============================================================================

