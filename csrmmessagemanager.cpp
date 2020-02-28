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
#include <CI/SystemInformation/systeminformation.h>

#include "CI/LogManager/loginterface.h"
#include <csystemresourcemanager.h>

extern "C" int softPowerKeyEvent();
extern "C" int lcdDispOn(void);

using namespace std;
using namespace ci::operatingenvironment;
using namespace ci::messagingsystem;
using namespace ci::hierarchicaldb;
using namespace ci::datastream;
using namespace dom;
using namespace ci::servicestartupmanager;
using namespace ci::resourcemanagement;
using namespace ci::logmanager;
using namespace ssdk;
using namespace ci::systeminformation;
using namespace ci::indexeddb;

namespace ci {
	namespace systemresourcemanager {

	// Inotify event buffer length
#define INOTIFY_BUFLEN (1024 * (sizeof(struct inotify_event) + 16))
/**
 * Generate Unique File name for creating Files.
 * Input File names possible : DefaultSystemResources 
 *                             GetSizeInformation
 *                             SetWaterMarkLevel
 *                             Subscribe
 *                             unSubscribe
 *                             AllocateQuota
 *                             NotifyError
 *                             ResetErrorCount
 */
CString getUniqueFileName(const CString inputFile)
{
   CUUID cuid;
   CString uid;
   uid=cuid.toString();
   CString uniquePath = uid + "_" + inputFile;
   return uniquePath;
}

CSystemResourceManager::CSystemResourceManager()
{
	DEBUGL8("CSystemResourceManager::CSystemResourceManager() Start\n");
	//create the SRM message IF port
	if ( STATUS_OK != OpenPort() )
	{
		DEBUGL1("SystemResourceManager: Failed to open message port for SRM \n");
	}
	// 
	if( pipe(m_InComingSubscriptionRequestsFd) < 0 )
	{
		perror("SystemResourceManager:pipe:");
		DEBUGL1("SystemResourceManager:Creation of m_InComingSubscriptionRequestsFd pipe file descriptor failed  \n");
	}

	m_iPowerType = -1;
	m_iPreviousPowerType = -1;
	m_pIdbPowerMonitor = ci::indexeddb::IndexedDB::Acquire();
	if (!m_pIdbPowerMonitor)
		DEBUGL1("CSystemResourceManager(): IndexedDB::Acquire failed\n");
	m_SystemResourceManagerDOM = NULL;
	Status retStatus = STATUS_OK;
	CString path = getenv("EB2");
	CString sTmpPath =  path + "/tmp";
	CString sBuildType = getenv("BUILD_TYPE");
	path = path + "/build/" + sBuildType;
	path.append("/bin/");
	CUUID cuid;
	CString uid;
	uid=cuid.toString();
	CString sDocName = getUniqueFileName ("DefaultSystemResources");
	CString sXMLFileName = path + "DefaultSystemResources.xml";
	CString sXMLFileName_SSD = path + "DefaultSystemResources_ReussSSD.xml";
	CString srm_dom_name = "DefaultSystemResources";
	CString srm_dom_path = "/work/ci/srm";
	CString srm_dom_file = srm_dom_path+"/"+srm_dom_name;
	dom::DocumentRef m_pDocument =NULL;
	CString sXMLFileName_new = srm_dom_path + "/" + "DefaultSystemResources_new.xml";
	char hard_disk_size[100] ={0};
	CString cmd;
	dom::DocumentRef tempdoc;
	CString isXmlCreationReq_file = srm_dom_path+"/SRM_isXmlCreationReq";
	if ( !(m_pHDB = ci::hierarchicaldb::HierarchicalDB::Acquire(NULL) ) ) 
	{
		DEBUGL1("SystemResourceManager: Failed to Acquire HDB\n"); 
	}

	//Check if the Secure Erase is enabled and create a flag file indicating the same
	FILE *fp;
	fp = setmntent(MOUNTED, "r");
	if(fp)
	{
		struct mntent *tmpmntent;
		while((tmpmntent=getmntent(fp)))
		{    
			//Get the MountPoint Directories
			CString mntDir = CString(tmpmntent->mnt_dir);
			//Check if the Partitions are either modified for Encryption or Secure Erase
			size_t findPos = mntDir.find("enc_");
			if(findPos == string::npos)
			{
				findPos = mntDir.find("sec_");
			}
			if(findPos!=string::npos)
			{
				if(!(File::Exists("/work/ci/secureEraseFlag")))
				{
					int fd = open("/work/ci/secureEraseFlag", O_RDWR|O_CREAT, 0777);
					if (fd < 0)
						DEBUGL1("CSystemResourceManager():: Creation of /work/ci/secureEraseFlag  failed, errno = %d\n", errno);
					else
					{
						DEBUGL8("CSystemResourceManager():: Creation of /work/ci/secureEraseFlag  is OK \n");
						close(fd);
					}
					break;
				}
				DEBUGL8("CSystemResourceManager()::Secure Erase Flag file exists in secure erase mode...hence not recreating it\n");
				break;
			}
			else if(File::Exists("/work/ci/secureEraseFlag") && ((string::npos != mntDir.find("registration")) || (string::npos != mntDir.find("encryption"))))
			{
				DEBUGL8("\n CSystemResourceManager()::Secure Erase is not enabled\n");
				//Delete the Secure Erase flag file
				File::DeleteFile("/work/ci/secureEraseFlag");
				//Delete the SRM dom
				m_pHDB->DeleteDocument(srm_dom_name,srm_dom_path);
			}
		}
		if (fp)
		    fclose(fp);
	}
	else
		DEBUGL1("SystemResourceManager:GetMountPoint: setmntent() call failed\n");
	//Creating/Deleting Secure erase falg file -- END
#if defined (WEISS_S2)
	if((ci::operatingenvironment::File::Exists(isXmlCreationReq_file.c_str())))
	{
		if (STATUS_OK != m_pHDB->OpenDocument(srm_dom_path,srm_dom_name, m_SystemResourceManagerDOM))
		{
			if(!Folder::Exists(srm_dom_path))
                        {
                                DEBUGL2("\n CSystemResourceManager() /work/ci/srm folder is not present, creating it!!\n");
                                if (!Folder::CreateFolder(srm_dom_path))
                                {
                                        DEBUGL2("\n CSystemResourceManager() /work/ci/srm couldnt be created, CreateFolder() failed with errno %d", errno);
                                }

                        }
			else				
			{
				DEBUGL2("\n CSystemResourceManager() DOM is not present or corrupted,creating new DOM from DefaultSystemResources_new.xml that has the updated HDD size\n");
				if(File::Exists(srm_dom_file))
				{
					DEBUGL2("\n CSystemResourceManager() DOM is corrupted,deleting the existing DOM\n");
					if((m_pHDB->DeleteDocument(srm_dom_name,srm_dom_path))!=STATUS_OK)
					{
						DEBUGL1("\n CSystemResourceManager() DeleteDocument failed\n");
						throw CException(retStatus);
					}

				}
			}
			if( STATUS_OK  != m_pHDB->CreateDocumentFromFile(srm_dom_path,srm_dom_name,m_SystemResourceManagerDOM,sXMLFileName_new))
			{
				DEBUGL1("\n CSystemResourceManager()Failed to CreateDocumentFromFile.\n");
				throw CException(retStatus);
			}
		}

	}

	//create the DefaultSystemResource.xml dynamically only once after installation
	// if the file is not existing it means that the Installation is happening for the first time
	if(!(ci::operatingenvironment::File::Exists(isXmlCreationReq_file.c_str())))
	{
		//Get the HDD size
		cmd = "fdisk -l| grep '/dev/sda:'|cut -d ' ' -f 3";
		FILE *fp = popen(cmd.c_str(), "r");
		if(fp)
		{
			fgets(hard_disk_size, sizeof(hard_disk_size), fp);
		}
		else
		{
			DEBUGL1("CSystemResourceManager popen() failed \n");
			exit(0);
		}
      pclose(fp);
		CString m_HDD_size(hard_disk_size);
      CString sub_product = getenv("SUB_PRODUCT");
      if(sub_product == "WEISS_H" || sub_product =="WEISS_L_HDD")   
     {
		size_t pos;
		pos = m_HDD_size.find_first_of(".");
    
		if(pos == string::npos)
		{
			DEBUGL1("SystemResourceManager:: the string holding the HDD size is invalid\n");
			exit(0);
		}
		m_HDD_size = m_HDD_size.substr(0,pos);
     }
		DEBUGL1("SystemResourceManager::hdd size is %s\n", m_HDD_size.c_str());

		//implementation to populate the DefaultSystemResource.xml dynamically based on the HDD Size
		Status st =	InitializePartitionSizeMap(m_HDD_size);	
		if(st != STATUS_OK )
		{
			DEBUGL1("CSystemResourceManager(): failed to populate the respective MAP \n");
			exit(0);
		}
		//iterate through the nodes and replace the value of the partition size by reading the values from respective maps

		if (STATUS_OK != m_pHDB->OpenDocument(srm_dom_path,srm_dom_name, m_SystemResourceManagerDOM))
                {
                        if(!Folder::Exists(srm_dom_path))
                        {
                                DEBUGL2("\n CSystemResourceManager() /work/ci/srm folder is not present, creating it!!\n");
                                if (!Folder::CreateFolder(srm_dom_path))
                                {
                                        DEBUGL2("\n CSystemResourceManager() /work/ci/srm couldnt be created, CreateFolder() failed with errno %d\n", errno);
                                }

                        }
                        else
                        {
                                DEBUGL2("\n CSystemResourceManager() DOM is not present or corrupted,creating new DOM from DefaultSystemResources.xml\n");
                                if(File::Exists(srm_dom_file))
                                {
                                        DEBUGL2("\n CSystemResourceManager() DOM is corrupted,deleting the existing DOM\n");
                                        if((m_pHDB->DeleteDocument(srm_dom_name,srm_dom_path))!=STATUS_OK)
                                        {
                                                DEBUGL1("\n CSystemResourceManager() DeleteDocument failed\n");
                                                throw CException(retStatus);
                                        }

                                }
                        }
                                if( STATUS_OK  != m_pHDB->CreateDocumentFromFile(srm_dom_path,srm_dom_name,m_SystemResourceManagerDOM,sXMLFileName))
                                {
                                        DEBUGL1("\n CSystemResourceManager()Failed to CreateDocumentFromFile.\n");
                                        throw CException(retStatus);
                                }
                 }

		ElementRef pTempSystemResources = m_SystemResourceManagerDOM->getDocumentElement();
		if(!pTempSystemResources)
		{
			DEBUGL1("SystemResourceManager::getDocumentElement() failed\n");
			exit(0);
		}
		Ref<NodeList> pListOfNodes = pTempSystemResources->getChildNodes();
		CString sTempStorageAttName;
		for (uint32 i = 0; i < pListOfNodes->getLength(); i++){
			Ref<Node> pChild = pListOfNodes->item(i);
			ElementRef pCurrent_Child = pChild;
			if(pCurrent_Child->getNodeName() == "Storage")
			{

				map<CString,CString>::iterator it;

				//if the HDD size is greater than 160GB, we will update the partition size with the m_HDD_partition map values. And the surplus size is added to the /storage partition
				sTempStorageAttName = pCurrent_Child->getAttribute("Name");
				it =  m_HDD_partition.find(sTempStorageAttName);
				Ref<Node> fChild=	pCurrent_Child->getFirstChild();
				CString Total_size ;
				if (m_HDD_size !=HDD_80_GB && m_HDD_size !=HDD_8_GB &&  m_HDD_size !=HDD_160_GB)
				{
					if(sTempStorageAttName == "/storage" )
					{
						int HDD_size=0;
						std::istringstream pss(m_HDD_size);
						pss >>HDD_size;

						HDD_size = HDD_size-HDD_SIZE_160_GB+STORAGE_PARTITION_SIZE_GB;
						uint64 storage = (uint64)HDD_size*1024*1024*1024;
						std::ostringstream oss;
						oss << storage;
						Total_size=oss.str();	
						fChild->setTextContent(Total_size);
					}
				}
				else{
						fChild->setTextContent(it->second);
					}
				


			}

		}
		//serialize the temp DOM to a DefaultSystemResources.xml file so that the updated xml file is used for every boot up after installation.
		CString xml_path = getenv("EB2");
		retStatus = m_pHDB->SerializeToFile(pTempSystemResources, sXMLFileName_new);
		if(retStatus != STATUS_OK)
		{
			DEBUGL1("SystemResourceManager SerializeToFile failed \n");	
			exit(0);
		}
	  	cmd ="touch "+srm_dom_path+"/SRM_isXmlCreationReq";
		system(cmd.c_str()); 
	}

#else
	if (STATUS_OK != m_pHDB->OpenDocument(srm_dom_path,srm_dom_name,m_SystemResourceManagerDOM))
	{
		if(!Folder::Exists(srm_dom_path))
		{
			DEBUGL2("\n CSystemResourceManager() /work/ci/srm folder is not present, creating it!!\n");
			if (STATUS_OK != Folder::CreateFolder(srm_dom_path))
			{
				DEBUGL2("\n CSystemResourceManager() /work/ci/srm couldnt be created, CreateFolder() failed with errno %d", errno);
			}

		}
		else
		{
			DEBUGL2("\n CSystemResourceManager() DOM is not present or corrupted,creating new DOM from DefaultSystemResources.xml\n");
			DEBUGL2 ("srm_dom_file is %s\n", srm_dom_file.c_str());
			if(File::Exists(srm_dom_file))
			{
				DEBUGL2("\n CSystemResourceManager() DOM is corrupted,deleting the existing DOM\n");
				if((m_pHDB->DeleteDocument(srm_dom_name,srm_dom_path))!=STATUS_OK)
				{
					DEBUGL1("\n CSystemResourceManager() DeleteDocument failed\n");
					throw CException(retStatus);
				}

			}
		}
      BoardType bType;
      SystemInformationRef sysInfo = SystemInformation::Acquire();
      if (sysInfo) 
      {
          Status bRetStat = STATUS_OK;
          bRetStat = sysInfo->GetSysBoardType (bType);
          if (bRetStat != STATUS_OK)
              DEBUGL1("CSystemResourceManager: Failed to get System board type \n");
      }
      if (bType == BOARD_SSD_REUSS)
      {
          if( STATUS_OK  != m_pHDB->CreateDocumentFromFile(srm_dom_path,srm_dom_name,m_SystemResourceManagerDOM,sXMLFileName_SSD))
          {
              DEBUGL1("\n CSystemResourceManager()Failed to CreateDocumentFromFile.\n");
              throw CException(retStatus);
          }
      }
      else
      {

          if( STATUS_OK  != m_pHDB->CreateDocumentFromFile(srm_dom_path,srm_dom_name,m_SystemResourceManagerDOM,sXMLFileName))
          {
              DEBUGL1("\n CSystemResourceManager()Failed to CreateDocumentFromFile.\n");
              throw CException(retStatus);
          }
      }
	}

#endif

	DEBUGL7("SystemResourceManager:Successfully created the System Resources DOM document using DefaultSystemResources.xml \n");
//initialize and get an inotify fd
if( STATUS_OK != InitializeMonitor() ) 
{
	DEBUGL1("CSystemResourceManager: Initialize monitor failed monitor thread \n");
}

//instantiate Storage object
m_storageRef = new Storage(m_pHDB,m_SystemResourceManagerDOM);
if(!m_storageRef)
{

	DEBUGL1("SystemResourceManager:Failed to instantiate storage object  \n"); 
	exit(0);
}
if(STATUS_OK != InitializeErrorMessageMap())
{
	DEBUGL1("CSystemResourceManager: Initialize ErrorMessageMap Failed\n");
}

 DEBUGL8("CSystemResourceManager::CSystemResourceManager() End\n");
}

CSystemResourceManager::~CSystemResourceManager()
{
	// Release all resources used
	m_storageRef=NULL;
	m_SystemResourceManagerDOM=NULL;
	m_pHDB=NULL;
	//Release pipe resource
   if(m_InComingSubscriptionRequestsFd[0])
   	close(m_InComingSubscriptionRequestsFd[0]);
   if(m_InComingSubscriptionRequestsFd[1])
   	close(m_InComingSubscriptionRequestsFd[1]);
}
	
Status CSystemResourceManager::OpenPort()
{
	// Create the SRM interface port 
	Status ret = ci::messagingsystem::MsgPort::Create(m_InterfacePortRef,0); 
	if(STATUS_OK != ret )
	{
		DEBUGL1("SystemResourceManager:Failed to create SystemResourceManager IF port ..\n");		
		return STATUS_FAILED;
}
	else
	{
		DEBUGL6("SystemResourceManager:Successfully created SystemResourceManager IF port :%05d\n", (uint32)m_InterfacePortRef->GetId());		
		return STATUS_OK;
	}
}	
	
int CSystemResourceManager::SetNonblocking(int Selectfd)
{
	/* Initialize data and pipe fd for timer thread create the pipe fds */
    int flags;
        /* If they have O_NONBLOCK (POSIX method)*/
#if defined(O_NONBLOCK)
           /* O_NONBLOCK is defined  */
            if (-1 == (flags = fcntl(Selectfd, F_GETFL, 0)))
               flags = 0;
               return fcntl(Selectfd, F_SETFL, flags | O_NONBLOCK);
#else
         DEBUGL1("SystemResourceManager: NON BLOCKING I/O not supported, this feature is required for select() . the program \n");
         _exit(0);
         /* use the way of doing it
         flags = 1;
         return ioctl(fd, FIOBIO, &flags); */
         flags = 1;
         return ( flags );
#endif
}


uint64 CSystemResourceManager::GetWaterMarkLevel(ElementRef pWaterMarkLevelElement, uint64 uTotalSpace)
{
	uint64 uCheckSpace = 0x0;
   try{	
	if(pWaterMarkLevelElement->getNodeName() == "WaterMarkLevel")
	{
		 int iWaterMarkLevel = atoi((pWaterMarkLevelElement->getTextContent()).c_str());
		 uCheckSpace =  (iWaterMarkLevel/100) * uTotalSpace;
	}
	else
	{
		DEBUGL1("SystemResourceManager:GetUpdatedWaterMarkLevel: GetNodeValue on 'WaterMarkLevel' failed ..\n");
		//ideally this must never happen
	}
   }  catch(DOMException except) {
      DEBUGL1("SystemResourceManager::GetWaterMarkLevel : Failed due to HDB Exception\n");
   }
	return uCheckSpace; 
}

Status CSystemResourceManager::CreateSRMDomFromDefaultSystemResourcesMgrXML()
{
	Status ret=STATUS_OK;
	//DefaultSystemResources
	ElementRef pSystemResources = m_SystemResourceManagerDOM->getDocumentElement();
	bool isinvalidPartition=false;
	//List of nodes consists of Storages,CPU, Network
	if(!pSystemResources)
	{
		DEBUGL1("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:getDocumentElement() failed\n");
		return STATUS_FAILED;
	}
   if(InitializePartitionNameMap()!=STATUS_OK)
   {
      DEBUGL1("SystemResourceManager:Failed to Initialize Partition Name Map\n");
   }
	Ref<NodeList> pListOfNodes = pSystemResources->getChildNodes();
	try
	{
		//loop through the list of events and search the event whose name attribute matches the triggerXpath
		for (uint32 i = 0; i < pListOfNodes->getLength(); i++)
		{//Storage/CPU/Network loop
			Ref<Node> pChild = pListOfNodes->item(i);
			if (!pChild || pChild->getNodeType() != Node::ELEMENT_NODE)
				continue;
			ElementRef commonElement = pChild;  //where commonElement can be Storage node or CPU node or Network node , cpu and network will be implemented future phases, depends on need
			if(commonElement->getNodeName()=="Storage")
			{	//validate if this storage is mountpoint
				ci::operatingenvironment::CString sStoragePath;
				CString  sStorageAttName = commonElement->getAttribute("Name");

				//Skip the DOM update for /backup to /sec_backup if the Secure Erase flag file exists
				if(File::Exists("/work/ci/secureEraseFlag"))
				{
					size_t findPos = sStorageAttName.find("enc_");
					if(findPos == string::npos)
						findPos = sStorageAttName.find("sec_");
					if(findPos == string::npos)
					{
						ModifyForPartitionNameChange(sStorageAttName);	
						commonElement->setAttribute("Name",sStorageAttName);
						DEBUGL7("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:Process Storage = [%s]\n",sStorageAttName.c_str());
					}
					else
						DEBUGL8("\nSystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:sStorageAttName is modified for sec_ or enc_\n");
				}
				//validate if it is mountpoint or partition	
				if(!m_storageRef->GetMountPoint(sStorageAttName,sStoragePath))
				{
					DEBUGL1("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:%s is not valid mount point of a partition \n",sStorageAttName.c_str());
					isinvalidPartition=true;
					//				return STATUS_FAILED;
				}

				//Initialize Storage's totalspace
				uint64 uTotalSpace =0x0;
				uint64 uSpaceAvailable=0x0;
				uint64 uSpaceUsed = 0x0;

				bool bHighWaterMarkLevelReached = false;

				//Scan for directory and file events and add them to inotify watch for monitor their changes
				Ref<NodeList> pListOfDirectoriesOrFiles = commonElement->getChildNodes();
				for(uint32 j = 0; j < pListOfDirectoriesOrFiles->getLength()&&!isinvalidPartition; j++ )
				{
					Ref<Node> pChildNode = pListOfDirectoriesOrFiles->item(j);
					if (!pChildNode || pChildNode->getNodeType() != Node::ELEMENT_NODE)
						continue;
					ElementRef commonDirFileElement = pChildNode;  //where commonDirFileElement can be Directory or File node
					DEBUGL7("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML: ==>%s \n",(commonDirFileElement->getNodeName()).c_str());

					if(commonDirFileElement->getNodeName() == "TotalSpace" && !isinvalidPartition)
					{
						/*get the storage's TotalSpace of this storage and test it for max limit
						 * Note: This must be set at design time !!!!
						 */
						//test if the max size is set
						m_storageRef->GetSize(commonDirFileElement,uTotalSpace);
						if( uTotalSpace <= 0 )
						{
							DEBUGL2("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:Totalsize for storage [%s] is not set ..skipping to next storage\n",sStorageAttName.c_str());
							continue;
						}
						DEBUGL7("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:Totalsize=%llu \n",uTotalSpace);

						//get the current size of the storage and verify if has exceeded max value

						CString sMountPoint ;
						m_storageRef->GetMountPoint(sStorageAttName,sMountPoint);
						if ( sMountPoint == sStorageAttName) // given storage is a partirion (mountpoint) by itself, if so find the sise of partition.
						{
							CString sTotalSpace; CString sSpaceUsed; CString sSpaceAvailable;
							m_storageRef->GetPartitionSizeInfo(sStorageAttName,sTotalSpace,sSpaceUsed,sSpaceAvailable);
							uSpaceUsed = m_storageRef->StringToUint64( sTotalSpace);
							DEBUGL3("SystemResourceManager:CreateQuotasFromDefaultSystemResourceDOM: Got a partition as mount point [%s] and spaceused is [%d]\n",sStorageAttName.c_str(),uSpaceUsed );
						}						
						else // its just a directory or file.
						{
							uSpaceUsed = m_storageRef->GetFileOrDirectorySize(sStorageAttName.c_str());
							DEBUGL3("SystemResourceManager:CreateQuotasFromDefaultSystemResourceDOM: Got directory  [%s] and spaceused is [%d]\n",sStorageAttName.c_str(),uSpaceUsed );


						}

						DEBUGL7("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:uSpaceUsed=%llu \n",uSpaceUsed);
						if( uSpaceUsed >= uTotalSpace )
						{
							//log error and skip to next storage in the DOM
							DEBUGL1("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:Quota is either equal to or has exceeded for %s  \n",sStorageAttName.c_str());
							DEBUGL1("SystemResourceManager:CreateQuotasFromDefaultSystemResourceDOM: Skipping to next storage \n");
							/*** Warning:this must never happen ***/
							continue;
						}
					}

					if((commonDirFileElement->getNodeName() ==  "SpaceUsed") &&  uTotalSpace )
					{
						//update storage's SpaceUsed
						m_storageRef->UpdateSize(commonDirFileElement,"SpaceUsed",uSpaceUsed);
					}
					else if ((commonDirFileElement->getNodeName() ==  "SpaceUsed") && isinvalidPartition)
					{
						m_storageRef->UpdateSize(commonDirFileElement,"SpaceUsed",0);
					}


					if(commonDirFileElement->getNodeName() == "WaterMarkLevel" && !isinvalidPartition)
					{
						//get 'WaterMarkLevel'
						uint64 uCheckSpace = GetWaterMarkLevel(commonDirFileElement,uTotalSpace);
						if(uSpaceUsed >= uCheckSpace)
						{ //water mark level has been reached, update event flag
							bHighWaterMarkLevelReached = true; //we cannot send this even to any client, as no client has subscribed at point
						}
					}

					if((commonDirFileElement->getNodeName() ==  "SpaceAvailable") && uTotalSpace && uSpaceUsed )
					{
						//Compute storage's SpaceAvailable and update it
						uSpaceAvailable = uTotalSpace - uSpaceUsed;
						m_storageRef->UpdateSize(commonDirFileElement,"SpaceAvailable",uSpaceAvailable);
					}
					else if ((commonDirFileElement->getNodeName() ==  "SpaceAvailable") && isinvalidPartition)
					{
						m_storageRef->UpdateSize(commonDirFileElement,"SpaceAvailable",0);
					}

					if(commonDirFileElement->getNodeName() == "Directory" && !isinvalidPartition)
					{	//validate the existence of directory
						DEBUGL7("\n\n Directory node processing in  \n");

						CString  sDirectoryAttName = commonDirFileElement->getAttribute("Name");
						if(File::Exists("/work/ci/secureEraseFlag"))
						{
							size_t findPos = sDirectoryAttName.find("enc_");
							if(findPos == string::npos)
								findPos = sDirectoryAttName.find("sec_");
							if(findPos == string::npos)
							{
								ModifyForPartitionNameChange(sDirectoryAttName);
								commonDirFileElement->setAttribute("Name",sDirectoryAttName);
								DEBUGL7("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:Process Directory = [%s]\n",sDirectoryAttName.c_str());
							}
							else
								DEBUGL8("\nSystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:sStorageAttName is sec_ or enc_\n");
						}
						if(!(m_storageRef->IsThisDirectoryPresent(sStorageAttName,sDirectoryAttName)))
						{
							DEBUGL2("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:[%s] Is not valid directory \n",sDirectoryAttName.c_str());
							DEBUGL2("Skipping to next directory under storage [%s]",sStorageAttName.c_str());
							continue;
						}

						if( STATUS_OK != UpdateDirNFileNodes(commonDirFileElement,sDirectoryAttName) )
						{
							//some issue with one of the node under File, so skipping that node
							continue;
						}
						DEBUGL7("Directory node processing out  \n\n");
					}
					// Handle file node
					else if(commonDirFileElement->getNodeName() == "File" && !isinvalidPartition )
					{	
						DEBUGL7("\n\n File node processing in \n");
						//validate the existence of directory
						CString  sFileAttName = commonDirFileElement->getAttribute("Name");
						if(File::Exists("/work/ci/secureEraseFlag"))
						{
							size_t findPos = sFileAttName.find("enc_");
							if(findPos == string::npos)
								findPos = sFileAttName.find("sec_");
							if(findPos == string::npos)
							{
								ModifyForPartitionNameChange(sFileAttName);
								commonDirFileElement->setAttribute("Name",sFileAttName);
								DEBUGL7("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:Process File = [%s]\n",sFileAttName.c_str());
							}
							else
								DEBUGL8("\nSystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:sStorageAttName is sec_ or enc_\n");
						}
						if(!(m_storageRef->IsThisFilePresent(sStorageAttName,sFileAttName)))
						{
							DEBUGL2("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:[%s] Is not valid file \n",sFileAttName.c_str());
							DEBUGL2("Skipping to next file  under storage [%s]",sStorageAttName.c_str());
							continue;
						}
						if( STATUS_OK != UpdateDirNFileNodes(commonDirFileElement,sFileAttName) )
						{
							//some issue with one of the node under File, so skipping that node
							continue;
						}
						DEBUGL7("File node processing out  \n\n");
					}

					//process Storage's event
					if (commonDirFileElement->getNodeName() == "Event" && !isinvalidPartition )
					{
						NodeRef pWaterMarkLevelReachedEventNode = commonDirFileElement->getFirstChild();
						if(pWaterMarkLevelReachedEventNode->getNodeName()=="HighWaterMarkLevelReached")
						{
							chelper::SetNodeValue(pWaterMarkLevelReachedEventNode,"HighWaterMarkLevelReached",chelper::Itoa(bHighWaterMarkLevelReached));
						}
					}

				}//loop for directory and file nodes
				//updating the taol space and available space to ZERO since its a invalid partition
				isinvalidPartition=false;
			}
			
			//TODO Handle CPU/Network : but design may not be complete as it depends on kernel support and is not required for CP3
			
		}//loop for storage/cpu/network
	}
	catch(CException except)
	{
		ret = static_cast<Status>(except);
		DEBUGL1("SystemResourceManager:CreateSRMDomFromDefaultSystemResourcesMgrXML:--> Failed. Error = 0x%x\n",ret);			
	}
	//extract name of the root element in the xpath
	
	return ret;
}

Status CSystemResourceManager::UpdateDirNFileNodes(ElementRef pDirFileElement, CString sDirFileAttName)
{
	DEBUGL7("SystemResourceManager:UpdateDirNFileNodes=pDirFileElement_NODE_NAME ==> [%s]\n",(pDirFileElement->getNodeName()).c_str());
	DEBUGL7("SystemResourceManager:UpdateDirNFileNodes: In\n");
	/*get the storage's TotalSpace of this storage and test it for max limit
	 * Note: This must be set at design time !!!!
	 */
	uint64 uTotalSpace =0x0;
	uint64 uSpaceUsed = 0x0;
	uint64 uSpaceAvailable = 0x0;
	bool bHighWaterMarkLevelReached=false;
	NodeListRef pChildList = pDirFileElement->getChildNodes();
	if(!pChildList) 
	{ 
		DEBUGL1("SystemResourceManager:UpdateDirNFileNodes: GetchildNodes on Directory/file=%s failed\n",sDirFileAttName.c_str()); 
		return STATUS_FAILED;
	}
   try {
	for(uint32 id = 0; id < pChildList->getLength() ; id++)
	{
		Ref<Node> pChildNode = pChildList->item(id);
		if (!pChildNode || pChildNode->getNodeType() != Node::ELEMENT_NODE)
			continue;
		ElementRef pCommonElement = pChildNode;  //where commonDirFileElement can be Directory or File node
		DEBUGL7("SystemResourceManager:UpdateDirNFileNodes: ==>%s \n",(pCommonElement->getNodeName()).c_str());
		if(pCommonElement->getNodeName() == "TotalSpace")
		{
			//test if the max size is set
			m_storageRef->GetSize(pCommonElement,uTotalSpace);
			if( uTotalSpace <= 0 )
			{
				DEBUGL2("SystemResourceManager:UpdateDirNFileNodes:Totalsize for storage [%s] is not set ..skipping to next directory/file\n",sDirFileAttName.c_str());
				continue;
			}
			//update the used size and available size of this directory or file
			//get the current size of the directory/file and verify if has exceeded max value 
			uSpaceUsed = m_storageRef->GetFileOrDirectorySize(sDirFileAttName.c_str());
			if( uSpaceUsed >= uTotalSpace )
			{
				//log error and skip to next storage in the DOM
				 DEBUGL2("SystemResourceManager:UpdateDirNFileNodes:Quota is either equal to or has exceeded for directory/file=%s  \n",sDirFileAttName.c_str());
				 DEBUGL2("SystemResourceManager:UpdateDirNFileNodes: Skipping to next node\n");
				 /*** Warning:this must never happen ***/
				 return STATUS_FAILED;
			}

		}
		if((pCommonElement->getNodeName() ==  "SpaceUsed") &&  uTotalSpace )
		{
			//update directory's or file's SpaceUsed
			m_storageRef->UpdateSize(pCommonElement,"SpaceUsed",uSpaceUsed);
		}
		
		if(pCommonElement->getNodeName() == "WaterMarkLevel" )
		{
			//get 'WaterMarkLevel'
			uint64 uCheckSpace = GetWaterMarkLevel(pCommonElement,uTotalSpace);
			 if(uSpaceUsed >= uCheckSpace)
			 { //water mark level has been reached, update event flag
				 bHighWaterMarkLevelReached = true;
			 }
		}
		
		if((pCommonElement->getNodeName() ==  "SpaceAvailable") && uTotalSpace && uSpaceUsed )
		{
			//Compute directory's or file's SpaceAvailable and update it
			uSpaceAvailable = uTotalSpace - uSpaceUsed;
			m_storageRef->UpdateSize(pCommonElement,"SpaceAvailable",uSpaceAvailable);
		}
		
		if(pCommonElement->getNodeName() ==  "Events")
		{
			//Scan and add directory events to inotify watch list
			if(ScanNAddEventsToWatchList(sDirFileAttName,pCommonElement, bHighWaterMarkLevelReached)!=STATUS_OK)
         {
            DEBUGL1("SystemResourceManager::ScanNAddEventsToWatchList Failed\n");
         }
		}
	}//loop for children of directory/file node
   } catch (DOMException except) {
      DEBUGL1("SystemResourceManager: UpdateDirNFileNodes Failed due to HDB Exception\n");
      return STATUS_FAILED;
   }
	DEBUGL7("SystemResourceManager:UpdateDirNFileNodes: out \n");
	return STATUS_OK;
}

Status CSystemResourceManager::ScanNAddEventsToWatchList(ci::operatingenvironment::CString sName,ElementRef pEventsNode, bool bHighWaterMarkLevelReached)  //where pBaseNode can be Storage or File or Directory
{
	Status ret = STATUS_OK;
	Ref<NodeList>	pListOfEventNodes = pEventsNode->getChildNodes();
   if(!pListOfEventNodes)
   {
      DEBUGL1("SystemResourceManager::ScanNAddEventsToWatchList : Empty Node List\n");
      return STATUS_FAILED;
   }
	try {
   for(uint32 k = 0; k < pListOfEventNodes->getLength(); k++)
	{
		Ref<Node> pEventNode = pListOfEventNodes->item(k);
		if (!pEventNode || pEventNode->getNodeType() != Node::ELEMENT_NODE)
		continue;
		ElementRef pEventElement = pEventNode;
		uint32 uWatchFlags = 0x0;
		CString sEventName = pEventElement->getNodeName();
		/*old code: if(!sEventName.compare("SizeChange"))
		{
			int32 bSizeChange = atoi((pEventElement->getTextContent()).c_str());
			//SizeChange i.e. create/move/write operation occurring in the file/directory/storage specified by sName.c_str() 
			uWatchFlags |= SRM_SIZE_CHANGE_EVENT;
			//to do Compute percentageAvailable and update
		}*/
		if(!sEventName.compare("WriteOperation"))
		{
			//Write operation occurring in the file/directory/storage specified by sName.c_str()
			int32 bWrite = atoi((pEventElement->getFirstChild()->getTextContent()).c_str());
			DEBUGL7("CSystemResourceManager:ScanNAddEventsToWatchList: bWrite=%d",bWrite);
			SetOrResetEvent(uWatchFlags,bWrite,SRM_FILE_WRITE_EVENT);
		}
		if(!sEventName.compare("FileCreated")) //This event is valid only for Directories 
		{
			//File create operation occurring in the file/directory/storage specified by sName.c_str() 
			SetOrResetEvent(uWatchFlags,1,SRM_FILE_CREATE_EVENT);
			
		}
		if(!sEventName.compare("FileDeleted")) //This event is valid only for Directories 
		{
			//File create operation occurring in the file/directory/storage specified by sName.c_str() 
			SetOrResetEvent(uWatchFlags,1,SRM_FILE_DELETE_EVENT);
			
		}

		if(!sEventName.compare("FileDeletedSelf")) //This event is valid for storage/directory/file
		{
			//File delete operation occurring in the file/directory/storage specified by sName.c_str() 
			int32 bDelete = atoi((pEventElement->getFirstChild()->getTextContent()).c_str());
			DEBUGL7("CSystemResourceManager:ScanNAddEventsToWatchList: bDelete=%d",bDelete);
			SetOrResetEvent(uWatchFlags,bDelete,SRM_FILE_DELETE_EVENT);
		}
		
		//if it is a directory
		if(!sEventName.compare("DirectoryDeletedSelf")) //This event is valid for storage/directory
		{
			//File delete operation occurring in the file/directory/storage specified by sName.c_str() 
			int32 bDelete = atoi((pEventElement->getFirstChild()->getTextContent()).c_str());
			DEBUGL7("CSystemResourceManager:ScanNAddEventsToWatchList: bDelete=%d",bDelete);
			SetOrResetEvent(uWatchFlags,bDelete,SRM_DIRECTORY_DELETE_EVENT);
		}
		
		if(!sEventName.compare("HighWaterMarkLevelReached"))
		{
			//chelper::SetNodeValue(pEventElement,"HighWaterMarkLevelReached",chelper::Itoa(bHighWaterMarkLevelReached));
			pEventElement->setTextContent(chelper::Itoa(bHighWaterMarkLevelReached));
		}
		
		//add to the inotify watch list
		int32 iSubscriberPort=-1;//-1 is used when is there subscriber is self i.e. SRM itself 
		ret = AddToWatchList(sName,iSubscriberPort,uWatchFlags);
		if(STATUS_OK != ret )
		{
			DEBUGL1("CSystemResourceManager::ScanNAddEventsToWatchList: AddToWatchList call failed with errors\n");
			break;
		}
	}//for loop of events
	} catch (DOMException except) {
      DEBUGL1("CSystemResourceManager::ScanNAddEventsToWatchList : Failed due to HDB exception\n");
      return STATUS_FAILED;
   }
   return ret;
}

void CSystemResourceManager::DisplayEvent(uint32_t mask)
{
	if( (IN_ACCESS & mask) == IN_ACCESS) DEBUGL7("File was accessed (read) \n");
	if( (IN_ATTRIB & mask) == IN_ATTRIB) DEBUGL7("Metadata changed (permissions, timestamps, extended attributes, etc.\n");
	if( (IN_CLOSE_WRITE & mask) == IN_CLOSE_WRITE ) DEBUGL7("File opened for writing was closed \n");
	if( (IN_CLOSE_NOWRITE & mask ) == IN_CLOSE_NOWRITE) DEBUGL7(" File not opened for writing was closed \n");;
	if( (IN_CREATE & mask ) ==  IN_CREATE) DEBUGL7("File/directory created in watched directory \n");
	if( (IN_DELETE & mask ) ==  IN_DELETE) DEBUGL7("File/directory deleted from watched directory (*)\n");
	if( (IN_DELETE_SELF & mask ) == IN_DELETE_SELF )  DEBUGL7("Watched file/directory was itself deleted\n");
	if( (IN_MODIFY & mask ) == IN_MODIFY ) DEBUGL7("File was modified \n");
	if( (IN_MOVE_SELF & mask ) == IN_MOVE_SELF )   DEBUGL7("Watched file/directory was itself moved\n");
	if( (IN_MOVED_FROM & mask ) == IN_MOVED_FROM ) DEBUGL7("File moved out of watched directory \n");
	if( (IN_MOVED_TO & mask ) == IN_MOVED_TO ) DEBUGL7("File moved into watched directory \n");
	if( (IN_OPEN & mask ) == IN_OPEN )  DEBUGL7("File was opened \n");
	return ;

}

int CSystemResourceManager::OnWriteGetNewSize(ci::operatingenvironment::CString FileOrDirectoryName, uint32_t mask)
{
	if( (IN_CLOSE_WRITE & mask) == IN_CLOSE_WRITE  || (IN_CLOSE_NOWRITE & mask) == IN_CLOSE_NOWRITE 
         || (IN_CREATE & mask ) ==  IN_CREATE ||  (IN_DELETE & mask ) ==  IN_DELETE || (IN_DELETE_SELF & mask ) == IN_DELETE_SELF 
         || (IN_MOVE_SELF & mask ) == IN_MOVE_SELF || (IN_MOVED_FROM & mask ) == IN_MOVED_FROM || (IN_MOVED_TO & mask ) == IN_MOVED_TO ) 
	{
		//int ret =system("du /tmp/testinotify/ -s");
		return  m_storageRef->GetFileOrDirectorySize(FileOrDirectoryName.c_str());
	}

return 0;
}


/* Reference to inotify event data structure 
 *          struct inotify_event {
 * 	      int      wd;       // Watch descriptor 
 *            uint32_t mask;     // Mask of events 
 *            uint32_t cookie;   // Unique cookie associating related events (for rename(2)) 
 *            uint32_t len;      // Size of ’name’ field 
 *            char     name[];   // Optional null-terminated name 
 *        };
*/

Status CSystemResourceManager::Listen()
{
	Status ret;
	bool bRunning = true ;
	int32 msgId;
	m_ssmClient = ci::servicestartupmanager::Client::Acquire(m_InterfacePortRef, SERVICE_CI_SYSTEM_RESOURCE_MANAGER);
	ci::servicestartupmanager::SSMContracts::stServiceStateBus ssmBus;
	ssmBus.serviceState = (SSMContracts::eServiceState)SSMContracts::eStarted;
	strcpy(ssmBus.sServiceName,SERVICE_CI_SYSTEM_RESOURCE_MANAGER);
	ssmBus.portID = m_InterfacePortRef->GetId();//MSGPORT_CI_SYSTEM_RESOURCE_MANAGER;
	ssmBus.iNextMessageInterval = 0;
	ssmBus.noOfReadyPendingServices = 0;
	ssmBus.iDataSchemaVersion = 1;
	ssmBus.iDataLength = 0;
	strcpy(ssmBus.data,"");
	//Status ret = ssmClient->Notify((SSMContracts::ecNotifications)SSMContracts::ecStartPending); 
	ret = m_ssmClient->SendStateNotification(ssmBus); 
	if (ret != STATUS_OK) 
	{ 
		DEBUGL1("SystemResourceManager:Params Failed to send eStarted messages to SSM\n"); 
		return STATUS_FAILED;
	} 
	
	//Send Ready Message to SSM
	ssmBus.serviceState = (SSMContracts::eServiceState)SSMContracts::eReady;
	strcpy(ssmBus.sServiceName,SERVICE_CI_SYSTEM_RESOURCE_MANAGER);
	ssmBus.portID = m_InterfacePortRef->GetId();
	ssmBus.iNextMessageInterval = 0;
	ssmBus.noOfReadyPendingServices = 0;
	ssmBus.iDataSchemaVersion = 1;
	ssmBus.iDataLength = 0;
	strcpy(ssmBus.data,"");
	DEBUGL7("SystemResourceManager: Sending ready message to SSM\n");			
	//if (ssmClient->Notify((SSMContracts::ecNotifications)SSMContracts::ecReady) != STATUS_OK)
	if(m_ssmClient->SendStateNotification(ssmBus) != STATUS_OK)
	{ 
		DEBUGL1("SystemResourceManager:Params:: Failed to send eReady messages to SSM\n"); 							
		return STATUS_FAILED;
	}
	else
	{
		DEBUGL1("SystemResourceManager:Params:: Sent eReady messages to SSM\n"); 
	}
	
	ci::servicestartupmanager::Client::WaitStartupSignal();
	while(bRunning)
	{
		
		DEBUGL6("SystemResourceManager: Waiting for incoming messages from UIController or DSM \n");
		MsgRef msg=new Msg;
		ret = m_InterfacePortRef->Receive(*msg); 

		if( STATUS_OK != ret )
		{
			DEBUGL1("SystemResourceManager: Message reception failed : status returned = %d\n",ret);
			continue;
		}
		
		msgId  =  msg->GetId();
		DEBUGL3("SystemResourceManager: Received Message Type=0x%x, id=0x%x, From %X\n", msg->GetType(), msgId, msg->GetSender());
		
		//process SSM messsage
		if(ci::CID_CI_SSM == ( msgId & CONTRACT_ID_MASK) )
		{
			DEBUGL7("SystemResourceManager: Recieve SSM message \n");
			ret = ProcessSSMMessages(msg);
			if( STATUS_OK != ret )			{
				DEBUGL1("SystemResourceManager: Process of SSM Messages failed for message 0x%x from SSM \n", msg->GetId() );
			}
			continue;
		}

		// handle requests (subscription, unsubscription, create and deleta quota 
		if(ci::CID_CI_SYSTEM_RESOURCE_MANAGEMENT == (msgId & CONTRACT_ID_MASK))
		{
			DEBUGL7("SystemResourceManager: Recieve SRM request from client \n");
			ret = ProcessSRMMessages(msg);
			if( STATUS_OK != ret )
			{
				DEBUGL1("SystemResourceManager: Process of SRM request failed for message 0x%x from client at port %d \n", msg->GetId(), msg->GetSender() );
			}
			continue;
		}
		
	}//while loop
	
	return STATUS_OK;
}

/* 
 * Method to preprocesses incomming SSM message as per SSM protocol  
 * @param  Msg	 		- Incoming message to be processed
 * @return Status		- Returns STATUS_OK on success and STATUS_FAILED on error  
 * @name ProcessSSMMessages
 */ 
Status CSystemResourceManager::ProcessSSMMessages(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::ProcessSSMMessages : Invalid msg Parameter\n");
      return STATUS_FAILED;
   }
	int32 msgId = msg->GetId();
	switch( msgId )
	{
		case ci::servicestartupmanager::SSMContracts::STOP:
		{
			DEBUGL1("CSystemResourceManager:Receive STOP message from SSM\n");
			//Got shut down message from SSM... So Send ShutDown Pwnding message back to SSM
			
			ci::servicestartupmanager::SSMContracts::stServiceStateBus stShutDownBus;
			stShutDownBus.serviceState = (SSMContracts::eServiceState)SSMContracts::eShuttingDownPending;
			strcpy(stShutDownBus.sServiceName,SERVICE_CI_SYSTEM_RESOURCE_MANAGER);
			stShutDownBus.portID = m_InterfacePortRef->GetId();
			stShutDownBus.iNextMessageInterval = 0;
			stShutDownBus.noOfReadyPendingServices = 0;
			stShutDownBus.iDataSchemaVersion = 1;
			stShutDownBus.iDataLength = 0;
			strcpy(stShutDownBus.data,"");
			DEBUGL1("SystemResourceManager:Sending shuttingdownpending to SSM\n");
			Status ret = m_ssmClient->SendStateNotification(stShutDownBus); 
			if (ret != STATUS_OK) 
			{ 
				DEBUGL1("SystemResourceManager:Receive Failed to send eShuttingDownPending messages to SSM\n"); 
				return STATUS_FAILED;
			}
			/* Dated: 24th April 2008
			 * TODO: SRM need to save the SRM DOM back into hard disk before sending shutdown to SSM
			 *  A generic method to do this may be provided by HDB interface. Need to do it once generic HDB IF 
			 * is available. For __NOW_, just like all other ebx components SRM will shutdown and will not flush the 
			 * SRM DOM back into the hard disk.
			 */  
			
			// After Sending ShutDown Messages to all the Subscribers, send Shutdown message to SSM
			stShutDownBus.serviceState = (SSMContracts::eServiceState)SSMContracts::eShuttingDown;
			strcpy(stShutDownBus.sServiceName,SERVICE_CI_SYSTEM_RESOURCE_MANAGER);
			stShutDownBus.portID = m_InterfacePortRef->GetId();
			stShutDownBus.iNextMessageInterval = 0;
			stShutDownBus.noOfReadyPendingServices = 0;
			stShutDownBus.iDataSchemaVersion = 1;
			stShutDownBus.iDataLength = 0;
			strcpy(stShutDownBus.data,"");
			DEBUGL1("SystemResourceManager:Sending shuttingdown to SSM\n");
			ret = m_ssmClient->SendStateNotification(stShutDownBus); 
			if (ret != STATUS_OK) 
			{ 
				DEBUGL1("SystemResourceManager:Receive Failed to send eShuttingDown messages to SSM\n"); 
				return STATUS_FAILED;
			}
			/*** SRM exits here ***/
			DEBUGL7("Before Exiting  \n");
			exit(0);//this is done because SSM requested to stop
			
		}
		break;
		default:
			DEBUGL2("SystemResourceManager:Receive Failed to send eShuttingDown messages to SSM\n"); 
			break;
			
	}
	return STATUS_OK;
}

Status CSystemResourceManager::ProcessSRMMessages(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg)
{
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::ProcessSRMMessages : Invalid msg Parameter\n");
      return STATUS_FAILED;
   }
	Status ret=STATUS_OK;
	int32 msgId = msg->GetId();
	switch( msgId )
	{
		case SystemResourceManager::Subscribe:
		{
			ret = HandleSubscriptionRequests(msg);
		}
		break;
		case SystemResourceManager::UnSubscribe:
		{
			ret = HandleUnSubscribeRequests(msg);
		}
		break;
		case SystemResourceManager::GetSizeInformation:
		{
			ret = HandleGetSizeInformationRequests(msg);
		}
		break;
		case SystemResourceManager::SetWaterMarkLevel:
		{
			ret = HandleSetWaterMarkLevelRequests(msg);
		}
		break;
		case SystemResourceManager::AllocateQuota:
		{
			ret = HandleAllocateQuotaRequests(msg);
		}
		break;
		case SystemResourceManager::DeallocateQuota:
		{
			ret = HandleDeallocateQuotaRequests(msg);
		}
		break;
		case SystemResourceManager::NotifyError:
		{
			ret = HandleNotifyErrorRequest(msg);
		}
		break;
		case SystemResourceManager::ResetErrorCount:
		{
			ret = HandleResetErrorCountRequest(msg);
		}
		break;
		case SystemResourceManager::StartSleepTimer:
		{
			ret = StartSleepTimer(msg);
		}
		break;
		case SystemResourceManager::StartDeepSleepTimer:
		{
			ret = StartDeepSleepTimer(msg);
		}
		break;
		case SystemResourceManager::StartEnergySaveTimer:
		{
			ret = StartEnergySaveTimer(msg);
		}
		break;
		case SystemResourceManager::StartReadyTimer:
		{
			ret = StartReadyTimer(msg);
		}
		break;
		case SystemResourceManager::StartWarmingUpTimer:
		{
			ret = StartWarmingUpTimer(msg);
		}
		break;
		case SystemResourceManager::StartPreviousTimer:
		{
			ret = StartPreviousTimer(msg);
		}
		break;
		case SystemResourceManager::StartPrintingTimer:
		{
			ret = StartPrintingTimer(msg);
		}
		break;
		case SystemResourceManager::StopPrintingTimer:
		{
			ret = StopPrintingTimer(msg);
		}
		break;
		case SystemResourceManager::StartScanningTimer:
		{
			ret = StartScanningTimer(msg);
		}
		break;
		case SystemResourceManager::StopScanningTimer:
		{
			ret = StopScanningTimer(msg);
		}
		break;
		case SystemResourceManager::StopTimer:
		{
			ret = StopTimer(msg);
		}
		break;
		//TODO : Other messages related to CPU/network, beyond the scope of CP3
		//TODO : Other messages related to CPU/network, beyond the scope of CP3
		default:
		DEBUGL2("SystemResourceManager:Receive invalid messages from client %d \n", msg->GetSender()); 
			break;
	}
	
