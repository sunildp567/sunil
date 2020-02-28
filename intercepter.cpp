// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

/*TODO:
 * This source file implements system call intercepter methods
 */



#define _GNU_SOURCE 1      // Do this in order to enable the RTLD_NEXT symbol
#include <stdio.h>

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <csystemresourcemanager.h>
#include <storage.h>


#include <CI/DataStream/datastream.h>
#include <CI/OperatingEnvironment/file.h>
#include "CI/OperatingEnvironment/cexception.h"


using namespace std;
using namespace ci::operatingenvironment;
using namespace ci::messagingsystem;
using namespace ci::hierarchicaldb;
using namespace ci::datastream;
using namespace dom;
using namespace ci::servicestartupmanager;
using namespace ci::systemresourcemanager;
 

typedef map < FILE*, CString > FILEMAP;
typedef map < int, CString > STREAMMAP;

typedef int (*OPENTYPE) (const char *, int ) ;

typedef int (*OPENTYPE_MODE) (const char *, int , mode_t ) ;
typedef ssize_t (*READTYPE)(int , void *, size_t count);
typedef ssize_t (*WRITETYPE)(int , const void *, size_t ) ;
typedef int (*CLOSETYPE)(int fd);

typedef  FILE * (*FOPENTYPE) ( const char * , const char *  );
typedef size_t (*FREADTYPE)(void *, size_t , size_t , FILE *);
typedef size_t (*FWRITETYPE)(const void *, size_t , size_t , FILE *);
typedef int (*FCLOSETYPE) ( FILE * stream );


class CInterCepter
{

	FILEMAP m_FilenameMap;
	STREAMMAP m_StreamMap;
	ci::hierarchicaldb::HierarchicalDBRef m_pHDB;
	dom::DocumentRef m_SystemResourceManagerDOM;
	ci::systemresourcemanager::StorageRef	m_storageRef ;

	public:
		
	CInterCepter();
	~CInterCepter();
	CString GetDirectoryFromFilePath ( CString sFullPathname);

	Status	HandleOpenCall(const char *pathname, int flags, int ret) ;	
	Status	HandleCloseCall( int handle );
	Status  HandleWriteCall (int fd, const void *buf, size_t count);
	
	Status	HandleFopenCall( const char * filename, const char * mode,FILE *fD );
	Status	HandleFcloseCall( FILE *fD );
	Status 	HandleFWriteCall(FILE * fD, int iDataSizeToWrite);	
};



//public CInterCepter object 
CInterCepter g_CInterCepter;


//The CInterdepter Constructor 
CInterCepter::CInterCepter()
{

	//DEBUGL7(" Inside the CIntercepter constructor \n");
	m_SystemResourceManagerDOM = NULL;
	Status retStatus = STATUS_OK;

	CString path = getenv("EB2");
	CString sTmpPath =	path + "/tmp";
	CString sDOMPath = "/work/ci/srm";
	CString sBuildType = getenv("BUILD_TYPE");
	path = path + "/build/" + sBuildType;
	path.append("/bin/");
	CString sDocName = "DefaultSystemResources";
	CString sXMLFileName = path + "DefaultSystemResources.xml";
	m_pHDB = ci::hierarchicaldb::HierarchicalDB::Acquire(NULL);
	retStatus = m_pHDB->OpenDocument(sDOMPath,sDocName,m_SystemResourceManagerDOM); //no path specified means it will create in $EB2/tmp/
	StorageRef	m_storageRef = new Storage(m_pHDB,m_SystemResourceManagerDOM); //	StorageRef	 is define in storage.h , inside the class 

	
//	retStatus = m_pHDB->CreateDocumentFromFile(sDocName,m_SystemResourceManagerDOM,sXMLFileName);//OpenDocument(sDocName,pDoc,sTmpPath);//no path specified means it will try to open from $EB2/tmp/
/*
// The following is something to do with checking the existance of the folder and nothing to do with the DOM
// this will give information about how much space it can be used ...
// and this information will be used to make sure that whehter a write operation can be done or not..
// so its irelevent in the fopen call..


	CString sBindPath = "";
	CString sPathName = "/work/drivers/client/UnixFilters";
	CString sMountPoint = "/";
	StorageRef	m_storageRef = new Storage(m_pHDB,m_SystemResourceManagerDOM); //	StorageRef	 is define in storage.h , inside the class 

	if( !(m_storageRef->GetMountPoint(sPathName,sMountPoint) ) )
	{
		DEBUGL1("CInterCepter::CInterCepter: GetMountPoint failed for %s \n",sPathName.c_str());
		exit(0);
	}

	if(sPathName.compare(sMountPoint) == 0)
	{//is set for storage (partition itself) 
		sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']";
	}
	else
	{	
		if(m_storageRef->IsThisDirectoryPresent(sPathName))
			sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/Directory[@Name='" + sPathName+ "']";
		//else --dont care about file 
			//sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/File[@Name='" + sPathName + "']";
	}

	DEBUGL1("CInterCepter::CInterCepter sBindPath=[%s] \n",sBindPath.c_str());


	//MAN THIS IS HOW YOU ARE GOING TO LOACK THE ELEMENT
	NodeRef	pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath);

	//mAN THIS IS HOW YOU ARE GOING TO RELEASE THE LOCK ON THE LEMENT 
	pStorageNode = NULL;


	//If the pTotalSpaceNode > greater than the Watermark Size+ newSize prevent it from doing so...
	*/		
	//DEBUGL7("CInterCepter::CInterCepter: Have created a CInterceptor object globally !\n");

}

