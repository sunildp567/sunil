// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

#ifndef STORAGE_H_
#define STORAGE_H_

#include <stdio.h>
#include <mntent.h>
#include <string>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <CI/status.h>
#include <CI/OperatingEnvironment/cstring.h>
#include <CI/HierarchicalDB/hierarchicaldb.h>
#include <CI/HierarchicalDB/DOM/document.h>
#include <CI/HierarchicalDB/DOM/node.h>

namespace ci
{
namespace systemresourcemanager
{
	DECL_OBJ_REF(Storage);

	//Class that does creation/deletion/add and remove of nodes understand the Storage node of the System resource manager document
	class Storage  
	{
		private:
			ci::hierarchicaldb::HierarchicalDBRef m_pHDB;
			dom::DocumentRef m_SystemResourceManagerDOM;
		public:
			Storage(ci::hierarchicaldb::HierarchicalDBRef mainHDBRef, dom::DocumentRef mainDomRef);
			~Storage();
			bool IsThisStoragePresent(ci::operatingenvironment::CString	sStorageName);
			Status AddStorage(ci::operatingenvironment::CString sStoragePath,ci::operatingenvironment::CString sTotalSpaceInBytes, \
						ci::operatingenvironment::CString sSpaceUsedInBytes, ci::operatingenvironment::CString sSpaceAvailableInBytes,ci::operatingenvironment::CString sWaterMarkLevel);
			Status AddDirectory(ci::operatingenvironment::CString sFilePath,ci::operatingenvironment::CString sTotalSpaceInBytes, \
					 ci::operatingenvironment::CString sSpaceUsedInBytes, ci::operatingenvironment::CString sSpaceAvailableInBytes,ci::operatingenvironment::CString sWaterMarkLevel);
			Status AddFile(ci::operatingenvironment::CString sFilePath,ci::operatingenvironment::CString sTotalSpaceInBytes, \
					ci::operatingenvironment::CString sSpaceUsedInBytes, ci::operatingenvironment::CString sSpaceAvailableInBytes,ci::operatingenvironment::CString sWaterMarkLevel);
			uint64 GetFileOrDirectorySize(const char *argv);
			Status GetSize(ci::operatingenvironment::Ref<dom::Node> pNode,uint64 &Size );
			void UpdateSize(ci::operatingenvironment::Ref<dom::Element> pNode,ci::operatingenvironment::CString sNodeName,uint64 Size );
			bool IsThisDirectoryPresent(ci::operatingenvironment::CString sDirectoryName );
			bool IsThisDirectoryPresent(ci::operatingenvironment::CString sStorageName, ci::operatingenvironment::CString sDirectoryName );
			bool IsThisFilePresent(ci::operatingenvironment::CString sStorageName, ci::operatingenvironment::CString sFileName );
			bool GetMountPoint(const ci::operatingenvironment::CString sFileOrDirectory, ci::operatingenvironment::CString &sMountPoint);
			Status DeleteStorage(ci::operatingenvironment::CString sStorageName);
			Status DeleteDirectory(ci::operatingenvironment::CString sDirectoryName);
			Status DeleteFile(ci::operatingenvironment::CString sFileName);
			Status AddChildNodeNSetItsValue(dom::NodeRef pParentNode,ci::operatingenvironment::CString sChildNodeName, ci::operatingenvironment::CString sChildNodeValue);
			Status GetPartitionSizeInfo(ci::operatingenvironment::CString sPath, ci::operatingenvironment::CString &sTotalSpace, ci::operatingenvironment::CString &sSpaceUsed, ci::operatingenvironment::CString &sSpaceAvailable);
			ci::operatingenvironment::CString Uint64ToString(uint64 ullNumber);
			uint64  StringToUint64(ci::operatingenvironment::CString sNumber);
			uint64  GetAvailableSize(ci::operatingenvironment::CString path);
						
	};
} //SystemResourceManager
} //CI


#endif /*STORAGE_H_*/
