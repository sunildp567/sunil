// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

#ifndef CPU_H_
#define CPU_H_

#include <status.h>
#include <types.h>
#include <CI/SoftwareDiagnostics/softwarediagnostics.h>
#include <CI/ServiceStartupManager/client.h>
#include <CI/ServiceStartupManager/ssmcontracts.h>
#include <CI/HierarchicalDB/hierarchicaldb.h>
#include <CI/HierarchicalDB/DOM/document.h>
#include <CI/HierarchicalDB/DOM/node.h>
#include <CI/OperatingEnvironment/thread.h>

/* TODO: Class that implements CPU reservation for System Resource Manager
 * 
 * Note: Not required for CP3 and features/kind of support in Linux Kernel may impact CPU part of SRM design
 */
	

namespace ci
{
namespace systemresourcemanager
{
	DECL_OBJ_REF(Cpu);

	//Class that does creation/deletion/add and remove of nodes understand the Storage node of the System resource manager document
	class Cpu  
	{
		public:
			Cpu(ci::hierarchicaldb::HierarchicalDBRef mainHDBRef, dom::DocumentRef mainDomRef);
			~Cpu();
		private:
			ci::hierarchicaldb::HierarchicalDBRef m_pHDB;
						
	};
} //SystemResourceManager
} //CI

#endif /*CPU_H_*/
