// Copyright 2008 TOSHIBA TEC CORPORATION All rights reserved //

//Jbinu: Very simple Unit test Library of  for SRM component
#include <iostream>
#include <map>

#include <status.h>
#include <string>
#include <stdio.h>
#include <fcntl.h>

#include "CI/OperatingEnvironment/cstring.h"

using namespace std;
using namespace ci::operatingenvironment;


typedef map < string, FILE* > FILEMAP;
typedef map < string, int > STREAMFILEMAP;

class CInterCepterTest
{

	FILEMAP m_FilenameMap;
	STREAMFILEMAP m_StreamFilenameMap;

	FILE * GetFdFromFileName(char *pUserInput);
	int  GetStreamFdFromFileName(char *pUserInput);

public:
		FILE * HandleFopen(char *pUserInput);
		FILE * HandleFWrite(char *pUserInput);
		FILE * HandleFClose(char *pUserInput);


		int  HandleOpen(const char *pUserInput);
		int  HandleOpenWithMode(const char *pUserInput);
		int  HandleWrite(char *pUserInput);
		int  HandleClose(char *pUserInput);




};


//getfd  for fopen , fwrite 
FILE * CInterCepterTest::GetFdFromFileName(char *pUserInput)
{
	FILE *fp= NULL;
	//fp = m_FilenameMap.find(pUserInput);
	if(m_FilenameMap.count(pUserInput) == 0)			
	{
	DEBUGL1(" Failed in CInterCepterTest::GetFdFromFileName m_FilenameMap[pUserInput] returned  NULL \n");
	return 	NULL;
	}
	fp = m_FilenameMap[pUserInput];
	printf(" File fd = %d \n" , (int)  fp);

	return fp;
}



//getfd  open , and read 
int  CInterCepterTest::GetStreamFdFromFileName(char *pUserInput)
{

	if(m_StreamFilenameMap.count(pUserInput) == 0)			
	{
	DEBUGL1(" Failed in CInterCepterTest::GetStreamFdFromFileName m_StreamFilenameMap[pUserInput] returned  NULL \n");
	return 	-1;
	}
	int fd  = m_StreamFilenameMap[pUserInput];
	printf(" File fd = %d \n" ,  fd);

	return fd;
}



//fopn 
FILE * CInterCepterTest::HandleFopen(char *pUserInput)
{
	FILE *fp= fopen(pUserInput, "a");
	if (NULL==fp)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleFopen fopen returned NULL with errno = %d\n", errno);
		return NULL;
		}
		
	m_FilenameMap.insert(pair< string,FILE* >(pUserInput,fp));
		DEBUGL1(" CInterCepterTest::HandleFopen success  FILEFD =%d \n" , (int) fp);
	return fp;
}

//fwrite
FILE * CInterCepterTest::HandleFWrite(char *pUserInput)
{
	char oneline[]= "this is just a another line \n";

	DEBUGL6(" CInterCepterTest::HandleFWrite Entering function with input [%s] \n",pUserInput);

	FILE *fp= GetFdFromFileName(pUserInput);
	if (NULL==fp)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleFWrite *fp= GetFdFromFileName  returned NULL \n");
		return NULL;
		}
		//fprintf(fp,"this is just a another line in %s\n" ,pUserInput);
		fwrite (oneline, sizeof ( oneline),1, fp);
		DEBUGL6(" CInterCepterTest::HandleFWrite return success  \n");

	return fp;
}


//fclose
FILE * CInterCepterTest::HandleFClose(char *pUserInput)
{

		FILE *fp= GetFdFromFileName(pUserInput);
		if (NULL==fp)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleFClose *fp= GetFdFromFileName  returned NULL \n");
		return NULL;
		}
		
		fprintf(fp,"This is going to be Close \n" );
		fclose(fp);
		m_FilenameMap.erase(m_FilenameMap.find(pUserInput));
		DEBUGL1(" CInterCepterTest::HandleFClose  success  \n");

return fp;
}




//open 
//int open(const char *pathname, int flags);

int  CInterCepterTest::HandleOpen(const char *pUserInput)
{
	int fd;
	fd= open(pUserInput, O_RDWR|O_APPEND);
	if (-1==fd)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleOopen fopen returned -1  with errno = %d\n", errno);
		return -1;
		}
		
	m_StreamFilenameMap.insert(pair< string,int >(pUserInput,fd));
		DEBUGL1(" CInterCepterTest::HandleOpen success  streamId =%d \n" ,  fd);
	return fd;
}


//open  with mode 
//int open(const char *pathname, int flags, mode_t mode);
//mode  must  be  specified  when O_CREAT is in the flags, and is ignored otherwise.