	return ret;
}

/* Gets the information regarding a subscribed folder or file
*/

Status CSystemResourceManager::HandleGetSizeInformationRequests(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
	if(!msg)
	{
		DEBUGL1("CSystemResourceManager::HandleGetSizeInformationRequests : Invalid msg Parameter\n");
		SendOffNormalResponse(msg,"GetSizeInformation");
		return STATUS_FAILED;
	}
	char* msgContent = static_cast<char *>(msg->GetContentPtr());
	DocumentRef pRequestDoc = NULL;   //Refer to the SystemResourceManager.xsd for payload XML
	CString sDocID =getUniqueFileName("GetSizeInformation");
	if (m_pHDB->CreateTempDocument(sDocID,pRequestDoc,msgContent) != STATUS_OK)
	{ 
		DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: CreateTempDocument of subscription request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	if(!pRequestDoc)
	{
		DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: CreateTempDocument of subscription request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}

	Status ret=STATUS_OK;
	//List of nodes consists of storages
	NodeRef	pTotalSpaceNode,pSpaceUsedNode,pSpaceAvailableNode;
	Ref<NodeList> pListOfStorages = pRequestDoc->getDocumentElement()->getChildNodes();

	try {
		for (uint32 i = 0; i < pListOfStorages->getLength(); i++)
		{//loop until all storages in the subscription request are processed

			CString sSpaceThresholdLevel="", sPathName="";

			Ref<Node> pStorage = pListOfStorages->item(i);
			if (!pStorage || pStorage->getNodeType() != Node::ELEMENT_NODE)
				continue;
			//get 'Name'attribute of Storage 
			sPathName = ( ( dom::ElementRef )( pStorage ) )->getAttribute( "Name" );
			ModifyForPartitionNameChange(sPathName);
			if(sPathName.empty())
			{
				DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: Storage name is empty ! ..skipping to next storage in the list\n");
				continue;
			}

			CString sMountPoint="";
			CString sBindPath = "";
			bool IsInvalidpartition = false;
			if( !(m_storageRef->GetMountPoint(sPathName,sMountPoint) ) )
			{
				DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: GetMountPoint failed for %s \n",sPathName.c_str());
				CString mntPath=sPathName.substr(0,sPathName.find('/',2));
				sBindPath = "SystemResources/Storage[@Name='" + mntPath + "']/Directory[@Name='" + sPathName+ "']";
				IsInvalidpartition = true;
			}
			else
			{
				if(sPathName.compare(sMountPoint) == 0)
				{  //is set for storage (partition itself) 
					sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']";
				}
				else
				{	
					if(m_storageRef->IsThisDirectoryPresent(sPathName))
						sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/Directory[@Name='" + sPathName+ "']";
					else
						sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/File[@Name='" + sPathName + "']";
				}
			}


			NodeRef	pDOMRootNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath);
			if(!pDOMRootNode)
			{
				DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: BindToElement failed for [%s]...skipping storage \n",sBindPath.c_str());
				// Append 'Status' with value STATUS_FAIL for mount point not found 
				
				AddNewNode(pStorage,"Status",(IsInvalidpartition?"STATUS_OK":"STATUS_FAIL"));
				continue;
			}
			DEBUGL7("SystemResourceManager:HandleGetSizeInformationRequests: sBindPath = [%s] \n", sBindPath.c_str());
			//get the total space from DOM
			chelper::GetNode(pDOMRootNode,"TotalSpace",pTotalSpaceNode);
			if(!pTotalSpaceNode)
			{
				DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: GetNode for totalspace failed...skipping storage [%s] \n",sPathName.c_str());
				// Append 'Status' with value STATUS_FAIL ,GetNode for totalspace failed
				AddNewNode(pStorage,"Status","STATUS_FAIL");
				continue;
			}
			AddNewNode(pStorage,"TotalSpace",pTotalSpaceNode->getTextContent());

			//get current SpaceUsed and spaceavailable and compute them again , if there is change then update them into DOM
			chelper::GetNode(pDOMRootNode,"SpaceUsed",pSpaceUsedNode);
			if(!pSpaceUsedNode)
			{
				DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: GetNode for SpaceUsed failed...skipping storage [%s] \n",sPathName.c_str());
				// Append 'Status' with value STATUS_FAIL ,GetNode for SpaceUsed failed
				AddNewNode(pStorage,"Status","STATUS_FAIL");
				continue;
			}

			chelper::GetNode(pDOMRootNode,"SpaceAvailable",pSpaceAvailableNode);
			if(!pSpaceAvailableNode) 
			{
				DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: GetNode for SpaceAvailable failed...skipping storage [%s] \n",sPathName.c_str());
				// Append 'Status' with value STATUS_FAIL ,GetNode for SpaceAvailable failed
				AddNewNode(pStorage,"Status","STATUS_FAIL");
				continue;
			}
			uint64 ullTotalSpace = 0;
			uint64 ullSpaceAvailable = 0;
			uint64 ullNewSpaceUsed = 0;
			if(!IsInvalidpartition)
			{
				//compute the space used 
				ullNewSpaceUsed = m_storageRef->GetFileOrDirectorySize(sPathName.c_str());
				//uint64 ullSpaceUsed = m_storageRef->StringToUint64(pSpaceUsedNode->getTextContent());

				ullTotalSpace = m_storageRef->StringToUint64(pTotalSpaceNode->getTextContent());
				ullSpaceAvailable = m_storageRef->GetAvailableSize(sPathName.c_str());
				if(ullTotalSpace<ullNewSpaceUsed)
				{
					ullSpaceAvailable=0;
				}
				else if((ullTotalSpace-ullNewSpaceUsed)<ullSpaceAvailable)
				{
					ullSpaceAvailable = ullTotalSpace - ullNewSpaceUsed;
				}
				//uint64 ullSpaceAvailable=(ullTotalSpace>ullNewSpaceUsed)?(ullTotalSpace - ullNewSpaceUsed):0;
				//spaceused has changed, so update SpaceAvailable in SRM DOM too

			}
			m_storageRef->UpdateSize(pSpaceUsedNode,"SpaceUsed",ullNewSpaceUsed);
			m_storageRef->UpdateSize(pSpaceAvailableNode,"SpaceAvailable",ullSpaceAvailable);

			AddNewNode(pStorage,"SpaceUsed",pSpaceUsedNode->getTextContent());

			AddNewNode(pStorage,"SpaceAvailable",pSpaceAvailableNode->getTextContent());

			//release the nodes
			pSpaceUsedNode=pTotalSpaceNode=pSpaceAvailableNode=pDOMRootNode=NULL;
			// Append 'Status' with value STATUS_OK for normal response.
			AddNewNode(pStorage,"Status","STATUS_OK");
			IsInvalidpartition = false;

		}//end of loop for storages
	} catch (DOMException except) {
		DEBUGL1("CSystemResourceManager::HandleGetSizeInformationRequests : Failed due to HDB Exception\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	//send subscribe response
	CString  sResponse="";
	ret = m_pHDB->Serialize(pRequestDoc,sResponse);
	if( ret == STATUS_OK )
	{
		ret = SendResponse(msg,sResponse, sDocID);
	}
	else
	{
		if(ret == STATUS_DISK_FULL)
		{
			DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
		}
		DEBUGL1("SystemResourceManager:HandleGetSizeInformationRequests: Serialize operation failed while processing Subscribe request\n");
		ret = SendOffNormalResponse(msg,sDocID);
	}

	return ret;
}

Status CSystemResourceManager::HandleSetWaterMarkLevelRequests(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::HandleSetWaterMarkLevelRequests : Invalid msg Paramter\n");
		SendOffNormalResponse(msg,"SetWaterMarkLevel");
      return STATUS_FAILED;
   }
	char* msgContent = static_cast<char *>(msg->GetContentPtr());
	DocumentRef pRequestDoc = NULL;   //Refer to the SystemResourceManager.xsd for payload XML
	CString sDocID = getUniqueFileName("SetWaterMarkLevel");
	if (m_pHDB->CreateTempDocument(sDocID,pRequestDoc,msgContent) != STATUS_OK)
	{ 
		DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: CreateTempDocument of subscription request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	if(!pRequestDoc)
	{
		DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: CreateTempDocument of subscription request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	
	//List of nodes consists of storages
	Ref<NodeList> pListOfStorages = pRequestDoc->getDocumentElement()->getChildNodes();
   if(!pListOfStorages)
   {
      DEBUGL1("CSystemResourceManager::HandleSetWaterMarkLevelRequests : Empty Child Nodes\n");
		SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
   try {
	for (uint32 i = 0; i < pListOfStorages->getLength(); i++)
	{//loop until all storages in the subscription request are processed
		CString sSpaceThresholdLevel="", sPathName="";
		Ref<Node> pStorage = pListOfStorages->item(i);
		if (!pStorage || pStorage->getNodeType() != Node::ELEMENT_NODE)
			continue;
		//get 'Name'attribute of Storage 
		sPathName = ( ( dom::ElementRef )( pStorage ) )->getAttribute( "Name" ); 
		ModifyForPartitionNameChange(sPathName);
      if(sPathName.empty())
		{
			DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: Storage name is empty ! ..skipping to next storage in the list\n");
			continue;
		}
		Ref<NodeList> pListOfNodes = pStorage->getChildNodes();
		for(uint32 j = 0; j < pListOfNodes->getLength(); j++)
		{
			Ref<Node> pNode = pListOfNodes->item(j);
			if (!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
				continue;
			
			//where commonElement can be Storage node or CPU node or Network node , cpu and network will be implemented future phases, depends on the need.
			ElementRef commonElement = pNode;  
			/* get the Threshold level for this storage,
			 * INFO: What is threshold level or HighWaterMarkLevel  ?
			 *       It is threshold level of space , when space used in this storage reaches a level that equal to or greater than threshold level,the SRM
			 * will send an 'HighWaterMarkLevelReached' event to subscribers of this event.  
			 * Default value is 80 ,=> indicates when 80% of max space allocated is reached , this event will be triggered
			 */ 
			if(commonElement->getNodeName()=="HighWaterMarkLevel")
			{	
				sSpaceThresholdLevel = commonElement->getTextContent();
			}

		}//end of loop for storage child nodes

		if( !sSpaceThresholdLevel.empty()) // have got a heigh watermark level to be set 
		{

			CString sMountPoint="";
			if( !(m_storageRef->GetMountPoint(sPathName,sMountPoint) ) )
		 	{
		 		DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: GetMountPoint failed for %s \n",sPathName.c_str());
				AddNewNode(pStorage,"Status","STATUS_INVALID_PATH");
		 		continue;
		 	}
			
			CString sBindPath = "";
			if(sPathName.compare(sMountPoint) == 0)
			{//water mark level is set for storage (partition itself 
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']";
			}
			else
			{	if(m_storageRef->IsThisDirectoryPresent(sPathName))
					sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/Directory[@Name='" + sPathName.c_str() + "']";
				else
					sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/File[@Name='" + sPathName.c_str() + "']";
			}
			DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: bindpath is = %s \n",sBindPath.c_str());

			NodeRef	pStartNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath);
			if(!pStartNode) 
			{ 
				DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: BindToElement failed ..skipping to next storage\n"); 
				AddNewNode(pStorage,"Status","STATUS_FAILED");
				continue;
			}
			NodeRef	pWaterMarkLevelNode;
								
			chelper::GetNode(pStartNode,"WaterMarkLevel",pWaterMarkLevelNode);
			if(!pWaterMarkLevelNode) 
			{ 
				DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: GetNode call failed for pWaterMarkLevelNode..skipping to next storage\n");
				AddNewNode(pStorage,"Status","STATUS_FAILED");
				continue;
			}
			
			DEBUGL7("SystemResourceManager:HandleSetWaterMarkLevelRequests: sSpaceThresholdLevel=[%s]\n",sSpaceThresholdLevel.c_str());
			if(pWaterMarkLevelNode && pStartNode) //if valid
			{
				//update sSpaceThresholdLevel in the SRM DOM respository
				pWaterMarkLevelNode->setTextContent(sSpaceThresholdLevel);
				CString sWaterMarkLevel = pWaterMarkLevelNode->getTextContent();
				DEBUGL7("SystemResourceManager:HandleSetWaterMarkLevelRequests: Updated WaterMarkLevel with value = [%s] \n",sWaterMarkLevel.c_str());
				/* After setting the water mark level , compute the space used 
				 * and see if the level is reached, if yes then send 'HighWaterMarkLevelReached' to 
				 * all subscribers. 
				 */ 
				VerifyWaterMarkLevelNSendEvent(pStartNode,sWaterMarkLevel);
				
			}
			// Append 'Status' with value STATUS_OK for normal response.
			AddNewNode(pStorage,"Status","STATUS_OK");
			//release the nodes
			pWaterMarkLevelNode=pStartNode=NULL;
		}
	}//end of loop for storages
   } catch (DOMException except) {
      DEBUGL1("CSystemResourceManager::HandleSetWaterMarkLevelRequests : Failed due to HDB Exception\n");
		SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	//send subscribe response
	CString  sResponse="";
	Status ret=STATUS_OK;
	ret = m_pHDB->Serialize(pRequestDoc,sResponse);
	if( ret == STATUS_OK )
	{
		ret = SendResponse(msg,sResponse, sDocID);
	}
	else
	{
		if(ret == STATUS_DISK_FULL)
		{	
			DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
		}
		DEBUGL1("SystemResourceManager:HandleSetWaterMarkLevelRequests: Serialize operation failed while processing Subscribe request\n");
		ret = SendOffNormalResponse(msg,sDocID);
	}
	
	return ret;
}

void CSystemResourceManager::VerifyWaterMarkLevelNSendEvent(NodeRef pStorageNode, CString sWaterMarkLevel)
{
	try { 
   /* Verify the new watermarklevel is reached, if yes, then send events to subscribers */
	NodeRef	pTotalSpaceNode, pSpaceUsedNode;
	chelper::GetNode(pStorageNode,"TotalSpace",pTotalSpaceNode);
	chelper::GetNode(pStorageNode,"SpaceUsed",pSpaceUsedNode);
	if(!pTotalSpaceNode || !pSpaceUsedNode) 
	{ 
		DEBUGL1("SystemResourceManager:VerifyWaterMarkLevelNSendEvent: GetNode call failed for TotalSpace/SpaceUsed...cannot verify watermarklevel\n"); 
		return;
	}
	
	CString sTotalSpace = pTotalSpaceNode->getTextContent();
	CString sSpaceUsed = pSpaceUsedNode->getTextContent();
	uint64 ullSpaceUsed = 0, ullTotalSpace = 0;
	int iWaterMarkLevel=0, iNewWaterMarkLevel=0;
	if(!sSpaceUsed.empty() && !sTotalSpace.empty() )
	{
		ullTotalSpace = m_storageRef->StringToUint64(sTotalSpace);
		ullSpaceUsed = m_storageRef->StringToUint64(sSpaceUsed);
		iWaterMarkLevel = m_storageRef->StringToUint64(sWaterMarkLevel);
	}

//	iNewWaterMarkLevel = (ullSpaceUsed/ullTotalSpace)*100;
//20081126 Riyasudeenkhan start change	
	if(ullTotalSpace != 0)
   {
      iNewWaterMarkLevel = (ullSpaceUsed*100)/ullTotalSpace;
   //20081126 Riyasudeenkhan end change	
	   if(iNewWaterMarkLevel >= iWaterMarkLevel)
	   {//water mark level is reached , so send events
		   SendWaterMarkLevelReachedEvent(((dom::ElementRef)(pStorageNode))->getAttribute("Name"));
	   }
   }
   } catch (DOMException except) {
      DEBUGL1("SystemResourceManager::VerifyWaterMarkLevelNSendEvent : Failed due to HDB Exception\n");
   }
	
}

int  CSystemResourceManager::GetConvertedTriBooleanValue(CString sValue) 
{
	int bTriBooleanValue;
	
	if(sValue == "true" )
	{
		bTriBooleanValue = 1;
	}
	else if (sValue == "false")
	{
		bTriBooleanValue = 0;
	}
	else
	{
		bTriBooleanValue = sValue.empty() ? -1 : atoi(sValue.c_str());
	}

	DEBUGL7("CSystemResourceManager:GetConvertedTriBooleanValue: String value = %s , converted value = %d \n",sValue.c_str(), bTriBooleanValue);

	return bTriBooleanValue;
}


Status CSystemResourceManager::HandleSubscriptionRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg)
{
   if(!msg)
   {
	   DEBUGL1("CSystemResourceManager::HandleSubscriptionRequests : Invalid msg Parameter\n");
		SendOffNormalResponse(msg,"Subscribe");
      return STATUS_FAILED;
   }
   char* msgContent = static_cast<char *>(msg->GetContentPtr());
	DocumentRef pSubscribeDoc = NULL;   //Refer to the SystemResourceManager.xsd for payload XML
	CString sDocID = getUniqueFileName("Subscribe");
	// Read the request message 
	if (m_pHDB->CreateTempDocument(sDocID,pSubscribeDoc,msgContent) != STATUS_OK)
	{ 
		DEBUGL1("SystemResourceManager:HandleSubscriptionRequests: CreateTempDocument of subscription request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	if(!pSubscribeDoc)
	{
		DEBUGL1("SystemResourceManager:HandleSubscriptionRequests: CreateTempDocument of subscription request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	
	
	//Find the List of nodes consists of storages from the incoming message 
	Ref<NodeList> pListOfStorages = pSubscribeDoc->getDocumentElement()->getChildNodes();
   if(!pListOfStorages)
   {
      DEBUGL1("CSystemResourceManager::HandleSubscriptionRequests : Empty Node List\n");
		SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
   try {
	for (uint32 i = 0; i < pListOfStorages->getLength(); i++)
	{//loop until all storages in the subscription request are processed
		int32 bSizeChange=0,bCreate=0,bWrite=0,bDelete=0;
		CString sEventName="", sPortNo="", sPathName="";
		
		Ref<Node> pStorage = pListOfStorages->item(i);
		if (!pStorage || pStorage->getNodeType() != Node::ELEMENT_NODE)
			continue;
		
		if( pStorage->getNodeName() != "Storage")
		{
			DEBUGL2("SystemResourceManager:HandleSubscriptionRequests: Expected storage node but found some other node [%s]\n",\
						(pStorage->getNodeName().c_str()));
			DEBUGL2("SystemResourceManager:HandleSubscriptionRequests: Skipping to node in the request\n");
			continue;
		}
		//get 'Name'attribute of Storage 
		sPathName = ( ( dom::ElementRef )( pStorage ) )->getAttribute( "Name" );
      ModifyForPartitionNameChange(sPathName);
		DEBUGL7("CSystemResourceManager:HandleSubscriptionRequests: Msg contains Storage name = %s \n",sPathName.c_str());

		// if the path is not there skip it 
	
		CString sMountPoint=""; // this will go as the storage name 
	
		if( !(m_storageRef->GetMountPoint(sPathName,sMountPoint) ) )
		{
			DEBUGL1("SystemResourceManager:HandleSubscriptionRequests: GetMountPoint failed for %s \n",sPathName.c_str());
			AddNewNode(pStorage,"Status", "STATUS_INVALID_PATH");
			continue;
		}
		CString sBindPath = "";
		if(sPathName.compare(sMountPoint) == 0)
		{//is set for storage (partition itself) 
			sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']";
		}
		else
		{	
			if(m_storageRef->IsThisDirectoryPresent(sPathName))
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/Directory[@Name='" + sPathName+ "']";
			else
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/File[@Name='" + sPathName + "']";
		}

		NodeRef pDOMRootNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath); // check whether this is already present in the tree
		if (!pDOMRootNode)
		{
			DEBUGL1("SystemResourceManager:HandleSubscriptionRequests: BindToElement failed for [%s] \n",sBindPath.c_str());
			AddNewNode(pStorage,"Status", "STATUS_INVALID_PATH");
			continue;
		}
		//TODO: Remove this comented block
		/*
		{
			if (!pDOMRootNode) // the storage is not there 
			{
				pDOMRootNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath,true); //create the path 
			}
		
			// ok done with the node creation 

			AddNewNode(pDOMRootNode,"TotalSpace","5000"); // have to get actual avalues 
			AddNewNode(pDOMRootNode,"SpaceUsed","2000"); // have to get actual avalues 
			AddNewNode(pDOMRootNode,"WaterMarkLevel","50");// have to get actual avalues 
			pDOMRootNode = AppendElementNode(pDOMRootNode,"Event");
			AddNewNode(pDOMRootNode,"DirectoryDeletedSelf","false");// have to get actual avalues 

			// ok done with xpath altogether 
			pDOMRootNode = NULL;
		
		}
		*/

		Ref<NodeList> pListOfNodes = pStorage->getChildNodes();
		for(uint32 j = 0; j < pListOfNodes->getLength(); j++)
		{
			bSizeChange=bCreate=bWrite=bDelete=-1; //purposely prefixed with  'b' to indicate these are used as boolean with an extra special value '-1'
			Ref<Node> pNode = pListOfNodes->item(j);
			if (!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
				continue;
			
			//get the absolute path  ( Note: if event is for file, then path will include the name of that file)
			ElementRef commonElement = pNode;  //where commonElement can be Storage node or CPU node or Network node , cpu and network will be implemented future phases, depends on need
			//printf("======> commonElement->getNodeName()= [%s]\n",(commonElement->getNodeName()).c_str());
			//get the optional port
			if(commonElement->getNodeName()=="Port")
			{	
				sPortNo = commonElement->getTextContent();
				DEBUGL7("CSystemResourceManager:HandleSubscriptionRequests: Msg contains Port = %s \n",sPortNo.c_str());
			}
			
			if(commonElement->getNodeName()=="Events")
			{	
				Ref<NodeList>	pListOfEventNodes = pNode->getChildNodes();
				for(uint32 ie = 0; ie < pListOfEventNodes->getLength(); ie++)
				{
					Ref<Node> pEventNode = pListOfEventNodes->item(ie);
					if (!pEventNode || pEventNode->getNodeType() != Node::ELEMENT_NODE)
					continue;
					ElementRef pEventElement = pEventNode;
					CString sTmpStr = "";
/*					if(pEventElement->getNodeName()=="SizeChange")
					{
						sTmpStr = pEventElement->getTextContent();
						bSizeChange = sTmpStr.empty() ? -1 : atoi(sTmpStr.c_str());
						DEBUGL7("CSystemResourceManager:HandleSubscriptionRequests: Msg contains SizeChange = %s \n",sPortNo.c_str());
					}
*/					
					//get the value of the node
					sTmpStr = pEventElement->getTextContent();
					if((pEventElement->getNodeName()=="DirectoryDeleted") || (pEventElement->getNodeName()=="FileDeleted") )
					{
						bDelete = GetConvertedTriBooleanValue(sTmpStr);
					}
					else if(pEventElement->getNodeName()=="FileCreated")
					{
						bCreate = GetConvertedTriBooleanValue(sTmpStr);
					}
					else if(pEventElement->getNodeName()=="WriteOperation")
					{
						bWrite = GetConvertedTriBooleanValue(sTmpStr);
					}
				}
			}//Event for loop
		}//end of loop for storage child nodes
		Status ret=STATUS_OK;
		if(!sPathName.empty())
		{
			int32  port=0x0; //default value, when port is not mentioned
			if(!sPortNo.empty())
			{
				port = atoi(sPortNo.c_str());
			}
			else
			{//no port mention, they use the Subscriber port
				port = msg->GetSender();
				DEBUGL7("SystemResourceManager:HandleSubscriptionRequests: No port found in request, hence using subscriber port=%d \n",port);
			}
			
			if(sEventName.empty()) 
			{ //empty means subscribe to all events based on file and directory of the path
				uint32 uWatchFlags=SRM_SIZE_CHANGE_EVENT|SRM_FILE_CREATE_EVENT|SRM_FILE_WRITE_EVENT|SRM_FILE_DELETE_EVENT;
				ret = AddToWatchList(sPathName,port,uWatchFlags);	
			}
			else
			{
				uint32 uWatchFlags=0x00;
				if(bSizeChange != -1) 
				{
					SetOrResetEvent(uWatchFlags, bSizeChange, SRM_SIZE_CHANGE_EVENT);
				}
				
				if(bCreate != -1 )
				{
					SetOrResetEvent(uWatchFlags, bCreate, SRM_FILE_CREATE_EVENT);
				}
					
				if(bWrite != -1) 
				{
					SetOrResetEvent(uWatchFlags, bWrite, SRM_FILE_WRITE_EVENT);
				}
				
				if(bDelete != -1 )
				{
					SetOrResetEvent(uWatchFlags, bDelete, SRM_FILE_DELETE_EVENT);
				}
				// this is to tell the os to watch the path 
				ret = AddToWatchList(sPathName,port,uWatchFlags);
			}
			//Update the DOM if required and 
			//Add the return status along with response  
			if(STATUS_OK==ret)
				{
					DEBUGL2("SystemResourceManager:HandleSubscriptionRequests: AddToWatchList returned STATUS_OK \n");
					//Append 'Status' with value STATUS_OK for off normal response.
					AddNewNode(pStorage,"Status", "STATUS_OK");
				}
			else if (STATUS_INVALID_PATH == ret)
				{
					//Append 'Status' with value STATUS_INVALID_PATH for Mount point failure 
					DEBUGL2("SystemResourceManager:HandleSubscriptionRequests: AddToWatchList returned STATUS_INVALID_PATH \n");
					AddNewNode(pStorage,"Status", "STATUS_INVALID_PATH");
				}
			else{
					//Append 'Status' with value STATUS_FAILED for off normal response.
					DEBUGL2("SystemResourceManager:HandleSubscriptionRequests: AddToWatchList returned STATUS_FAILED \n");
					AddNewNode(pStorage,"Status", "STATUS_FAILED");
				}
		}
		else
		{
			DEBUGL2("SystemResourceManager:HandleSubscriptionRequests: storage name is empty, hence sending off normal response with status=STATUS_INVALID_PATH\n");
			//Append 'Status' with value STATUS_INVALID_PATH for not finding the path 
			AddNewNode(pStorage,"Status","STATUS_INVALID_PATH");
		}
		
	}//end of loop for storages
   } catch (DOMException except) {
      DEBUGL1("SystemResourceManager:HandleSubscriptionRequests: Failed due to HDB Exception\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	//send subscribe response
	CString  sSubscribeResponse="";
	Status ret=STATUS_OK;
	ret = m_pHDB->Serialize(pSubscribeDoc,sSubscribeResponse);
	if( ret == STATUS_OK )
	{
		ret = SendResponse(msg,sSubscribeResponse, sDocID);
	}
	else
	{
		if(ret == STATUS_DISK_FULL)
		{
			DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
		}
		DEBUGL1("SystemResourceManager:HandleSubscriptionRequests: Serialize operation failed while processing Subscribe request\n");
		ret = SendOffNormalResponse(msg,sDocID);
	}
	return ret;
}
/*
 *Unsubscribe from Systemresourcemanager client list to notify for a particular event 
 *If the Unsubscripion is successful SRM will not send any further notification to the client
*/

Status CSystemResourceManager::HandleUnSubscribeRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg)
{
   if(!msg)
   {
      DEBUGL1("SystemResourceManager::HandleUnSubscribeRequests : Invalid msg Parameter\n");
      return STATUS_FAILED;
   }
	char* msgContent = static_cast<char *>(msg->GetContentPtr());
	DocumentRef pUnSubscribeDoc = NULL;   //Refer to the SystemResourceManager.xsd for payload XML
	CString sDocID = getUniqueFileName("unSubscribe");
	if (m_pHDB->CreateTempDocument(sDocID,pUnSubscribeDoc,msgContent) != STATUS_OK)
	{ 
		DEBUGL1("SystemResourceManager:HandleUnSubscribeRequests: CreateTempDocument of unsubscribe request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	if(!pUnSubscribeDoc)
	{
		DEBUGL1("SystemResourceManager:HandleUnSubscribeRequests: CreateTempDocument of unsubscribe request failed !\n");
		SendOffNormalResponse(msg,sDocID);
		return STATUS_FAILED;
	}
	
	//List of nodes consists of storages
	Ref<NodeList> pListOfStorages = pUnSubscribeDoc->getDocumentElement()->getChildNodes();
   if(!pListOfStorages)
   {
      DEBUGL1("SystemResourceManager::HandleUnSubscribeRequests : Empty Child Nodes\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
   try {
	for (uint32 i = 0; i < pListOfStorages->getLength(); i++)
	{//loop until all storages in the subscription request are processed
		//int32 bSizeChange,bCreate,bWrite,bDelete;
		CString sPathName="";
		CString sPortNo="";
		Ref<Node> pStorage = pListOfStorages->item(i);
		if (!pStorage || pStorage->getNodeType() != Node::ELEMENT_NODE)
			continue;
		if(pStorage->getNodeName() == "Storage")
		{
			//get 'Name'attribute of Storage 
			sPathName = ( ( dom::ElementRef )( pStorage ) )->getAttribute( "Name" );
         ModifyForPartitionNameChange(sPathName);
		}
		else
		{
			DEBUGL2("SystemResourceManager:HandleUnSubscribeRequests: Expected storage node but found some other node [%s]\n",\
					(pStorage->getNodeName().c_str()));
			DEBUGL2("SystemResourceManager:HandleUnSubscribeRequests: Skipping to node in the request\n");
			continue;
		}
		
		Ref<NodeList> pListOfNodes = pStorage->getChildNodes();
		for(uint32 j = 0; j < pListOfNodes->getLength(); j++) 
		{
			Ref<Node> pNode = pListOfNodes->item(j);
			if (!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
				continue;
			
			ElementRef commonElement = pNode;  //where commonElement can be Storage node or CPU node or Network node , cpu and network will be implemented future phases, depends on need
			/* sPathName is already present in Name 
			if(commonElement->getNodeName()=="Path")
			{	
				//get the absolute path  ( Note: if event is for file, then path will include the name of that file)
				sPathName = commonElement->getTextContent();
			}
			*/
			//get the optional port
			if(commonElement->getNodeName()=="Port")
			{	
				sPortNo = commonElement->getTextContent();
			}
			
		}//end of loop for storage child nodes
		if(!sPathName.empty())
		{
			Status ret=STATUS_OK;
			uint16 port=-1;
			if(!sPortNo.empty())
			{
				port = atoi(sPortNo.c_str());
			}
			 //unsubscribe all events of input file/directory
			uint32 uWatchFlags=0x00;
			SetOrResetEvent(uWatchFlags, 0, SRM_SIZE_CHANGE_EVENT);
			SetOrResetEvent(uWatchFlags, 0, SRM_FILE_CREATE_EVENT);
			SetOrResetEvent(uWatchFlags, 0, SRM_FILE_WRITE_EVENT);
			SetOrResetEvent(uWatchFlags, 0, SRM_FILE_DELETE_EVENT);
			ret = RemoveFromWatchList(sPathName);
			// Append 'Status' with value STATUS_OK for normal response.
			NodeRef pStatusNode = chelper::AppendElementNode(pStorage,"Status");
			if (pStatusNode)
			{
				if(STATUS_OK==ret)
					{
						chelper::AppendTextNode(pStatusNode,"STATUS_OK");
					}
			
				else{
						//Append 'Status' with value STATUS_FAILED for off normal response.
						DEBUGL2("SystemResourceManager:HandleUnSubscribeRequests: RemoveFromWatchList returned STATUS_FAILED \n");
						chelper::AppendTextNode(pStatusNode,"STATUS_FAILED");
					}
			}

		}
		else
		{//attribut 'Name' is empty
			DEBUGL1("SystemResourceManager:HandleUnSubscribeRequests: Storage attribute 'Name' is empty or not found ..skipping to next storage\n");
			//Append 'Status' with value STATUS_FAILED for off normal response.
			NodeRef pStatusNode = chelper::AppendElementNode(pStorage,"Status");
			if (pStatusNode)
			{
				//Append 'Status' with value STATUS_INVALID_PATH for Mount point failure 
				chelper::AppendTextNode(pStatusNode,"STATUS_INVALID_PATH");
			}
		}
	}//end of loop for storages
   } catch (DOMException except) {
      DEBUGL1("SystemResourceManager::HandleUnSubscribeRequests : Failed due to HDB Exception\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	//send subscribe response
	CString  sUnSubscribeResponse="";
	Status ret=STATUS_OK;
	ret = m_pHDB->Serialize(pUnSubscribeDoc,sUnSubscribeResponse);
	if( ret == STATUS_OK )
	{
		ret = SendResponse(msg,sUnSubscribeResponse, "Subscribe");
	}
	else
	{
		if(ret == STATUS_DISK_FULL)
		{
			DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
		}
		DEBUGL1("SystemResourceManager:HandleUnSubscribeRequests: Serialize operation failed while processing Subscribe request\n");
		ret = SendOffNormalResponse(msg,"Subscribe");
	}
	return ret;
}
Status CSystemResourceManager::SendResponse(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg,CString sResponse, CString sMsgName)
{
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::SendResponse : Invalid msg Paramter\n");
      return STATUS_FAILED;
   }
   size_t findPos = sMsgName.find_first_of("_");
   if(findPos!=string::npos)
      sMsgName = sMsgName.substr(findPos);
	// The size of the message should be +1 the content to accomodate the remination character 
	Msg mReply = msg->Reply(sResponse.c_str(),sResponse.size()+1);
	if(m_InterfacePortRef->Send(mReply) != STATUS_OK)		
	{
		DEBUGL1("SystemResourceManager:SendResponse:Failed to send %s response\n", sMsgName.c_str());
		return STATUS_FAILED;
	}
	DEBUGL7("SystemResourceManager:SendResponse:Sent payload is [%s] \n", sResponse.c_str());
	return STATUS_OK;
}

Status CSystemResourceManager::SendOffNormalResponse(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg,CString sMsgName)
{
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::SendOffNormalResponse : Invalid msg Parameter\n");
      return STATUS_FAILED;
   }
	Msg mReply = msg->ReplyOffNormal();
   size_t findPos = sMsgName.find_first_of("_");
   if(findPos!=string::npos)
      sMsgName = sMsgName.substr(findPos);
	if(m_InterfacePortRef->Send(mReply) != STATUS_OK)		
	{
		DEBUGL1("SystemResourceManager:SendResponse:Failed to send %s off normal response\n", sMsgName.c_str());
		return STATUS_FAILED;
	}
	DEBUGL7("SystemResourceManager:SendResponse:Sent %s off normal response \n", sMsgName.c_str());
	return STATUS_OK;
}


void  CSystemResourceManager::SetOrResetEvent(uint32& uWatchFlags, int32 input, uint32 mask)//arg 1 (uWatchFlags) will be fill by this method
{
	if(input == 1) //NOTE: value of input must always be 1 or 0 for this method !
		uWatchFlags |= mask ;
	else
		uWatchFlags &= ~(mask);
	return;
}

/* IMPORTANT NOTE: AllocateQuota will just test storage/directory/file and add it to SRM dom. SRM will _NOT_
 * monitor the newly added storage/directory/file until (at least) one client subscribes for events fo this new storage/directory/file
 */
Status CSystemResourceManager::HandleAllocateQuotaRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg)
{
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::HandleAllocateQuotaRequests : Invalid msg Paramter\n");
      return STATUS_FAILED;
   }
	char* msgContent = static_cast<char *>(msg->GetContentPtr());
	DocumentRef pDoc = NULL;
	CString sDocID = getUniqueFileName("AllocateQuota");
	if (m_pHDB->CreateTempDocument(sDocID,pDoc, msgContent) != STATUS_OK)
	{ 
		DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: create temp document using message content [%s] failed \n",msgContent);
		return STATUS_FAILED;
	}
	NodeListRef pListOfStorageNodes = pDoc->getDocumentElement()->getChildNodes();
   if(!pListOfStorageNodes)
   {
      DEBUGL1("SystemResourceManager::HandleAllocateQuotaRequests : Empty Child Nodes\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	//loop through list of 'Quota' nodes
   try {
	for (uint32 i = 0; i < pListOfStorageNodes->getLength(); i++)
	{//Storage loop
		CString sPathName="";
		CString sStorageName="";
		Ref<Node> pStorageNode = pListOfStorageNodes->item(i);
		if (!pStorageNode || pStorageNode->getNodeType() != Node::ELEMENT_NODE)
			continue;
		ElementRef pStorageElement = pStorageNode;
		if(pStorageElement->getNodeName() !="Storage" )
		{
			DEBUGL2("SystemResourceManager:HandleAllocateQuotaRequests: Expected storage node but found some other node [%s]\n",\
					(pStorageNode->getNodeName().c_str()));
			DEBUGL2("SystemResourceManager:HandleAllocateQuotaRequests: Skipping to node in the request\n");
			continue;
		}
		//get 'Name'attribute of Storage from the incoming message 
		sStorageName = pStorageElement->getAttribute( "Name" );
      ModifyForPartitionNameChange(sStorageName);
		DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: Msg contains Storage name = %s \n",sStorageName.c_str());
		CString	sTotalSpace="",sSpaceUsed="",sSpaceAvailable="",sPort="",sHighWaterMarkLevel="";
		Ref<NodeList> pListOfNodes = pStorageElement->getChildNodes();
		for(uint32 j = 0; j < pListOfNodes->getLength(); j++)
		{
			Ref<Node> pNode = pListOfNodes->item(j);
			if (!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
				continue;
			
			//get the absolute path  ( Note: if event is for file, then path will include the name of that file)
			ElementRef commonElement = pNode; 
			//int iPercentageAvailable=0x0,
			uint64 uTotalSpace,uSpaceUsed,uSpaceAvailable;
			
			//get the optional port
			if(commonElement->getNodeName()=="MaxSize")
			{
				sTotalSpace = commonElement->getTextContent();
				if((sTotalSpace.compare("0") == 0) || sTotalSpace.empty())
				{//then get the total size from File system 
					if(STATUS_OK != m_storageRef->GetPartitionSizeInfo(sStorageName,sTotalSpace,sSpaceUsed,sSpaceAvailable))
					{
						DEBUGL1("SystemResourceManager:HandleCreateQuotaRequests: Error in GetPartitionSizeInfo() ...so skipping to next storage\n");
						continue;
					}
					uTotalSpace = m_storageRef->StringToUint64(sTotalSpace);
					DEBUGL7("SystemResourceManager:HandleCreateQuotaRequests: GetPartitionSizeInfo returned [%s] for totalspace\n", sTotalSpace.c_str());

									
				}
				else
				{
					uTotalSpace = atoi(sTotalSpace.c_str());
				}
				uSpaceUsed =  m_storageRef->GetFileOrDirectorySize(sStorageName.c_str());
				sSpaceUsed = m_storageRef->Uint64ToString(uSpaceUsed);
				
				uSpaceAvailable = uTotalSpace - uSpaceUsed;
				sSpaceAvailable = m_storageRef->Uint64ToString(uSpaceAvailable);
				
				//iPercentageAvailable = 100 - (((uTotalSpace - uSpaceUsed)/uTotalSpace)*100);
				DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: Processed 'MaxSize' option , value is = %s \n",sTotalSpace.c_str());
				DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: computed  value of 'SpaceUsed' option , value is = %s \n",sSpaceUsed.c_str());
				DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: computed  value of 'SpaceAvailable' option , value is = %s \n",sSpaceAvailable.c_str());
			}
			
			if(commonElement->getNodeName() == "HighWaterMarkLevel")  //this is expressed as a value from 0 to 100, where 100 represent maximum level (i.e. totalspace available) 
			{
				sHighWaterMarkLevel = commonElement->getTextContent();
			}
			
			if(commonElement->getNodeName() == "Port") // port is used for sending the response for AllocateQuota request
			{
				sPort = commonElement->getTextContent();
			}
		}
		if(!sStorageName.empty() && !sTotalSpace.empty() && !sSpaceUsed.empty() && !sSpaceAvailable.empty())
		{
			Status retHandle=STATUS_OK;
			//Storage
		 	CString sMountPoint="";
		 	m_storageRef->GetMountPoint(sStorageName,sMountPoint);
		 	if(!sMountPoint.compare(sStorageName))
		 	{
		 		DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: Identified the mountpoint=[%s] of new storage=[%s] \n",sStorageName.c_str(),sMountPoint.c_str());
		 		retHandle = m_storageRef->AddStorage(sStorageName,sTotalSpace,sSpaceUsed,sSpaceAvailable,sHighWaterMarkLevel);
		 	}
			else //directory
			if (m_storageRef->IsThisDirectoryPresent(sStorageName) )
			{
		 		DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: Identify the mountpoint of new storage=[%s] \n",sStorageName.c_str());
		 		retHandle = m_storageRef->AddDirectory(sStorageName,sTotalSpace,sSpaceUsed,sSpaceAvailable,sHighWaterMarkLevel);
			}
			else //file
			{
		 		DEBUGL7("SystemResourceManager:HandleAllocateQuotaRequests: Identify the mountpoint of new file=[%s] \n",sStorageName.c_str());
		 		retHandle = m_storageRef->AddFile(sStorageName,sTotalSpace,sSpaceUsed,sSpaceAvailable,sHighWaterMarkLevel);
			}
			
			if(STATUS_OK == retHandle)
			{
				DEBUGL7("SystemResourceManager:HandleCreateQuotaRequests: Successfully added Storage=[%s] with TotalSpace =[%s] , \
						  SpaceUsed=[%s], SpaceAvailable=%[s]\n",sStorageName.c_str(),sTotalSpace.c_str(),sSpaceUsed.c_str(),sSpaceAvailable.c_str());
				//add Status node with value success 
				AddNewNode(pStorageElement,"Status", "STATUS_OK");
			}
			else
			{
				DEBUGL1("SystemResourceManager:HandleCreateQuotaRequests: Error occurred while adding storage ...so skipping to next storage\n");
				//add Status success/failure 
				AddNewNode(pStorageElement,"Status", "STATUS_FAILED");
				continue;
			}
		}
		else
		{
			DEBUGL1("SystemResourceManager:HandleCreateQuotaRequests: Invalid storage in allocate quota request...so next skipping to storage\n");
			AddNewNode(pStorageElement,"Status","STATUS_INVALID_PATH");
			continue;
		}
	}//'Storage' for loop 
   } catch (DOMException except) {
      DEBUGL1("SystemResourceManager::HandleAllocateQuotaRequests : Failed due to HDB Exception\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	
	//send AllocateQuota response
	CString  sResponse="";
	Status ret=STATUS_OK;
	ret = m_pHDB->Serialize(pDoc,sResponse);
	if( ret == STATUS_OK )
	{
		ret = SendResponse(msg,sResponse, sDocID);
	}
	else
	{
		if(ret == STATUS_DISK_FULL)
		{
			DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
		}
		DEBUGL1("SystemResourceManager:HandleCreateQuotaRequests: Serialize operation failed while processing %s request\n",sDocID.c_str());
		ret = SendOffNormalResponse(msg,sDocID);
	}
	
	return STATUS_OK;
}

void CSystemResourceManager::AddNewNode(ElementRef pStorageElement,CString sNameOfNewNode, CString sValueOfNewNode)
{
	try {
   DEBUGL7("CSystemResourceManager:AddNewNode: IN:nodename=[%s]\n",(pStorageElement->getNodeName()).c_str());
	NodeRef pNode = chelper::AppendElementNode(pStorageElement,sNameOfNewNode);
	if (pNode)
	{
		chelper::AppendTextNode(pNode,sValueOfNewNode);
	}
	else
	{
		//ideally this must not happen, if it happens, something is really wrong in hdb, hard disk or code is buggy is somewhere
		DEBUGL1("CSystemResourceManager:AddNewNode: ApplendElementNode Failed !!!..fix me\n");
	}
	DEBUGL7("CSystemResourceManager:AddNewNode: OUT:nodevalue=[%s]\n",(pNode->getTextContent()).c_str());
    return;
   } catch (DOMException except) {
      DEBUGL1("CSystemResourceManager::AddNewNode: Failed due to HDB Exception\n");
      return;
   }
}


Status CSystemResourceManager::HandleDeallocateQuotaRequests(ci::operatingenvironment::Ref< ci::messagingsystem::Msg >  msg)
{	
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::HandleDeallocateQuotaRequests : Invalid msg Parameter\n");
      return STATUS_FAILED;
   }
	char* msgContent = static_cast<char *>(msg->GetContentPtr());
	DocumentRef pDoc = NULL;
	CString sDocID = "DeallocateQuota";
	if (m_pHDB->CreateTempDocument(sDocID,pDoc, msgContent) != STATUS_OK)
	{ 
		DEBUGL7("SystemResourceManager:HandleDeallocateQuotaRequests: create temp document using message content [%s] failed \n",msgContent);
		return STATUS_FAILED;
	}
	
	/* 
	 *  1)  Transverse the list and remove all the storage listed in the DeallocateQuota request
	 *  2)  While doing so, search the storage being deleted in the m_storageSubscriberEventMap ( also from m_StorageToWatchDescMap & m_WatchDescToStorageMap )
	 *  3)  Append status node for each storage deleted 
	 *  4)  Send response msg back to the called
	 */ 
	
	NodeListRef pListOfStorageNodes = pDoc->getDocumentElement()->getChildNodes();
   if(!pListOfStorageNodes)
   {
      DEBUGL1("CSystemResourceManager::HandleDeallocateQuotaRequests : Empty Child Nodes\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	//loop through list of 'Quota' nodes
   try {
	for (uint32 i = 0; i < pListOfStorageNodes->getLength(); i++)
	{//Storage loop
		CString sPathName="";
		CString sStorageName="";
		Ref<Node> pStorageNode = pListOfStorageNodes->item(i);
		if (!pStorageNode || pStorageNode->getNodeType() != Node::ELEMENT_NODE)
			continue;
		ElementRef pStorageElement = pStorageNode;
		if(pStorageElement->getNodeName() !="Storage" )
		{
			DEBUGL2("SystemResourceManager:HandleAllocateQuotaRequests: Expected storage node but found some other node [%s]\n",\
					(pStorageNode->getNodeName().c_str()));
			DEBUGL2("SystemResourceManager:HandleAllocateQuotaRequests: Skipping to node in the request\n");
			continue;
		}
		//get 'Name'attribute of Storage 
		sStorageName = pStorageElement->getAttribute( "Name" );
      ModifyForPartitionNameChange(sStorageName);
		if(!sStorageName.empty())
		{
			Status retHandle=STATUS_OK;
			//Storage
		 	CString sMountPoint="";
		 	m_storageRef->GetMountPoint(sStorageName,sMountPoint);
		 	if(!sMountPoint.compare(sStorageName))
		 	{
		 		retHandle = m_storageRef->DeleteStorage(sStorageName);
		 	}
		 	else //directory
			if (m_storageRef->IsThisDirectoryPresent(sStorageName) )
			{
		 		retHandle = m_storageRef->DeleteDirectory(sStorageName);
			}
			else //file
			{
		 		retHandle = m_storageRef->DeleteFile(sStorageName);
			}
			
			if((STATUS_OK == retHandle) && STATUS_OK == RemoveFromWatchList(sStorageName))
			{
				DEBUGL7("SystemResourceManager:HandleDeallocateQuotaRequests: Successfully removed Storage=[%s] \n",sStorageName.c_str());
				//add Status node with value success 
				AddNewNode(pStorageElement,"Status","STATUS_OK");
				continue;
			}
			else
			{
				DEBUGL1("SystemResourceManager:HandleDeallocateQuotaRequests: Error occurred while remove storage ...so skipping to next storage");
				//add Status success/failure 
				AddNewNode(pStorageElement,"Status","STATUS_FAILED");
				continue;
			}
		}
		else
		{
			DEBUGL1("SystemResourceManager:HandleDeallocateQuotaRequests: Invalid storage found in deallocate quota request...so next skipping to storage");
			continue;
		}
	}//'Storage' for loop
   } catch (DOMException except) {
      DEBUGL1("CSystemResourceManager::HandleDeallocateQuotaRequests : Failed due to HDB Exception\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
	//send Deallocate response
	CString  sResponse="";
	Status ret=STATUS_OK;
	ret = m_pHDB->Serialize(pDoc,sResponse);
	if( ret == STATUS_OK )
	{
		ret = SendResponse(msg,sResponse, sDocID);
	}
	else
	{
		if( ret == STATUS_DISK_FULL)
		{
			DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
		}
		DEBUGL1("SystemResourceManager:HandleDeallocateQuotaRequests: Serialize operation failed while processing %s request\n",sDocID.c_str());
		ret = SendOffNormalResponse(msg,sDocID);
	}
	
	return STATUS_OK;
}

Status CSystemResourceManager::InitializeMonitor()
{
   //initialize and get an inotify fd
   m_inotifyWatchfd = inotify_init();
   //if((EMFILE == m_inotifyWatchfd) || (ENFILE == m_inotifyWatchfd) || ( ENOMEM == m_inotifyWatchfd) )
   if(m_inotifyWatchfd < 0)	//20081216 Riyasudeenkhan change code
   {
	   perror("SystemResourceManager:InitializeMonitor: inotify_init failed:");
	   return STATUS_FAILED;
   }
   return STATUS_OK;
}


// This is for Letting the OS keep a track of the Events happening on each element.
//
Status CSystemResourceManager::AddToWatchList(CString sFileDirStorageName, int32 iSubscriberPort, uint32 uWatchFlags)
{
	DEBUGL7("CSystemResourceManager:AddToWatchList: eventData.iSubscriberPort = %d \n",iSubscriberPort);
	DEBUGL7("CSystemResourceManager:AddToWatchList: eventData.uEventFlags = %d \n",uWatchFlags);
	CString sMountPoint="";
 	if( !(m_storageRef->GetMountPoint(sFileDirStorageName,sMountPoint)) )
 	{
 		DEBUGL1("SystemResourceManager:AddToWatchList: GetMountPoint failed for %s \n",sFileDirStorageName.c_str());
 		return STATUS_INVALID_PATH;
 	}
 	
 	bool bIsDirectoryFlag = m_storageRef->IsThisDirectoryPresent(sFileDirStorageName);

	// Check whether the path already in the watch list
	map<CString, list<stEventData> >::iterator it = m_storageSubscriberEventMap.find(sFileDirStorageName);
	if( it == m_storageSubscriberEventMap.end() )
    {	//if not already present insert into to the map
				
		DEBUGL7("Path = %s not found in the watch list, hence adding it to watch list\n",sFileDirStorageName.c_str());
		stEventData   tmpEventData(uWatchFlags,iSubscriberPort,sMountPoint,bIsDirectoryFlag);
		ListOfEventEntries	tmpListOfEventEntries;
		tmpListOfEventEntries.push_back(tmpEventData);
		std::pair<map<CString, ListOfEventEntries>::iterator,bool> retVal;
		retVal = m_storageSubscriberEventMap.insert(map<CString, ListOfEventEntries>::value_type(sFileDirStorageName,tmpListOfEventEntries));
	    if (retVal.second == true)
	    {//insertion successful, so add into watch list of inotify call
	    	//add directories/files to inotify watch list 
	    	//char *pathname = new char[sFileDirStorageName.size()];
         //char pathname [sFileDirStorageName.size()]; // the above line was a memory leak 
	    	//strcpy(pathname,sFileDirStorageName.c_str());
	 	   //int wd = inotify_add_watch(m_inotifyWatchfd, pathname, uWatchFlags);
         int wd = inotify_add_watch(m_inotifyWatchfd, sFileDirStorageName.c_str(), uWatchFlags);
	 	   if ( wd < 0 ) 
	 	   { 
	 		   DEBUGL1("SystemResourceManager:AddToWatchList:inotify_add_watch failed. This is fatal, may be a bug or system issue: with errno set to %d ", errno); 
	 		   return STATUS_FAILED; 
	 	   }
	 	   //insert into the watch file descriptor map.
	 	  m_StorageToWatchDescMap.insert(map<CString,int>::value_type(sFileDirStorageName,wd));
	 	  m_WatchDescToStorageMap.insert(map<int,CString>::value_type(wd,sFileDirStorageName));
	 	  
	 	   //Write into pipe for refreshing watchlist of the select() call
	 		if( write(m_InComingSubscriptionRequestsFd[1],static_cast<const void*>("R"),1) != 1)
	 		{
	 			DEBUGL1("SystemResourceManager:AddToWatchList: m_InComingSubscriptionRequestsFd Pipe IO failed:" );
	 			return STATUS_FAILED;
	 		}
	 	   DEBUGL7("SystemResourceManager:AddToWatchList:Watch descriptor for [%s] is : %d\n",sFileDirStorageName.c_str(),wd);
	 	   DEBUGL7("Successfully added path %s to watch list\n",sFileDirStorageName.c_str());
	    }
	    	
    }
	else
	{ //The storage path is already in the map, so we just have to add new subscriber to this storage path
		DEBUGL7("Path=%s already in the watch list, so just adding new subscriber at port %d to it's list of subscribers\n",sFileDirStorageName.c_str(),iSubscriberPort);
		stEventData   tmpEventData(uWatchFlags,iSubscriberPort,sMountPoint,bIsDirectoryFlag);
	    //insert into the list
		((*it).second).push_back(tmpEventData);
	    //add directories/files to inotify watch list 
 	   int wd = inotify_add_watch(m_inotifyWatchfd, sFileDirStorageName.c_str(), uWatchFlags);
 	   if ( wd < 0 ) 
 	   { 
 		   DEBUGL1("SystemResourceManager:AddToWatchList:inotify_add_watch failed. This is fatal, may be a bug or system issue: with errno set to %d", errno); 
 		   return STATUS_FAILED; 
 	   }
 	   //insert into the watch file descriptor map.
 	  m_StorageToWatchDescMap.insert(map<CString,int>::value_type(sFileDirStorageName,wd));
 	  m_WatchDescToStorageMap.insert(map<int,CString>::value_type(wd,sFileDirStorageName));
 	  
 	   //Write into pipe for refreshing watchlist of the select() call
 		if( write(m_InComingSubscriptionRequestsFd[1],static_cast<const void*>("R"),1) != 1)
 		{
 			DEBUGL1("SystemResourceManager:AddToWatchList: m_InComingSubscriptionRequestsFd Pipe IO failed:" );
 			return STATUS_FAILED;
 		}
 	   DEBUGL7("SystemResourceManager:AddToWatchList:Watch descriptor for [%s] is : %d\n",sFileDirStorageName.c_str(),wd);
	    	
	}
	return STATUS_OK;
}

Status CSystemResourceManager::RemoveFromWatchList(CString sFileDirStorageName)
{
	map<CString, list<stEventData> >::iterator it = m_storageSubscriberEventMap.find(sFileDirStorageName);
	if( it != m_storageSubscriberEventMap.end() )
    {	//remove the subscription entry  both the map
		map<CString, int>::iterator wdItr = m_StorageToWatchDescMap.find(sFileDirStorageName);
		if(wdItr != m_StorageToWatchDescMap.end())
		{
			int wd = wdItr->second;
			//add directories/files to inotify watch list 
	 	   int ret = inotify_rm_watch(m_inotifyWatchfd,wd );
	 	   if ( ret < 0 ) 
	 	   { 
	 		   DEBUGL1("SystemResourceManager:RemoveFromWatchList:inotify_rm_watch failed. This is fatal, may be a bug or system issue: with errno set to %d", errno); 
	 		   return STATUS_FAILED; 
	 	   }
	 	   //remove watch descriptor from both maps (two-map)  storagename to wd and wd to storagename 
	 	   m_WatchDescToStorageMap.erase(wd);
	 	   m_StorageToWatchDescMap.erase(sFileDirStorageName);
	 	   //remove event map from Subscriber map
	 	   m_storageSubscriberEventMap.erase(sFileDirStorageName);
	 	   		  
	 	   //Write into pipe for refreshing watchlist of the select() call
	 		if( write(m_InComingSubscriptionRequestsFd[1],static_cast<const void*>("R"),1) != 1)
	 		{
	 			DEBUGL1("SystemResourceManager:RemoveFromWatchList: m_InComingSubscriptionRequestsFd Pipe IO failed:");
	 			return STATUS_FAILED;
	 		}
	 	   DEBUGL7("SystemResourceManager:RemoveFromWatchList:Successfully removed path %s from watch list and from subscription map\n",sFileDirStorageName.c_str());
		}
    }
	else
	{ //not found in the map so there is no need to remove 
	 	DEBUGL2("SystemResourceManager:RemoveFromWatchList: Path %s not foudn in subscription map ....remove failed\n",sFileDirStorageName.c_str());
		return STATUS_FAILED;
	}
	return STATUS_OK;
}


void *CSystemResourceManager::Run( void *executeFunc)
{
//  int arg;
  //char *fileOrDirectoryToBeWatched=NULL;
  unsigned char gBuf[INOTIFY_BUFLEN]={0}; 
  unsigned char buff[2]={0};

  DEBUGL7(" CSystemResourceManager::Run Starting the Thread  \n");

  
  //create empty file that will be first file to be monitored
  CString emptyFile="/tmp/_srm_.lock";
  DataStreamRef pStream = File::Open(emptyFile, "rw");
  pStream=NULL;


   //add directories/files that must be watched
   int32 iSubscriberPort=-1; //-1 is used a default value when no client is available, this used when SRM itself want to monitor events
   AddToWatchList(emptyFile,iSubscriberPort, IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_MODIFY|IN_MOVED_FROM|IN_MOVED_TO);

   int ret;
   struct timeval tme;
   while (true)
    {
		/* Reset the select FD set, timeout */
		FD_ZERO (&m_inotifyDescSet);
	
		/* Add FDs of each Event type to the m_inotifyDescSet*/
		FD_SET (m_inotifyWatchfd, &m_inotifyDescSet);
		FD_SET (m_InComingSubscriptionRequestsFd[0],&m_inotifyDescSet);
		
		tme.tv_sec = 5;
		tme.tv_usec = 0;
	
	       /* Select of the FD set */
		ret = select((m_inotifyWatchfd+1),&m_inotifyDescSet,0, 0, &tme);
		if (ret == -1)
		{
			/*error occured */
			
			DEBUGL1("CSystemResourceManager::Run select returned an error . the program with errno = %d\n", errno);
			_exit(1);
		}
		else if (ret == 0)
		{
			//DEBUGL7("Select call Timeout !\n");
		}
		else
		{
			
			if(FD_ISSET(m_InComingSubscriptionRequestsFd[0], &(m_inotifyDescSet)))
	        {//indicates subscription request has come
				
	            read(m_InComingSubscriptionRequestsFd[0],buff,1);
	            //NOTE: if needed handle post processing here 
	        } 
	
			if(FD_ISSET(m_inotifyWatchfd, &(m_inotifyDescSet)))
			{ // an event has occurred
				ssize_t remainingSize = 0;
				/* Found an event */
				DEBUGL7("Found an event, reading the event !\n");
				
		   		ssize_t sizeRead = 0;
		   		do {
		   				sizeRead = read(m_inotifyWatchfd, gBuf, INOTIFY_BUFLEN);
		   			} while ( sizeRead == -1 && errno == EINTR );
			
				if( sizeRead == -1 && !(errno == EWOULDBLOCK || errno == EINTR ) )	
				{ 
					DEBUGL2("reading inotify events failed ."); 
				}
		
				while ( remainingSize < sizeRead )
				{
					struct inotify_event * iEvent = (struct inotify_event *) &gBuf[remainingSize];
					DEBUGL7("iEvent->wd = %d \n",iEvent->wd);
					DEBUGL7("iEvent->len = %d \n",iEvent->len);
					if (iEvent->len > 0 )
					{//no name	
					    DEBUGL7("iEvent->name = [%s] \n",iEvent->name);
					}
					//handle incoming events and send notifications to subscribers
					HandleStorageEvents(iEvent);
					DisplayEvent(iEvent->mask);
					//OnWriteUpdateSize(iEvent->mask);
					//update remainingEvents counter
					remainingSize += sizeof(struct inotify_event) + (ssize_t) iEvent->len;
				}
			}
		}
   }//while infinite loop
   return 0;
} 

Status CSystemResourceManager::HandleStorageEvents(struct inotify_event * iEvent)
{
	int ret=SRM_STATUS_OTHER_EVENT;
	DEBUGL7("SystemResourceManager:HandlStorageEvents: start HandleStorageEvent method \n");
	CString sFileDirStorageName="";
	//using iEvent->wd get the Storage name from map
	map<int,CString>::iterator wdItr = m_WatchDescToStorageMap.find(iEvent->wd);
	if(wdItr != m_WatchDescToStorageMap.end())
	{
		//get the storage that is associated with this watch descriptor
		sFileDirStorageName = wdItr->second;
		DEBUGL7("CSystemResourceManager:HandleStorageEvents: FileDirStorageName = %s\n",sFileDirStorageName.c_str());
		//using the storage name get the Event data from Subscriber map
		map<CString, list<stEventData> >::iterator it = m_storageSubscriberEventMap.find(sFileDirStorageName);
		if( it != m_storageSubscriberEventMap.end() )
	    {
			list<stEventData> ListOfSubscriberEventDataEntries = it->second;
			list<stEventData>::iterator listItr;
		    for(listItr=ListOfSubscriberEventDataEntries.begin(); listItr!=ListOfSubscriberEventDataEntries.end();listItr++)
		    {//loop to send events to each subscriber in the list 
		    	
		    	stEventData eventData = *listItr;
		    	if (iEvent->len > 0 )
		    	{//with file/directory names
		    		ret = SendStorageEvents(sFileDirStorageName, eventData,iEvent->name,iEvent->mask);
		    	}
		    	else
		    	{//without file/directory names
		    		ret = SendStorageEvents(sFileDirStorageName, eventData, "", iEvent->mask);
		    		DEBUGL7("SystemResourceManager:HandlStorageEvents: iEvent->len=0  DontKnow What event it is  !!!!\n");
		    	}
		    }
	    }
	}
	else
	{
		DEBUGL2("SystemResourceManager:HandlStorageEvents:Watch descriptor recieved from system/os is not found in the SRM map, hence dropping it\n");
	}
	
	if( SRM_STATUS_SELF_DELETE_EVENT == ret )
	{//remove the subscription entries for the deleted file/directory
		RemoveFromWatchList(sFileDirStorageName);
	}	
	
	DEBUGL7("SystemResourceManager:HandlStorageEvents: end HandleStorageEvent method \n");
	return STATUS_OK;
}

Status CSystemResourceManager::SendWaterMarkLevelReachedEvent(CString sFileDirStorageName)
{
   ModifyForPartitionNameChange(sFileDirStorageName);
	CString sEventPayload = "<Notify Name='" + sFileDirStorageName + "'> \
							 	<Events>\
									<HighWaterMarkLevelReached>true</HighWaterMarkLevelReached>\
								</Events>\
							</Notify>";
	Msg mNotifyEvent(MSG_TYPE_EVENT,sEventPayload.c_str(),sEventPayload.size());
	mNotifyEvent.SetId(ci::systemresourcemanager::SystemResourceManager::Notify);
	mNotifyEvent.SetSender(m_InterfacePortRef->GetId());
	
	DEBUGL7("SystemResourceManager:SendWaterMarkLevelReachedEvent: start HandleStorageEvent method \n");
	DEBUGL7("CSystemResourceManager:HandleStorageEvents: FileDirStorageName = %s\n",sFileDirStorageName.c_str());
	//using the storage name get the Event data from Subscriber map
	map<CString, list<stEventData> >::iterator it = m_storageSubscriberEventMap.find(sFileDirStorageName);
	if( it != m_storageSubscriberEventMap.end() )
    {
		list<stEventData> ListOfSubscriberEventDataEntries = it->second;
		list<stEventData>::iterator listItr;
	    for(listItr=ListOfSubscriberEventDataEntries.begin(); listItr!=ListOfSubscriberEventDataEntries.end();listItr++)
	    {//loop to send events to each subscriber in the list 
	    	
	    	stEventData eventData = *listItr;
	    	 if(eventData.iSubscriberPort != -1 )
			 {
				if( STATUS_OK != m_InterfacePortRef->Send(mNotifyEvent,eventData.iSubscriberPort) )
				{
					DEBUGL1("SystemResourceManager:SendWaterMarkLevelReachedEvent: Send Call for Notify event failed for port=%d ....\n",eventData.iSubscriberPort );
					return STATUS_FAILED;
				}
				DEBUGL7("SystemResourceManager:SendWaterMarkLevelReachedEvent: Sent Notify event to subscriber at port=%d ....\n",eventData.iSubscriberPort );
			 }
			 else
			 {
				 DEBUGL7("SystemResourceManager:SendWaterMarkLevelReachedEvent: Rxd event from OS for SRM, hence not forwarding to subscribers.\n");
			 }
	    }
    }
	else
	{
		DEBUGL1("SystemResourceManager:SendWaterMarkLevelReachedEvent: Storage=[%s] not found in the SRM map, hence not sending High Water Mark level reached event\n", sFileDirStorageName.c_str());
		return STATUS_FAILED; 
	}
	DEBUGL7("SystemResourceManager:SendWaterMarkLevelReachedEvent: end HandleStorageEvent method \n");
	return STATUS_OK;
}

int CSystemResourceManager::SendStorageEvents(CString sFileDirStorageName, stEventData eventData, CString sRxdEventFileRDirName,uint32 uRxdEventMask)
{
	 
	
	DEBUGL7("*** Inside SystemResourceManager:SendStorageEvents***\n");
	int  iRetVal = SRM_STATUS_OTHER_EVENT;
    CString sFileDirStorName=sFileDirStorageName;
    ModifyForPartitionNameChange(sFileDirStorName);
	 CString sXMLHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
	 CString sMsgPayLoadStart = "<Notify xsi:noNamespaceSchemaLocation=\"SystemResourcesManager.xsd\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\
	 							<Path>"+ sFileDirStorName + "</Path><Events>";
	 CString sMsgPayLoadEnd = "</Events></Notify>";
		 CString sFileCreated="";
	 CString sDirectoryDeletedSelf="";
	 CString sFileDeletedSelf="";
	 CString sFileDeleted="";
	 CString sWriteOperation="";
	
    if(!sRxdEventFileRDirName.empty())
    {
	    if( ((eventData.uEventFlags & uRxdEventMask)==SRM_FILE_CREATE_EVENT) )
	    {
	    	sFileCreated = "<FileCreated><Name>" + sRxdEventFileRDirName + "</Name></FileCreated>";
	    }
	    if(((eventData.uEventFlags & uRxdEventMask)==IN_DELETE))
	    {
	    	sFileDeleted = "<FileDeleted><Name>" + sRxdEventFileRDirName + "</Name></FileDeleted>";
	    }
	    
    }

    //File storage event related payload parts 
    if( ((eventData.uEventFlags & uRxdEventMask)==SRM_FILE_WRITE_EVENT) )
    {
	 	sWriteOperation = "<WriteOperation>1</WriteOperation>";
    }
    
	CString sBindPath = "";
			
	if( eventData.bIsDirectoryFlag )
		sBindPath = "SystemResources/Storage[@Name='" + eventData.sStorageName + "']/Directory[@Name='" + sFileDirStorageName + "']";
	else
		sBindPath = "SystemResources/Storage[@Name='" + eventData.sStorageName + "']/File[@Name='" + sFileDirStorageName + "']";
	
	DEBUGL7("SystemResourceManager:SendStorageEvents: bindpath=[%s] and bIsDirectoryFlag = %d \n",sBindPath.c_str(),eventData.bIsDirectoryFlag);
	
	NodeRef pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath);
	if(!pStorageNode)
	{
 		DEBUGL3("SystemResourceManager:SendStorageEvents: bind to element failed for xpath [%s] \n",sBindPath.c_str());
 		return STATUS_FAILED;
	}





	
    printf("========> A & B = [%x]  IN_DELETE_SELF=[%x] \n",(eventData.uEventFlags & uRxdEventMask), IN_DELETE_SELF);
    if( !((eventData.uEventFlags & uRxdEventMask) == IN_DELETE_SELF) )
    {
    	
		CString sTotalSpace = "";
		CString sSpaceUsed = "";
		CString sSpaceAvailable = "";
			
		//Added Code for Disk Full Check
		CString sTotalSpaceDisk = "";
		CString sSpaceUsedDisk = "";
		CString sSpaceAvailableDisk = "";
		
		uint64 uSpaceAvailableDisk = 0;
		
		uint64 uTotalSpace=0;
		NodeRef pTotalSpaceNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath+"/TotalSpace");
		if(pTotalSpaceNode)
		if(STATUS_FAILED == m_storageRef->GetSize(pTotalSpaceNode,uTotalSpace) )
		{
	 		DEBUGL1("SystemResourceManager:SendStorageEvents: bind to element failed for xpath [%s] \n",sBindPath.c_str());
	 		return STATUS_FAILED;
		}
	
		uint64 uSpaceUsed=0;
		NodeRef pSpaceUsedNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath+"/SpaceUsed");
		if(pSpaceUsedNode)
		if(STATUS_FAILED == m_storageRef->GetSize(pSpaceUsedNode,uSpaceUsed) )
		{
	 		DEBUGL1("SystemResourceManager:SendStorageEvents: failed to getSize for  [%s] \n",sBindPath.c_str());
	 		return STATUS_FAILED;
		}
		uint64 uSpaceAvailable=0;
		NodeRef pSpaceAvailableNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath+"/SpaceAvailable");
		if(pSpaceAvailableNode)
		if(STATUS_FAILED == m_storageRef->GetSize(pSpaceAvailableNode,uSpaceAvailable) )
		{
	 		DEBUGL1("SystemResourceManager:SendStorageEvents: bind to element failed for xpath [%s] \n",sBindPath.c_str());
	 		return STATUS_FAILED;
		}



		uint64 uWaterMarkLevel=0;
		NodeRef pWaterMarkLevel = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath+"/WaterMarkLevel");
		if(pWaterMarkLevel)
		if(STATUS_FAILED == m_storageRef->GetSize(pWaterMarkLevel,uWaterMarkLevel) ) // GetSize will work as long as not has integer 
		{
			DEBUGL1("SystemResourceManager:SendStorageEvents: was not able to retrive watermark level for [%s] \n",sBindPath.c_str());
			return STATUS_FAILED;
		}


		if(eventData.sStorageName.size() >= 1 )
		{
			/* Which is best :
			 *  Chosen:  1 - Update the size of a storage (mountpoint/partition) only when a client requests it.
			 * 					-efficient and uses less cpu but size in DOM does not reflect current size of storage
			 *                  - this is done when GetSizeInformation request message is request from client
			 *                  
			 *	Rejected: 2 - Update whenever any hard disk event occurs
			 * 					 - Size is always current in DOM but it is not efficient and cpu intensive.  
			 */  
			
		}
		
		uint64 uUpdatedSpaceUsed = m_storageRef->GetFileOrDirectorySize(sFileDirStorageName.c_str());

		//Added code to get the Available Disk Space
		if(STATUS_FAILED == m_storageRef->GetPartitionSizeInfo(eventData.sStorageName,sTotalSpaceDisk,sSpaceUsedDisk,sSpaceAvailableDisk))
		{
			DEBUGL1("SystemResourceManager:SendStorageEvents was not able to retrieve Disk Details for [%s]", eventData.sStorageName.c_str());
			return STATUS_FAILED;
		}

		uSpaceAvailableDisk = m_storageRef->StringToUint64(sSpaceAvailableDisk);

		uint64 uCurrentUsage =0; // initilize if there is no change it should not be a bug 
		
		if(uUpdatedSpaceUsed != uSpaceUsed ) //SpaceUsed has changed so update SpaceUsed and SpaceAvailable into SRM DOM  
		{
			uSpaceAvailable = uTotalSpace - uUpdatedSpaceUsed;

		   if(uTotalSpace	!= 0)
   			uCurrentUsage = (uUpdatedSpaceUsed/uTotalSpace)*100;
			m_storageRef->UpdateSize(pSpaceUsedNode,"SpaceUsed",uUpdatedSpaceUsed);
			m_storageRef->UpdateSize(pSpaceAvailableNode,"SpaceAvailable",uSpaceAvailable);
		}

		DEBUGL7("SystemResourceManager:SendStorageEvents:[Spaceused=%d,TotalSpace=%d,SpaceAvailabe=%d]\n",uUpdatedSpaceUsed,uTotalSpace,uSpaceAvailable);
		DEBUGL7("SystemResourceManager:SendStorageEvents:Watermark[High=%d,Current=%d]\n",uWaterMarkLevel ,uCurrentUsage );

		bool bHighWaterMark =false;		
		//If Available Disk Space is less than the allocated space send HighWaterMarkLevel reached notification
		if(uSpaceAvailableDisk <= uSpaceAvailable)
		{
			bHighWaterMark = true;
		}
		if (uWaterMarkLevel <= uCurrentUsage )
		{ // We need to set the watermark High and send the event also 
			bHighWaterMark= true;
		}


				//Compute percentage available 
					/* 
					 *	 % usage = 100-((TotalSpace - SpaceUsed)/TotalSpace)*100)
					 */ 
				//int iPercentageAvailable = 100 - uCurrentUsage;
			
				//Common sizechange event payload part
				//CString sSizeChange = "<SizeChange><PercentageAvailable>" + (chelper::Itoa(iPercentageAvailable)) + "</PercentageAvailable></SizeChange>";
			
				//update events in the SRM DOM
		NodeListRef pNodeListRef = pStorageNode->getChildNodes();
		if(!pNodeListRef)
		{
			DEBUGL1("SystemResourceManager:SendStorageEvents: getChildNodes() failed for xpath [%s] \n",sBindPath.c_str());
			return STATUS_FAILED;
		}
		DEBUGL7("SystemResourceManager:SendStorageEvents: pNode name = [%s] \n",(pStorageNode->getNodeName()).c_str());
		UpdateEventsIntoDOM(pNodeListRef,eventData.uEventFlags,uRxdEventMask,bHighWaterMark);
		//Send the water mark reached high if it reached.
		if(bHighWaterMark)
			SendWaterMarkLevelReachedEvent(sFileDirStorageName);

		
    }//'if' it is not self delete 
    else
	{ //if  self delete 
    	/* file/directory being monitored itself is deleted, so remove the storage from SRM DOM and 
		  * 
		  */
		NodeRef pParentNode = pStorageNode->getParentNode();
		if(pParentNode)
		{
			NodeRef pRemovedNode = pParentNode->removeChild(pStorageNode);
			if(pRemovedNode)
			{
				DEBUGL7("SystemResourceManager:SendStorageEvents: Successfully removed the node on self delete event\n");
			}
			else
			{ //ideally this code must not be executed
				DEBUGL1("SystemResourceManager:SendStorageEvents: Failed to remove the node on self delete event\n");
			}
		}
		
    	//check if it is directory event
    	if(eventData.bIsDirectoryFlag)
    	{
	    	//Directory storage event related payload parts
	    	sDirectoryDeletedSelf = "<DirectoryDeletedSelf>1</DirectoryDeletedSelf>";
    	}
	    else
	    {//it a file
	    	sFileDeletedSelf = "<FileDeletedSelf>1</FileDeletedSelf>";
	    	 
	    }
    	iRetVal = SRM_STATUS_SELF_DELETE_EVENT;
    

    }
	//release node
	DEBUGL7("SystemResourceManager:SendStorageEvents:eventData.iSubscriberPort = %d \n",eventData.iSubscriberPort);
	DEBUGL7("SystemResourceManager:SendStorageEvents:eventData.uEventFlags = %d \n",eventData.uEventFlags);
		
	pStorageNode = NULL; 

  // Send notification

	
	if(eventData.iSubscriberPort != -1 )
	 {
		//CString sNotifyPayload = sMsgPayLoadStart+sSizeChange+sDirectoryDeletedSelf+sFileDeletedSelf+sFileDeleted+sFileCreated+sWriteOperation+sMsgPayLoadEnd;
		CString sNotifyPayload = sMsgPayLoadStart+sDirectoryDeletedSelf+sFileDeletedSelf+sFileDeleted+sFileCreated+sWriteOperation+sMsgPayLoadEnd;
		Msg mNotifyEvent(MSG_TYPE_EVENT,sNotifyPayload.c_str(),sNotifyPayload.size());
		mNotifyEvent.SetId(ci::systemresourcemanager::SystemResourceManager::Notify);
		mNotifyEvent.SetSender(m_InterfacePortRef->GetId());
		if( STATUS_OK != m_InterfacePortRef->Send(mNotifyEvent,eventData.iSubscriberPort) )
		{
			DEBUGL1("SystemResourceManager:SendStorageEvents: Send Call for Notify event failed for port=%d ....\n",eventData.iSubscriberPort );
			return STATUS_FAILED;
		}
		DEBUGL7("SystemResourceManager:SendStorageEvents: Sent Notify event to subscriber at port=%d ....\n",eventData.iSubscriberPort );
	 }
	 else
	 {
		 DEBUGL7("SystemResourceManager:SendStorageEvents: Rxd event from OS for SRM, hence not forwarding to subscribers.\n");
	 }
	return iRetVal;
}

void CSystemResourceManager::UpdateEventsIntoDOM(dom::NodeListRef pListOfNodes,uint32 uCurrentEventFlags, uint32 uNewEventFlags, bool bWatermarkLevel)
{
	int iBufSize=16;
	char tempBuf[iBufSize];
	memset(tempBuf,'\0',iBufSize);
	
	//Scan for directory and file events and add them to inotify watch for monitor their changes
   try {
	for(uint32 j = 0; j < pListOfNodes->getLength(); j++ )
	{
		Ref<Node> pNode = pListOfNodes->item(j);
		if (!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
			continue;
		DEBUGL7("SystemResourceManager:UpdateEventsIntoDOM: pNode name = [%s] \n",(pNode->getNodeName()).c_str());
		ElementRef commonElement = pNode; 
		if(commonElement->getNodeName()=="Events")
		{	
			Ref<NodeList>	pListOfEventNodes = pNode->getChildNodes();
			for(uint32 ie = 0; ie < pListOfEventNodes->getLength(); ie++)
			{
				Ref<Node> pEventNode = pListOfEventNodes->item(ie);
				if (!pEventNode || pEventNode->getNodeType() != Node::ELEMENT_NODE)
				continue;
				DEBUGL7("SystemResourceManager:UpdateEventsIntoDOM: pEventNode name = [%s] \n",(pEventNode->getNodeName()).c_str());
						
				ElementRef  pEventElement = pEventNode;
				/*if(pEventElement->getNodeName()=="SizeChange")
				{
					memset(tempBuf,'\0',iBufSize);
					sprintf(tempBuf,"%d",iPercentageAvailable);
					chelper::SetNodeValue(pEventElement,"SizeChange",tempBuf);
				}*/
				
				if(pEventElement->getNodeName()=="DirectoryDeletedSelf")
				{
					memset(tempBuf,'\0',iBufSize);
					sprintf(tempBuf,"%d",(((uCurrentEventFlags & uNewEventFlags)==IN_DELETE_SELF))?1:0);
					pEventElement->setNodeValue(tempBuf);
				}
				else if(pEventElement->getNodeName()=="FileCreated")
				{
					memset(tempBuf,'\0',iBufSize);
					sprintf(tempBuf,"%d",(((uCurrentEventFlags & uNewEventFlags)==SRM_FILE_CREATE_EVENT))?1:0);
					pEventElement->setNodeValue(tempBuf);
					//TODO handle HighWaterMarkLevelReached
				}
				else if(pEventElement->getNodeName()=="FileDeleted")
				{
					memset(tempBuf,'\0',iBufSize);
					sprintf(tempBuf,"%d",(((uCurrentEventFlags & uNewEventFlags)==IN_DELETE))?1:0);
					pEventElement->setNodeValue(tempBuf);
            
				}
				else if(pEventElement->getNodeName()=="FileDeletedSelf")
				{
					memset(tempBuf,'\0',iBufSize);
					sprintf(tempBuf,"%d",(((uCurrentEventFlags & uNewEventFlags)==IN_DELETE_SELF))?1:0);
					pEventElement->setNodeValue(tempBuf);
				}
				else if(pEventElement->getNodeName()=="WriteOperation")
				{
					memset(tempBuf,'\0',iBufSize);
					sprintf(tempBuf,"%d",(((uCurrentEventFlags & uNewEventFlags)==SRM_FILE_WRITE_EVENT))?1:0);
					pEventElement->setNodeValue(tempBuf);
					//TODO handle HighWaterMarkLevelReached
				}
				else if(pEventElement->getNodeName()=="HighWaterMarkLevelReached"  ) //handle HighWaterMarkLevelReached in the calling function
				{
					memset(tempBuf,'\0',iBufSize);
					if (bWatermarkLevel)
						sprintf(tempBuf,"true");
					else
						sprintf(tempBuf,"false");
					pEventElement->setNodeValue(tempBuf);
					
				}
			}//loop to process all event under 'Events' node
		}
	}//loop for finding Event node of 'Directory' or 'File'
   } catch (DOMException except) {
      DEBUGL1("SystemResourceManager:UpdateEventsIntoDOM : Failed due to HDB Exception\n");
      return;
   }
		DEBUGL7("SystemResourceManager:UpdateEventsIntoDOM: Updated new events into SRM DOM\n");
	return;
}

Status CSystemResourceManager::GetErrorCode(ci::operatingenvironment::CNoCaseString& errorCode, ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg) {
    errorCode = "";
    if(!msg) {
        DEBUGL1("CSystemResourceManager::HandleNotifyErrorRequest : Invalid msg Parameter\n");
        return STATUS_FAILED;
    }
    char* msgContent = static_cast<char *>(msg->GetContentPtr());
    DocumentRef pNotifyErrorDoc = NULL;   //Refer to the SystemResourceManager.xsd for payload XML
    CString sDocID = getUniqueFileName("NotifyError");
DEBUGL4("SystemResourceManager : NotifyError Message received from [%d]\n",msg->GetSender());
    // Read the request message 
    if (m_pHDB->CreateTempDocument(sDocID,pNotifyErrorDoc,msgContent) != STATUS_OK) { 
        DEBUGL1("SystemResourceManager:HandleNotifyErrorRequests: CreateTempDocument of Notify Error request failed !\n");
        SendOffNormalResponse(msg,sDocID);
        return STATUS_FAILED;
    }
    if(!pNotifyErrorDoc) {
         DEBUGL1("SystemResourceManager:HandleNotifyErrorRequests: CreateTempDocument of Notify Error request failed !\n");
         SendOffNormalResponse(msg,sDocID);
        return STATUS_FAILED;
    }
    NodeRef pError = pNotifyErrorDoc->getDocumentElement();
    Ref<NodeList> pListOfNodes = pError->getChildNodes();
    try {
        for(uint32 i=0;i<pListOfNodes->getLength();i++) {
            Ref<Node> pNode = pListOfNodes->item(i);
            if(!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
                continue;
            ElementRef commonElement = pNode;
            //Get the Error Code value for the XML Payload
            if(commonElement->getNodeName()=="ErrorCode") {
                errorCode = case_cast(commonElement->getTextContent());
            }
        }
        if(errorCode.empty()) {
            //Get the Error Code value for the XML Payload
            AddNewNode(pError,"Status","STATUS_FAILED");
        } else {
            AddNewNode(pError,"Status","STATUS_OK");
        }
    } catch (DOMException except) {
      DEBUGL1("SystemResourceManager:HandleNotifyErrorRequests: Failed due to HDB Exception\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
    }
    CString  sNotifyErrorResponse="";
    Status ret=STATUS_OK;
    ret = m_pHDB->Serialize(pNotifyErrorDoc,sNotifyErrorResponse);
    if( ret == STATUS_OK ) {
        //Send the response message 
        ret = SendResponse(msg,sNotifyErrorResponse, sDocID);
    } else {
        if(ret == STATUS_DISK_FULL) {
            DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
        }
        DEBUGL1("SystemResourceManager:HandleNotifyErrorRequests: Serialize operation failed while processing request\n");
        ret = SendOffNormalResponse(msg,sDocID);
    }
    return STATUS_OK;
}

Status CSystemResourceManager::CheckErrorAndSendServiceMessage(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg){
    Status retStatus = STATUS_OK;
    DEBUGL4("CSystemResourceManager::CheckErrorAndSendServiceMessage: Enter\n");
    CNoCaseString errorCode = "";
    if(GetErrorCode(errorCode, msg) != STATUS_OK) {
        return STATUS_FAILED;
    }
    CString errorMessage;
    map<CNoCaseString,CString>::iterator ItNotificationMap = m_NotificationMap.find(errorCode);
    map<CNoCaseString,CString>::iterator ItMessageLogMap = m_MessageLogMap.find(errorCode);
    if(!errorCode.compare(ERROR_F121)){
        if(ItMessageLogMap != m_MessageLogMap.end()) {
            errorMessage = ItMessageLogMap->second;
            DEBUGL7("CSystemResourcemanager:CheckErrorAndSendServiceMessage: Writing ErrorCode to the Message Log\n");
            retStatus = ErrorNotificationWriteToLog(errorMessage);
            if( retStatus!=STATUS_OK) {
                DEBUGL1("CSystemResourceManager::CheckErrorAndSendServiceMessage: Failed to Write ErrorCode to the MessageLog\n");
            }
        }
 
        if(ItNotificationMap != m_NotificationMap.end()) {
            errorMessage = ItNotificationMap->second;
            retStatus = sendNotificationMessage(case_cast(errorCode));
            if(retStatus != STATUS_OK) {
               DEBUGL1("SystemResourceManager:CheckErrorAndSendServiceMessage: Failed to send Notification Message\n");
            }
        }
    }

    if(ItNotificationMap != m_NotificationMap.end()) {
        errorMessage = ItNotificationMap -> second;
        DEBUGL8("SystemResourceManager:CheckErrorAndSendServiceMessage: Displaying Service call  [%s:%s]!\n", errorCode.c_str(), errorMessage.c_str());
        retStatus =sendServiceMessage(errorMessage,case_cast(errorCode));
        if(retStatus != STATUS_OK) {
            DEBUGL1("SystemResourceManager:CheckErrorAndSendServiceMessage: Failed to make Service Call - %s\n", errorCode.c_str());
            return STATUS_FAILED;
        }
    }
    return STATUS_OK;
}
 
Status CSystemResourceManager::HandleNotifyErrorRequest(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
    Status ret=STATUS_OK;
    DEBUGL4("Notify Error Called\n");
    CNoCaseString errorCode = "";
    if(GetErrorCode(errorCode, msg) != STATUS_OK) {
        return STATUS_FAILED;
    }
    DEBUGL4("errorCode -> (%s)\n", errorCode.c_str());
    map<CNoCaseString,CString>::iterator ItNotificationMap = m_NotificationMap.find(errorCode);
    map<CNoCaseString,CString>::iterator ItMessageLogMap = m_MessageLogMap.find(errorCode);
    map<CNoCaseString,CString>::iterator ItOnlyServiceMap = m_OnlyServiceCallMap.find(errorCode);
 
   // Added code for Error F120 and F130 
    if((!errorCode.compare(ERROR_F121)) || (!errorCode.compare(ERROR_F130))) { 
       DEBUGL8("CResourceManager::HandleNotifyErrorRequest:: Service call error if F121 or F130 \n");	  
       time_t diffTotalSecs;
       uint64 ErrorMonitorKey;
       int TimeIntSec;
       int keyValue;     

       ErrorMonitorKey = IndexedDB::CreateKey(8, 9788, 0);
       if (m_pIdbPowerMonitor->KeyExists(ErrorMonitorKey)) {
	    DEBUGL8("CResourceManager::HandleNotifyErrorRequest:: 08-9788 KeyExists\n");
           keyValue = m_pIdbPowerMonitor->GetInt16Value(ErrorMonitorKey);
               DEBUGL1("CSystemResourceManager::HandleNotifyErrorRequest() keyValue :%d\n",keyValue);
           if(keyValue == 12){
              
	       if(!errorCode.compare(ERROR_F121)){
			CString errorMessage;
		       if(ItMessageLogMap != m_MessageLogMap.end()) {
			       errorMessage = ItMessageLogMap->second;
			       DEBUGL7("CSystemResourcemanager::Writing ErrorCode to the Message Log\n");
			       ret = ErrorNotificationWriteToLog(errorMessage);
			       if( ret!=STATUS_OK) {
				       DEBUGL1("CSystemResourceManager::Failed to Write ErrorCode to the MessageLog\n");
			       }
		       }

		       if(ItNotificationMap != m_NotificationMap.end()) {
			       errorMessage = ItNotificationMap->second;
			       ret = sendNotificationMessage(case_cast(errorCode));
			       if(ret != STATUS_OK) {
				       DEBUGL1("SystemResourceManager: Failed to send Notification Message\n");
			       }
		       }
	       }
		AddToErrorList(errorCode.c_str());
	       restartSystem();
               return STATUS_OK;
           }
	   else if(keyValue == 0) {
	       DEBUGL1("CResourceManager::keyValue is: %d\n" ,keyValue); 
      	       ret=CheckErrorAndSendServiceMessage(msg);
	       if(ret != STATUS_OK){
	          return STATUS_FAILED;
	       }
     	       return STATUS_OK;	      
	   }   
       }
       
       map<int,int>::iterator ErrorServiceMap = m_CheckServiceCallMap.find(keyValue);
       if(ErrorServiceMap != m_CheckServiceCallMap.end()){
           TimeIntSec = (ErrorServiceMap->second)*60;
       }
         
       string path = "/work/ci/srm";
       string Dpath = "/work/ci/srm/srmSCErrorRestar";
       string filename = "srmSCErrorRestar";
       Status retStatus ;
       string xmlstring  ="<SRMServiceCallError><F121><Time></Time></F121><F130><Time></Time></F130></SRMServiceCallError>";
       Ref<dom::Document> pdoc;
       HierarchicalDBRef hdbobj = HierarchicalDB::Acquire(NULL);
       
       if (STATUS_OK != hdbobj->OpenDocument(path,filename,pdoc)){
                if(File::Exists(Dpath)){
                        if(File::DeleteFile(Dpath) != STATUS_OK){
                                DEBUGL2(" CResourceManager::HandleNotifyErrorRequest() Delete filePath = %s FAILED  \n",filename.c_str());
                                return STATUS_FAILED;
                        }
                }

                if( STATUS_OK != hdbobj->CreateDocumentFromString(path, filename, pdoc, xmlstring ) ){
                        DEBUGL1( "CResourceManager::HandleNotifyErrorRequest() Create Document Failure.\n" );
                        return STATUS_FAILED;
                }

       }
        // Check for Error F121
       if (!errorCode.compare(ERROR_F121)){
     
           time_t currentTimeF121 = time(NULL);
           CString epochTimeF121;
           // Get prev stored time form Dom
           NodeRef timeNodeF121 = hdbobj->BindToElement(pdoc, "SRMServiceCallError/F121/Time");
           if(!timeNodeF121){
               DEBUGL1("CResourceManager:HandleNotifyErrorRequest: SetModifiedTime Failed BindToElement\n");
               return STATUS_FAILED;
           }
           CString timeStrF121 = timeNodeF121->getTextContent();
           time_t prevTimeF121  = atoi(timeStrF121.c_str());  

	   DEBUGL8("CResourceManager:HandleNotifyErrorRequest: currentTimeF121: %d , prevTimeF121 :%d\n",currentTimeF121, prevTimeF121);
           //time Differences  
           diffTotalSecs = currentTimeF121 - prevTimeF121 ;
           if (diffTotalSecs <= TimeIntSec && (!timeStrF121.empty())){
               DEBUGL8("CResourceManager:HandleNotifyErrorRequest: diffTotalSecs for F121 :%d\n",diffTotalSecs);
               //Set timer in Dom
               epochTimeF121 = chelper::Itoa(currentTimeF121);
               timeNodeF121->setTextContent(epochTimeF121);
	       ret = CheckErrorAndSendServiceMessage(msg);
               if(ret != STATUS_OK){
                  DEBUGL1("SystemResourceManager:HandleNotifyErrorRequest: Failed to make Service Call\n");
                  return STATUS_FAILED;
               }
               return STATUS_OK;
           }
           else {
               //Set timer in Dom
               timeNodeF121 = hdbobj->BindToElement(pdoc, "SRMServiceCallError/F121/Time");
               if(!timeNodeF121){
                   DEBUGL1("CResourceManager:HandleNotifyErrorRequest:SetModifiedTime Failed BindToElement \n");
                   return STATUS_FAILED;
               }
               epochTimeF121 = chelper::Itoa(currentTimeF121);
               timeNodeF121->setTextContent(epochTimeF121); 
               //Message log          
               
              //Email notification
               if(ItMessageLogMap != m_MessageLogMap.end()) {
                   CString errorMessage = ItMessageLogMap->second;
                   DEBUGL7("CSystemResourcemanager:HandleNotifyErrorRequest:Writing ErrorCode to the Message Log\n");
                   ret = ErrorNotificationWriteToLog(errorMessage);
                   if(ret !=STATUS_OK) {
                       DEBUGL1("CSystemResourceManager::Failed to Write ErrorCode to the MessageLog\n");
                   }
               }

              if(ItNotificationMap != m_NotificationMap.end()) {
                   CString errorMessage = ItNotificationMap->second;
                   ret = sendNotificationMessage(case_cast(errorCode));
                   if(ret != STATUS_OK) {
                       DEBUGL1("SystemResourceManager: Failed to send Notification Message\n");
                   }
              }

              AddToErrorList(ERROR_F121);       
              
               restartSystem();
               return STATUS_OK;
           }
       }
       // Check for Error F130
       if(!errorCode.compare(ERROR_F130)){
            
            time_t currentTimeF130 = time(NULL);
            CString epochTimeF130;
            // get Prev time from Dom for F130 Error;
            NodeRef timeNodeF130 = hdbobj->BindToElement(pdoc, "SRMServiceCallError/F130/Time");
            if(!timeNodeF130){
                DEBUGL1("CResourceManager:HandleNotifyErrorRequest:BindToElement Failed BindToElement \n");
                return STATUS_FAILED;
            }
            CString timeStrF130 = timeNodeF130->getTextContent();
            time_t prevTimeF130 = atoi(timeStrF130.c_str());
                     
	    DEBUGL8("CResourceManager:HandleNotifyErrorRequest:currentTimeF130: %d , prevTimeF130 :%d\n",currentTimeF130, prevTimeF130);
            //time Differences
            diffTotalSecs = currentTimeF130 - prevTimeF130;
            if ((diffTotalSecs <= TimeIntSec) && (!timeStrF130.empty())){
                DEBUGL8("CResourceManager:HandleNotifyErrorRequest:diffTotalSecs for F130 :%d\n",diffTotalSecs);               
                 //Set timer for F130
                epochTimeF130 = chelper::Itoa(currentTimeF130);
                timeNodeF130->setTextContent(epochTimeF130);
	        ret = CheckErrorAndSendServiceMessage(msg);
                if(ret != STATUS_OK){
		    DEBUGL1("SystemResourceManager:HandleNotifyErrorRequest: Failed to make Service Call\n");
                    return STATUS_FAILED;
                }
                return STATUS_OK; 
            }
            else {
                //Set timer for F130
                timeNodeF130 = hdbobj->BindToElement(pdoc, "SRMServiceCallError/F130/Time");
       		if(!timeNodeF130){
                    DEBUGL1("CResourceManager:HandleNotifyErrorRequest:SetModifiedTime Failed BindToElement \n");
                    return STATUS_FAILED;
                }
       		epochTimeF130 = chelper::Itoa(currentTimeF130);
       		timeNodeF130->setTextContent(epochTimeF130);
                // Error history
                AddToErrorList(ERROR_F130);
                restartSystem(); 
                return STATUS_OK;
            }
       }
    }
        
    // TODO: Change the data structure and need to integrate the following block to OnlyServiceMap.
    // Display "Reboot the machine" when license flag mismatch 
    if(!errorCode.compare(ERROR_FFFF0001)) {
        CNoCaseString dmy_errorCode="";
        CString errorMessage = "REBOOT THE MACHINE";
        ret =sendServiceMessage(errorMessage,case_cast(dmy_errorCode));
        if(ret!=STATUS_OK) {
            DEBUGL1("SystemResourceManager: Failed to make Service Call\n");
            return STATUS_FAILED;
        }
        return STATUS_OK;
    }
    DEBUGL4("SystemResourceManager: Calling serviceCall\n");
    CString errorMessage = "UNKNOWN ERROR";
    
    //If found in the map which contains only service call error codes.
    //Display the error & return. Do not find this error in other maps.
    if(ItOnlyServiceMap != m_OnlyServiceCallMap.end()) {
        errorMessage = ItOnlyServiceMap -> second;
        DEBUGL2("SystemResourceManager: Only SERVICE CALL DISPLAY is availabe for this Error [%s:%s]!\n", errorCode.c_str(), errorMessage.c_str());
        ret =sendServiceMessage(errorMessage,case_cast(errorCode));
        if(ret!=STATUS_OK) {
            DEBUGL1("SystemResourceManager: Failed to make Service Call\n");
            return STATUS_FAILED;
        }
        return STATUS_OK;
    }
    
    if(ItMessageLogMap != m_MessageLogMap.end()) {
        errorMessage = ItMessageLogMap->second;
        DEBUGL7("CSystemResourcemanager::Writing ErrorCode to the Message Log\n");
        ret = ErrorNotificationWriteToLog(errorMessage);
        if(ret !=STATUS_OK) {
            DEBUGL1("CSystemResourceManager::Failed to Write ErrorCode to the MessageLog\n");
        }
    }
    if(ItNotificationMap != m_NotificationMap.end()) {
        errorMessage = ItNotificationMap->second;
        ret = sendNotificationMessage(case_cast(errorCode));
        if(ret != STATUS_OK) {
            DEBUGL1("SystemResourceManager: Failed to send Notification Message\n");
        }
    }
    // Not to display SC when [0x7153] USB debug log 
    if(!errorCode.compare(ERROR_7153)) {
        return STATUS_OK;
    }
 
    // TODO: Data Structure should be changed.
    if((!errorCode.compare(ERROR_F521)) ||
       (!errorCode.compare(ERROR_F130)) ||
       (!errorCode.compare(ERROR_F510)) ||
       (!errorCode.compare(ERROR_F800)) ||
		 (!errorCode.compare(ERROR_F131))) {
        errorMessage = "CALL FOR SERVICE";
    }
    ret =sendServiceMessage(errorMessage,case_cast(errorCode));
    if(ret!=STATUS_OK) {
        DEBUGL1("SystemResourceManager: Failed to make Service Call\n");
        return STATUS_FAILED;
    }
    return STATUS_OK;
}

Status CSystemResourceManager::restartSystem() {
    DEBUGL8("SystemResourceManager:Entering Restart System\n");
    CString system = "_system";
    //Send system restart message to SSM 
    Status ret = m_ssmClient->Restart(system);
    if(ret != STATUS_OK) {  
        DEBUGL1("SystemResourceManager : Restart System Failed\n");
        return STATUS_FAILED;
    }
    DEBUGL4("Successfully send Restart request to SSM\n");
    return STATUS_OK;
}

Status CSystemResourceManager::sendServiceMessage(CString errorMessage, CString errorCode) {
    DEBUGL4("SystemResourceManager : Entering Send Service Call\n");
    //Define the AL Panel services which have to be stopped before the service call
    CString alpanel = CString(AL_PANEL_SERVICE);
    CString alrenderer = CString(AL_RENDERER_SERVICE);
    CString nsm = CString(AL_NSM);
    char * errString = new char[errorMessage.size()+1];
    memset(errString,'\0',errorMessage.size()+1);
    strcpy(errString,errorMessage.c_str());
    
    char * errCode = new char[errorCode.size()+1];
    memset(errCode,'\0',errorCode.size()+1);
    strcpy(errCode,errorCode.c_str());
    
    /* Call the lcdDispOn function to switch the lcd backlight on */
    lcdDispOn();
    /*
     *Kill the following process before proceeding with service Call
     */ 
    //Stopping PANEL services
    Status retStatus = m_ssmClient->Stop(alpanel);
    if(retStatus != STATUS_OK) {
        DEBUGL1("SystemResourceManager: Failed to stop AL PANEL service\n");
    }
    retStatus = m_ssmClient->Stop(alrenderer);
    if(retStatus != STATUS_OK) {
        DEBUGL1("SystemResourceManager: Failed to stop AL RENDERER service\n");
    }

    //Stopping nsm with delay of 5secs in child process for not delaying the SC error display
    int npid = fork();
    if(!npid)
    {
	    DEBUGL8("SystemResourceManager::Child process entered to kill nsm service\n");
	    sleep(5);
	    retStatus = m_ssmClient->Stop(nsm);
	    if(retStatus != STATUS_OK) {
		    DEBUGL1("SystemResourceManager: Failed to stop AL NSM service\n");
	    }
	    //Stopping ebx_dl and cissm
	    DEBUGL7("SystemResouceManager : Stopping ebx_dl, cissm services\n");
	    CString killciSSMCmd = "killall cissm";
	    system(killciSSMCmd.c_str());
	    CString killeBxCmd = "killall ebx_dl";
	    system(killeBxCmd.c_str());
	    DEBUGL8("SystemResourceManager::Child process exit after killing of nsm, ssm and ebx_dl service\n");
	    exit(retStatus);	
    }

    DEBUGL7("SystemResouceManager : Stopping syscallerr services\n");
    CString killsyscallErr = "service run_syscall_err stop";
    system(killsyscallErr.c_str());
    //Not checking for status as service call should be made irrespective of the status
    AddToErrorList(errorCode);
    
    int ret;
    /* Initialize the Display Panel, This function should be called
     * before calling any other function
     */
    ret = plt_conpanel_init ();
    if(ret != 0) {
        DEBUGL1("SystemResourceManager: plt_conpanel_init Failed with ret value %d\n",ret);
    }
    /* Call the plt_paintScreen function to clear the screen */
    DEBUGL7("SystemResourceManager: Clearing the Screen\n");
    ret = plt_paintScreen();
    if(ret != 0) {
        DEBUGL1("SystemResourceManager: plt_paintScreen Failed with ret value %d\n",ret);
    }
    //Initialize values for making platform serice call
    char counter[256];
    char scerrcode[256];
    memset(counter, '\0', sizeof(counter));
    memset(scerrcode, '\0', sizeof(scerrcode));
    
    string envvar;
    envvar = getenv("PRODUCT");
    if(envvar == "MASH" || envvar == "LOIRE" || envvar == "ALABAMA" || envvar == "WEISS"||  envvar == "S2_PRODUCT") {
        ret = plt_ServiceCallUI_1(errString,scerrcode,errCode,counter);
        if(ret != 0) {
            DEBUGL1("CSystemResourceManger::sendServiceMessage:Failed to Display Error on Screen\n");
            retStatus = STATUS_FAILED;
        }
    } else if(envvar == "BP") {
        ret = plt_ServiceCallUI_2(errString,scerrcode,errCode,counter);
        if(ret!=0) {
            DEBUGL1("CSystemResourceManger::sendServiceMessage:Failed to Display Error on Screen for BP\n");
            retStatus = STATUS_FAILED;
        }
    } else {
        DEBUGL1("CSystemResourceMangaer::sendServiceMessage: Failed to determine Machine Type \n");
    }
    if(retStatus != STATUS_OK) {
        DEBUGL1("CSystemResourceMangaer::sendServiceMessage: Failed to determine Machine Type \n");
        if(errString)   delete [] errString;
        if(errCode)     delete [] errCode;
        return STATUS_FAILED;
    }
    ret = plt_led_on(LED_ERROR);
    if(errString) delete [] errString;
    if(errCode) delete [] errCode;
    if(ret != 0) {
       DEBUGL1("CSystemResourceManager::sendServiceMessage to set Error LED On\n");
       return STATUS_FAILED;
    }
    return STATUS_OK;
}
Status CSystemResourceManager::HandleResetErrorCountRequest(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
   DEBUGL4("Reset ErrorCount Called\n");
   if(!msg)
   {
      DEBUGL1("CSystemResourceManager::HandleResetErrorCountRequest : Invalid msg Parameter\n");
      return STATUS_FAILED;
   }
   char* msgContent = static_cast<char *>(msg->GetContentPtr());
   DocumentRef pResetErrorCountDoc = NULL;   //Refer to the SystemResourceManager.xsd for payload XML
   CString sDocID = getUniqueFileName("ResetErrorCount");
   CString errorCode = "";

   // Read the request message 
   if (m_pHDB->CreateTempDocument(sDocID,pResetErrorCountDoc,msgContent) != STATUS_OK)
   { 
      DEBUGL1("SystemResourceManager:ResetErrorCountRequests: CreateTempDocument of ResetErrorCount request failed !\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
   if(!pResetErrorCountDoc)
   {
      DEBUGL1("SystemResourceManager:ResetErrorCountRequests: CreateTempDocument of ResetErrorCount request failed !\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
   NodeRef pError = pResetErrorCountDoc->getDocumentElement();
   Ref<NodeList> pListOfNodes = pError->getChildNodes();
   try {
   for(uint32 i=0;i<pListOfNodes->getLength();i++)
   {
      Ref<Node> pNode = pListOfNodes->item(i);
      if(!pNode || pNode->getNodeType() != Node::ELEMENT_NODE)
         continue;
      ElementRef commonElement = pNode;
      //Get the ErrorCode value whose error Count has to be reset
      if(commonElement->getNodeName()=="ErrorCode")
      {
         errorCode = commonElement->getTextContent();
      }
   }
   if(errorCode.empty())
   {
      //If the Payload does not contain an error code, return STATUS_FAILED
      AddNewNode(pError,"Status","STATUS_FAILED");
   }
   else
   {
      AddNewNode(pError,"Status","STATUS_OK");
   }
   } catch (DOMException except) {
      DEBUGL1("CSystemResourceManager::HandleResetErrorCountRequest : Failed due to HDB Exception\n");
      SendOffNormalResponse(msg,sDocID);
      return STATUS_FAILED;
   }
   CString  sResetErrorCountResponse="";
   Status ret=STATUS_OK;
   ret = m_pHDB->Serialize(pResetErrorCountDoc,sResetErrorCountResponse);
   if( ret == STATUS_OK )
   {
      //Send the response message for the request
      ret = SendResponse(msg,sResetErrorCountResponse, sDocID);
   }
   else
   {
      if(ret == STATUS_DISK_FULL)
      {
         DEBUGL1("SystemResourceManager: Serialize() Failed: Disk Full Returned\n");
      }
      DEBUGL1("SystemResourceManager:HandleResetErrorCountRequest: Serialize operation failed while processing request\n");
      ret = SendOffNormalResponse(msg,sDocID);
   }
   ci::operatingenvironment::Ref<ci::indexeddb::IndexedDB>  indexDBObj = ci::indexeddb::IndexedDB::Acquire("STORAGE");
   if(!indexDBObj)
   {
      DEBUGL1("SystemResourceManager:: Acquiring IndexedDB failed\n");
      return STATUS_FAILED;
   }
   char * endPtr = NULL;
   uint64 errorCodeKey = strtoull(errorCode.c_str(),&endPtr,10);
   if(!indexDBObj->KeyExists(errorCodeKey))
   {  
      DEBUGL1("SystemResourceManager:: ErrorCode[%llu] does not exist in the IndexedDB\n",errorCodeKey);
      return STATUS_FAILED;
   }
   //Reset ErrorCode count value to zero in IndexedDB
   int value = 0;
   ret = indexDBObj->SetIntValue(errorCodeKey,value);
   if(ret!=STATUS_OK)
   {
      DEBUGL1("SystemResourceManager: Failed to increment error count in IndexDB for Error[%llu]\n",errorCodeKey);
      return STATUS_FAILED;
   }
   return STATUS_OK;	
}
Status CSystemResourceManager::InitializePartitionSizeMap(CString HDD_str)
{
	DEBUGL1("CSystemResourceManager::InitializePartitionSizeMap: Size of HDD is %s \n", HDD_str.c_str());
	if (HDD_str == HDD_8_GB)
	{
		m_HDD_partition.insert(pair<CString,CString>(CString("/system"),CString("3758096384")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/work"),CString("966367642")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/registration"),CString("536870912")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/backup"),CString("107374182")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/imagedata"),CString("1610612736")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/storage"),CString("0")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/swap"),CString("1610612736")));
		return STATUS_OK;
	}
	else if(HDD_str == HDD_80_GB)
	{

		m_HDD_partition.insert(pair<CString,CString>(CString("/system"),CString("8053063680")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/work"),CString("10737418240")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/registration"),CString("3221225472")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/backup"),CString("1073741824")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/imagedata"),CString("26843545600")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/storage"),CString("30064771072")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/swap"),CString("2147483648")));
		return STATUS_OK;
	}
	else 
	{
		m_HDD_partition.insert(pair<CString,CString>(CString("/system"),CString("9126805504")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/work"),CString("27380416512")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/registration"),CString("3221225472")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/backup"),CString("1073741824")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/imagedata"),CString("34359738368")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/storage"),CString("90194313216")));
		m_HDD_partition.insert(pair<CString,CString>(CString("/swap"),CString("2147483648")));
		return STATUS_OK;
	}

}
Status CSystemResourceManager::InitializeErrorMessageMap()
{
   //Map for Message Log support
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF103"),CString("F103")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F103"),CString("F103")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF104"),CString("F104")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F104"),CString("F104")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF121"),CString("F121")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F121"),CString("F121")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF123"),CString("F123")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F123"),CString("F123")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF400"),CString("F400")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F400"),CString("F400")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF550"),CString("F550")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F550"),CString("F550")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF700"),CString("F700")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F700"),CString("F700")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF521"),CString("F521")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F521"),CString("F521")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0x7153"),CString("7153")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("7153"),CString("7153")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF131"),CString("F131")));//added to support detecting of filtering file corruption
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F131"),CString("F131")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF410"),CString("F410")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F410"),CString("F410")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF130"),CString("F130")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F130"),CString("F130")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF125"),CString("F125")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F125"),CString("F125")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF124"),CString("F124")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F124"),CString("F124")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF126"),CString("F126")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F126"),CString("F126")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF127"),CString("F127")));
   m_MessageLogMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F127"),CString("F127")));

   
   //Map for Notification support
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF103"),CString("HDD TIMEOUT")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F103"),CString("HDD TIMEOUT")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF104"),CString("CRC ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F104"),CString("CRC ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF121"),CString("USER MANAGEMENT DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F121"),CString("USER MANAGEMENT DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF122"),CString("MESSAGE LOG DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F122"),CString("MESSAGE LOG DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF123"),CString("JOB LOG DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F123"),CString("JOB LOG DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF200"),CString("SECURITY ENABLER ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F200"),CString("SECURITY ENABLER ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF400"),CString("CPU FAN ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F400"),CString("CPU FAN ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF550"),CString("ENCRYPTED PARTITION ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F550"),CString("ENCRYPTED PARTITION ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF700"),CString("OVER WRITE ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F700"),CString("OVER WRITE ERROR")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF131"),CString("Printer Needs Attention: Call for service")));//added to support detecting of filtering file corruption
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F131"),CString("Printer Needs Attention: Call for service")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF410"),CString("CALL FOR SERVICE")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F410"),CString("CALL FOR SERVICE")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF130"),CString("CALL FOR SERVICE")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F130"),CString("CALL FOR SERVICE")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF125"),CString("HOME SCREEN DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F125"),CString("HOME SCREEN DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF124"),CString("APP MANAGEMENT DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F124"),CString("APP MANAGEMENT DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF126"),CString("JOB HISTORY DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F126"),CString("JOB HISTORY DB CORRUPTION")));
    m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF127"),CString("APP LICENSE DB CORRUPTION")));
   m_NotificationMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F127"),CString("APP LICENSE DB CORRUPTION")));

   //Map for Error History List
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F130"),CString("28")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF130"),CString("28")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F200"),CString("400")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF200"),CString("400")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F121"),CString("701")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF121"),CString("701")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F122"),CString("702")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF122"),CString("702")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F521"),CString("731")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF521"),CString("731")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F800"),CString("800")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF800"),CString("800")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F400"),CString("670")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF400"),CString("670")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F410"),CString("671")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF410"),CString("671")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F125"),CString("705")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF125"),CString("705")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F124"),CString("704")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF124"),CString("704")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F126"),CString("706")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF126"),CString("706")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F127"),CString("707")));
   m_ErrorHistListErrorMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF127"),CString("707")));

   //Only Service Call errors. No Notification/Error History/MessageLog is performed for below errors.
   m_OnlyServiceCallMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF105"),CString("HDD ERROR")));
   m_OnlyServiceCallMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F105"),CString("HDD ERROR")));
   //m_OnlyServiceCallMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF124"),CString("CALL FOR SERVICE")));
   //m_OnlyServiceCallMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F124"),CString("CALL FOR SERVICE")));
   m_OnlyServiceCallMap.insert(pair<CNoCaseString,CString>(CNoCaseString("0xF901"),CString("CALL FOR SERVICE")));
   m_OnlyServiceCallMap.insert(pair<CNoCaseString,CString>(CNoCaseString("F901"),CString("CALL FOR SERVICE")));
 
  // for Error check F121 and F130
   m_CheckServiceCallMap.insert(pair<int,int>(int(0),int(0)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(1),int(10)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(2),int(30)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(3),int(60)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(4),int(360)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(5),int(720)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(6),int(1440)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(7),int(2880)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(8),int(10080)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(9),int(43800)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(10),int(525600)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(11),int(2628000)));
   m_CheckServiceCallMap.insert(pair<int,int>(int(12),int(-1)));
   return STATUS_OK;
}

