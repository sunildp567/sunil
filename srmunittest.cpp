// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

//JBINU:Very simple Unit test App for SRM component
// This application send different messages to cisystemresourcemanager according to user input. 
// The ststus of these messages can be found from the cisrm.lof file 
#include <iostream.h>
#include <status.h>
#include <CI/MessagingSystem/msg.h>
#include <CI/MessagingSystem/msgport.h>
#include <CI/SystemResourceManager/systemresourcemanager.h>
#include <CI/SystemResourceManager/test/rxthread.h>

using namespace std;
using namespace ci::operatingenvironment;
using namespace ci::messagingsystem;
using namespace ci::softwarediagnostics;

MsgPortRef	g_InterfacePortRef;

CString g_SubScribeRequest = "<Subscribe xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
									<Storage Name=\"/work/drivers/client/UnixFilters\">\
								        <Events>\
								            <HighWaterMarkLevelReached>75</HighWaterMarkLevelReached>\
								            <DirectoryDeletedSelf>true</DirectoryDeletedSelf>\
								            <FileDeletedSelf>true</FileDeletedSelf>\
								            <FileCreated>true</FileCreated>\
								            <FileDeleted>true</FileDeleted>\
								            <WriteOperation>true</WriteOperation>\
								        </Events>\
								    </Storage>\
									<Storage Name=\"/backup/fax/RX/testfile1\">\
								        <Events>\
								            <HighWaterMarkLevelReached>85</HighWaterMarkLevelReached>\
								            <FileDeletedSelf>true</FileDeletedSelf>\
								            <WriteOperation>true</WriteOperation>\
								        </Events>\
								    </Storage>\
								</Subscribe>";

CString g_UnSubScribeRequest = "<UnSubscribe xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
									<Storage Name=\"/work/drivers/client/UnixFilters\">\
								    </Storage>\
									<Storage Name=\"/backup/fax/RX/testfile1\">\
									</Storage>\
								</UnSubscribe>";

CString g_AllocateQuotaRequest = "<AllocateQuota xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
									<Storage Name=\"/work/drivers/client/UnixFilters\">\
									<MaxSize>0</MaxSize>\
									<HighWaterMarkLevel>99</HighWaterMarkLevel> \
									</Storage>\
								</AllocateQuota>";

CString g_DeAllocateQuotaRequest = "<DeallocateQuota xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
									<Storage Name=\"/imagedata/newdir1\">\
									</Storage>\
								    </DeallocateQuota>";


CString g_GetSizeInfoRequest = "<GetSizeInformation xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
									<Storage Name=\"/work/drivers/client/UnixFilters\"></Storage>\
								</GetSizeInformation>";



CString g_SetWaterMarkLevelRequest = "<SetWaterMarkLevel xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
									<Storage Name=\"/work/drivers/client/UnixFilters\"> \
									<HighWaterMarkLevel>70</HighWaterMarkLevel> \
									</Storage>\
									<Storage Name=\"/work/drivers/client/UnixFilters/testfile1\"> \
									<HighWaterMarkLevel>65</HighWaterMarkLevel> \
									</Storage>\
									</SetWaterMarkLevel>";


/* TODO: 12 May 2008,Not a priority for now. 
	and also further study of K2 kernel and features supported by it need to be done for CPu and Network reservation.
	CString g_AddNetworkQosRequest
	CString g_AddNetworkQosRequest
	CString g_AddNetworkQosRequest
*/

void Usage(char * app)
{
	printf(" ./%s  <[-f/-d]> path to file or directory to be watched> \n",app);
	exit(0);
}

void DisplayLogExitSRM(CString sLogMsg)
{
	DEBUGL1("SRM:main: %s !\n",sLogMsg.c_str());
	DEBUGL1("SRM:main:exiting....\n");
	return;
}

void SendRequestToSRM(MsgPortId pSRMPort, MsgId iMsgId, CString sMessageBuff, CString sMsgName )
{

    Msg msg(MSG_TYPE_REQUEST, sMessageBuff.c_str(), sMessageBuff.size());
    msg.SetId(iMsgId);
    msg.SetTarget(pSRMPort);
    if( STATUS_OK != g_InterfacePortRef->Send(msg))
    {
    	DEBUGL1("SendRequestToSRM: Failed to send [%s] msg to SRM at port %d \n",sMsgName.c_str(), pSRMPort);
    	return;
    }
	DEBUGL7("SendRequestToSRM: Successfully sent \n[%s]\n msg \[%s]\n to SRM at port %d \n",sMsgName.c_str(), sMessageBuff.c_str(),pSRMPort);
    return;
}