// The destructor 
CInterCepter::~CInterCepter()
{
	//DEBUGL7("CInterCepter::~CInterCepter() \n");
}


CString CInterCepter::GetDirectoryFromFilePath ( CString sFullPathname)
{

	
	CString sPath;

	size_t pos= sFullPathname.rfind('/');
	if (string::npos == pos)// we have to take current working directory 
		{
		char tempPath [500]; // if we are not able to read the current working directory it is assumed that the directory need not be nmonitored
		if (NULL== getcwd(tempPath  ,500))
			return NULL;
		else
		sPath =tempPath;
		}
	else
	{
		sPath =  sFullPathname.substr (0,pos);
	}

	//DEBUGL7(" CInterCepter::GetDirectoryFromFilePath path is [%s] \n", sPath.c_str());

	return sPath ;
}



Status	CInterCepter::HandleOpenCall(const char *pathname, int flags, int ret) 
{
	//DEBUGL7(" CInterCepter::HandleOpenCall opencall  \n");

	if (flags& O_RDONLY) // only read 
		return STATUS_OK ; // there is no write going to happen 
	CString sFileName =  pathname;	
	CString sPathName = GetDirectoryFromFilePath ( pathname);
	if ( sPathName.c_str() == NULL)
		return STATUS_INVALID;

	m_StreamMap.insert( pair < int, CString >( ret,sPathName));

	//DEBUGL7(" CInterCepter::HandleOpenCall: m_StreamMap.insert<%d,%s>\n",ret,sPathName.c_str());

	return STATUS_OK;
	
}




Status	CInterCepter::HandleCloseCall( int handle )
{
	//DEBUGL7("CInterCepter::HandleCloseCall close call [fd=%d] \n",handle);

	m_StreamMap.erase(handle); // doesnot make any difference if it doesnot exist

	return STATUS_OK;
	
}




