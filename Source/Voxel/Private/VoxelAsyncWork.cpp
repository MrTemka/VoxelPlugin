// Copyright 2020 Phyronnaz

#include "VoxelAsyncWork.h"
#include "VoxelGlobals.h"
#include "HAL/Event.h"
#include "VoxelStatsUtilities.h"

FVoxelAsyncWork::~FVoxelAsyncWork()
{
	VOXEL_FUNCTION_COUNTER();
	
	// Can't delete while we're in a critical section, eg if delete is called async by PostDoWork
	DoneSection.Lock();
	DoneSection.Unlock();
	checkf(IsDone(), TEXT("Name: %s"), *Name.ToString()); // Else it means we're still somewhere in a thread pool
}

void FVoxelAsyncWork::DoThreadedWork()
{
	VOXEL_FUNCTION_COUNTER();
	
	check(!IsDone());

	if (!IsCanceled())
	{
		VOXEL_SCOPE_COUNTER_FORMAT("DoWork: %s", *Name.ToString());
		DoWork();
	}

	DoneSection.Lock();

	IsDoneCounter.Increment();

	if (!IsCanceled())
	{
		VOXEL_SCOPE_COUNTER("PostDoWork");
		check(IsDone());
		PostDoWork();
	}

	if (bAutodelete)
	{
		DoneSection.Unlock();
		delete this;
		return;
	}

	DoneSection.Unlock();
	// Might be deleted right after this
}

void FVoxelAsyncWork::Abandon()
{
	VOXEL_FUNCTION_COUNTER();
	
	check(!IsDone());

	DoneSection.Lock();

	IsDoneCounter.Increment();
	WasAbandonedCounter.Increment();
	
	if (bAutodelete)
	{
		DoneSection.Unlock();
		delete this;
	}
	else
	{
		DoneSection.Unlock();
	}
}

bool FVoxelAsyncWork::CancelAndAutodelete()
{
	VOXEL_FUNCTION_COUNTER();
	
	DoneSection.Lock();
	
	check(!bAutodelete);

	bAutodelete = true;
	CanceledCounter.Increment();

	if (IsDone())
	{
		DoneSection.Unlock();
		delete this;
		return true;
	}
	else
	{
		DoneSection.Unlock();
		return false;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FVoxelAsyncWorkWithWait::FVoxelAsyncWorkWithWait(FName Name, double PriorityDuration, bool bAutoDelete)
	: FVoxelAsyncWork(Name, PriorityDuration, bAutoDelete)
{
	DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
	DoneEvent->Reset();
}

FVoxelAsyncWorkWithWait::~FVoxelAsyncWorkWithWait()
{
	if (DoneEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		DoneEvent = nullptr;
	}
}

void FVoxelAsyncWorkWithWait::PostDoWork()
{
	PostDoWorkBeforeTrigger();
	DoneEvent->Trigger();
}

void FVoxelAsyncWorkWithWait::WaitForCompletion()
{
	DoneEvent->Wait();
	check(IsDone());
}
