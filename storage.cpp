// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

#include <storage.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <iostream>
#include <libgen.h>
#include <sys/statfs.h>
#include <errno.h>
#include <sys/statvfs.h>

using namespace std;
using namespace ci::operatingenvironment;
using namespace ci::hierarchicaldb;
using namespace dom;


namespace ci
{
namespace systemresourcemanager 
{

static ci::operatingenvironment::CString	g_sStorageEventTemplate = "<Events>\
																		<HighWaterMarkLevelReached>0</HighWaterMarkLevelReached>\
            															</Events>";

static ci::operatingenvironment::CString	g_sDirectoryEventTemplate = "<Events>"\
		"																<HighWaterMarkLevelReached>0</HighWaterMarkLevelReached>\
                														<DirectoryDeletedSelf>false</DirectoryDeletedSelf>\
                														<FileCreated>\
                    														<Name>""</Name>\
                														</FileCreated>\
                														<FileDeleted>\
                    														<Name>""</Name>\
                														</FileDeleted>\
            															</Events>";

static ci::operatingenvironment::CString	g_sFileEventTemplate = "<Events>\
																		<HighWaterMarkLevelReached>0</HighWaterMarkLevelReached>\
                														<WriteOperation>false</WriteOperation>\
															            <FileDeletedSelf>false</FileDeletedSelf>\
            														</Events>";

Storage::Storage(ci::hierarchicaldb::HierarchicalDBRef mainHDBRef, dom::DocumentRef mainDomRef)
{
	// Initialize all private members
	m_pHDB = mainHDBRef;
	m_SystemResourceManagerDOM = mainDomRef;
}

Storage::~Storage()
{
	// Release all resources
	m_SystemResourceManagerDOM = NULL;
	m_pHDB = NULL;
}

bool Storage::IsThisStoragePresent(CString	StorageName) //storage name is the path to root directory of the hard disk/partition/media
{
	NodeRef pDocumentNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,"SystemResources/Storage[@Name='"+StorageName+"']");
	if(!pDocumentNode)
	{
		DEBUGL2("Storage:IsThisStoragePresent: Storage = %s not found in SRM dom \n", StorageName.c_str());
		return false;
	}
	DEBUGL7("Storage:IsThisStoragePresent: Storage = %s is found in SRM dom \n", StorageName.c_str());
	return true;
}


#if 0  //This is for  developer reference of FTS APIs and data structure, used for computing the size of a file or all files in a directory including subdirectory 
FTS * fts_open(char * const *path_argv, int options, int (*compar)(const FTSENT **, const FTSENT **));

FTSENT * fts_read(FTS *ftsp);

FTSENT * fts_children(FTS *ftsp, int options);

int fts_set(FTS *ftsp, FTSENT *f, int options);

int fts_close(FTS *ftsp);


typedef struct _ftsent {
             u_short fts_info;               /* flags for FTSENT structure */
             char *fts_accpath;              /* access path */
             char *fts_path;                 /* root path */
             short fts_pathlen;              /* strlen(fts_path) */
             char *fts_name;                 /* filename */
             short fts_namelen;              /* strlen(fts_name) */
             short fts_level;                /* depth (-1 to N) */
             int fts_errno;                  /* file errno */
             long fts_number;                /* local numeric value */
             void *fts_pointer;              /* local address value */
             struct ftsent *fts_parent;      /* parent directory */
             struct ftsent *fts_link;        /* next file structure */
             struct ftsent *fts_cycle;       /* cycle structure */
             struct stat *fts_statp;         /* stat(2) information */
     } FTSENT;
#endif

     
uint64 Storage::GetFileOrDirectorySize(const char *argv)
{
	
   uint64 totalsize=0;
   char stLine[1024]={0};
   char ignore[1024]={0};
   CString path(argv);
   CString cmd = "du -bs -- \'"+path+"\'";
   FILE *p = popen(cmd.c_str(),"r");
   if(p)
   {
      fgets(stLine,sizeof(stLine),p);
      sscanf(stLine,"%llu %s",&totalsize,ignore);
      pclose(p);
   }
   else
	DEBUGL1("Storage::GetFileOrDirectorySize: popen failed with errno = %d\n", errno);
   return totalsize;
}

void Storage::UpdateSize(Ref<Element> pNode,CString NodeName,uint64 Size )
{
	//update SpaceUsed 
	char tempStrSize[128];
	memset(tempStrSize,'\0',128);
	sprintf(tempStrSize,"%llu",Size);
   try { 
	   pNode->setTextContent(tempStrSize);
   } catch (DOMException except) {
      DEBUGL1("Storage::UpdateSize : Failed due to HDB Exception\n");
   }
	return;
}

Status Storage::GetSize(Ref<Node> pNode, uint64 &Size )//size is returned in Size paramater
{
	char *endPtr=NULL;
	DEBUGL7("Storage: in=> GetSize: Size = %llu \n",Size);
	CString value;
	try {
   if (pNode->getNodeType() == Node::TEXT_NODE)
	{
		value=((TextRef)pNode)->getNodeValue();
	}
	else if (pNode->getNodeType() == Node::ATTRIBUTE_NODE)
	{
		value=((AttrRef)pNode)->getValue();
	}
	else
	{
		value = pNode->getFirstChild()->getTextContent();
	}
	Size = strtoull(value.c_str(),&endPtr,10);
	if (errno < 0)
	{
	   DEBUGL1("Storage:GetSize: failed in stroul call:returned size =%d, errno = %d \n",Size,errno);
	   perror(" Storage:GetSize:strtoull: ");
       return STATUS_FAILED; 
   }
   } catch (DOMException except) {
      DEBUGL1("Storage:GetSize: Failed due to HDB Exception\n");
      return STATUS_FAILED;
   }
	DEBUGL7("Storage: out => GetSize: Size = %llu \n",Size);
	return STATUS_OK;
}

bool Storage::IsThisDirectoryPresent(CString sDirectoryName ) //DirectoryName includes absolute path along with name of the directory
{
	CString sStorageName="";
   if(sDirectoryName.empty())
   {
      DEBUGL1("Storage::IsThisDirectoryPresent : Empty Argument Passed\n");
      return false;
   }
	return (GetMountPoint(sDirectoryName,sStorageName) ? IsThisDirectoryPresent(sStorageName,sDirectoryName):false);
}

bool Storage::IsThisDirectoryPresent(CString sStorageName, CString sDirectoryName) //DirectoryName includes absolute path along with name of the directory
{
	CString sBindPath = "SystemResources/Storage[@Name='"+sStorageName+"']"+"/Directory[@Name='"+sDirectoryName+"']";
	struct stat  dirStat;
	if(stat(sDirectoryName.c_str(),&dirStat) < 0)
	{
		DEBUGL1("Storage:IsThisDirectoryPresent: Directory/file = %s not found under storage = %s in hard disk with errno = %d \n",sDirectoryName.c_str(),sStorageName.c_str(), errno);
		return false;
	}
	
	if(S_ISDIR(dirStat.st_mode) != 0)
	{
		DEBUGL7("Storage:IsThisDirectoryPresent: Directory/file = %s is found under storage = %s in SRM dom is valid directory \n",sDirectoryName.c_str(),sStorageName.c_str());
		return true;
	}
	DEBUGL1("Storage:IsThisDirectoryPresent: Directory = %s is found under storage = %s in SRM dom is not a valid directory \n",sDirectoryName.c_str(),sStorageName.c_str());
	return false;
}

bool Storage::IsThisFilePresent(CString sStorageName, CString sFileName ) //filename must include absolute path along with name of the file
{
	NodeRef pDocumentNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,"SystemResources/Storage[@Name='"+sStorageName+"']"+"/File[@Name='"+sFileName+"']");
	struct stat  fileStat;
	if(stat(sFileName.c_str(),&fileStat) < 0)
	{
		DEBUGL1("Storage:IsThisFilePresent: File  = %s not found under storage = %s in hard disk with errno = %d\n",sFileName.c_str(),sStorageName.c_str(), errno);
		return false;
	}
	if(S_ISREG(fileStat.st_mode) != 0)
	{
		DEBUGL7("Storage:IsThisFilePresent: File = %s is found under storage = %s in SRM dom is valid regular file \n",sFileName.c_str(),sStorageName.c_str());
		return true;
	}
	return false;
}

