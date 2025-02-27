#include <Windows.h>
#include <tchar.h>

#include <algorithm>

#include "gui/application.h"
#include "../utils/applicationinfo.h"
#include "../utils/logger.h"

namespace
{
int argCount = 0;
LPWSTR *argList = nullptr;

int
WSMessageBox(HWND hOwner, LPCTSTR szTitle, UINT nStyle, LPCTSTR szFormat, ...)
{
    va_list arg_list;
    va_start(arg_list, szFormat);

    TCHAR Buf[1024];
    _vsntprintf(Buf, 1023, szFormat, arg_list);
    Buf[1023] = _T('\0');

    va_end(arg_list);

    int nResult = ::MessageBox(hOwner, Buf, szTitle, nStyle);

    return nResult;
}

// This function cannot handle negative coordinates passed with the -center option; -dir option seems to break sometimes too
// For now (we can make this better later) we assume 2 additional args will be passed with -center and 1 for -dir
bool CheckCommandLineArgument(LPCWSTR argumentToCheck, int *valueIndex = nullptr,
                              int *valueCount = nullptr)
{
    if (!argumentToCheck)
        return false;

    if (!argList)
        argList = CommandLineToArgvW(GetCommandLine(), &argCount);

    bool result = false;
    for (int i = 0; i < argCount; ++i) {
        if (wcscmp(argList[i], argumentToCheck))
            continue;
        result = true;
        if (!valueCount)
            continue;
        *valueCount = 0;
        if (valueIndex)
            *valueIndex = -1;
        for (int j = i + 1; j < argCount; ++j) {
            if (wcsncmp(argList[j], L"-", 1)) {
                if (valueIndex && *valueIndex < 0)
                    *valueIndex = j;
                ++*valueCount;
            }
	        else
	        {
    		    break;
	        }
        }
    }
    return result;
}

bool GetCommandLineArgumentIndex(LPCWSTR argumentToCheck, int *valueIndex)
{
	if (!argumentToCheck)
		return false;

	if (!argList)
		argList = CommandLineToArgvW(GetCommandLine(), &argCount);

	bool result = false;
	for (int i = 0; i < argCount; ++i) {
		if (wcscmp(argList[i], argumentToCheck))
			continue;
		result = true;

		*valueIndex = i + 1;
	}
	return result;
}

}  // namespace


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    if (CheckCommandLineArgument(L"-help"))
    {
        WSMessageBox(NULL, _T("Windscribe Install Options"), MB_OK | MB_ICONINFORMATION,
            _T("The Windscribe installer accepts the following optional commmand-line parameters:\n\n"
               "-help\n"
                "Show this information.\n\n"
                "-silent\n"
                "Instructs the installer to hide its user interface.\n\n"
                "-dir \"x:\\dirname\"\n"
                "Overrides the default installation directory."));
        return 0;
    }
	
    // Useful for debugging
	// AllocConsole();
	// FILE *stream;
	// freopen_s(&stream, "CONOUT$", "w+t", stdout);

    const bool isUpdateMode =
        CheckCommandLineArgument(L"-update") || CheckCommandLineArgument(L"-q");
    int expectedArgumentCount = isUpdateMode ? 2 : 1;

    int window_center_x = -1, window_center_y = -1;
    int center_coord_index = 0;
    if (GetCommandLineArgumentIndex(L"-center", &center_coord_index)) {
            window_center_x = _wtoi(argList[center_coord_index]);
            window_center_y = _wtoi(argList[center_coord_index+1]);
        expectedArgumentCount += 3;
    }

    bool isSilent = false;
    std::wstring installPath;

    if (!isUpdateMode)
    {
       isSilent = CheckCommandLineArgument(L"-silent");
       if (isSilent) {
           expectedArgumentCount += 1;
       }

       int install_path_index = 0, install_path_count;
       if (GetCommandLineArgumentIndex(L"-dir", &install_path_index))
       {
           if (install_path_index > 0)
           {
               installPath = argList[install_path_index];
               std::replace(installPath.begin(), installPath.end(), L'/', L'\\');
               expectedArgumentCount += 2;
           }
       }
    }

    if (argCount != expectedArgumentCount)
    {
        WSMessageBox(NULL, _T("Windscribe Install Error"), MB_OK | MB_ICONERROR,
            _T("Incorrect number of arguments passed to installer.\n\nUse the -help argument to see available arguments and their format."));
        return 0;
    }

    Log::instance().init(true);
    Log::instance().out(L"Installing Windscribe version %ls", ApplicationInfo::instance().getVersion().c_str());
    Log::instance().out(L"Command-line args: %ls", ::GetCommandLine());

    Application app(hInstance, nCmdShow, isUpdateMode, isSilent, installPath);
    if (app.init(window_center_x, window_center_y))
        return app.exec();
    return -1;
}