int  CInterCepterTest::HandleOpenWithMode(const char *pUserInput)
{
	int fd= open(pUserInput, O_RDWR|O_CREAT|O_APPEND, S_IRWXU| S_IRGRP|S_IRWXO);
	if (-1 ==fd)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleOopenWithMode open returned -1  \n");
		return -1;
		}
		
	m_StreamFilenameMap.insert(pair< string,int >(pUserInput,fd));
		DEBUGL1(" CInterCepterTest::HandleOpen success  streamId =%d \n" ,  fd);
	return fd;
}


//write
int CInterCepterTest::HandleWrite(char *pUserInput)
{
	char oneline[]= "This is just a another line for Write \n";

	DEBUGL6(" CInterCepterTest::HandleWrite Entering function with input [%s] \n",pUserInput);

	int fd = GetStreamFdFromFileName(pUserInput);
	if (-1==fd)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleWrite fd= GetStreamFdFromFileName  returned NULL \n");
		return -1;
		}
	if(write (fd,oneline, sizeof ( oneline)) != -1)
		DEBUGL6(" CInterCepterTest::HandleWrite return success  \n");
	else
		DEBUGL6(" CInterCepterTest::HandleWrite Fail returned -1 \n");

	return fd;
}


//close
int CInterCepterTest::HandleClose(char *pUserInput)
{

		int fd = GetStreamFdFromFileName(pUserInput);
		if (NULL==(int)fd)
		{
		DEBUGL1(" Failed in CInterCepterTest::HandleFClose fd = GetFdFromFileName  returned NULL \n");
		return NULL;
		}
				
		close(fd);
		m_StreamFilenameMap.erase(m_StreamFilenameMap.find(pUserInput));
		DEBUGL1(" CInterCepterTest::HandleClose  success  \n");

return fd;
}




int main (int argc, char **argv)
{
	
	CInterCepterTest obj_iTest;


	CString argstr;
	int arg;
	

   
    ProgramOptions::BeginOpts(argc, argv, "suwagdp:",
    		 "(-p <port>)",
               "    -p = SRM's port\n");
    
    while ((arg = ProgramOptions::GetOpt(argstr)) >= 0)
    	{};
    ProgramOptions::EndOpts();







	while (1)
	{
		//CString  sInput="";
		char sInput [200];
		sInput [199]= 0;
		char    *pUserInput=sInput ;
		char 	option = 0;
		//Get User input
	GetUserInput:
		cout << "Input your option: [Please dont input any wrong format, No validation done for inputs ] \n"
			   	"o <filename>= FOpen a file \n"
				"w <filename>= FWrite into the file \n"
		       	"c <filename>= FClose the file \n"
				"P <filename>= Open a file [No mode]\n"
				"O <filename>= Open a file \n"
				"W <filename>= Write into the file \n"
		       	"C <filename>= Close the file \n"
				

				"q  Quit This Test\n "
		      	<<endl;

		//getline(cin, sInput);
//		cin>>sInput;

		pUserInput=sInput ;
		gets(sInput);
	
		//get option 
		while (*pUserInput && isspace(*pUserInput))
		{
			pUserInput++; //skiping the space 
		}
		if (*pUserInput == '\0' )
		{
			goto GetUserInput; // ignore blank lines 
		}
		else
		{
			option = *pUserInput;
			pUserInput ++;
			if (option=='q') exit(0);
			if (option=='Q') exit(0);
			//cout << "Option Received: \n" << endl;
		}
		
		// get file name 
	    while (*pUserInput && isspace(*pUserInput))
		{
			pUserInput++; //skiping the space 
		}
		if (*pUserInput == '\0' )
		{
			//cout << "File name not received  \n" << endl;
			goto GetUserInput; // ignore blank lines 
		}
		
		//cout << "Received option: ["<<option <<"] File: ["<<pUserInput <<"]" << endl;


		
		switch (option)
		{
		case 'o': obj_iTest.HandleFopen(pUserInput);break;
		case 'w': obj_iTest.HandleFWrite(pUserInput);break;
		case 'c': obj_iTest.HandleFClose(pUserInput);break;
		case 'P': obj_iTest.HandleOpen(pUserInput);break;
		case 'O': obj_iTest.HandleOpenWithMode(pUserInput);break;
		case 'W': obj_iTest.HandleWrite(pUserInput);break;
		case 'C': obj_iTest.HandleClose(pUserInput);break;
		
		default :
		//cout << " Invalid input , please enter one of listed options...\n"<< endl;
		break;
		}
	}
    
    
	return 0;
}