/*
 * Restrictions: 
 *   - All mount points (partition, include root folder) must be listed in /etc/fstab of the system where this SRM will executed
 */
bool Storage::GetMountPoint(const CString sFileOrDirectory, CString &sMountPoint) //contents of argument sMountPoint will be filled by this method
{
 
#ifdef SRM_VMFP
    sMountPoint = sFileOrDirectory;
     DEBUGL8("Storage:GetMountPoint:VMF sMountPoint %s,%s\n",sMountPoint.c_str(),sFileOrDirectory.c_str());
    return true;
#else 
	struct stat inputfilestat, tmpstat;
    if (stat(sFileOrDirectory.c_str(), &inputfilestat) == -1)
    {
    	DEBUGL2("Storage:GetMountPoint: input file/directory not found in the system returning with errno =%d\n",errno);
       return false;
    }
    FILE *fp;
    fp = setmntent(MOUNTED, "r");
    if(!fp)
    {
        DEBUGL1("Storage:GetMountPoint: setmntent() call failed .. returning false \n");
        return false;
    }
    struct mntent *tmpmntent;
    while((tmpmntent=getmntent(fp)))
    {
         //ignore invalid entries
         if (stat(tmpmntent->mnt_fsname,&tmpstat) == -1) continue;
         //ignore device
         if (memcmp("/dev/",tmpmntent->mnt_fsname,5)) continue;
         //compare the devce id of filename with the mounted device
         if (inputfilestat.st_dev == tmpstat.st_rdev)
        {
        	  sMountPoint = std::string(tmpmntent->mnt_dir);
              DEBUGL7("Storage:GetMountPoint:Found mount point name => %s \n",tmpmntent->mnt_fsname );
              DEBUGL7("Storage:GetMountPoint:Found mount point directory => %s\n",tmpmntent->mnt_dir );
              fclose(fp);
              return true;
        }
    }
    DEBUGL2("Storage:GetMountPoint: input file/directory not found in the system\n");
    sMountPoint = "";
    fclose(fp);
    return false;
#endif  
}