Status CSystemResourceManager::InitializePartitionNameMap()
{
    FILE *fp;
    fp = setmntent(MOUNTED, "r");
    if(!fp)
    {
        DEBUGL1("SystemResourceManager:GetMountPoint: setmntent() call failed\n");
        return STATUS_FAILED;
    }
    struct mntent *tmpmntent;
    while((tmpmntent=getmntent(fp)))
    {    
         //Get the MountPoint Directories
         CString mntDir = CString(tmpmntent->mnt_dir);
         //Check if the Partitions are either modified for Encryption or Secure Erase
         size_t findPos = mntDir.find("enc_");
         if(findPos == string::npos)
         {
            findPos = mntDir.find("sec_");
         }
         if(findPos!=string::npos)
         {
            //Create the Map for the modified partition names.
            CString origName = "/" + mntDir.substr(findPos+4);
            m_PartitionNameMap.insert(pair<CString,CString>(origName,mntDir));
            //For Reverse lookup
            m_PartitionNameMap.insert(pair<CString,CString>(mntDir,origName));
            DEBUGL7("Map %s to %s\n",origName.c_str(),mntDir.c_str());
         }
    }
    fclose(fp);
    DEBUGL7("SystemResourceManager::Initialize Partition Name Map completed\n");
    return STATUS_OK;
}
Status CSystemResourceManager::ModifyForPartitionNameChange(CString & path)
{

   size_t findPos = path.find('/',2);
   CString mntPath = path;
   if(findPos!=string::npos)
   {
      //Get the mount path directory
      mntPath=path.substr(0,findPos);
   }
   DEBUGL7("mntPath = %s\n",mntPath.c_str());
   map<CString,CString>::iterator It = m_PartitionNameMap.find(mntPath);
   if(It != m_PartitionNameMap.end())
   {
      //Get the modified mount path name
      CString modifiedPathName = It->second;
      path.replace(0,mntPath.size(),modifiedPathName);
   }
   else if (File::Exists("/work/ci/secureEraseFlag"))
   {
    FILE *fp;
    fp = setmntent(MOUNTED, "r");
    if(!fp)
    {
        DEBUGL1("SystemResourceManager::ModifyForPartitionNameChange: setmntent() call failed\n");
        return STATUS_FAILED;
    }
    struct mntent *tmpmntent;
    while((tmpmntent=getmntent(fp)))
    {    
	    //Get the MountPoint Directories
	    CString mntDir = CString(tmpmntent->mnt_dir);
	    CString mntPathCmp = mntPath.substr(1);
	    //We need to insert the entry in m_PartitionNameMap only for required partition
	    if (mntDir.find(mntPathCmp) == string::npos)
		    continue;
	    //Check if the Partitions are either modified for Encryption or Secure Erase
	    size_t findPos = mntDir.find("enc_");
	    if(findPos == string::npos)
	    {
		    findPos = mntDir.find("sec_");
	    }
	    if(findPos!=string::npos)
	    {
		    //Create the Map for the modified partition names.
		    CString origName = "/" + mntDir.substr(findPos+4);
		    m_PartitionNameMap.insert(pair<CString,CString>(origName,mntDir));
		    //For Reverse lookup
		    m_PartitionNameMap.insert(pair<CString,CString>(mntDir,origName));
		    path.replace(0,mntPath.size(), mntDir);
		    DEBUGL7("SystemResourceManager::ModifyForPartitionNameChange: Map %s to %s, replaced path:%s\n",origName.c_str(),mntDir.c_str(), path.c_str());
	    }
	    break;
    }
    fclose(fp);
    DEBUGL7("SystemResourceManager::ModifyForPartitionNameChange: Update Partition Name Map check completed\n");	
   }

   return STATUS_OK;
}