void ReplaceDummyWithValue(CString & obj_SubScribeRequest , CString dumy, CString sValue)
{
		size_t start = obj_SubScribeRequest.find ( dumy);
		obj_SubScribeRequest.replace ( start, dumy.length(),sValue );
}

// Thil will return false if not interested in calling back 
bool  SendCustomRequestToSRM(MsgPortId iPort)
{

CString dumy = "DUMMY_PATH";
CString dumy2 = "DUMMY_HIGHWATERMARK";
CString dumy_maxsize = "DUMMY_MAX";




CString obj_SubScribeRequest = 
"<Subscribe xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
<Storage Name=\"DUMMY_PATH\">\
    <Events>\
        <HighWaterMarkLevelReached>DUMMY_HIGHWATERMARK</HighWaterMarkLevelReached>\
        <DirectoryDeletedSelf>true</DirectoryDeletedSelf>\
        <FileDeletedSelf>true</FileDeletedSelf>\
        <FileCreated>true</FileCreated>\
        <FileDeleted>true</FileDeleted>\
        <WriteOperation>true</WriteOperation>\
    </Events>\
</Storage>\
</Subscribe>";

CString obj_UnSubScribeRequest = 
"<UnSubscribe xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
	<Storage Name=\"DUMMY_PATH\">\
    </Storage>\
</UnSubscribe>";

CString obj_AllocateQuotaRequest =
"<AllocateQuota xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
	<Storage Name=\"DUMMY_PATH\">\
	<MaxSize>DUMMY_MAX</MaxSize>\
	<HighWaterMarkLevel>DUMMY_HIGHWATERMARK</HighWaterMarkLevel> \
	</Storage>\
</AllocateQuota>";

CString obj_DeAllocateQuotaRequest = 
"<DeallocateQuota xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
	<Storage Name=\"DUMMY_PATH\">\
	</Storage>\
</DeallocateQuota>";


CString obj_GetSizeInfoRequest = 
"<GetSizeInformation xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
	<Storage Name=\"DUMMY_PATH\"></Storage>\
</GetSizeInformation>";

CString obj_SetWaterMarkLevelRequest =
"<SetWaterMarkLevel xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
	<Storage Name=\"DUMMY_PATH\"> \
	<HighWaterMarkLevel>DUMMY_HIGHWATERMARK</HighWaterMarkLevel> \
	</Storage>\
</SetWaterMarkLevel>";


	//Q:why is this not implemented in a while loop ? A: I dont want to initialize every thing agaig and agin, let compiler do it.
	bool bContinueCustom = true; // return value saying call me back
	

	char *sTokens[5]; /// 0-command 1-direcotory 2-maxsize 3-watermark 4- NULL
	char str [200];
  	sTokens[0]=sTokens[1]=sTokens[2]=sTokens[3]=sTokens[4]=NULL;
	cout << "CUSTOM MESSAGE MENU [Error checks are not done for invalid inputs !!!!!]\n"
	    		   "s <directory name > <Heigh WaterMark>= 'Subcribe' request to SRM \n"
	       		   "u <directory name >= Send 'UnSubcribe' request to SRM \n"
	               "l <directory name > <Heigh Watermark>= Send 'SetWaterMarkLevel' request to SRM \n"
	    		   "g <directory name >= Send 'GetSizeInformation' request to SRM \n"
	               "a <directory name > <MaxSize> <Heigh Watermark>= Send 'AllocateQuota' request to SRM \n"
	               "d <directory name >= Send 'DeallocateQuota' request to SRM \n"
	               "q =Back to Main Menu \n"
	               "=: " <<endl;
	//cin.getline (str, 198);
	gets(str);
	char * pch =NULL;
	int i=0;
	CString sInput[4];

	pch = strtok (str," ");
	if (!pch)
	{
	cout << "[#$@$# valid inputs please ]\n";
	return true; // no valid input from used , let hime come again 
	}
	sTokens[i++]= pch ;
	while (pch != NULL)
	{
	 pch = strtok (NULL, " ");
	 sTokens[i++]= pch ;
	}
i=0; 
while(sTokens[i]) // the last  also should be removed , output undefined for invalid input 
{
	sInput [i]= sTokens[i];
	i++;
}
/*
	CString sDirectory;
	CString sWaterMark ;

	if(sTokens[1]) // assign only if its there , ekse it will carsh 
		sDirectory =sTokens[1];
	if (sTokens[2]) // assign only if not NULL 
		sWaterMark =sTokens[2];
*/

	//cin >> sWaterMark;

	switch (*sTokens[0])
		  {
		   case 's':
		   	{	
				ReplaceDummyWithValue(obj_SubScribeRequest,dumy,sInput [1]);
				ReplaceDummyWithValue(obj_SubScribeRequest,dumy2,sInput [2]);
				//ReplaceDummyWithValue(obj_SubScribeRequest,dumy,sDirectory);
				SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::Subscribe, obj_SubScribeRequest, "Subscribe");
			}
		   break;
		   case 'u':
		   	{	
				ReplaceDummyWithValue(obj_UnSubScribeRequest,dumy,sInput [1]);
				//ReplaceDummyWithValue(obj_UnSubScribeRequest,dumy,sDirectory);
				SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::UnSubscribe,obj_UnSubScribeRequest,"UnSubscribe");
		   	}
		   break;
		   case 'g':
		   	{	ReplaceDummyWithValue(obj_GetSizeInfoRequest,dumy,sInput [1]);
				//ReplaceDummyWithValue(obj_GetSizeInfoRequest,dumy,sDirectory);
			   	SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::GetSizeInformation,obj_GetSizeInfoRequest,"GetSizeInformation");
			}
		   break;
		   case 'l':
		   	 {	
				ReplaceDummyWithValue(obj_SetWaterMarkLevelRequest,dumy,sInput [1]);
				ReplaceDummyWithValue(obj_SetWaterMarkLevelRequest,dumy2,sInput [2]);
				//ReplaceDummyWithValue(obj_SetWaterMarkLevelRequest,dumy,sDirectory);
				//ReplaceDummyWithValue(obj_SetWaterMarkLevelRequest,dumy2,sWaterMark);
				SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::SetWaterMarkLevel, obj_SetWaterMarkLevelRequest, "SetWaterMarkLevel");
			}
		   break;
		   case 'a': 
		   	 {	ReplaceDummyWithValue(obj_AllocateQuotaRequest,dumy,sInput [1]);
			 	ReplaceDummyWithValue(obj_AllocateQuotaRequest,dumy_maxsize,sInput [2]);
				ReplaceDummyWithValue(obj_AllocateQuotaRequest,dumy2,sInput [3]);
			 
				//ReplaceDummyWithValue(obj_AllocateQuotaRequest,dumy,sDirectory);
				SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::AllocateQuota,obj_AllocateQuotaRequest,"AllocateQuota");
			}
		   break;
		   case 'd':
		   	 {	
				ReplaceDummyWithValue(obj_DeAllocateQuotaRequest,dumy,sInput [1]);
				//ReplaceDummyWithValue(obj_DeAllocateQuotaRequest,dumy,sDirectory);
				SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::DeallocateQuota,obj_DeAllocateQuotaRequest,"DeallocateQuota");
			}
		   break;
		   case 'q':
		   	{
		   		bContinueCustom =false; // exir custom ;
		   	}
		   	break;
		   
		   }
 return bContinueCustom;
}


