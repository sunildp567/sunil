// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

#include "rxthread.h"

using namespace std;
using namespace ci::operatingenvironment;
using namespace ci::messagingsystem;
using namespace ci::systemresourcemanager;

RXThread::RXThread(ci::messagingsystem::MsgPortRef	pPortRef)
{
	m_PortRef=pPortRef;
}

RXThread::~RXThread()
{
	m_PortRef = NULL;
}


void *RXThread::Run( void *executeFunc)
{

	while (true)
	{
		DEBUGL7("SRMUnitTest:RXThread: Listening to incoming message \n");
		MsgRef msg=NULL;//this is done to force the release of memory every time for loop
		msg = new Msg;
		
		if( STATUS_FAILED == m_PortRef->Receive(*msg) )
		{
			DEBUGL1("SRMUnitTest: Message reception failed ..retrying\n");
			continue;
		}
		
		int32 msgId  =  msg->GetId();
		DEBUGL7("SRMUnitTest: Received Message Type=%c, id=0x%x, from [%d]  \n", msg->GetType(), msgId, msg->GetSender());
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
		CString response = (char *)msg->GetContentPtr();
		//default case : display all other msgs
		DEBUGL7("SRMUnitTest: Received Response Msg=\n[%s]\n",response.c_str());

	}
	return NULL;
}

