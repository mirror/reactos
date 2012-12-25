/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>
#include <debug.h>

PCSTR OptionsMenuList[] =
{
	"Safe Mode",
	"Safe Mode with Networking",
	"Safe Mode with Command Prompt",

	NULL,

	"Enable Boot Logging",
	"Enable VGA Mode",
	"Last Known Good Configuration",
	"Directory Services Restore Mode",
	"Debugging Mode",
	"FreeLdr debugging",

	NULL,

#ifdef HAS_OPTION_MENU_CUSTOM_BOOT
	"Custom Boot",
#endif
#ifdef HAS_OPTION_MENU_REBOOT
	"Reboot",
#endif
};

PCSTR FrldrDbgMsg = "Enable FreeLdr debug channels\n"
                    "Acceptable syntax: [level1]#channel1[,[level2]#channel2]\n"
                    "level can be one of: trace,warn,fixme,err\n"
                    "  if the level is ommited all levels\n"
                    "  are enabled for the specified channel\n"
                    "# can be either + or -\n"
                    "channel can be one of the following:\n"
                    "  all,warning,memory,filesystem,inifile,ui,disk,cache,registry,\n"
                    "  reactos,linux,hwdetect,windows,peloader,scsiport,heap\n"
                    "Examples:\n"
                    "  trace+windows,trace+reactos\n"
                    "  +hwdetect,err-disk\n"
                    "  +peloader\n"
                    "NOTE: all letters must be lowercase, no spaces allowed.";

enum OptionMenuItems
{
	SAFE_MODE = 0,
	SAFE_MODE_WITH_NETWORKING = 1,
	SAFE_MODE_WITH_COMMAND_PROMPT = 2,

	SEPARATOR1 = 3,

	ENABLE_BOOT_LOGGING = 4,
	ENABLE_VGA_MODE = 5,
	LAST_KNOWN_GOOD_CONFIGURATION = 6,
	DIRECTORY_SERVICES_RESTORE_MODE = 7,
	DEBUGGING_MODE = 8,
	FREELDR_DEBUGGING = 9,

	SEPARATOR2 = 10,

#ifdef HAS_OPTION_MENU_CUSTOM_BOOT
	CUSTOM_BOOT = 11,
#endif
#ifdef HAS_OPTION_MENU_REBOOT
	REBOOT = 12,
#endif
};

ULONG OptionsMenuItemCount = sizeof(OptionsMenuList) / sizeof(OptionsMenuList[0]);

BOOLEAN SafeMode = FALSE;
BOOLEAN SafeModeWithNetworking = FALSE;
BOOLEAN SafeModeWithCommandPrompt = FALSE;
BOOLEAN BootLogging = FALSE;
BOOLEAN VgaMode = FALSE;
BOOLEAN LastKnownGoodConfiguration = FALSE;
BOOLEAN DirectoryServicesRepairMode = FALSE;
BOOLEAN DebuggingMode = FALSE;

VOID DoOptionsMenu(VOID)
{
	ULONG SelectedMenuItem;
	CHAR  DebugChannelString[100];

	if (!UiDisplayMenu("Select an option:", "",
	                   OptionsMenuList,
	                   OptionsMenuItemCount,
	                   0, -1,
	                   &SelectedMenuItem,
	                   TRUE,
	                   NULL))
	{
		// The user pressed ESC
		return;
	}

	// Clear the backdrop
	UiDrawBackdrop();

	switch (SelectedMenuItem)
	{
	case SAFE_MODE:
		SafeMode = TRUE;
		BootLogging = TRUE;
		break;
	case SAFE_MODE_WITH_NETWORKING:
		SafeModeWithNetworking = TRUE;
		BootLogging = TRUE;
		break;
	case SAFE_MODE_WITH_COMMAND_PROMPT:
		SafeModeWithCommandPrompt = TRUE;
		BootLogging = TRUE;
		break;
	//case SEPARATOR1:
	//	break;
	case ENABLE_BOOT_LOGGING:
		BootLogging = TRUE;
		break;
	case ENABLE_VGA_MODE:
		VgaMode = TRUE;
		break;
	case LAST_KNOWN_GOOD_CONFIGURATION:
		LastKnownGoodConfiguration = TRUE;
		break;
	case DIRECTORY_SERVICES_RESTORE_MODE:
		DirectoryServicesRepairMode = TRUE;
		break;
	case DEBUGGING_MODE:
		DebuggingMode = TRUE;
		break;
	case FREELDR_DEBUGGING:
		DebugChannelString[0]=0;
		if (UiEditBox(FrldrDbgMsg, DebugChannelString, 100))
			DbgParseDebugChannels(DebugChannelString);
		break;
	//case SEPARATOR2:
	//	break;
#ifdef HAS_OPTION_MENU_CUSTOM_BOOT
	case CUSTOM_BOOT:
		OptionMenuCustomBoot();
		break;
#endif
#ifdef HAS_OPTION_MENU_REBOOT
	case REBOOT:
		OptionMenuReboot();
		break;
#endif
	}
}

VOID AppendBootTimeOptions(PCHAR BootOptions)
{
	if (SafeMode)
		strcat(BootOptions, " /SAFEBOOT:MINIMAL /SOS"); //FIXME: NOGUIBOOT should also be specified

	if (SafeModeWithNetworking)
		strcat(BootOptions, " /SAFEBOOT:NETWORK /SOS"); //FIXME: NOGUIBOOT should also be specified

	if (SafeModeWithCommandPrompt)
		strcat(BootOptions, " /SAFEBOOT:MINIMAL(ALTERNATESHELL) /SOS"); //FIXME: NOGUIBOOT should also be specified

	if (BootLogging)
		strcat(BootOptions, " /BOOTLOG");

	if (VgaMode)
		strcat(BootOptions, " /BASEVIDEO");

	if (LastKnownGoodConfiguration)
		DbgPrint("Last known good configuration is not supported yet!\n");

	if (DirectoryServicesRepairMode)
		strcat(BootOptions, " /SAFEBOOT:DSREPAIR /SOS");

	if (DebuggingMode)
		strcat(BootOptions, " /DEBUG");
}