Status CSystemResourceManager::ErrorNotificationWriteToLog(CString ErrorNumber)
{
   //Read the Notification message from Presentatiton Resource Manager
   Ref<ci::resourcemanagement::ResourceManager> pm;
   if(!(pm=ResourceManager::Acquire()))
   {
      DEBUGL1("CSystemResourceManager::ErrorNotificationWriteToLog:Failed to acquire ResourceManager\n");
      return STATUS_FAILED;
   }
   int msgID = ERROR_CODE_MESSAGE_LOG_ID;
   DEBUGL7("Value of resource ID = %d\n",msgID);
   CString data = "";
   DEBUGL4("Calling CI::PresentationResources::GetMessage\n");
   //Calling ResourceManager::GetMessage()
   pm->GetMessage(msgID,CString("PANEL_RENDERER_SESSION"),data,true,CString(""),CString(""),CString(""));
   DEBUGL7("Get Message Returned with data = [%s]\n",data.c_str());
   
   //Obtaining the SSDK userToken
   ssdk::SSDKStatus retStatus = ssdk::OK;
   SSDKSecurityManagerInterface* ssdkSecurityManagerInterface = ssdk::GetSecurityManager();
   SSDKUserTokenInterface *userToken = NULL;
   SSDKUserTokenInterface* usertokenInterface = dynamic_cast<SSDKUserTokenInterface*> (ssdkSecurityManagerInterface->GetInterface(SSDKSecurityManagerInterface::USER_TOKEN_INTERFACE,retStatus));
   if(!usertokenInterface || retStatus!=ssdk::OK)
   {
      DEBUGL1("CSystemResourceManager::Failed to obtain SSDK UserToken Interface\n");
      return STATUS_FAILED;
   }
   DEBUGL7("CSystemResourceManager::Obtained UserToken Interface Successfully\n");
   //Get Autoprocessing token
   retStatus = usertokenInterface->Authenticate(ssdk::SSDKUserTokenInterface::TOKEN_CONTEXT_AUTOPROCESSING, userToken);
   if ((retStatus != ssdk::OK) && (retStatus != ssdk::AUTH_GOOD_AUTHENTICATION_NEEDS_PASSWORD_CHANGE))
   {
      DEBUGL1("CSystemResourceManager::Failed to obtain SSDK UserToken\n");
      return STATUS_FAILED;
   }
   if( userToken == NULL )
   {
      DEBUGL1("CSystemResourceManager:: User Token Returned in NULL\n");
      return STATUS_FAILED;
   }
   DEBUGL7("CSystemResourceManager::User Token Authenticated\n");
   //Obtaining a log pointer
   Ref<ci::logmanager::LogInterface> logInterface = ci::logmanager::LogInterface::getLogInterface(retStatus);
   if( retStatus != ssdk::OK )
   {
      DEBUGL1("CSystemResourceManager::Failed to get LogInterface\n");
      return STATUS_FAILED;
   }
   DEBUGL7("CSystemResourceManager::LogInterface Obtained\n");
   //Setting the MessageLogRecord Parameters.
   LogInterface::MsgLogRecord messageLogRecord;
   messageLogRecord.logLevel = ci::logmanager::LogInterface::llError;
   messageLogRecord.ErrorCode = ErrorNumber;
   messageLogRecord.MessageID = data;
   messageLogRecord.operationType = ci::logmanager::LogInterface::opUnknown;
   messageLogRecord.operationTarget = ci::logmanager::LogInterface::otUnknown;
   messageLogRecord.OperationApplication = ci::logmanager::LogInterface::OA_Application;
   messageLogRecord.timeOfLogEntry = time(NULL);
   //Write to the MessageLog
   DEBUGL7("CSystemResourceManager::Calling WriteMsgLogEntry\n");
   retStatus = logInterface->WriteMsgLogEntry(userToken, messageLogRecord);
   if(retStatus != ssdk::OK)
   {
      DEBUGL1("CSystemResourceManager::Failed to Write Message Log Entry\n");
      return STATUS_FAILED;
   }
   else
   {
      DEBUGL7("CSystemResourceManager::Message Log Entry was successfull\n");
   }
   return STATUS_OK;
}

