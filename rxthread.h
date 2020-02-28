// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

#ifndef RXTHREAD_H_
#define RXTHREAD_H_
#include <CI/OperatingEnvironment/thread.h>
#include <status.h>
#include <types.h>
#include <CI/SoftwareDiagnostics/softwarediagnostics.h>
#include <CI/MessagingSystem/msg.h>
#include <CI/MessagingSystem/msgport.h>
#include <CI/ci_service_name.h>
#include <CI/cicontracts.h>
#include <contracts.h>
#include <CI/SystemResourceManager/systemresourcemanager.h>

class RXThread : public ci::operatingenvironment::LocalThread {
	public:
		RXThread(ci::messagingsystem::MsgPortRef);
		virtual	~RXThread();
		void *Run(void *executeFunc);
	private:
		/* Msg Q port for rx responses and notifications*/
	ci::messagingsystem::MsgPortRef		 m_PortRef;
		
};

#endif /*RXTHREAD_H_*/
