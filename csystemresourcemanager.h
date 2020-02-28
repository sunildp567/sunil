// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

//Padua for SRM component
#ifndef CSYSTEMRESOURCEMANAGER_H_
#define CSYSTEMRESOURCEMANAGER_H_

#include <sys/inotify.h>
#include <map>
#include <list>
#include <errno.h>
#include <status.h>
#include <types.h>
#include <time.h>
#include <CI/SoftwareDiagnostics/softwarediagnostics.h>
#include <CI/ServiceStartupManager/client.h>
#include <CI/ServiceStartupManager/ssmcontracts.h>
#include <CI/MessagingSystem/msg.h>
#include <CI/MessagingSystem/msg.h>
#include <CI/MessagingSystem/msgport.h>
#include <msgdefs.h>
#include <CI/status.h>
#include <CI/HierarchicalDB/hierarchicaldb.h>
#include <CI/HierarchicalDB/DOM/document.h>
#include <CI/HierarchicalDB/DOM/node.h>
#include <CI/OperatingEnvironment/thread.h>
#include <CI/OperatingEnvironment/ref.h>
#include <storage.h>
#include <CI/ci_service_name.h>
#include <CI/cicontracts.h>
#include <contracts.h>
#include <CI/SystemResourceManager/systemresourcemanager.h>
#include "cUserInterface.h"
#include <CI/IndexedDB/indexeddb.h>
#include <CI/OperatingEnvironment/cuuid.h>
#include <vector>
#include <CI/PresentationResources/resourcemanager.h>
#include <CI/PresentationResources/resourcepool.h>
#include <CI/PresentationResources/message.h>

#define ERROR_F200 "F200"
#define ERROR_F121 "F121"
#define ERROR_F122 "F122"
#define ERROR_F123 "F123"
#define ERROR_F124 "F124"
#define ERROR_F510 "F510"
#define ERROR_F521 "F521"
#define ERROR_7153 "7153"
#define ERROR_F130 "F130"
#define ERROR_F800 "F800"
#define ERROR_F901 "F901"
#define ERROR_F131 "F131"

#define ERROR_CODE_MESSAGE_LOG_ID 102701