Status Storage::AddChildNodeNSetItsValue(NodeRef pParentNode,CString sChildNodeName, CString sChildNodeValue)
{
	Status ret=STATUS_OK;
	//create a text node
	try {
   NodeRef pChildNode = pParentNode->getOwnerDocument()->createTextNode(sChildNodeName);
	if(pChildNode)
	{
		//set its value
		pChildNode->setNodeValue(sChildNodeValue);
		//add it is a child to the parent node
		pParentNode->appendChild(pChildNode);
	}
	else
	{
		DEBUGL1("Storage:AddChildNodeNSetItsValue: child node [%s] creation failed \n",sChildNodeName.c_str());
		ret=STATUS_FAILED;
	}
	} catch (DOMException except) {
      DEBUGL1("Storage:AddChildNodeNSetItsValue: Failed due to HDB Exception\n");
      return STATUS_FAILED;
   }
	return ret;
}


/* This method assumes that caller of this method has ensured the storage path input is unique in the dom document. */ 
Status Storage::AddStorage(CString sStoragePath,CString sTotalSpaceInBytes,CString sSpaceUsedInBytes, CString sSpaceAvailableInBytes, CString sWaterMarkLevel)
{
	CString sMountPoint="";
	if(!GetMountPoint(sStoragePath,sMountPoint))
	{
		DEBUGL1("Storage:AddStorage: Failed to find the partition/mount with name=[%s]\n",sStoragePath.c_str());
		return STATUS_FAILED;
	}
	
	NodeRef pDocumentNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,"SystemResources");
	if(!pDocumentNode)
	{
		DEBUGL2("Storage:AddStorage: Failed to bind to SystemResources node\n");
		return STATUS_FAILED;
	}
	
	//create 'Storage' node under 'SystemResources'
	ElementRef pStorageElement = m_SystemResourceManagerDOM->createElement("Storage");
	CString sStorage = "<Storage Name='"+ sStoragePath + "'>\
						<TotalSpace>" + sTotalSpaceInBytes + "</TotalSpace>\
						<SpaceUsed> " + sSpaceUsedInBytes + "</SpaceUsed>\
						<SpaceAvailable>" + sSpaceAvailableInBytes + "</SpaceAvailable>\
						<WaterMarkLevel>" + sWaterMarkLevel + "</WaterMarkLevel>" \
						+ g_sStorageEventTemplate + "</Storage>";
	printf("Serialize content: \n[%s]\n",sStorage.c_str());
	
	if( STATUS_OK != m_pHDB->Deserialize(pStorageElement,sStorage.c_str()) )
	{
		DEBUGL1("Storage:AddStorage: Addition of 'Storage' node failed for storage [%s] \n",sStoragePath.c_str());
		return STATUS_FAILED;
	}
	
	DEBUGL7("Storage:AddStorage: Successfully added 'Storage' node  for storage [%s] \n",sStoragePath.c_str());
	return STATUS_OK; 
}

