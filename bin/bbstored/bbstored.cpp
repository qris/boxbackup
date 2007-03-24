// --------------------------------------------------------------------------
//
// File
//		Name:    bbstored.cpp
//		Purpose: main file for backup store daemon
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BackupStoreDaemon.h"
#include "MainHelper.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

int main(int argc, const char *argv[])
{
	MAINHELPER_START

	Logging::SetProgramName("Box Backup (bbstored)");
	Logging::ToConsole(true);
	Logging::ToSyslog (true);

	BackupStoreDaemon daemon;
	return daemon.Main(BOX_FILE_BBSTORED_DEFAULT_CONFIG, argc, argv);
	
	MAINHELPER_END
}