#define AL_PANEL_SERVICE   "panel"
#define AL_RENDERER_SERVICE "renderer"
#define AL_NSM "nsm"
#define ERRORLISTFILE "/registration/dl/log/ErrHist.bin"
namespace ci
{
namespace systemresourcemanager
{
//Used as shown in inotify man page as reference
#define SRM_SIZE_CHANGE_EVENT 		IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_MODIFY|IN_DELETE_SELF
#define SRM_DIRECTORY_DELETE_EVENT	IN_DELETE|IN_DELETE_SELF
#define	SRM_FILE_CREATE_EVENT		IN_CREATE
#define SRM_FILE_DELETE_EVENT		SRM_DIRECTORY_DELETE_EVENT		
#define	SRM_FILE_WRITE_EVENT		IN_CLOSE_WRITE

#define SRM_STATUS_SELF_DELETE_EVENT		0xfff1
#define SRM_STATUS_OTHER_EVENT				0xfff2
#define HDD_SIZE_160_GB         160
#define STORAGE_PARTITION_SIZE_GB 84
#define HDD_80_GB "80"
#define HDD_8_GB "7918"
#define HDD_160_GB "160"
   struct MCN_TBL_ERRHIST
   {
      int32  iTotalCount;  // Total Count 
      uint16 hErrCod;      // Error Code
      int16  hJobid;       // Job ID 
      int16  hMapX;        // Magnification X
      int16  hMapY;        // Magnification Y
      int16  hPapSize;     // Code of Paper Size
      int8   aDate[12];    // Date and Time
      int8   aCst;         // Cassette
      int8   aSortMode;    // SortMode
      int8   aDFSMode;     // DF Mode
      int8   aAPSAMS;      // APS/AMS Mode
      int8   aDplxMode;    // Duplex Mode
      int8   aCvrSheet;    // Cover Sheet
      int8   aImgShift;    // Image Shift
      int8   aEdit;        // Edit
      int8   aEdgeErase;   // Edge Erase
      int8   aSheetIns;    // Sheet Insert 
      int8   aColor;       // Color Mode 
      int8   aPapKind;     
      int8   aDFMix;
      uint32 WORKFLOW_ID;  
      int8   aDummy1;      // Dummy 
      int8   aDummy2;      // Dummy      
      int8   aDummy3;      // Dummy 
   };
   struct ErrorNode
   {
      MCN_TBL_ERRHIST val;
      ErrorNode * next;
   };


typedef struct stEventData {
        uint32    uEventFlags; //bitmap of events which are subscribed by the subscriber at port 'SubscriberPort'
        int32     iSubscriberPort;//port of the subscriber of events
        ci::operatingenvironment::CString sStorageName;
        bool	 bIsDirectoryFlag;
        stEventData() {}
        stEventData(uint32 _uEventFlags,int32 _iSubscriberPort, ci::operatingenvironment::CString _sStorageName, bool _bIsDirectoryFlag)
        {
        	uEventFlags = _uEventFlags;
        	iSubscriberPort = _iSubscriberPort;
        	sStorageName = _sStorageName;
        	bIsDirectoryFlag = _bIsDirectoryFlag;
        }
}tmpstEventData;

typedef std::list<stEventData>  ListOfEventEntries;


DECL_OBJ_REF(CSystemResourceManager);
DECL_OBJ_REF(MonitorThread);
DECL_OBJ_REF(MonitorSoftPowerSaveThread);

class MonitorThread : public ci::operatingenvironment::LocalThread {
	public:
		void (*MonitorCallbackFunction)();
		int MonitorSleepTime;
		void *Run(void *execFunc);
};

class MonitorSoftPowerSaveThread : public ci::operatingenvironment::LocalThread {
        public:
                void (*MonitorCallbackFunction)();
                int MonitorSleepTime;
		void *Run(void *execFuncPowerSave);
};

class CSystemResourceManager : public ci::operatingenvironment::LocalThread {
	public:
		CSystemResourceManager();
		virtual	~CSystemResourceManager();
		Status 	CreateSRMDomFromDefaultSystemResourcesMgrXML();
		void *Run(void *executeFunc);
		Status	Listen();
	private:
		ci::hierarchicaldb::HierarchicalDBRef m_pHDB;
		dom::DocumentRef m_SystemResourceManagerDOM;
		// Start Added variables for Power monitoring
		struct inotify_event *m_inotifyEventdata;
		struct timespec m_timerPower;
		struct timespec m_timerPrint;
		struct timespec m_timerScan;
		int m_iPowerType;
		int m_iPreviousPowerType;
		ci::operatingenvironment::Ref<ci::indexeddb::IndexedDB> m_pIdbPowerMonitor;
		// End Power monitoring
		StorageRef	m_storageRef;
		int			m_InComingSubscriptionRequestsFd[2];
		/* Select call file descriptor set type*/
		fd_set		m_inotifyDescSet;
		int			m_maxInotifyFds;
		int 		m_inotifyWatchfd;
		ci::operatingenvironment::CString m_HDD_size;
		ci::operatingenvironment::Ref<ci::servicestartupmanager::Client>	m_ssmClient;
		//For storing the subscription request and events subscribed
		std::map<ci::operatingenvironment::CString,ListOfEventEntries>	m_storageSubscriberEventMap;
		//For storing the inotify watch descriptor related to each subscription
		std::map<ci::operatingenvironment::CString, int>	m_StorageToWatchDescMap;	
		std::map<int, ci::operatingenvironment::CString>	m_WatchDescToStorageMap;	
      std::map<ci::operatingenvironment::CNoCaseString,ci::operatingenvironment::CString> m_MessageLogMap;
      std::map<ci::operatingenvironment::CNoCaseString,ci::operatingenvironment::CString> m_NotificationMap;
      std::map<ci::operatingenvironment::CNoCaseString,ci::operatingenvironment::CString> m_ErrorHistListErrorMap;
      std::map<ci::operatingenvironment::CNoCaseString,ci::operatingenvironment::CString> m_OnlyServiceCallMap;
      //Map to store the modified partition names
      std::map<ci::operatingenvironment::CString, ci::operatingenvironment::CString> m_PartitionNameMap;
		//Map to store the partition size to create DefaultSystemresource.xml dynamically
      std::map<ci::operatingenvironment::CString,ci::operatingenvironment::CString> m_HDD_partition;
      std::map<int,int> m_CheckServiceCallMap;

		/* Msg Q port for rx/tx requests/responses */
		ci::messagingsystem::MsgPortRef		 m_InterfacePortRef;
		
