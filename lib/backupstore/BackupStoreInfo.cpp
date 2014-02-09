// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreInfo.cpp
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>

#include "Archive.h"
#include "BackupStoreInfo.h"
#include "BackupStoreException.h"
#include "RaidFileWrite.h"
#include "RaidFileRead.h"

#include "MemLeakFindOn.h"

#ifdef BOX_RELEASE_BUILD
	#define 	NUM_DELETED_DIRS_BLOCK	256
#else
	#define 	NUM_DELETED_DIRS_BLOCK	2
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::BackupStoreInfo()
//		Purpose: Default constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreInfo::BackupStoreInfo()
	: mAccountID(-1),
	  mDiscSet(-1),
	  mReadOnly(true),
	  mIsModified(false),
	  mClientStoreMarker(0),
	  mLastObjectIDUsed(-1),
	  mBlocksUsed(0),
	  mBlocksInCurrentFiles(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0),
	  mBlocksInDirectories(0),
	  mNumCurrentFiles(0),
	  mNumOldFiles(0),
	  mNumDeletedFiles(0),
	  mNumDirectories(0),
	  mAccountEnabled(true)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::~BackupStoreInfo
//		Purpose: Destructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreInfo::~BackupStoreInfo()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::CreateNew(int32_t, const std::string &, int)
//		Purpose: Create a new info file on disc
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::CreateNew(int32_t AccountID, const std::string &rRootDir, int DiscSet, int64_t BlockSoftLimit, int64_t BlockHardLimit)
{
	BackupStoreInfo info;
	info.mAccountID = AccountID;
	info.mDiscSet = DiscSet;
	info.mReadOnly = false;
	info.mLastObjectIDUsed = 1;
	info.mBlocksSoftLimit = BlockSoftLimit;
	info.mBlocksHardLimit = BlockHardLimit;

	// Generate the filename
	ASSERT(rRootDir[rRootDir.size() - 1] == '/' ||
		rRootDir[rRootDir.size() - 1] == DIRECTORY_SEPARATOR_ASCHAR);
	info.mFilename = rRootDir + INFO_FILENAME;
	info.mExtraData.SetForReading(); // extra data is empty in this case

	info.Save(false);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::Load(int32_t, const std::string &,
//			 int, bool)
//		Purpose: Loads the info from disc, given the root
//			 information. Can be marked as read only.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreInfo> BackupStoreInfo::Load(int32_t AccountID,
	const std::string &rRootDir, int DiscSet, bool ReadOnly,
	int64_t *pRevisionID)
{
	// Generate the filename
	std::string fn(rRootDir + INFO_FILENAME);
	
	// Open the file for reading (passing on optional request for revision ID)
	std::auto_ptr<RaidFileRead> rf(RaidFileRead::Open(DiscSet, fn, pRevisionID));

	// Read in format and version
	int32_t magic;
	if(!rf->ReadFullBuffer(&magic, sizeof(magic), 0))
	{
		THROW_FILE_ERROR("Failed to read store info file: "
			"short read of magic number", fn,
			BackupStoreException, CouldNotLoadStoreInfo);
	}

	bool v1 = false, v2 = false;

	if(ntohl(magic) == INFO_MAGIC_VALUE_1)
	{
		v1 = true;
	}
	else if(ntohl(magic) == INFO_MAGIC_VALUE_2)
	{
		v2 = true;
	}
	else
	{
		THROW_FILE_ERROR("Failed to read store info file: "
			"unknown magic " << BOX_FORMAT_HEX32(ntohl(magic)),
			fn, BackupStoreException, BadStoreInfoOnLoad);
	}

	// Make new object
	std::auto_ptr<BackupStoreInfo> info(new BackupStoreInfo);
	
	// Put in basic location info
	info->mAccountID = AccountID;
	info->mDiscSet = DiscSet;
	info->mFilename = fn;
	info->mReadOnly = ReadOnly;
	int64_t numDelObj = 0;

	if (v1)
	{
		// Read in a header
		info_StreamFormat_1 hdr;
		rf->Seek(0, IOStream::SeekType_Absolute);

		if(!rf->ReadFullBuffer(&hdr, sizeof(hdr),
			0 /* not interested in bytes read if this fails */))
		{
			THROW_FILE_ERROR("Failed to read store info header",
				fn, BackupStoreException, CouldNotLoadStoreInfo);
		}
		
		// Check it
		if((int32_t)ntohl(hdr.mAccountID) != AccountID)
		{
			THROW_FILE_ERROR("Found wrong account ID in store info",
				fn, BackupStoreException, BadStoreInfoOnLoad);
		}
		
		// Insert info from file
		info->mClientStoreMarker	= box_ntoh64(hdr.mClientStoreMarker);
		info->mLastObjectIDUsed		= box_ntoh64(hdr.mLastObjectIDUsed);
		info->mBlocksUsed 		= box_ntoh64(hdr.mBlocksUsed);
		info->mBlocksInOldFiles 	= box_ntoh64(hdr.mBlocksInOldFiles);
		info->mBlocksInDeletedFiles	= box_ntoh64(hdr.mBlocksInDeletedFiles);
		info->mBlocksInDirectories	= box_ntoh64(hdr.mBlocksInDirectories);
		info->mBlocksSoftLimit		= box_ntoh64(hdr.mBlocksSoftLimit);
		info->mBlocksHardLimit		= box_ntoh64(hdr.mBlocksHardLimit);
		
		// Load up array of deleted objects
		numDelObj = box_ntoh64(hdr.mNumberDeletedDirectories);
	}
	else if(v2)
	{
		Archive archive(*rf, IOStream::TimeOutInfinite);

		// Check it
		int32_t FileAccountID;
		archive.Read(FileAccountID);
		if (FileAccountID != AccountID)
		{
			THROW_FILE_ERROR("Found wrong account ID in store "
				"info: " << BOX_FORMAT_HEX32(FileAccountID),
				fn, BackupStoreException, BadStoreInfoOnLoad);
		}

		archive.Read(info->mAccountName);
		archive.Read(info->mClientStoreMarker);
		archive.Read(info->mLastObjectIDUsed);
		archive.Read(info->mBlocksUsed);
		archive.Read(info->mBlocksInCurrentFiles);
		archive.Read(info->mBlocksInOldFiles);
		archive.Read(info->mBlocksInDeletedFiles);
		archive.Read(info->mBlocksInDirectories);
		archive.Read(info->mBlocksSoftLimit);
		archive.Read(info->mBlocksHardLimit);
		archive.Read(info->mNumCurrentFiles);
		archive.Read(info->mNumOldFiles);
		archive.Read(info->mNumDeletedFiles);
		archive.Read(info->mNumDirectories);
		archive.Read(numDelObj);
	}

	// Then load the list of deleted directories
	if(numDelObj > 0)
	{
		int64_t objs[NUM_DELETED_DIRS_BLOCK];
		
		int64_t toload = numDelObj;
		while(toload > 0)
		{
			// How many in this one?
			int b = (toload > NUM_DELETED_DIRS_BLOCK)?NUM_DELETED_DIRS_BLOCK:((int)(toload));
			
			if(!rf->ReadFullBuffer(objs, b * sizeof(int64_t), 0 /* not interested in bytes read if this fails */))
			{
				THROW_EXCEPTION(BackupStoreException, CouldNotLoadStoreInfo)
			}
			
			// Add them
			for(int t = 0; t < b; ++t)
			{
				info->mDeletedDirectories.push_back(box_ntoh64(objs[t]));
			}
			
			// Number loaded
			toload -= b;
		}
	}

	// Final check
	if(static_cast<int64_t>(info->mDeletedDirectories.size()) != numDelObj)
	{
		THROW_EXCEPTION(BackupStoreException, BadStoreInfoOnLoad)
	}

	if(v2)
	{
		Archive archive(*rf, IOStream::TimeOutInfinite);
		archive.ReadIfPresent(info->mAccountEnabled, true);
	}
	else
	{
		info->mAccountEnabled = true;
	}
	
	// If there's any data left in the info file, from future additions to
	// the file format, then we need to load it so that it won't be lost when
	// we resave the file.
	IOStream::pos_type bytesLeft = rf->BytesLeftToRead();
	if (bytesLeft > 0)
	{
		rf->CopyStreamTo(info->mExtraData);
	}
	info->mExtraData.SetForReading();
	
	// return it to caller
	return info;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::CreateForRegeneration(...)
//		Purpose: Return an object which can be used to save for regeneration.
//		Created: 23/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreInfo> BackupStoreInfo::CreateForRegeneration(
	int32_t AccountID, const std::string& rAccountName,
	const std::string &rRootDir, int DiscSet,
	int64_t LastObjectID, int64_t BlocksUsed,
	int64_t BlocksInCurrentFiles, int64_t BlocksInOldFiles,
	int64_t BlocksInDeletedFiles, int64_t BlocksInDirectories,
	int64_t BlockSoftLimit, int64_t BlockHardLimit,
	bool AccountEnabled, IOStream& ExtraData)
{
	// Generate the filename
	std::string fn(rRootDir + INFO_FILENAME);
	
	// Make new object
	std::auto_ptr<BackupStoreInfo> info(new BackupStoreInfo);
	
	// Put in basic info
	info->mAccountID = AccountID;
	info->mAccountName = rAccountName;
	info->mDiscSet = DiscSet;
	info->mFilename = fn;
	info->mReadOnly = false;

	// Insert info starting info
	info->mClientStoreMarker	= 0;
	info->mLastObjectIDUsed		= LastObjectID;
	info->mBlocksUsed 		= BlocksUsed;
	info->mBlocksInCurrentFiles	= BlocksInCurrentFiles;
	info->mBlocksInOldFiles 	= BlocksInOldFiles;
	info->mBlocksInDeletedFiles	= BlocksInDeletedFiles;
	info->mBlocksInDirectories	= BlocksInDirectories;
	info->mBlocksSoftLimit		= BlockSoftLimit;
	info->mBlocksHardLimit		= BlockHardLimit;
	info->mAccountEnabled		= AccountEnabled;
	
	ExtraData.CopyStreamTo(info->mExtraData);
	info->mExtraData.SetForReading();

	// return it to caller
	return info;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::Save(bool allowOverwrite)
//		Purpose: Save modified info back to disc
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::Save(bool allowOverwrite)
{
	// Make sure we're initialised (although should never come to this)
	if(mFilename.empty() || mAccountID == -1 || mDiscSet == -1)
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}

	// Can we do this?
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	// Then... open a write file
	RaidFileWrite rf(mDiscSet, mFilename);
	rf.Open(allowOverwrite);
	
	// Make header
	int32_t magic = htonl(INFO_MAGIC_VALUE_2);
	rf.Write(&magic, sizeof(magic));
	Archive archive(rf, IOStream::TimeOutInfinite);

	archive.Write(mAccountID);
	archive.Write(mAccountName);
	archive.Write(mClientStoreMarker);
	archive.Write(mLastObjectIDUsed);
	archive.Write(mBlocksUsed);
	archive.Write(mBlocksInCurrentFiles);
	archive.Write(mBlocksInOldFiles);
	archive.Write(mBlocksInDeletedFiles);
	archive.Write(mBlocksInDirectories);
	archive.Write(mBlocksSoftLimit);
	archive.Write(mBlocksHardLimit);
	archive.Write(mNumCurrentFiles);
	archive.Write(mNumOldFiles);
	archive.Write(mNumDeletedFiles);
	archive.Write(mNumDirectories);

	int64_t numDelObj = mDeletedDirectories.size();
	archive.Write(numDelObj);

	// Write the deleted object list
	if(mDeletedDirectories.size() > 0)
	{
		int64_t objs[NUM_DELETED_DIRS_BLOCK];
		
		int tosave = mDeletedDirectories.size();
		std::vector<int64_t>::iterator i(mDeletedDirectories.begin());
		while(tosave > 0)
		{
			// How many in this one?
			int b = (tosave > NUM_DELETED_DIRS_BLOCK)?NUM_DELETED_DIRS_BLOCK:((int)(tosave));
			
			// Add them
			for(int t = 0; t < b; ++t)
			{
				ASSERT(i != mDeletedDirectories.end());
				objs[t] = box_hton64((*i));
				i++;
			}

			// Write			
			rf.Write(objs, b * sizeof(int64_t));
			
			// Number saved
			tosave -= b;
		}
	}
	
	archive.Write(mAccountEnabled);
	
	mExtraData.Seek(0, IOStream::SeekType_Absolute);
	mExtraData.CopyStreamTo(rf);
	mExtraData.Seek(0, IOStream::SeekType_Absolute);

	// Commit it to disc, converting it to RAID now
	rf.Commit(true);
	
	// Mark is as not modified
	mIsModified = false;
}

int BackupStoreInfo::ReportChangesTo(BackupStoreInfo& rOldInfo)
{
	int numChanges = 0;

	#define COMPARE(attribute) \
	if (rOldInfo.Get ## attribute () != Get ## attribute ()) \
	{ \
		BOX_ERROR(#attribute " changed from " << \
			rOldInfo.Get ## attribute () << " to " << \
			Get ## attribute ()); \
		numChanges++; \
	}

	COMPARE(AccountID);
	COMPARE(AccountName);
	COMPARE(LastObjectIDUsed);
	COMPARE(BlocksUsed);
	COMPARE(BlocksInCurrentFiles);
	COMPARE(BlocksInOldFiles);
	COMPARE(BlocksInDeletedFiles);
	COMPARE(BlocksInDirectories);
	COMPARE(BlocksSoftLimit);
	COMPARE(BlocksHardLimit);
	COMPARE(NumCurrentFiles);
	COMPARE(NumOldFiles);
	COMPARE(NumDeletedFiles);
	COMPARE(NumDirectories);

	#undef COMPARE

	return numChanges;
}

#define APPLY_DELTA(field, delta) \
	if(mReadOnly) \
	{ \
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly) \
	} \
	\
	if((field + delta) < 0) \
	{ \
		THROW_EXCEPTION_MESSAGE(BackupStoreException, \
			StoreInfoBlockDeltaMakesValueNegative, \
			"Failed to reduce " << #field << " from " << \
			field << " by " << delta); \
	} \
	\
	field += delta; \
	mIsModified = true;

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksUsed(int32_t)
//		Purpose: Change number of blocks used, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksUsed(int64_t Delta)
{
	APPLY_DELTA(mBlocksUsed, Delta);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInCurrentFiles(int32_t)
//		Purpose: Change number of blocks in current files, by a delta
//			 amount
//		Created: 2010/08/26
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInCurrentFiles(int64_t Delta)
{
	APPLY_DELTA(mBlocksInCurrentFiles, Delta);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInOldFiles(int32_t)
//		Purpose: Change number of blocks in old files, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInOldFiles(int64_t Delta)
{
	APPLY_DELTA(mBlocksInOldFiles, Delta);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInDeletedFiles(int32_t)
//		Purpose: Change number of blocks in deleted files, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInDeletedFiles(int64_t Delta)
{
	APPLY_DELTA(mBlocksInDeletedFiles, Delta);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInDirectories(int32_t)
//		Purpose: Change number of blocks in directories, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInDirectories(int64_t Delta)
{
	APPLY_DELTA(mBlocksInDirectories, Delta);
}

void BackupStoreInfo::AdjustNumCurrentFiles(int64_t increase)
{
	APPLY_DELTA(mNumCurrentFiles, increase);
}

void BackupStoreInfo::AdjustNumOldFiles(int64_t increase)
{
	APPLY_DELTA(mNumOldFiles, increase);
}

void BackupStoreInfo::AdjustNumDeletedFiles(int64_t increase)
{
	APPLY_DELTA(mNumDeletedFiles, increase);
}

void BackupStoreInfo::AdjustNumDirectories(int64_t increase)
{
	APPLY_DELTA(mNumDirectories, increase);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::CorrectAllUsedValues(int64_t, int64_t, int64_t, int64_t)
//		Purpose: Set all the usage counts to specific values -- use for correcting in housekeeping
//				 if something when wrong during the backup connection, and the store info wasn't
//				 saved back to disc.
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
void BackupStoreInfo::CorrectAllUsedValues(int64_t Used, int64_t InOldFiles, int64_t InDeletedFiles, int64_t InDirectories)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	// Set the values
	mBlocksUsed = Used;
	mBlocksInOldFiles = InOldFiles;
	mBlocksInDeletedFiles = InDeletedFiles;
	mBlocksInDirectories = InDirectories;
	
	mIsModified = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::AddDeletedDirectory(int64_t)
//		Purpose: Add a directory ID to the deleted list
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::AddDeletedDirectory(int64_t DirID)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	mDeletedDirectories.push_back(DirID);
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::RemovedDeletedDirectory(int64_t)
//		Purpose: Remove a directory from the deleted list
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::RemovedDeletedDirectory(int64_t DirID)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	std::vector<int64_t>::iterator i(std::find(mDeletedDirectories.begin(), mDeletedDirectories.end(), DirID));
	if(i == mDeletedDirectories.end())
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoDirNotInList)
	}
	mDeletedDirectories.erase(i);
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeLimits(int64_t, int64_t)
//		Purpose: Change the soft and hard limits
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeLimits(int64_t BlockSoftLimit, int64_t BlockHardLimit)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}

	mBlocksSoftLimit = BlockSoftLimit;
	mBlocksHardLimit = BlockHardLimit;
	
	mIsModified = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::AllocateObjectID()
//		Purpose: Allocate an ID for a new object in the store.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
int64_t BackupStoreInfo::AllocateObjectID()
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	if(mLastObjectIDUsed < 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotInitialised)
	}
	
	// Return the next object ID
	return ++mLastObjectIDUsed;
	
	mIsModified = true;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::SetClientStoreMarker(int64_t)
//		Purpose: Sets the client store marker
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
void BackupStoreInfo::SetClientStoreMarker(int64_t ClientStoreMarker)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	mClientStoreMarker = ClientStoreMarker;
	
	mIsModified = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::SetAccountName(const std::string&)
//		Purpose: Sets the account name
//		Created: 2008/08/22
//
// --------------------------------------------------------------------------
void BackupStoreInfo::SetAccountName(const std::string& rName)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	mAccountName = rName;
	
	mIsModified = true;
}