Status Storage::AddDirectory(CString sDirectoryPath,CString sTotalSpaceInBytes,CString sSpaceUsedInBytes, CString sSpaceAvailableInBytes, CString sWaterMarkLevel)
{
	ci::operatingenvironment::CString sMountPoint="";
	//get the storage under which this directory is present
	if(!GetMountPoint(sDirectoryPath,sMountPoint))
	{
		DEBUGL1("Storage:AddDirectory: Failed to find the storage that contains directory=[%s]\n",sDirectoryPath.c_str());
		return STATUS_FAILED;
	}
	CString xPathToBind = "SystemResources/Storage[@Name='"+ sMountPoint + "']";
	NodeRef pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,xPathToBind);
	if(!pStorageNode)
	{
		DEBUGL2("Storage:AddDirectory: Failed to bind to %s node\n",xPathToBind.c_str());
		return STATUS_FAILED;
	}
	
	CString sDirectory = "<Directory Name='"+ sDirectoryPath + "'>\
						<TotalSpace>" + sTotalSpaceInBytes + "</TotalSpace>\
						<SpaceUsed> " + sSpaceUsedInBytes + "</SpaceUsed>\
						<SpaceAvailable>" + sSpaceAvailableInBytes + "</SpaceAvailable>\
						<WaterMarkLevel>" + sWaterMarkLevel + "</WaterMarkLevel>" \
						+ g_sDirectoryEventTemplate+ "</Directory>";
	printf("Serialize content: \n[%s]\n",sDirectory.c_str());
	if( STATUS_OK != m_pHDB->Deserialize(pStorageNode,sDirectory.c_str()) )
	{
		DEBUGL1("Storage:AddDirectory: Addition of 'Directory' node failed for storage [%s] \n",xPathToBind.c_str());
		return STATUS_FAILED;
	}
	
	DEBUGL7("Storage:AddDirectory: Successfully added 'Directory' node  for storage [%s] \n",xPathToBind.c_str());
	return STATUS_OK;
}

Status Storage::AddFile(CString sFilePath,CString sTotalSpaceInBytes,CString sSpaceUsedInBytes, CString sSpaceAvailableInBytes,CString sWaterMarkLevel)
{
	ci::operatingenvironment::CString sMountPoint="";
	//get the storage under which this directory is present
	if(!GetMountPoint(sFilePath,sMountPoint))
	{
		DEBUGL1("Storage:AddDirectory: Failed to find the storage that contains directory=[%s]\n",sFilePath.c_str());
		return STATUS_FAILED;
	}
	CString xPathToBind = "SystemResources/Storage[@Name='"+ sMountPoint + "']";
	NodeRef pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,xPathToBind);
	if(!pStorageNode)
	{
		DEBUGL2("Storage:AddFile: Failed to bind to %s node\n",xPathToBind.c_str());
		return STATUS_FAILED;
	}
	
	CString sFile = "<File Name='"+ sFilePath + "'>\
					<TotalSpace>" + sTotalSpaceInBytes + "</TotalSpace>\
					<SpaceUsed> " + sSpaceUsedInBytes + "</SpaceUsed>\
					<SpaceAvailable>" + sSpaceAvailableInBytes + "</SpaceAvailable>\
					<WaterMarkLevel>" + sWaterMarkLevel + "</WaterMarkLevel>" \
					+ g_sFileEventTemplate + "</File>";
	printf("Serialize content: \n[%s]\n",sFile.c_str());
	if( STATUS_OK != m_pHDB->Deserialize(pStorageNode,sFile.c_str()))
	{
		DEBUGL1("Storage:AddFile: Addition of 'File' node failed for storage [%s] \n",xPathToBind.c_str());
		return STATUS_FAILED;
	}
	
	DEBUGL7("Storage:AddFile: Successfully added 'File' node  for storage [%s] \n",xPathToBind.c_str());							
	return STATUS_OK;
}