Status CSystemResourceManager::AddToErrorList(CString errCode)
{
   //Check if error code is present in the ErrorHistList Map
   //Only error codes present in the Map are written into the ErrorHistList file
   map<CNoCaseString,CString>::iterator ItErrorHistListMap = m_ErrorHistListErrorMap.find(case_cast(errCode));
   if(ItErrorHistListMap == m_ErrorHistListErrorMap.end())
   {
        DEBUGL1("CSystemResourceManager: ErrorCode Not present in ErrorHist List Map, so not writing to ErrorhistList\n");
        return STATUS_FAILED;
   }
   else
   {
       //Get the error codes as expected by DL
       errCode =  ItErrorHistListMap->second;
   }
   if(!File::Exists(ERRORLISTFILE))
   {
        //Creating Empty file with 1000 entries 
        ErrorNode entry;
        memset(&(entry.val),0,sizeof(entry.val));
        FILE *filePtr = fopen(ERRORLISTFILE,"wb");
        if(!filePtr)
        {
            DEBUGL1("Failed to create ErrorHistory List File with errno = %d\n", errno);
            return STATUS_FAILED;
        }
        //Writing 1000 empty entries
        for(int i=0;i<=1000;i++)
        {
            fwrite(&(entry.val),sizeof(entry.val),1,filePtr);
        }
        fclose(filePtr);
    
   }
   list<ErrorNode> errorNodeList;
   list<ErrorNode>::iterator it= errorNodeList.begin();
   ErrorNode errorNode,entry;
   //Initializing the Entry
   memset(&(entry.val),0,sizeof(entry.val));
   int errorNodeCount = 0;
   FILE *filePtr = fopen(ERRORLISTFILE,"rb");
   if(!filePtr)
   {  
      DEBUGL1("Failed to open ErrorList File for read with errno = %d\n", errno);
      return STATUS_FAILED;
   }
   //Read Error Entries from file to List
   while(fread(&(errorNode.val),sizeof(errorNode.val),1,filePtr)!=0)
   {
      //Push Each Entry into the ErrorNode List
      errorNodeList.push_back(errorNode);
      if(errorNode.val.hErrCod)
      {
         //Update the ErrorNodeIterator with the latest ErrorNode entry
         it++;
         errorNodeCount++;
      }
   }
   fclose(filePtr);
   //Get the New ErrorNode entry to be added
   getNewErrorEntry(errCode,entry);
   //Insert into the Error Node List
   it++;
   if(it == errorNodeList.end())
      errorNodeList.push_back(entry);
   else
      errorNodeList.insert(it,entry);
   //Update the errorNode count
   errorNodeCount++;
   
   //Trim the Error List to 1000 Entries
   while(errorNodeCount>1000)
   {
      errorNodeList.pop_front();
      errorNodeCount--;
   }
   while(errorNodeList.size()>1000)
   {
      errorNodeList.pop_back();
   }
   filePtr = fopen(ERRORLISTFILE,"wb");
   if(!filePtr)
   {  
      DEBUGL1("Failed to open ErrorList File for write with errno = %d\n", errno);
      return STATUS_FAILED;
   }
   for(it=errorNodeList.begin();it!=errorNodeList.end();it++)
   {
      //Write the values back into the file
      entry.val = it->val;
      if(!entry.val.aPapKind)
      entry.val.aPapKind=0x30;
      fwrite(&(entry.val),sizeof(entry.val),1,filePtr);
   }
   fclose(filePtr);
   DEBUGL4("CSystemResourceManager::Exiting from AddToErrorHist List\n");
   return STATUS_OK;
}

