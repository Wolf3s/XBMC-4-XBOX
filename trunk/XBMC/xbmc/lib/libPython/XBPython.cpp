#include "XBPython.h"
#include "..\..\sectionLoader.h"

	 /* PY_RW stay's loaded as longs as m_pPythonParser != NULL.
	  * When someone runs a script for the first time both sections PYTHON and PY_RW
	  * are loaded. After that script has finished only section PYTHON is unloaded
	  * and m_pPythonParser is 'not' deleted.
		* Only delete m_pPythonParser and unload PY_RW if you don't want to use Python
		* anymore
		*/

XBPython m_pythonParser;

XBPython::XBPython()
{
	bInitialized = false;
	bThreadInitialize = false;
	nextid = 0;
	mainThreadState = NULL;
	InitializeCriticalSection(&m_critSection);
	m_hEvent = CreateEvent(NULL, false, false, "pythonEvent");
	dThreadId = GetCurrentThreadId();
}

void XBPython::Initialize()
{
	g_sectionLoader.Load("PYTHON");

	if(!bInitialized)
	{
		if(dThreadId == GetCurrentThreadId())
		{
			g_sectionLoader.Load("PY_RW");

			Py_Initialize();
			PyEval_InitThreads();
			initxbmc(); // init xbmc modules
			// redirecting default output to debug console
			if (PyRun_SimpleString(""
					"import xbmc\n"
					"class xbmcout:\n"
					"	def write(self, data):\n"
					"		xbmc.output(data)\n"
					"	def close(self):\n"
					"		xbmc.output('.')\n"
					"	def flush(self):\n"
					"		xbmc.output('.')\n"
					"\n"
					"import sys\n"
					"sys.stdout = xbmcout()\n"
					"sys.stderr = xbmcout()\n"
					"print '-->Python Initialized<--'\n"
					"") == -1)
			{
				OutputDebugString("Python Initialize Error\n");
			}

			mainThreadState = PyThreadState_Get();
			// release the lock
			PyEval_ReleaseLock();

			bInitialized = true;
			bThreadInitialize = false;
			PulseEvent(m_hEvent);
		}
		else
		{
			// only the main thread should initialize python, unload python again.
			g_sectionLoader.Unload("PYTHON");
			bThreadInitialize = true;
			WaitForSingleObject(m_hEvent, INFINITE);
		}
	}
}

void XBPython::Finalize()
{
	CloseHandle(m_hEvent);
	g_sectionLoader.Unload("PYTHON");
}

void XBPython::FreeResources()
{
	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		it->pyThread->stop();
		++it;
	}
	Process();

	if(bInitialized)
	{
		// shut down the interpreter
		PyEval_AcquireLock();
		PyThreadState_Swap(mainThreadState);
		Py_Finalize();
		// free_arenas();
	}
	g_sectionLoader.Load("PY_RW");
	DeleteCriticalSection(&m_critSection);
}

void XBPython::Process()
{
	if (bThreadInitialize) Initialize();
	if (!bInitialized) return;

	EnterCriticalSection(&m_critSection );

	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		//delete scripts which are done
		if (it->bDone)
		{
			//wait 10 sec, should be enough for slow scripts :-)
			it->pyThread->WaitForThreadExit(10000); 
			delete it->pyThread;
			vecPyList.erase(it);
			Finalize();
		}
		else ++it;
	}

	LeaveCriticalSection(&m_critSection );
}

int XBPython::evalFile(const char *src)
{
	Initialize();

	nextid++;
	XBPyThread *pyThread = new XBPyThread(this, mainThreadState, nextid);
	pyThread->evalFile(src);
	PyElem inf;
	inf.id = nextid;
	inf.bDone = false;
	inf.strFile = src;
	inf.pyThread = pyThread;

	EnterCriticalSection(&m_critSection );
	vecPyList.push_back(inf);
	LeaveCriticalSection(&m_critSection );

	return nextid;
}

int XBPython::evalString(const char *src)
{
	/*
	nextid++;
	if (!Py_IsInitialized()) return -1;
	XBPyThread *pyThread = new XBPyThread(this, mainThreadState, nextid);
	pyThread->evalString(src);
	//strcpy(strFile, "");
	//addToList(pyThread);
	return nextid;
	*/
	return 0;
}

void XBPython::setDone(int id)
{
	EnterCriticalSection(&m_critSection);
	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		if (it->id == id)
		{
			if (it->pyThread->isStopping())
				OutputDebugString("Python script interrupted by user\n");
			else
				OutputDebugString("Python script stopped\n");
			it->bDone = true;
		}
		++it;
	}
	LeaveCriticalSection(&m_critSection);
}

void XBPython::stopScript(int id)
{
	EnterCriticalSection(&m_critSection);

	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		if (it->id == id) {
			Py_Output("Stopping script with id: %i\n", id);
			it->pyThread->stop();
			LeaveCriticalSection(&m_critSection);
			return;
		}
		++it;
	}
	LeaveCriticalSection(&m_critSection );
}

PyThreadState *XBPython::getMainThreadState()
{
	return mainThreadState;
}

int XBPython::ScriptsSize()
{
	int iSize;
	EnterCriticalSection(&m_critSection);

	iSize = vecPyList.size();

	LeaveCriticalSection(&m_critSection);
	return iSize;	
}

const char* XBPython::getFileName(int scriptId)
{
	const char* cFileName = NULL;
	EnterCriticalSection(&m_critSection);

	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		if (it->id == scriptId) cFileName = it->strFile.c_str();
		++it;
	}

	LeaveCriticalSection(&m_critSection);
	return cFileName;
}

int XBPython::getScriptId(const char* strFile)
{
	int iId = -1;
	EnterCriticalSection(&m_critSection);

	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		if (!strcmp(it->strFile.c_str(), strFile)) iId = it->id;
		++it;
	}

	LeaveCriticalSection(&m_critSection);
	return iId;
}

bool XBPython::isRunning(int scriptId)
{
	bool bRunning = false;
	EnterCriticalSection(&m_critSection);

	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		if (it->id == scriptId)	bRunning = true;
		++it;
	}

	LeaveCriticalSection(&m_critSection);
	return bRunning;
}

bool XBPython::isStopping(int scriptId)
{
	bool bStopping = false;
	EnterCriticalSection(&m_critSection);

	PyList::iterator it = vecPyList.begin();
	while (it != vecPyList.end())
	{
		if (it->id == scriptId) bStopping = it->pyThread->isStopping();
		++it;
	}

	LeaveCriticalSection(&m_critSection);
	return bStopping;
}

int XBPython::GetPythonScriptId(int scriptPosition)
{
	return (int)vecPyList[scriptPosition].id;
}