Status CInterCepter::HandleWriteCall (int fd, const void *buf, size_t nBuffSize) 
{

		//DEBUGL7("CInterCepter::HandleWriteCall  write call [fd =%d]  \n", fd);

		// this part is for validating the request.. nothing to do with geting the information from DOM
	
		if(0==	m_StreamMap.count(fd))
			{
				printf("CInterCepter::HandleWriteCall Didnot able to get a valid path from the FileDescriptor  \n");
				return STATUS_INVALID;
			}
		CString sPathName = m_StreamMap[fd]; // We need to check whether the path of the file is crossing the watermark level 

		//TODO:// We need to check whether the FILE itself is  crossing the watermark level 


		// OK the path is obtained ... in sPathName 
		// now getting the mount point.. mount point is the drive 
		
		if (NULL == sPathName.c_str())
		{
			printf("CInterCepter::HandleWriteCall File Map returned NULL for the given Fd =[%d] \n", (int) fd);
			return STATUS_INVALID;
		}
			
		CString sMountPoint="";
		if( !(m_storageRef->GetMountPoint(sPathName,sMountPoint) ) )
	 	{
	 		printf("CInterCepter::HandleWriteCall: GetMountPoint failed for %s \n",sPathName.c_str());
			return STATUS_INVALID ; 
		}

		//As of now its being done for checking the directory which is having the file , TODO : for File also 
		 CString sBindPath = "";
			if(m_storageRef->IsThisDirectoryPresent(sPathName))
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/Directory[@Name='" + sPathName+ "']";
			else
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/File[@Name='" + sPathName + "']";
				

		// oGet the x-path , from which the data needs to be retrived .
		NodeRef	 pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath);
		if(!pStorageNode)
		{
			printf("CInterCepter::HandleWriteCall: BindToElement failed for [%s] so skipping storage \n",sBindPath.c_str());
		return STATUS_INVALID;
		}

		// It was writen to prevent the write operation if the water mark was reached.
		/*
		DEBUGL7("SystemResourceManager:HandleWriteCall success: sBindPath = [%s] \n", sBindPath.c_str());

		NodeRef	pWaterMarkLevel;
		chelper::GetNode(pStorageNode,"HighWaterMarkLevelReached",pWaterMarkLevel);
		if(!pWaterMarkLevel)
		{
			return STATUS_OK; // Go ahead and write the content its OK
		}
		else
		{
		CString  sWaterMarkLevel = pWaterMarkLevel->getTextContent(); 
		
		if (sWaterMarkLevel=="true")
			{
			//Dont allow the guy to write the data to the file....File
			return STATUS_FAILED;
			}
		else
			return STATUS_OK ; // Go ahead and write the content its OK	
		
		}
		*/
		// now modifying to prevent the write if the used_size + new_size >  max_size.
	
		Status retStatus = STATUS_OK;
		NodeRef	pMaxSize, pUsedSize;
		chelper::GetNode(pStorageNode,"MaxSize",pMaxSize);
		chelper::GetNode(pStorageNode,"SpaceUsed",pUsedSize);
		if(!pMaxSize || !pUsedSize)
		{

			printf("CInterCepter::HandleWriteCall pMaxSize=NULL Or pUsedSize =NULL \n");	
			retStatus = STATUS_OK; // Go ahead and write the content , we dont have any other option to check
		}
		else
		{
			char *endPtr=NULL;
			
			CString sMaxSize =pMaxSize->getTextContent();
			CString sUsedSize=pUsedSize->getTextContent();
			uint64 uMaxSize=strtoull(sMaxSize.c_str(),&endPtr,10);
			uint64 uUsedSize=strtoull(sUsedSize.c_str(),&endPtr,10);
			uint64 uNewSize = uUsedSize + nBuffSize;
			
			if(uNewSize <= uMaxSize )
			{
				printf("CInterCepter::HandleWriteCall allowed writing data to the file as uNewSize[%d]<=sMaxSize[%d]\n" , uNewSize , uMaxSize);	
				//TODO:update the new size in the SRM DOM respository
				//1- Will ir creatate another fwrite call , creating an over head , that is too big to ignore??
				//2- Can we assume that only one write call comes and then the close call comes. If that is the case, cisystemresource manager will update DOM
				//char tempStrSize[128];
				//memset(tempStrSize,'\0',128);
				//sprintf(tempStrSize,"%llu",uNewSize);
				//sUsedSize = tempStrSize;
				//pUsedSize->setTextContent(sUsedSize);
				retStatus = STATUS_OK; // Go ahead and write the content its OK

			}
			else
			{
				//Dont allow the guy to write the data to the file....File
				printf("CInterCepter::HandleWriteCall prevented writing data to the file as uNewSize[%d] > sMaxSize[%d]\n" , uNewSize , uMaxSize);	
				retStatus = STATUS_FAILED;
			}
		}
	pStorageNode=pMaxSize=pUsedSize= NULL;
	return retStatus;
}



