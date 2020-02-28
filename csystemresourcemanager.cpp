// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

//Binu for SRM component
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <CI/DataStream/datastream.h>
#include <CI/OperatingEnvironment/file.h>
#include "CI/OperatingEnvironment/cexception.h"
#include <portids.h>

#include <csystemresourcemanager.h>

extern "C" int softPowerKeyEvent();
extern "C" int softPowerSaveKeyEvent();

using namespace std;
using namespace ci::operatingenvironment;

namespace ci {
	namespace systemresourcemanager {

MonitorThreadRef MonitorKeyPress = new ci::systemresourcemanager::MonitorThread();
MonitorSoftPowerSaveThreadRef MonitorSoftPowerSaveKeyPress = new ci::systemresourcemanager::MonitorSoftPowerSaveThread();

void SystemResourceManager::CIcallbackRegistrationSoftPowerKey(void (*handlerFunction)(),int sleepTime)
{
	static bool regId =false;
	if(regId == true)
	{
		DEBUGL2("Callback function already registered\n");
		return; 
	}
	regId = true;
		
	DEBUGL4("CIcallbackRegistrationSoftPowerKey::Enter CI Call Back Registration\n");
	MonitorKeyPress->MonitorCallbackFunction = handlerFunction;
	MonitorKeyPress->MonitorSleepTime = sleepTime;
	MonitorKeyPress->Start();
	DEBUGL1("Call back Function called\n");
}

void SystemResourceManager::CIcallbackRegistrationSoftPowerSaveKey(void (*handlerFunction)(),int sleepTime)
{
        static bool regId =false;
        if(regId == true)
        {
                DEBUGL2("Callback function already registered\n");
                return;
        }
        regId = true;

        DEBUGL4("CIcallbackRegistrationSoftPowerSaveKey::Enter CI Call Back Registration\n");
        MonitorSoftPowerSaveKeyPress->MonitorCallbackFunction = handlerFunction;
        MonitorSoftPowerSaveKeyPress->MonitorSleepTime = sleepTime;
        MonitorSoftPowerSaveKeyPress->Start();
        DEBUGL1("Call back Function for SoftPowerSave key called called\n");
}

void * MonitorThread::Run(void *execFunc)
{
	DEBUGL4("CIcallbackRegistrationSoftPowerKey::Entering Callback thread\n");
	while(true)
	{
		if(softPowerKeyEvent()==0)	 //Check the event, if If button pressed for more than 1 sec 
		{
			DEBUGL5("CIcallbackRegistrationSoftPowerKey::Button Pressed more than 1 sec::Calling Handlerfunction\n");
			MonitorCallbackFunction();
			// This function keeps on monitoring whether the soft power key even is triggered or not.
			// break
		}
		else
		{
			DEBUGL5("CIcallbackRegistrationSoftPowerKey::Button Pressed less than 1 sec::Going to sleep\n");
			//The default sleep time is 3 ,if its <=0
			sleep(MonitorSleepTime<=0?3:MonitorSleepTime); //3 
		}
	}
}

void * MonitorSoftPowerSaveThread::Run(void *execFuncPowerSave)
{
	DEBUGL4("CIcallbackRegistrationSoftPowerSaveKey::Entering Callback thread\n");
	while(true)
	{
		if(softPowerSaveKeyEvent()==0)       //Check the event, if If button pressed for more than 1 sec
		{
			DEBUGL5("CIcallbackRegistrationSoftPowerSaveKey::Button Pressed more than 1 sec::Calling Handlerfunction\n");
			MonitorCallbackFunction();
			// This function keeps on monitoring whether the soft power key even is triggered or not.
		}
		else
		{
			DEBUGL5("CIcallbackRegistrationSoftPowerSaveKey::Button Pressed less than 1 sec::Going to sleep\n");
			//The default sleep time is 3 ,if its <=0
			sleep(MonitorSleepTime<=0?3:MonitorSleepTime); //3
		}
	}
}


	}//SystemResourceManager
unill.c//CI
