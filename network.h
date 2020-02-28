// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

#ifndef NETWORK_H_
#define NETWORK_H_

#include <status.h>
#include <types.h>
#include <CI/SoftwareDiagnostics/softwarediagnostics.h>
#include <CI/ServiceStartupManager/client.h>
#include <CI/ServiceStartupManager/ssmcontracts.h>
#include <CI/HierarchicalDB/hierarchicaldb.h>
#include <CI/HierarchicalDB/DOM/document.h>
#include <CI/HierarchicalDB/DOM/node.h>
#include <CI/OperatingEnvironment/thread.h>

/* TODO: A class that implements Network QoS for System Resource Manager
 * 
 * Note: Not required for CP3 and features/kind of support in Linux Kernel may impact network part of SRM design
 */
	

namespace ci
{
namespace systemresourcemanager
{
	DECL_OBJ_REF(Network);

	//Class that does creation/deletion/add and remove of nodes understand the Storage node of the System resource manager document
	class Network  
	{
		public:
			Network(ci::hierarchicaldb::HierarchicalDBRef mainHDBRef, dom::DocumentRef mainDomRef);
			~Network();
		private:
			ci::hierarchicaldb::HierarchicalDBRef m_pHDB;
						
	};
} //SystemResourceManager
} //CI

#endif /*NETWORK_H_*/