Status	CInterCepter::HandleFopenCall( const char * filename, const char * mode, FILE *fD )
{

	CString sMode = mode;
	//Ignore all read only opens 
	if (string::npos  == sMode.find('w') &&  string::npos  == sMode.find('a'))
		return STATUS_OK ; // there is no write going to happen 
	CString sFileName =  filename;	
	CString sPathName = GetDirectoryFromFilePath ( sFileName);
	if ( sPathName.c_str() == NULL)
		return STATUS_INVALID;

	m_FilenameMap.insert( pair < FILE*, CString >( fD,sPathName));

	return STATUS_OK;
	
}


Status	CInterCepter::HandleFcloseCall( FILE *fD )
{
	m_FilenameMap.erase(fD); // doesnot make any difference if it doesnot exist

	return STATUS_OK;
	
}



Status CInterCepter::HandleFWriteCall(FILE * fd, int iDataSizeToWrite)

{

		// this part is for validating the request.. nothing to do with geting the information from DOM
	
		if(0==	m_FilenameMap.count(fd))
			{
				//DEBUGL1("CInterCepter::HandleFWriteCall Didnot able to get a valid path for the FileDescriptor [%d]  \n", (int)fd);
				return STATUS_INVALID;
			}
		CString sPathName = m_FilenameMap[fd]; // We need to check whether the path of the file is crossing the watermark level 

		//TODO:// We need to check whether the FILE itself is  crossing the watermark level 


		// OK the path is obtained ... in sPathName 
		// now getting the mount point.. mount point is the drive 
		
		if (NULL == sPathName.c_str())
		{
			printf("CInterCepter::HandleFWriteCall File Map returned NULL for the given Fd =[%d] \n", (int) fd);
			return STATUS_INVALID;
		}
			
		CString sMountPoint="";
		if( !(m_storageRef->GetMountPoint(sPathName,sMountPoint) ) )
	 	{
	 		printf("CInterCepter:HandleGetSizeInformationRequests: GetMountPoint failed for %s \n",sPathName.c_str());
			return STATUS_INVALID ; 
		}

		//As of now its being done for checking the directory which is having the file 
		 CString sBindPath = "";
			if(m_storageRef->IsThisDirectoryPresent(sPathName))
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/Directory[@Name='" + sPathName+ "']";
			else
				sBindPath = "SystemResources/Storage[@Name='" + sMountPoint + "']/File[@Name='" + sPathName + "']";
				

		// oGet the x-path , from which the data needs to be retrived .
		NodeRef	 pStorageNode = m_pHDB->BindToElement(m_SystemResourceManagerDOM,sBindPath);
		if(!pStorageNode)
		{
			printf("CInterCepter:HandleGetSizeInformationRequests: BindToElement failed for [%s]...skipping storage \n",sBindPath.c_str());
		return STATUS_INVALID;
		}
		//DEBUGL7("CInterCepter:HandleGetSizeInformationRequests: sBindPath = [%s] \n", sBindPath.c_str());
/*
		NodeRef	pWaterMarkLevel;
		chelper::GetNode(pStorageNode,"HighWaterMarkLevelReached",pWaterMarkLevel);
		if(!pWaterMarkLevel)
		{
			return STATUS_OK; // Go ahead and write the content its OK
		}
		else
		{
		CString  sWaterMarkLevel = pWaterMarkLevel->getTextContent(); 
		
		if (sWaterMarkLevel=="true")
			{
			//Dont allow the guy to write the data to the file....File
			return STATUS_FAILED;
			}
		else
			return STATUS_OK ; // Go ahead and write the content its OK	
		
		}

*/	

		Status retStatus = STATUS_OK;
		NodeRef	pMaxSize, pUsedSize;
		chelper::GetNode(pStorageNode,"TotalSpace",pMaxSize);
		chelper::GetNode(pStorageNode,"SpaceUsed",pUsedSize);
		if(!pMaxSize )
		{

			printf("CInterCepter::HandleFWriteCall NULL obtained for pMaxSize\n");	
			retStatus = STATUS_OK; // Go ahead and write the content , we dont have any other option to check
		}
		else if(!pUsedSize)
		{

			printf("CInterCepter::HandleFWriteCall NULL for pUsedSize\n");
			retStatus = STATUS_OK; // Go ahead and write the content , we dont have any other option to check
		}
		else
		{
			char *endPtr=NULL;
			
			CString sMaxSize =pMaxSize->getTextContent();
			CString sUsedSize=pUsedSize->getTextContent();
			uint64 uMaxSize=strtoull(sMaxSize.c_str(),&endPtr,10);
			uint64 uUsedSize=strtoull(sUsedSize.c_str(),&endPtr,10);
			uint64 uNewSize = uUsedSize + iDataSizeToWrite;
			
			//DEBUGL7("CInterCepter::HandleFWriteCall [uMaxSize=%d, uUsedSize=%d , iDataSizeToWrite=%d]\n",uMaxSize, uUsedSize,iDataSizeToWrite);	

			if(uNewSize <= uMaxSize )
			{
				printf("CInterCepter::HandleFWriteCall allowed writing data to the file as uNewSize[%d]<=uMaxSize[%d]\n" , uNewSize , uMaxSize);	
				//TODO:update the new size in the SRM DOM respository
				//1- Will ir creatate another fwrite call , creating an over head , that is too big to ignore??
				//2- Can we assume that only one write call comes and then the close call comes. If that is the case, cisystemresource manager will update DOM
				//char tempStrSize[128];
				//memset(tempStrSize,'\0',128);
				//sprintf(tempStrSize,"%llu",uNewSize);
				//sUsedSize = tempStrSize;
				//pUsedSize->setTextContent(sUsedSize);
				retStatus = STATUS_OK; // Go ahead and write the content its OK

			}
			else
			{
				//Dont allow the guy to write the data to the file....File
				printf("CInterCepter::HandleFWriteCall prevented writing data to the file as [uNewSize =%d] > [uMaxSize=%d]\n" , uNewSize , uMaxSize);	
				retStatus = STATUS_FAILED;
			}
		}
	pStorageNode=pMaxSize=pUsedSize= NULL;
	return retStatus;

		/* No need to check the space , just check whether the water mark level high is reached 
		//get the total space from DOM
		
		NodeRef	pTotalSpaceNode,pSpaceUsedNode,pWaterMarkLevel;
		chelper::GetNode(pStorageNode,"TotalSpace",pTotalSpaceNode);
		chelper::GetNode(pStorageNode,"SpaceUsed",pSpaceUsedNode);
		chelper::GetNode(pStorageNode,"WaterMarkLevel",pWaterMarkLevel);
		if ((!pTotalSpaceNode) || (!pSpaceUsedNode)||  (!pWaterMarkLevel))
		{ // node kitty illa athra thanne 
			return STATUS_INVALID; 
		}
		else{
			CString sTotalSpace = pTotalSpaceNode->getTextContent();
			CString sSpaceUsed = pSpaceUsedNode->getTextContent();
			CString sWaterMarkLevel = pWaterMarkLevel->getTextContent();
			if(!sTotalSpace.empty() && !sSpaceUsed.empty()&& !sWaterMarkLevel.empty() )
					{
		
				uint64 ullSpaceUsed = 0, ullTotalSpace = 0;
				int iWaterMarkLevel=0, iNewWaterMarkLevel=0;
				ullTotalSpace =strtoull(sTotalSpace.c_str(),NULL,10);
				ullSpaceUsed =strtoull(sSpaceUsed.c_str(),NULL,10);			
				iWaterMarkLevel =strtoull(sWaterMarkLevel.c_str(),NULL,10);		

				iNewWaterMarkLevel = (ullSpaceUsed + iDataSizeToWrite)/ ullTotalSpace ;  
				if (iWaterMarkLevel > iNewWaterMarkLevel)
				{
					//Dont allow the guy to write the data to the file....File
					return STATUS_FAILED;

				}
				
			}
			else 
				return STATUS_INVALID; //false some field is empty 
		}
		*/
		//get current SpaceUsed and spaceavailable and compute them again , if there is change then update them into DOM
		//dont do this m_storageRef->UpdateSize(pSpaceAvailableNode,"SpaceAvailable",ullSpaceAvailable);
	
	return STATUS_OK;
}


