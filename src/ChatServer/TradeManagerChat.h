/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2008 The swgANH Team

---------------------------------------------------------------------------------------
*/

#ifndef ANH_ZONESERVER_TradeManager_H
#define ANH_ZONESERVER_TradeManager_H

//#include "ObjectFactory.h"
//#include "ZoneTree.h"
//#include "SkillManager.h"

#include "DatabaseManager/DatabaseCallback.h"
#include "Common/MessageDispatchCallback.h"
#include <vector>
#include <queue>
#include "ChatOpcodes.h"
#include "ChatManager.h"
#include "Utils/typedefs.h"
#include "TradeManagerHelp.h"
#include "ChatMessageLib.h"
#include "Utils/Timer.h"
#include "Utils/TimerCallback.h"
#include "Utils/mutex.h"

#define	gTradeManager	TradeManager::getSingletonPtr()

//======================================================================================================================

typedef std::queue<uint32>						TimerEventQueue;

class Message;
class Database;
class MessageDispatch;

//class ChatManager;


//typedef std::map<uint32,Player*>		PlayerAccountMap;
typedef std::vector<Bazaar*>		BazaarList;
typedef std::vector<Timer*>			TimerList;
typedef std::vector<AuctionItem*>	AuctionList;




//======================================================================================================================

class TradeManagerChatHandler : public MessageDispatchCallback, public DatabaseCallback, public TimerCallback
{
	public:

		static TradeManagerChatHandler*	getSingletonPtr() { return mSingleton; }
		static TradeManagerChatHandler*	Init(Database* database,MessageDispatch* dispatch, ChatManager* chatManager);

		~TradeManagerChatHandler();

		TradeManagerChatHandler(Database* database,MessageDispatch* dispatch, ChatManager* chatManager);

		void				Shutdown();
		void				Process();
				// TimerCallback
		virtual void		handleTimer(uint32 id, void* container);

		virtual void		handleDispatchMessage(uint32 opcode,Message* message,DispatchClient* client);
		virtual void		handleDatabaseJobComplete(void* ref,DatabaseResult* result);

		CommoditiesClass*	Commodities;

		uint32				TerminalRegionbyID(uint64 id);
		uint32				getBazaarRegion(uint64 ID);
		string				getBazaarString(uint64 ID);
		Bazaar*				getBazaarInfo(uint64 ID);
		

	private:

		void				processRetrieveAuctionItemMessage(Message* message,DispatchClient* client);
		void				processCreateItemMessage(Message* message,DispatchClient* client);
		void				processHandleIsVendorMessage(Message* message,DispatchClient* client);
		void				processHandleopAuctionQueryHeadersMessage(Message* message,DispatchClient* client);
		void				processGetAuctionDetails(Message* message,DispatchClient* client);
		void				processCancelLiveAuctionMessage(Message* message,DispatchClient* client);
		void				processBidAuctionMessage(Message* message,DispatchClient* client);
		void				ProcessCreateAuction(Message* message,DispatchClient* client);
		void				processAuctionBid(TradeManagerAsyncContainer* asynContainer, Player* player);
		void				ProcessRequestTypeList(Message* message,DispatchClient* client);

		void				ProcessBankTip(Message* message,DispatchClient* client);
		void				processAuctionEMails(AuctionItem* AuctionTemp);
			

		void				LockTimerEventQueue(){ mTimerMutex.Lock(); }
		void				UnlockTimerEventQueue(){ mTimerMutex.Unlock(); }
		
		
		// process chat timers
		void				handleGlobalTickPreserve();
		void				processTimerEvents();
		void				handleGlobalTickUpdate();
		void				handleCheckAuctions();
		uint64				getGlobalTickCount(){return mGlobalTickCount;}
		


		static TradeManagerChatHandler*	mSingleton;
		static bool					mInsFlag;

		BazaarList					mBazaars;
		AttributesList				mAtrributesList;
									
		AuctionClass*				auction;
	
		ListStringStruct*			ListStringHandler;
		ListStringList				mListStringList;

		Database*					mDatabase;
		MessageDispatch*			mMessageDispatch;
		TRMPermissionType			mPermissionTyp;
		bool						mBazaarsLoaded;
		uint32						mBazaarCount;
		PlayerAccountMap			mPlayerAccountMap;

		ChatManager*				mChatManager;
		
		uint64      				mGlobalTickCount;
		TimerList					mTimers;
		TimerEventQueue				mTimerEventQueue;
		Mutex						mTimerMutex;
		uint64						mTimerQueueProcessTimeLimit;
		uint32						mBazaarMaxBid;

		AuctionList					mAuction;


		
};

#endif 