Status CSystemResourceManager::getNewErrorEntry(CString errCode, ErrorNode & entry)
{
   //Initializing the Entry
   DEBUGL4("SystemResourceManager : Generating ErrorHist Entry for [%s]\n",errCode.c_str());
   time_t rawtime;
   time(&rawtime);
   struct tm * curTime = localtime(&rawtime);
   char aTime[6], aToAscii[12];
   aTime[0] = (curTime->tm_year % 100); /* 00-99 Year value    */
   aTime[1] = (curTime->tm_mon + 1);    /* 01-12 Month Value   */
   aTime[2] = (curTime->tm_mday);       /* 01-31 Date value    */
   aTime[3] = (curTime->tm_hour);       /* 00-23 Hour value    */
   aTime[4] = (curTime->tm_min);        /* 00-59 Min  value    */
   aTime[5] = (curTime->tm_sec);        /* 00-59 Sec  value    */
   //Generating TimeStamp as per Spec
   convertTime(aTime, aToAscii);
   //Updated to use new errCode as per request
   entry.val.hErrCod = (uint16)atoi(errCode.c_str());
   //Get the current time for the error
   strncpy(entry.val.aDate,aToAscii,12);
   return STATUS_OK;
}

Status CSystemResourceManager::sendNotificationMessage(CString errCode)
{
   vector<CString> mailID;
   Status st = getNotificationEmailID(mailID);
   if(st != STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager::Failed to get MailID's from ControllerDOM\n");
      return STATUS_FAILED;
   }
   if(mailID.size()==0)
   {
      DEBUGL2("CSystemResourceManager::sendNotificationMessage : No Email ID registerd\n");
      return STATUS_OK;
   }
   CString messageToSend;
   st = createNotificationMessage(mailID, errCode, messageToSend);
   if(st != STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager::Failed to create MessageNotification Payload\n");
      return STATUS_FAILED;
   }
   st = sendReportManagerMessage(messageToSend);
   if(st != STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager::Failed to send MessageNotification to ReportManager\n");
      return STATUS_FAILED;
   }
   DEBUGL4("CSystemResourceManager::Successfully sent Notification message to ReportManager\n");
   return STATUS_OK;
}
Status CSystemResourceManager::getNotificationEmailID(vector<CString> &mailID)
{
   //Get the Notification Addresses from the Controller DOM
   HierarchicalDBRef pHDB;
   NodeRef tempNode = NULL;
   if (!(pHDB = ci::hierarchicaldb::HierarchicalDB::Acquire())) 
   {
      DEBUGL1("CSystemResourceManager:: Failed to acquire HierarchicalDB \n");
      return STATUS_FAILED;
   }
   dom::DocumentRef opendoc;
   //Open the controller DOM
   Status st = pHDB->OpenDocument("/work/al/etc/dom","Controller",opendoc);
   if(st != STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager::getNotificationEmailID: Failed to open Controller DOM\n");
      return STATUS_FAILED;
   }
   if(pHDB->BeginTransaction(opendoc,eREAD,true) != STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager:: Failed to Lock Controller DOM\n");
   }
   //Get the Notification Email IDs
   const int MAX_NO_ADMIN_MAIL_IDS = 3;
   for(int i = 1; i <= MAX_NO_ADMIN_MAIL_IDS; i++)
   {
      char indexval[2];
      sprintf(indexval,"%d",i);
      //Check if Notification is enabled for the Email ID
      CString XpathEnabled = "Controller/Settings/Notifications/Destinations/Email[@index=\"" + CString(indexval) + "\"]/Enabled";
      NodeRef tempNode = pHDB->BindToElement(opendoc,XpathEnabled);
      if(tempNode)
      {
         CString val = chelper::GetNodeValue(tempNode,tempNode->getNodeValue().c_str());
         if(val.compare("false"))
         {
            CString Xpath = "Controller/Settings/Notifications/Destinations/Email[@index=\"" + CString(indexval) + "\"]/Address";
            tempNode = pHDB->BindToElement(opendoc,Xpath);
            if(tempNode)
            {  
               //Push the Mail ID into the list if Notification is enabled
               CString valAddress = chelper::GetNodeValue(tempNode,tempNode->getNodeValue().c_str());
               mailID.push_back(valAddress);
            }
         }
      }
   } 
   pHDB->EndTransaction(opendoc,eREAD);
   return STATUS_OK;
}