int open(const char *pathname, int flags) {

	static OPENTYPE func;
	int ret;
	//DEBUGL7("Inside the library open function call for [%s]\n", pathname);
	
	if(!func)
	func = (OPENTYPE) dlsym(RTLD_NEXT, "open");
	ret=func(pathname,flags);
	
	g_CInterCepter.HandleOpenCall(pathname,flags,ret);


	return ret; 


}

int open(const char *pathname, int flags, mode_t mode) {

	static OPENTYPE_MODE func;
	int ret;
	//DEBUGL7("Inside the library open (with mode) function call for [%s]\n", pathname);
	
	if(!func)
	func = (OPENTYPE_MODE) dlsym(RTLD_NEXT, "open");
	ret=func(pathname,flags,mode);
	
	g_CInterCepter.HandleOpenCall(pathname,flags,ret);


	return ret; 


}

//TODO: There is no need to handle read .delete it 
/*
ssize_t read(int fd, void *buf, size_t count) {
	//void *handle;
	//char *error;
	static ssize_t(*func)(int, const void*,size_t);
		
	if(!func)
		func = (ssize_t (*)(int,const void*,size_t)) dlsym(RTLD_NEXT,"read");
	

	return(func(fd,buf,count));
}

*/
ssize_t write(int fd, const void *buf, size_t count) {
	//void *handle;
	//char *error;
	//DEBUGL7("Inside the library call write going to write [%s] \n", buf );

	static WRITETYPE func;

	if(!func)
		func = (WRITETYPE) dlsym(RTLD_NEXT,"write");


	if (STATUS_FAILED == g_CInterCepter.HandleWriteCall(fd,buf,count))
	{
		//STATUS_FAILED , will not write to disk 
		printf("Inside the library call write : returning with out write as watermark level reached. !!!\n");
		return 0;// Return the invalid case, I dont know how to send the notifiaction though 
	}

	else // invalid and OK cases are treated good for calling the fwrite 
	{
		return(func(fd,buf,count));
	}

}

