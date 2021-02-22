#ifndef __SimplifyThread_H__
#define __SimplifyThread_H__


#include <assert.h>

#ifdef   WIN32
#include <process.h>
#include <windows.h>


#define MS_VC_EXCEPTION 0x406D1388

typedef struct tagTHREADNAME_INFO
{
	DWORD  dwType;     // Must be 0x1000.
	LPCSTR szName;     // Pointer to name (in user addr space).
	DWORD  dwThreadID; // Thread ID (-1=caller thread).
	DWORD  dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;

typedef HANDLE         ThreadHandle_t;
typedef unsigned int   ThreadRet_t;


#else
#include <pthread.h>


typedef pthread_t      ThreadHandle_t;
typedef void *         ThreadRet_t;
#endif


// ------------------------------------------------------------------------
// thread 
template <typename TThreadCallback>
class CSimplifyThread
{
public:
	explicit CSimplifyThread(void * user = 0)
		: m_obj(0)
		, m_mfunc(0)
		, m_param(0)
		, m_handle(NULL)
		, m_id(0)
		, m_user(user)
		, m_name(0)
	{}

	~CSimplifyThread()
	{
		SafeStop();
	}

	int Start(TThreadCallback * tc, void (TThreadCallback::*tf)(void*), void * param = 0)
	{
		if(m_handle)
		{
			// Assert(0);
			return -1;
		}

		m_obj   = tc;
		m_mfunc = tf;
		m_param = param;
		return CreateThread();
	}

	int SafeStop(int nWait = INFINITE)
	{
		if(!m_handle)
		{
			return 0;
		}

		#ifdef _WIN32
		if(WAIT_OBJECT_0 == WaitForSingleObject(m_handle, nWait))
		#else
		pthread_join(m_handle, 0);
		#endif
		{
			CloseHandle(m_handle);
			m_handle = NULL;
			return 0;
		}

		return -1;
	}

	bool   IsStop()  { return m_handle == 0; }
	void * GetUser() { return m_user; }
	int    Id()      { return m_id; }
	void   SetName(const char * name) { m_name = name; }


private:
	static ThreadRet_t __stdcall ThreadFunctionImpl(void* param)
	{
		assert(param != NULL);

		typedef CSimplifyThread<TThreadCallback> CSimplifyThreadObj;
		CSimplifyThreadObj * pThread = static_cast<CSimplifyThreadObj*>(param);

		assert(pThread);
		if(pThread->m_name)  pThread->SetThreadName();
		if(pThread->m_obj && pThread->m_mfunc)
		{
			(pThread->m_obj->*(pThread->m_mfunc))(pThread->m_param);
		}

		return 0;
	}

	int  CreateThread()
	{
		#ifdef _WIN32
		/* 
		 * uintptr_t _beginthreadex( 
		 *		void     *security,
		 *		unsigned stack_size,
		 *		unsigned ( *start_address )( void * ),
		 *		void     *arglist,
		 *		unsigned initflag,
		 *		unsigned *thrdaddr 
		 *		);
		 */
		if(m_handle) CloseHandle(m_handle);
		return (m_handle = (HANDLE)_beginthreadex(NULL, 0, ThreadFunctionImpl, this, 0, &m_id)) != NULL ? 0 : -1;
		#else
		if(m_handle) pthread_join(m_handle, 0);
		return pthread_create(&m_handle, 0, ThreadFunctionImpl, this);
		#endif
	}

	void SetThreadName()
	{
		#ifdef _WIN32
		THREADNAME_INFO info;
		info.dwType     = 0x1000;
		info.szName     = m_name;
		info.dwThreadID = m_id;
		info.dwFlags    = 0;

		__try
		{
			RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(DWORD), (DWORD*)&info);
		}
		__except(EXCEPTION_CONTINUE_EXECUTION)
		{
			return ;
		}
		#endif
	}


private:
	typedef void (TThreadCallback::*ThreadMemFunctor1)(void *);
	TThreadCallback   * m_obj;
	ThreadMemFunctor1   m_mfunc;
	void              * m_param, * m_user;
	ThreadHandle_t      m_handle;
	unsigned int        m_id; 
	const char        * m_name;
};


#endif