Status CSystemResourceManager::createNotificationMessage(vector<CString> mailID, CString errorCode, CString &result)
{
   //Generate the message Payload for Notification Message
   DEBUGL4("CSystemResourceMangaer::createNotificationMessage Enter\n");

   std::stringstream commandStream;
   commandStream << "<Command>";
   commandStream <<   "<GenerateReport>";
   commandStream <<   "<sessionID>NOTIFY_ERROR</sessionID>";
   commandStream <<     "<commandNode>Reporting</commandNode>";
   commandStream <<     "<Params>";
   commandStream <<       "<MessageNotification>";
   commandStream <<         "<ErrorCode>" << errorCode << "</ErrorCode>";
   commandStream <<       "</MessageNotification>";
   commandStream <<       "<Destination>";
   commandStream <<         "<email>";
   for (vector<CString>::const_iterator it = mailID.begin(); it != mailID.end(); it++)
   {
      commandStream <<       "<mailAddress>";
      commandStream <<         "<address>" << (*it) << "</address>";
      commandStream <<         "<destinationType>To</destinationType>";
      commandStream <<       "</mailAddress>";
   }
   commandStream <<         "</email>";
   commandStream <<       "</Destination>";
   commandStream <<     "</Params>";
   commandStream <<   "</GenerateReport>";
   commandStream << "</Command>";

   result = commandStream.str();

   return STATUS_OK;
}

