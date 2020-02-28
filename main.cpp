// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

//Padua for SRM component
#include <iostream>
#include <status.h>
#include <csystemresourcemanager.h>

using namespace std;
using namespace ci::operatingenvironment;

using namespace ci::systemresourcemanager;

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

int main (int argc, char **argv)
{
	 /* The ProgramOptions object internally handles 
	  * following standard EBX commandline options
	  * -t -T, -S, -H
	  */ 
		

	 CString argstr; // These object should be decalred here for accessing inside if (argc > 1)
	 int arg = 0;
	 if (argc > 1) 
	    { 
	
		 ProgramOptions::BeginOpts(argc, argv, "-s:m:",
	                                                "-s PowerFailureRestart\n-m BootMode  ",
	                                                "    -sPowerFailureRestart = Power failure restart \n");
		 while ((arg = ProgramOptions::GetOpt(argstr)) >= 0)
		 	{
				 switch (arg) 
				 {
				 default: //do nothing 
					 break;
				 }
			}
		 ProgramOptions::EndOpts();
	    } 

    CSystemResourceManagerRef  SRMRef = new CSystemResourceManager();
	//instantiate System Resource Manager DOM using default XML
	if(!SRMRef)
	{
		DEBUGL1("SRM:main:creation of CSystemResourceManager object failed \n");
		DisplayLogExitSRM("Failed to create CSystemResourceManager ....");
	}
	//Start the inotify thread for listen to kernel inotify events
	if(STATUS_OK != SRMRef->Start())
	{
		DisplayLogExitSRM("Failed to start inotify thread ....");
	}
	
	if (STATUS_OK != SRMRef->CreateSRMDomFromDefaultSystemResourcesMgrXML())
	{
		DEBUGL1("SRM:main:Check if DefaultSystemResourceManager.xml is available in EB2 bin direcotry\n");
		DisplayLogExitSRM("Failed to create SRM from default xml....");
	}
	
	//Listen for incoming requests from clients and process them
	DEBUGL7("SRM:main:Started listening to incoming events\n");
	if(STATUS_OK != SRMRef->Listen())
	{
		DisplayLogExitSRM("Failed to listen to incoming messages ....");
	}
	return 0;
}

