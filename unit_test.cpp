// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //


//JBINU:Very simple Unit test App for SRM component
// This application send different messages to cisystemresourcemanager according to user input. 
// The ststus of these messages can be found from the cisrm.lof file 

#include <boost/test/included/unit_test.hpp>
#include <boost/test/parameterized_test.hpp>
#include </usr/local/include/boost-1_35/boost/test/test_tools.hpp>

#include <boost/mpl/range_c.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/array.hpp>

#include <iostream.h>
#include <status.h>
#include <CI/MessagingSystem/msg.h>
#include <CI/MessagingSystem/msgport.h>
#include <CI/SystemResourceManager/systemresourcemanager.h>
#include <CI/SystemResourceManager/test/rxthread.h>
#include <CI/ServiceStartupManager/client.h>
//#include <CI/ServiceStartupManager/ssmcontracts.h>
#include <csystemresourcemanager.h>
using namespace std;
using namespace ci::operatingenvironment;
using namespace ci::messagingsystem;
using namespace ci::softwarediagnostics;
using namespace ci::servicestartupmanager;

using namespace boost::unit_test;
using namespace boost::unit_test::log;


CString g_StorageName = "<Storage Name=\"/imagedata\">";

CString g_SubScribeRequest = "<Subscribe xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">" 
									//<Storage Name=\"/work/drivers/client/UnixFilters\">
                           + g_StorageName +
								        "<Events>\
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

CString g_UnSubScribeRequest = "<UnSubscribe xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
									//<Storage Name=\"/work/drivers/client/UnixFilters\">
                           + g_StorageName +
								   " </Storage>\
									 <Storage Name=\"/backup/fax/RX/testfile1\">\
									</Storage>\
								</UnSubscribe>";

CString g_AllocateQuotaRequest = "<AllocateQuota xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
									//<Storage Name=\"/work/drivers/client/UnixFilters\">
                           + g_StorageName +
									"<MaxSize>100</MaxSize>\
									<HighWaterMarkLevel>99</HighWaterMarkLevel> \
									</Storage>\
								</AllocateQuota>";

CString g_DeAllocateQuotaRequest = "<DeallocateQuota xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
									//<Storage Name=\"/imagedata/newdir1\">
                           + g_StorageName +
									"</Storage>\
								    </DeallocateQuota>";


CString g_GetSizeInfoRequest = "<GetSizeInformation xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
									//<Storage Name=\"/work/drivers/client/UnixFilters\"></Storage>
                           + g_StorageName +
								   "</GetSizeInformation>";



CString g_SetWaterMarkLevelRequest = "<SetWaterMarkLevel xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
									//<Storage Name=\"/work/drivers/client/UnixFilters\"> 
                           + g_StorageName +
									"<HighWaterMarkLevel>70</HighWaterMarkLevel> \
									</Storage>\
									<Storage Name=\"/work/drivers/client/UnixFilters/testfile1\"> \
									<HighWaterMarkLevel>65</HighWaterMarkLevel> \
									</Storage>\
									</SetWaterMarkLevel>";


CString g_ForceWaterMarkLevelReached = "<SetWaterMarkLevel xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
									//<Storage Name=\"/work/drivers/client/UnixFilters\"> 
                           + g_StorageName +
									"<HighWaterMarkLevel>5</HighWaterMarkLevel> \
									</Storage>\
									</SetWaterMarkLevel>";

/* TODO: 12 May 2008,Not a priority for now. 
	and also further study of K2 kernel and features supported by it need to be done for CPu and Network reservation.
	CString g_AddNetworkQosRequest
	CString g_AddNetworkQosRequest
	CString g_AddNetworkQosRequest
*/


MsgPortRef	g_InterfacePortRef; 
int         g_iPort; // port number to which cisrm is listening to 
int g_noOfTestCases = 0; 
int g_passedTestCases = 0;
Status g_status;

//void SendRequestToSRM(MsgPortId pSRMPort, MsgId iMsgId, CString sMessageBuff, CString sMsgName )


void GetMessagePort()
   {
     	++g_noOfTestCases;
         
    ////I got the following (id1)from  "04_Design\CI\SampleCode\samples\ServiceStartupManager\Sample_ServiceStartupManager\Sample_GetServiceState"

      ci::messagingsystem::MsgPortRef port = NULL;
	   MsgPortId id1 = STATIC_PORT_ID_MASK | 0x103;
	   ci::messagingsystem::MsgPort::Create(port, id1);
	   if (!port)
	   {
	      printf("SRMUnitTest:Failed to create SRMUnitTest IF port to SSM .....exiting\n");		
		   return ;
	   }
	   CString name = "cisrmtestsuit";
	   // Using the SSM Client interface
	   Ref<ci::servicestartupmanager::Client> myClient;
	   myClient = ci::servicestartupmanager::Client::Acquire(port, name.c_str());
	   ci::servicestartupmanager::SSMContracts::stServiceStateBus ssBus = myClient->GetServiceState("cisystemresourcemanager");
	   g_iPort = ssBus.portID;

      Status ret = ci::messagingsystem::MsgPort::Create(g_InterfacePortRef,0); 
      if(STATUS_OK != ret )
      {
	      printf("SRMUnitTest:Failed to create SRMUnitTest IF port to cisystemresourcemanager.....exiting\n");		
	    
      }
      else
      {    g_passedTestCases++;
	      printf("SRMUnitTest:Successfully created SRMUnitTest IF port :%d\n", (uint32)g_InterfacePortRef->GetId());		
      }
   
   }