		Status 	OpenPort();
		int 	SetNonblocking(int Selectfd);
		void 	DisplayEvent(uint32_t mask);
		Status 	ScanNAddEventsToWatchList(ci::operatingenvironment::CString sName,ci::operatingenvironment::Ref<dom::Element> pNode, bool bHighWaterMarkLevelReached);
		int    	OnWriteGetNewSize(ci::operatingenvironment::CString FileOrDirectoryName, uint32_t mask);
		Status 	ProcessSSMMessages(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
		Status 	ProcessSRMMessages(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
		Status 	HandleSubscriptionRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
		Status	HandleUnSubscribeRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
		Status 	HandleAllocateQuotaRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
		Status 	HandleDeallocateQuotaRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
		int   	SendStorageEvents(stEventData eventData);
		Status 	HandleStorageEvents(struct inotify_event * iEvent);
		Status 	SendResponse(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg,ci::operatingenvironment::CString sResponse, ci::operatingenvironment::CString sMsgName);
		Status 	SendOffNormalResponse(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg,ci::operatingenvironment::CString sMsgName);
		void   	SetOrResetEvent(uint32& uWatchFlags, int32 input, uint32 mask);
		Status 	InitializeMonitor();
		Status 	AddToWatchList(ci::operatingenvironment::CString sFileDirStoragename, int32 iSubscriberPort, uint32 uWatchFlags);
		Status 	RemoveFromWatchList(ci::operatingenvironment::CString sFileDirStoragename);
		int 	SendStorageEvents(ci::operatingenvironment::CString sFileDirStorageName, stEventData eventData, ci::operatingenvironment::CString sRxdEventFileRDirName,uint32 uRxdEventMask);
		void 	UpdateEventsIntoDOM(dom::NodeListRef pListOfNodes,uint32 uCurrentEventFlags, uint32 uNewEventFlags, bool bWatermarkLevel);
		Status 	HandleSetWaterMarkLevelRequests(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
		Status 	HandleGetSizeInformationRequests(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
		uint64  GetWaterMarkLevel(dom::ElementRef pWaterMarkLevelElement, uint64 uTotalSpace);
		Status  UpdateDirNFileNodes(dom::ElementRef pDirFileElement, ci::operatingenvironment::CString sDirFileAttName);
		void 	AddNewNode(dom::ElementRef pStorageElement,ci::operatingenvironment::CString sNameOfNewNode, ci::operatingenvironment::CString sValueOfNewNode);
		int  	GetConvertedTriBooleanValue(ci::operatingenvironment::CString sValueOfNewNode);
		void 	VerifyWaterMarkLevelNSendEvent(dom::NodeRef pStorageNode, ci::operatingenvironment::CString sWaterMarkLevel);
		Status 	SendWaterMarkLevelReachedEvent(ci::operatingenvironment::CString sFileDirStorageName);
		Status 	HandleNotifyErrorRequest(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
		Status 	CheckErrorAndSendServiceMessage(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
        Status GetErrorCode(ci::operatingenvironment::CNoCaseString& errorCode, ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
      Status restartSystem();
      Status sendServiceMessage(ci::operatingenvironment::CString errorMessage,ci::operatingenvironment::CString);
      Status HandleResetErrorCountRequest(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg);
      Status InitializeErrorMessageMap();
      Status InitializePartitionNameMap();
      Status InitializePartitionSizeMap(ci::operatingenvironment::CString HDD_size);
      Status ModifyForPartitionNameChange(ci::operatingenvironment::CString & path);
      Status ErrorNotificationWriteToLog(ci::operatingenvironment::CString ErrorNumber);
      Status AddToErrorList(ci::operatingenvironment::CString errrCode);
      Status getNewErrorEntry(ci::operatingenvironment::CString errCode, ErrorNode & entry);
      Status sendNotificationMessage(ci::operatingenvironment::CString errCode);
      Status getNotificationEmailID(std::vector<ci::operatingenvironment::CString > & mailID);
      Status createNotificationMessage(std::vector<ci::operatingenvironment::CString > mailID, ci::operatingenvironment::CString errCode, ci::operatingenvironment::CString & result);
      Status sendReportManagerMessage(ci::operatingenvironment::CString result);
      Status convertTime(char * aTime, char * aToAscii);
	Status StartSleepTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartDeepSleepTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartEnergySaveTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartReadyTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartWarmingUpTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartPreviousTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status UpdateSRAMForPowerTimer(int iSRAMSubCode);
	Status StopTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartPrintingTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StopPrintingTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StartScanningTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
	Status StopScanningTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg);
};
}//SystemResourceManager
}//CI

#endif /*CSYSTEMRESOURCEMANAGER_H_*/