Status Storage::DeleteStorage(ci::operatingenvironment::CString sStorageName)
{
	CString	sPath2Bind	= "SystemResources/Storage[@Name='" + sStorageName +"']";
	NodeRef pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sPath2Bind);
	if(!pStorageNode)
	{
		DEBUGL2("Storage:DeleteStorage: Failed to bind to storage node=[%s]\n",sStorageName.c_str());
		return STATUS_FAILED;
	}
	
   try {
	NodeRef pParentNode = pStorageNode->getParentNode();
   pParentNode->removeChild(pStorageNode);
	DEBUGL7("Deleted the storage with name=[%s] from System Resource DOM\n", sStorageName.c_str());
   } catch (DOMException except) {
      DEBUGL1("Storage:DeleteStorage: Failed due to HDB Exception\n");
      return STATUS_FAILED;
   }
	return STATUS_OK;
}

/* Note: Delete the entry of directory and its events from the SystemResource DOM document */
Status Storage::DeleteDirectory(ci::operatingenvironment::CString sDirectoryName) //DirectoryName contains directory along with absolute path 
{
	ci::operatingenvironment::CString sStorageName;
	//get the storage under which this directory is present
	if(!GetMountPoint(sDirectoryName,sStorageName))
	{
		DEBUGL1("Storage:DeleteDirectory: Failed to file the storage in System Resources DOM that contains directory=[%s] \n",sDirectoryName.c_str());
		return STATUS_FAILED;
	}
	CString sPath2Bind = "SystemResources/Storage[@Name='" + sStorageName +"']"+"Directory[@Name='" + sDirectoryName + "']";
	NodeRef pDirectoryNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sPath2Bind.c_str());
	if(!pDirectoryNode)
	{
		DEBUGL2("Storage:DeleteDirectory: Failed to bind to directory node=[%s] under storage=[%s] \n",sDirectoryName.c_str(),sStorageName.c_str());
		return STATUS_FAILED;
	}
	NodeRef pParentNode = pDirectoryNode->getParentNode();
	try {
   pParentNode->removeChild(pDirectoryNode);
	DEBUGL7("Storage:DeleteDirectory: Deleted the directory with name=[%s] from System Resource DOM\n", sDirectoryName.c_str());
   } catch (DOMException except) {
      DEBUGL1("Storage:DeleteDirectory: Failed due to HDB Exception\n");
      return STATUS_FAILED;
   }
	return STATUS_OK;
}

/* Note: Delete the entry of file and its events from the SystemResource DOM document */
Status Storage::DeleteFile(ci::operatingenvironment::CString sFileName) //FileName contains directory along with absolute path 
{
	ci::operatingenvironment::CString sStorageName;
	//get the storage under which this directory is present
	if(!GetMountPoint(sFileName,sStorageName))
	{
		DEBUGL1("Storage:DeleteFile: Failed to file the storage in System Resources DOM that contains file=[%s] \n",sFileName.c_str());
		return STATUS_FAILED;
	}
	CString sPath2Bind = "SystemResources/Storage[@Name='" + sStorageName +"']"+"File[@Name='" + sFileName + "']";
	NodeRef pFileNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sPath2Bind.c_str());
	if(!pFileNode)
	{
		DEBUGL2("Storage:DeleteFile: Failed to bind to directory node=[%s] under storage=[%s] \n",sFileName.c_str(),sStorageName.c_str());
		return STATUS_FAILED;
	}
	try {
   NodeRef pParentNode = pFileNode->getParentNode();
	pParentNode->removeChild(pFileNode);
	DEBUGL7("Storage:DeleteFile: Deleted the directory with name=[%s] from System Resource DOM\n", sFileName.c_str());
   } catch (DOMException except) {
      DEBUGL1("Storage:DeleteFile: Failed due to HDB Exception\n");
      return STATUS_FAILED;
   }
	return STATUS_OK;
}