int close(int fd)
{
	static CLOSETYPE func;
	g_CInterCepter.HandleCloseCall(fd);
	
	if(!func)
		func = (CLOSETYPE) dlsym(RTLD_NEXT,"close");
	return(func(fd));
}


  

FILE * fopen ( const char * filename, const char * mode ){
       //Commenting DEBUGL statement  in all places in this file to avoid stack overflow as in DEBUGL functions again this fopen is called
	//DEBUGL7("Inside the library call fopen filename [%s] mode[%s] \n",filename,mode);

	FILE * pRetFD =NULL;

	static FOPENTYPE func ;
	if(!func)
		func = (FOPENTYPE) dlsym(RTLD_NEXT, "fopen");
	pRetFD =(func(filename,mode));

	g_CInterCepter.HandleFopenCall(filename,mode,pRetFD);

	return pRetFD;
}



  

int fclose ( FILE * stream )
{
	//DEBUGL7("Inside the library call fclose fd=[%d] \n" , stream);


	//void *handle;
	//char *error;
	

	g_CInterCepter.HandleFcloseCall(stream );

	static FCLOSETYPE func ;
	if(!func)
		func = (FCLOSETYPE) dlsym(RTLD_NEXT, "fclose");
	return (func(stream));
	
}



size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
	//void *handle;
	//char *error;
	//DEBUGL7("Inside the library call fwrite going to write [%s] \n",(char *) ptr );
	//DEBUGL7("Inside the library call fwrite going to write \n");
	static  FWRITETYPE func;
	if(!func)
		func = (FWRITETYPE) dlsym(RTLD_NEXT,"fwrite");



	if (STATUS_FAILED == g_CInterCepter.HandleFWriteCall(stream,size*nmemb))
	{
		//STATUS_FAILED , will not write to disk 
		printf("Inside the library call fwrite : returning with out fwrite as maximun size reached reached. !!!\n");
		return 0;// Return the invalid case, I dont know how to send the notifiaction though 
	}

	else // invalid and OK cases are treated good for calling the fwrite 
	{
		//DEBUGL7("Inside the library call fwrite : Writing the content to the file with out any issues.\n");
		return(func(ptr, size, nmemb, stream));
	}

}

//TODO: there is no need to handle fread delete it 
/*
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	DEBUGL7("Inside the library call fread \n");
	//void *handle;
	//char *error;
	static ssize_t(*func)(void*,size_t,size_t,FILE*);

	if(!func)
		func = (ssize_t (*)(void*,size_t,size_t,FILE*)) dlsym(RTLD_NEXT,"fread");
	return(func(ptr,size,nmemb,stream));
}
*/