int main (int argc, char **argv)
{
	CString argstr;
	int arg;
	
	int iPort=0x0;

    /* handle standard EBX commandline options
     * -t -T, -S, -H
     */ 
    ProgramOptions::BeginOpts(argc, argv, "suwagdp:",
    		 "(-p <port>)",
               "    -p = SRM's port\n");
    
    while ((arg = ProgramOptions::GetOpt(argstr)) > 0)
           switch (arg) {
           		case 'p': iPort = string_cast<int32>(argstr); break;
           }
 
    if (!iPort) //(bSubscribe||bUnSubscribe||bSetWaterMarkLevel||bAllocate||bDeallocate))
          ProgramOptions::ReportOptErr("Must specify -p port no of SRM and select any one of the remaining options");
    ProgramOptions::EndOpts();
    
    // Create the SRM interface port 
	Status ret = ci::messagingsystem::MsgPort::Create(g_InterfacePortRef,0); 
	if(STATUS_OK != ret )
	{
		DEBUGL1("SRMUnitTest:Failed to create SRMUnitTest IF port .....exiting\n");		
		return 0;
	}
	else
	{
		DEBUGL6("SRMUnitTest:Successfully created SRMUnitTest IF port :0x%05x\n", (uint32)g_InterfacePortRef->GetId());		
	}

// Running the thread to receive the messages from systemresource manager 
	DEBUGL7("SRMUnitTest: SetUp of Thread Listening to incoming message \n");
	RXThread msgReadThread(g_InterfacePortRef); 
	msgReadThread.Start();

   while (1)
   {
	   CString   sInput="";
	   char     *pUserInput=NULL;
	   //Get User input
GetUserInput:
		cout <<	"MAIN menu \n"
    		   	"s = Send 'Subcribe' request to SRM \n"
       		   	"u = Send 'UnSubcribe' request to SRM \n"
               	"l = Send 'SetWaterMarkLevel' request to SRM \n"
    		   	"g = Send 'GetSizeInformation' request to SRM \n"
               	"a = Send 'AllocateQuota' request to SRM \n"
               	"d = Send 'DeallocateQuota' request to SRM \n"
               	"c = Send Custom Request request to SRM \n"
				"q = Quit This Test\n"
               	"=: " <<endl;
       if (cin.peek()==EOF) {
               cout << "Exiting !" << endl;
           return 0;
       }
       getline(cin, sInput);
       pUserInput = const_cast<char*>(sInput.c_str());
       while (*pUserInput && isspace(*pUserInput)) pUserInput++;
        if (*pUserInput == '\0' )
	{
      		goto GetUserInput; // ignore blank lines 
	}
	else
	{
		cout << "Input Received: \n" << endl;
	}
	    
       switch (*pUserInput)
       {
		case 's': SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::Subscribe, g_SubScribeRequest, "Subscribe");break;
      		case 'u': SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::UnSubscribe,g_UnSubScribeRequest,"UnSubscribe");break;
      		case 'g': SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::GetSizeInformation,g_GetSizeInfoRequest,"GetSizeInformation");break;
      		case 'l': SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::SetWaterMarkLevel, g_SetWaterMarkLevelRequest, "SetWaterMarkLevel");break;
      		case 'a': SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::AllocateQuota,g_AllocateQuotaRequest,"AllocateQuota");break;
      		case 'd': SendRequestToSRM(iPort, ci::systemresourcemanager::SystemResourceManager::DeallocateQuota,g_DeAllocateQuotaRequest,"DeallocateQuota");break;
			case 'c': while(SendCustomRequestToSRM(iPort));break; // this is a loop , exit once you are done with custom
		case 'q': exit(0);break;
      			default :
      					cout << " Invalid input , please enter one of listed options...\n"<< endl;
      					break;
     	}
 	/*    
		DEBUGL7("SRMUnitTest: Listening to incoming message \n");
		MsgRef msg=NULL;//this is done to force the release of memory every time for loop
		msg = new Msg;
		ret = g_InterfacePortRef->Receive(*msg); 
		if( STATUS_FAILED == ret )
		{
			DEBUGL1("SRMUnitTest: Message reception failed : status returned = %d\n",ret);
			continue;
		}
		
		int32 msgId  =  msg->GetId();
		DEBUGL7("SRMUnitTest: Received Message Type=0x%x, id=0x%x, from 0x%05\n", msg->GetType(), msgId, msg->GetSender());
		if(msg->GetType() == MSG_TYPE_ACK)
		{
	   		DEBUGL7("SRMUnitTest: Received Ack Message\n");
	   		continue;
		}
		
		if((msgId == ci::systemresourcemanager::SystemResourceManager::Notify) && (msg->GetType() == MSG_TYPE_EVENT ))
		{
	   		DEBUGL7("SRMUnitTest: Received Notify Event \n");
	   		DEBUGL7("sRxdMsg = [%s]\n",msg->GetContentPtr());
			continue;
		}
		//default case : display all other msgs
   		DEBUGL7("SRMUnitTest: Received response Message\n");
   		DEBUGL7("sRxdMsg = [%s]\n",msg->GetContentPtr());
 */
   		
   }
    
    
	return 0;
}