Status Storage::GetPartitionSizeInfo(CString sPath, CString &sTotalSpace, CString &sSpaceUsed, CString &sSpaceAvailable)
{
	DEBUGL7("Storage:GetPartitionSizeInfo: Getting Filesystem statistics for partition that contains the path=[%s]\n", sPath.c_str());
    struct statfs64 fs_stat;
    long ret =  statfs64(sPath.c_str(),  &fs_stat);
     if( ret < 0 )
     {
           perror("statfs64() call failed, please check your inputs: ");
           return STATUS_FAILED;
     }
     
 /*   For Reference as taken from man page:
  * struct statfs {
  * 	long    f_type;     // type of filesystem
  * 	long    f_bsize;    // optimal transfer block size 
  * 	long    f_blocks;   // total data blocks in file system 
  * 	long    f_bfree;    // free blocks in fs 
  * 	long    f_bavail;   // free blocks avail to non-superuser 
  * 	long    f_files;    // total file nodes in file system 
  * 	long    f_ffree;    // free file nodes in fs 
  * 	fsid_t  f_fsid;     // file system id 
  * 	long    f_namelen;  // maximum length of filenames 
  *  };
  */
    cout << "FS stats for mount point on path = [" << sPath.c_str() << "]  : " << endl;
    cout << "f_type = " << fs_stat.f_type << endl;
    cout << "f_bsize = " << fs_stat.f_bsize << endl;
    cout << "f_blocks = " << fs_stat.f_blocks << endl;
    cout << "f_bfree = " << fs_stat.f_bfree << endl;
    cout << "f_bavail = " << fs_stat.f_bavail << endl;
    cout << "f_files = " << fs_stat.f_files << endl;
    cout << "f_ffree = " << fs_stat.f_ffree << endl;
    
    uint64 ullTotalSpace=0,/* ullSpaceUsed=0, */ ullSpaceAvailable=0;
    ullTotalSpace = fs_stat.f_bsize*fs_stat.f_blocks;
    ullSpaceAvailable = fs_stat.f_bsize*fs_stat.f_bavail;
    
    cout << "ullTotalSpace" << ullTotalSpace << endl;
    cout << "ullSpaceAvailable" << ullSpaceAvailable << endl;
        
    
    sTotalSpace = Uint64ToString(ullTotalSpace);
    sSpaceAvailable = Uint64ToString(ullSpaceAvailable);
    sSpaceUsed = ""; // will be computed at the place of use
    
    return STATUS_OK;
}

CString Storage::Uint64ToString(uint64 ullNumber)
{
	char tempStrSize[128];
	memset(tempStrSize,'\0',128);
	sprintf(tempStrSize,"%llu",ullNumber);
	return tempStrSize;
}

uint64 Storage::StringToUint64(CString sNumber)
{
	char *endPtr=NULL;
	return strtoull(sNumber.c_str(),&endPtr,10);
}

uint64 Storage::GetAvailableSize(CString path)
{
   struct statvfs info;
   uint64 TotalAvail =  0;
   if (-1 == statvfs(path.c_str(), &info))
   {
      DEBUGL1("Failed to Get Available Space \n");
      return TotalAvail;
   }
   //Total Available is calculated from the statvfs command by multiplying the 
   //free blocks for root user with the block size
   //
   //Note:Previously this used to return the product of available blocks and blocksize
   //which used to match the df output, but was always less than the actual available size.
   TotalAvail =((uint64)info.f_bfree)*((uint64)info.f_bsize);

   if (TotalAvail < 16777216)
      TotalAvail = 0;
   else
    TotalAvail = TotalAvail - 16777216; // fix for DTFR_14419 and STFR_16461
   
   return TotalAvail;

}

}//SystemResourceManager
}//CI