Status CSystemResourceManager::sendReportManagerMessage(CString result)
{
   ci::messagingsystem::MsgPortRef AL_InterfacePort;
   Status ret = ci::messagingsystem::MsgPort::Create(AL_InterfacePort);
   if(ret!=STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager::sendReportManagerMessage : Failed to create Msg Port\n");
      return STATUS_FAILED;
   }
   DEBUGL6("CSystemResourceManager::sendReportManagerMessage: Successfully created message Port [%d]\n",(uint32)AL_InterfacePort->GetId());
   
   Msg msg(MSG_TYPE_EVENT,result.c_str(),result.size());
   msg.SetId(CID_AL_BO_SERVER);   //Only CID_AL_BO_SERVER msg ID is defined in AL ReportManager to handle Notification messages.
   msg.SetTarget(MSGPORT_AL_REPORT_MANAGER);
   
   ret = AL_InterfacePort->Send(msg);
   if(ret != STATUS_OK)
   {
      DEBUGL1("CSystemResourceManager::sendReportManagerMessage : Failed to send Message\n");
      return STATUS_FAILED;
   }
   return STATUS_OK;
}
Status CSystemResourceManager::convertTime(char * aTime, char * aToAscii)
{
   //Converting time format as per specification provided in
   //SYSROM_SRC/dev/DL/eB3_Wrapper/cmn/rtcDrvEx.c
   DEBUGL4("CSystemResourceManager::Converting Time format\n");
   char i, aData;
   int aK1, aK2;
   for(i=0;i<6;i++)
   {
       aData = *aTime++;
       if(aData > 9)
       {
         aK2 = (aData/10);
         aK1 = (aData%10);
         *aToAscii = aK2 + 0x30;
         aToAscii++;
         *aToAscii = aK1 + 0x30;
         aToAscii++;
        }
        else
        {
            *aToAscii = 0x30;
            aToAscii++;
            *aToAscii = aData + 0x30;
            aToAscii++;
        }
   }
   return STATUS_OK;
}

Status CSystemResourceManager::StartSleepTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        Status ret=STATUS_OK;
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartSleepTimer : Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
	//To measure the time of the current power status
	if (STATUS_OK != StopTimer(msg))
	{
		DEBUGL1("CSystemResourceManager::StartSleepTimer : Stop timer failed \n");
                return STATUS_FAILED;
	}

        CString sMsgname = "StartSleepTimer";

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerPower)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartSleepTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

	m_iPowerType = 1;

	DEBUGL8("CSystemResourceManager::StartSleepTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StartDeepSleepTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartDeepSleepTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
	//To measure the time of the current power status
	if (STATUS_OK != StopTimer(msg))
	{
		DEBUGL1("CSystemResourceManager::StartDeepSleepTimer : Stop timer failed \n");
                return STATUS_FAILED;
	}

        CString sMsgname = "StartDeepSleepTimer";
	Status ret=STATUS_OK;
	struct timeval tvDSTimer;

	errno = 0;
	/* Populate current time */
        if(gettimeofday(&tvDSTimer, 0)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartDeepSleepTimer: gettimeofday Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

	m_timerPower.tv_sec = tvDSTimer.tv_sec + (tvDSTimer.tv_usec/1000000);
	m_timerPower.tv_nsec = 0x00L;

	m_iPreviousPowerType = m_iPowerType;
	m_iPowerType = 2;
	DEBUGL8("CSystemResourceManager::StartDeepSleepTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StartEnergySaveTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartEnergySaveTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
	//To measure the time of the current power status
	if (STATUS_OK != StopTimer(msg))
	{
		DEBUGL1("CSystemResourceManager::StartEnergySaveTimer : Stop timer failed \n");
                return STATUS_FAILED;
	}

        CString sMsgname = "StartEnergySaveTimer";
	Status ret=STATUS_OK;

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerPower)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartEnergySaveTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }
	
	m_iPowerType = 3;
	DEBUGL8("CSystemResourceManager::StartEnergySaverTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StartReadyTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartReadyTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
	//To measure the time of the current power status
	if (STATUS_OK != StopTimer(msg))
	{
		DEBUGL1("CSystemResourceManager::StartReadyTimer : Stop timer failed \n");
                return STATUS_FAILED;
	}

        CString sMsgname = "StartReadyTimer";
	Status ret=STATUS_OK;

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerPower)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartReadyTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

	m_iPowerType = 4;
	DEBUGL8("CSystemResourceManager::StartReadyTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StartWarmingUpTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartWarmingUpTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
	//To measure the time of the current power status
	if (STATUS_OK != StopTimer(msg))
	{
		DEBUGL1("CSystemResourceManager::StartWarmingUpTimer : Stop timer failed \n");
                return STATUS_FAILED;
	}

        CString sMsgname = "StartWarmingUpTimer";
	Status ret=STATUS_OK;

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerPower)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartWarmingUpTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

	m_iPowerType = 5;
	DEBUGL8("CSystemResourceManager::StartWarmingUpTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StartPreviousTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartPreviousTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
	//To measure the time of the current power status
	if (STATUS_OK != StopTimer(msg))
	{
		DEBUGL1("CSystemResourceManager::StartPreviousTimer : Stop timer failed \n");
                return STATUS_FAILED;
	}

        CString sMsgname = "StartPreviousTimer";
	Status ret=STATUS_OK;
	
	// Starting previous timer
	errno = 0;
        /* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerPower)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartPreviousTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

	m_iPowerType = m_iPreviousPowerType;

	DEBUGL8("CSystemResourceManager::StartPreviousTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::UpdateSRAMForPowerTimer(int iSRAMSubCode)
{
	int cumTotalSecs;
	struct timespec cumPTimer;
	uint64 iIdbPowerMonitorKey;

	cumPTimer.tv_sec = 0x00L;

	if (clock_gettime(CLOCK_MONOTONIC, &cumPTimer)==0)
	{
		cumPTimer.tv_sec= cumPTimer.tv_sec + (cumPTimer.tv_nsec/1000000000);
		if (m_pIdbPowerMonitor)
		{
			iIdbPowerMonitorKey = IndexedDB::CreateKey(8, 3618, iSRAMSubCode);
			if (m_pIdbPowerMonitor->KeyExists(iIdbPowerMonitorKey))
			{
				cumTotalSecs = m_pIdbPowerMonitor->GetIntValue(iIdbPowerMonitorKey);
				cumPTimer.tv_sec = cumPTimer.tv_sec - (m_timerPower.tv_sec + (m_timerPower.tv_nsec/1000000000));

				cumTotalSecs = cumTotalSecs + (int)cumPTimer.tv_sec;
				if(cumTotalSecs <= 0)
				{
					DEBUGL1("CSystemResourceManager::UpdateSRAMForPowerTimer: The cummulative value < 0=%d \n", cumTotalSecs);
					return STATUS_FAILED;
				}
				DEBUGL8("CSystemResourceManager::UpdateSRAMForPowerTimer: cumTotalSecs= %d\n", cumTotalSecs);
				if( m_pIdbPowerMonitor->SetIntValue(iIdbPowerMonitorKey, cumTotalSecs) !=STATUS_OK)
				{
					DEBUGL1("CSystemResourceManager::UpdateSRAMForPowerTimer: SetIntValue Failed for Ready time \n");
					return STATUS_FAILED;
				}
			}
			else
			{
				DEBUGL1("CSystemResourceManager::UpdateSRAMForPowerTimer: 08-3618 key does not exists\n");
				return STATUS_FAILED;
			}
		}
	}
	else
	{
		DEBUGL1("CSystemResourceManager::UpdateSRAMForPowerTimer: clock_gettime failed\n");
		return STATUS_FAILED;
	}
	return STATUS_OK;
}

Status CSystemResourceManager::StopTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
	Status ret=STATUS_OK;
	if(!msg)
	{
		DEBUGL1("CSystemResourceManager::StopTimer: Invalid msg Parameter\n");
		//SendOffNormalResponse(msg,"StopTimer");
		ret = STATUS_FAILED;
	}
	//Measure the time of the current power status and update the cumulative total time taken in SRAM [via 08 codes]
	int cumTotalSecs;
	struct timeval tv;
	struct timespec cumPTimer;
	uint64 iIdbPowerMonitorKey;

	cumPTimer.tv_sec = 0x00L;

	int32 msgID = msg->GetId();
	//if m_iPowerType=-1: No timer is still not started hence not updating SRAM values
	
	if (m_iPowerType == -1)
	{
		DEBUGL2("CSystemResourceManager::StopTimer: There is no Power Monitor timer is running. Hence 08 codes are not updated\n");
		ret = STATUS_OK;
	}
        else if (m_iPowerType == 1)
        {
		ret = UpdateSRAMForPowerTimer(0);
        }
	else if (m_iPowerType == 2) 
	{
		//It is not possible to measure deep sleep time by clock_gettime(CLOCK_MONOTONIC) hence we use gettimeofday()
		if (0 == gettimeofday(&tv,0))
		{
			tv.tv_sec= tv.tv_sec + (tv.tv_usec/1000000);
		}
		else
		{
			DEBUGL1("CSystemResourceManager::StopTimer: gettimeofday failed\n");
			ret = STATUS_FAILED;
		}
		if (m_pIdbPowerMonitor && ret==STATUS_OK)
		{
			iIdbPowerMonitorKey = IndexedDB::CreateKey(8, 3618, 1);
			if (m_pIdbPowerMonitor->KeyExists(iIdbPowerMonitorKey))
			{
				cumTotalSecs = m_pIdbPowerMonitor->GetIntValue(iIdbPowerMonitorKey);
				cumPTimer.tv_sec = tv.tv_sec - (m_timerPower.tv_sec + (m_timerPower.tv_nsec/1000000000));
				
				cumTotalSecs = cumTotalSecs + (int)cumPTimer.tv_sec;
				if(cumTotalSecs > 0)
				{
					DEBUGL8("CSystemResourceManager::StopTimer: cumTotalSecs= %d\n", cumTotalSecs);
					if( m_pIdbPowerMonitor->SetIntValue(iIdbPowerMonitorKey, cumTotalSecs) !=STATUS_OK)
					{
						DEBUGL1("CSystemResourceManager::StopTimer: SetInt64Value Failed for DeepSleep\n");
						ret = STATUS_FAILED;
					}
				}
				else
				{
					DEBUGL1("CSystemResourceManager::StopTimer: cummulative value is < 0= %d\n", cumTotalSecs);
					ret = STATUS_FAILED;
				}
			}
			else
			{
				DEBUGL1("CSystemResourceManager::StopTimer: 08-3618 key does not exists\n");
				ret = STATUS_FAILED;
			}
		}
	}
        else if (m_iPowerType == 3)
        {
		ret = UpdateSRAMForPowerTimer(2);
        }
        else if (m_iPowerType == 4)
        {
		ret = UpdateSRAMForPowerTimer(3);
        }
        else if (m_iPowerType == 5)
        {
		ret = UpdateSRAMForPowerTimer(4);
        }

	if (msgID ==SystemResourceManager::StopTimer && ret == STATUS_OK)
	{
        	DEBUGL8("CSystemResourceManager::StopTimer: Successfully updated the cummulative time '%u' to 08-3618\n", cumTotalSecs);
	}

	return ret;
}

Status CSystemResourceManager::StartPrintingTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartPrintingTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
        CString sMsgname = "StartPrintingTimer";
	Status ret=STATUS_OK;

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerPrint)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartPrintingTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

        DEBUGL8("CSystemResourceManager::StartPrintingTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StopPrintingTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
	if(!msg)
	{
		DEBUGL1("CSystemResourceManager::StopPrintingTimer: Invalid msg Parameter\n");
		return STATUS_FAILED;
	}
	CString sMsgname = "StopPrintingTimer";
	Status ret=STATUS_OK;
	int cumTotalSecs;
	uint64 iIdbPowerMonitorKey;

	struct timespec curTime;
	//Initialize
	curTime.tv_sec     = 0x00L;
	curTime.tv_nsec    = 0x00L;

	errno = 0;
	/* Populate current time */

	if(clock_gettime(CLOCK_MONOTONIC, &curTime)!=0)
	{
		DEBUGL1("CSystemResourceManager::StopPrintingTimer: clock_gettime Failed with errno<%d>!\n", errno);
		return STATUS_FAILED;
	}

	curTime.tv_sec = curTime.tv_sec + (curTime.tv_nsec/1000000000);
	curTime.tv_sec = curTime.tv_sec - (m_timerPrint.tv_sec + (m_timerPrint.tv_nsec/1000000000));

	iIdbPowerMonitorKey = IndexedDB::CreateKey(8, 3618, 5);
	if (m_pIdbPowerMonitor)
	{
		if (m_pIdbPowerMonitor->KeyExists(iIdbPowerMonitorKey))
		{
			cumTotalSecs = m_pIdbPowerMonitor->GetIntValue(iIdbPowerMonitorKey);

			cumTotalSecs = cumTotalSecs + (int)curTime.tv_sec;
			DEBUGL8("CSystemResourceManager::StopPrintingTimer: cumTotalSecs= %d\n", cumTotalSecs);
			if( m_pIdbPowerMonitor->SetIntValue(iIdbPowerMonitorKey, cumTotalSecs) !=STATUS_OK)
			{
				DEBUGL1("CSystemResourceManager::StopPrintingTimer: SetInt64Value Failed\n");
				return STATUS_FAILED;
			}
		}
		else
		{
			DEBUGL1("CSystemResourceManager::StopPrintingTimer: 08-3618 key does not exists\n");
			return STATUS_FAILED;
		}
	}

	DEBUGL8("CSystemResourceManager::StopPrintingTimer: Successfully updated the cummulative time '%u' to 08-3618\n", cumTotalSecs);

	return ret;
}

Status CSystemResourceManager::StartScanningTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
        if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StartScanningTimer: Invalid msg Parameter\n");
                return STATUS_FAILED;
        }
        CString sMsgname = "StartScanningTimer";
	Status ret=STATUS_OK;

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &m_timerScan)!=0)
        {
                DEBUGL1("CSystemResourceManager::StartScanningTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }
        
	DEBUGL8("CSystemResourceManager::StartScanningTimer: Successfully started Timer \n");

	return ret;
}

Status CSystemResourceManager::StopScanningTimer(ci::operatingenvironment::Ref<ci::messagingsystem::Msg> msg)
{
	if(!msg)
        {
                DEBUGL1("CSystemResourceManager::StopScanningTimer: Invalid msg Parameter\n");
                SendOffNormalResponse(msg,"StopScanningTimer");
                return STATUS_FAILED;
        }
        CString sMsgname = "StopScanningTimer";
	Status ret=STATUS_OK;
	int cumTotalSecs;
	uint64 iIdbPowerMonitorKey;

        struct timespec curTime;
        //Initialize
        curTime.tv_sec     = 0x00L;
        curTime.tv_nsec    = 0x00L;

	errno = 0;
	/* Populate current time */
        if(clock_gettime(CLOCK_MONOTONIC, &curTime)!=0)
        {
                DEBUGL1("CSystemResourceManager::StopScanningTimer: clock_gettime Failed with errno<%d>!\n", errno);
                return STATUS_FAILED;
        }

        curTime.tv_sec = curTime.tv_sec + (curTime.tv_nsec/1000000000);
        curTime.tv_sec = curTime.tv_sec - (m_timerScan.tv_sec + (m_timerScan.tv_nsec/1000000000));

        iIdbPowerMonitorKey = IndexedDB::CreateKey(8, 3618, 6);
        if (m_pIdbPowerMonitor)
        {
                if (m_pIdbPowerMonitor->KeyExists(iIdbPowerMonitorKey))
                {
                        cumTotalSecs = m_pIdbPowerMonitor->GetIntValue(iIdbPowerMonitorKey);

                        cumTotalSecs = cumTotalSecs + (int)curTime.tv_sec;
			DEBUGL8("CSystemResourceManager::StopScanningTimer: cumTotalSecs= %d\n", cumTotalSecs);
                        if( m_pIdbPowerMonitor->SetIntValue(iIdbPowerMonitorKey, cumTotalSecs) !=STATUS_OK)
                        {
                                DEBUGL1("CSystemResourceManager::StopScanningTimer: SetInt64Value Failed\n");
                                return STATUS_FAILED;
                        }
                }
                else
                {
                        DEBUGL1("CSystemResourceManager::StopScanningTimer: 08-3618 key does not exists\n");
                        return STATUS_FAILED;
                }
        }

	DEBUGL8("CSystemResourceManager::StopScanningTimer: Successfully updated the cummulative time '%u' to 08-3618\n", cumTotalSecs);

	return ret;
}

    }//SystemResourceManager
}//CI