void SendAllocateRequest()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_AllocateQuotaRequest.c_str(), g_AllocateQuotaRequest.size());
    msg.SetId(ci::systemresourcemanager::SystemResourceManager::AllocateQuota);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("SendAllocateRequest: Failed \n");

    }
	 else
    {  g_passedTestCases++;
       printf("SendAllocateRequest: Success\n");
    }
    
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }


void SendDeAllocateQuotaRequest ()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_DeAllocateQuotaRequest.c_str(), g_DeAllocateQuotaRequest.size());
    msg.SetId(   ci::systemresourcemanager::SystemResourceManager::DeallocateQuota  );
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("SendDeAllocateQuotaRequest: Failed \n");
    }
	 else
     {  
       g_passedTestCases++;
       printf("SendDeAllocateQuotaRequest: Success\n");
     }
    
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }



void SendSubscribeRequest()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_SubScribeRequest.c_str(), g_SubScribeRequest.size());
    msg.SetId(ci::systemresourcemanager::SystemResourceManager:: Subscribe);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("SendSubscribeRequest: Failed \n");
    }
	 else
     {  
       g_passedTestCases++;
       printf("SendSubscribeRequest: Success\n");
     }
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }

void SendUnSubScribeRequest()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_UnSubScribeRequest.c_str(), g_UnSubScribeRequest.size());
    msg.SetId(  ci::systemresourcemanager::SystemResourceManager::UnSubscribe);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("SendUnSubScribeRequest: Failed \n");
    }
	 else
     {  
       g_passedTestCases++;
       printf("SendUnSubScribeRequest: Success\n");
     }
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }

void SendGetSizeInformationRequest()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_GetSizeInfoRequest.c_str(), g_GetSizeInfoRequest.size());
    msg.SetId(  ci::systemresourcemanager::SystemResourceManager::GetSizeInformation);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("SendGetSizeInformationRequest: Failed \n");
    }
	 else
     {  
       g_passedTestCases++;
       printf("SendGetSizeInformationRequest: Success\n");
     }
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }

void SendSetWaterMarkLevelRequest()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_SetWaterMarkLevelRequest.c_str(), g_SetWaterMarkLevelRequest.size());
    msg.SetId(  ci::systemresourcemanager::SystemResourceManager::SetWaterMarkLevel);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("SendSetWaterMarkLevelRequest: Failed \n");
    }
	 else
     {  
       g_passedTestCases++;
       printf("SendSetWaterMarkLevelRequest: Success\n");
     }
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }

void SendForceWaterMarkLevel()
   {
   	++g_noOfTestCases;
   
    Msg msg(MSG_TYPE_REQUEST, g_ForceWaterMarkLevelReached.c_str(), g_ForceWaterMarkLevelReached.size());
    msg.SetId(  ci::systemresourcemanager::SystemResourceManager::SetWaterMarkLevel);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf(" ForceWaterMarkLevelReached: Failed \n");
    }
	 else
     {  
       g_passedTestCases++;
       printf(" ForceWaterMarkLevelReached: Success\n");
     }
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }


void StopSystemResourceManager()
   {
   	++g_noOfTestCases;
    CString stop = "Stop";
    Msg msg(MSG_TYPE_REQUEST, stop.c_str(), stop.size());
    msg.SetId( ci::servicestartupmanager::SSMContracts::STOP);
    msg.SetTarget(g_iPort);
    g_status = g_InterfacePortRef->Send(msg);
    if( STATUS_OK != g_status )
    {
    	printf("Stop: Failed \n");
    }
	 else
    {  
       g_passedTestCases++;
       printf("Stop: Success\n");
    }
    BOOST_CHECK_EQUAL(g_status,STATUS_OK);

   }


 

void PrintStatus()
{
   printf("\nComponent Name : cisystemresourcemanager \n");
   printf("TestCases  Passed  Failed  Pass percentage  \n");
   printf("%d        %d      %d      %.2f\n",g_noOfTestCases,g_passedTestCases,g_noOfTestCases-g_passedTestCases,g_passedTestCases*100.0/g_noOfTestCases);

}



test_suite* init_unit_test_suite(int argc,char* argv[])
{

   test_suite* test = BOOST_TEST_SUITE("Master Test suite");
	   
	test->add(BOOST_TEST_CASE(&GetMessagePort));
	test->add(BOOST_TEST_CASE(&SendAllocateRequest));
   test->add(BOOST_TEST_CASE(&SendSubscribeRequest));
   test->add(BOOST_TEST_CASE(&SendSetWaterMarkLevelRequest));
   test->add(BOOST_TEST_CASE(&SendGetSizeInformationRequest));
   
   // subscribe to the same to exersise the other branch of the function
   test->add(BOOST_TEST_CASE(&SendSubscribeRequest)); 
   
   //try to generate a system event watermark level reached 
   test->add(BOOST_TEST_CASE(&SendForceWaterMarkLevel)); 
   
   test->add(BOOST_TEST_CASE(&SendUnSubScribeRequest));
   test->add(BOOST_TEST_CASE(&SendDeAllocateQuotaRequest));
   test->add(BOOST_TEST_CASE(&StopSystemResourceManager));
   test->add(BOOST_TEST_CASE(&PrintStatus));
   
   //TODO: test cases to be added to handle the sorage events 
   //like  max size reached , file deleted, etc 


	return test;
}


