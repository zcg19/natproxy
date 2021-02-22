#ifndef __Lock_H__
#define __Lock_H__


#ifdef _WIN32
#include <windows.h>

#else
#include <pthread.h>

#define  CRITICAL_SECTION                pthread_mutex_t
#define  InitializeCriticalSection(_ps)  memset(_ps, 0, sizeof(*(_ps)))
#define  DeleteCriticalSection(_ps)      pthread_mutex_destroy(_ps)
#define  EnterCriticalSection(_ps)       pthread_mutex_lock(_ps)
#define  LeaveCriticalSection(_ps)       pthread_mutex_unlock(_ps)
#endif


class CBaseLockObject
{
public:
	virtual void Lock()   = 0;
	virtual void Unlock() = 0;
};


class CGenericLockHandler
{
public:
	CGenericLockHandler(CBaseLockObject & lock)
		: m_lock(lock)
		, m_locked(false)
	{
		Lock();
	}

	virtual ~CGenericLockHandler()
	{
		Unlock();
	}

	// dangerous ???
	// Unlock(): 'if(m_locked)' maybe multithread 'm_locked' state not sync.
	virtual void Lock()   { m_lock.Lock(); m_locked = true; }
	virtual void Unlock() { if(m_locked) { m_locked = false; m_lock.Unlock(); } }
	virtual BOOL IsLock() { return m_locked; }


protected:
	CBaseLockObject & m_lock;
	BOOL              m_locked;
};


class CCriticalSetionObject : public CBaseLockObject
{
public:
	CCriticalSetionObject(CRITICAL_SECTION & cs, BOOL bInit = TRUE)
		: m_cs(cs)
		, m_init(bInit)
	{
		if(m_init) InitializeCriticalSection(&m_cs);
	}

	CCriticalSetionObject()
		: m_init(TRUE)
	{
		if(m_init) InitializeCriticalSection(&m_cs);
	}

	~CCriticalSetionObject()
	{
		if(m_init) DeleteCriticalSection(&m_cs);
	}

	void Lock()
	{
		EnterCriticalSection(&m_cs);
	}

	void Unlock()
	{
		LeaveCriticalSection(&m_cs);
	}


private:
	CRITICAL_SECTION m_cs;
	BOOL             m_init;
};


#endif
