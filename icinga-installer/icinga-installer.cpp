// SPDX-FileCopyrightText: 2012 Icinga GmbH <https://icinga.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>
#include <vector>
#include <fstream>
#include <direct.h>
#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sys/types.h>
#include <sys/stat.h>

static std::string GetIcingaInstallPath(void)
{
	char szFileName[MAX_PATH];
	if (!GetModuleFileName(nullptr, szFileName, sizeof(szFileName)))
		return "";

	if (!PathRemoveFileSpec(szFileName))
		return "";

	if (!PathRemoveFileSpec(szFileName))
		return "";

	return szFileName;
}


static bool ExecuteCommand(const std::string& app, const std::string& arguments)
{
	SHELLEXECUTEINFO sei = {};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpFile = app.c_str();
	sei.lpParameters = arguments.c_str();
	sei.nShow = SW_HIDE;
	if (!ShellExecuteEx(&sei))
		return false;

	if (!sei.hProcess)
		return false;

	WaitForSingleObject(sei.hProcess, INFINITE);

	DWORD exitCode;
	BOOL res = GetExitCodeProcess(sei.hProcess, &exitCode);
	CloseHandle(sei.hProcess);

	if (!res)
		return false;

	return exitCode == 0;
}

static bool ExecuteIcingaCommand(const std::string& arguments)
{
	return ExecuteCommand(GetIcingaInstallPath() + "\\sbin\\icinga2.exe", arguments);
}

static std::string DirName(const std::string& path)
{
	char *spath = strdup(path.c_str());

	if (!PathRemoveFileSpec(spath)) {
		free(spath);
		throw std::runtime_error("PathRemoveFileSpec failed");
	}

	std::string result = spath;

	free(spath);

	return result;
}

static bool PathExists(const std::string& path)
{
	struct _stat statbuf;
	return (_stat(path.c_str(), &statbuf) >= 0);
}

static std::string GetIcingaDataPath(void)
{
	char path[MAX_PATH];
	if (!SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, path)))
		throw std::runtime_error("SHGetFolderPath failed");
	return std::string(path) + "\\icinga2";
}

/* Settings passed as MSI properties are persisted per instance so that they are
 * still available during upgrades and uninstalls where MSI properties are not passed.
 */
static std::string SettingsKeyPath(const std::string& instanceId)
{
	return "SOFTWARE\\Icinga GmbH\\Icinga 2\\Instances\\" + instanceId;
}

/* Returns the numeric part of a WiX instance id ("Instance2" -> "2"). The default
 * instance has no suffix.
 */
static std::string InstanceSuffix(const std::string& instanceId)
{
	if (instanceId.empty() || instanceId == "Default")
		return "";

	std::string suffix = instanceId;
	if (suffix.size() > 8 && _strnicmp(suffix.c_str(), "Instance", 8) == 0)
		suffix = suffix.substr(8);

	return suffix;
}

static std::string ReadPersistedString(const std::string& instanceId, const char *name)
{
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SettingsKeyPath(instanceId).c_str(), 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
		return "";

	std::string result;
	BYTE pvData[1024];
	DWORD cbData = sizeof(pvData) - 1;
	DWORD lType;
	if (RegQueryValueEx(hKey, name, nullptr, &lType, pvData, &cbData) == ERROR_SUCCESS && lType == REG_SZ) {
		pvData[cbData] = '\0';
		result = (char *)pvData;
	}

	RegCloseKey(hKey);

	return result;
}

static void PersistString(const std::string& instanceId, const char *name, const std::string& value)
{
	std::string keyPath = SettingsKeyPath(instanceId);

	HKEY hKey;
	if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0,
		KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
		throw std::runtime_error("failed to create registry key " + keyPath);

	LONG res;
	if (value.empty())
		res = RegDeleteValue(hKey, name);
	else
		res = RegSetValueEx(hKey, name, 0, REG_SZ,
			(const BYTE *)value.c_str(), (DWORD)value.size() + 1);

	RegCloseKey(hKey);

	if (res != ERROR_SUCCESS && !(value.empty() && res == ERROR_FILE_NOT_FOUND))
		throw std::runtime_error(std::string("failed to persist registry value ") + name);
}

/* Strips the whitespace appended in the WiX ExeCommand to protect trailing
 * backslashes, as well as stray quotes and trailing backslashes themselves.
 */
static std::string TrimField(std::string value)
{
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '"' || value.back() == '\\'))
		value.pop_back();

	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '"'))
		value.erase(0, 1);

	return value;
}

static std::string WideToNarrow(const wchar_t *str)
{
	int len = WideCharToMultiByte(CP_ACP, 0, str, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return "";

	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_ACP, 0, str, -1, &result[0], len, nullptr, nullptr);
	return result;
}

static void MkDir(const std::string& path)
{
	if (mkdir(path.c_str()) < 0 && errno != EEXIST)
		throw std::runtime_error("mkdir failed");
}

static void MkDirP(const std::string& path)
{
	size_t pos = 0;

	while (pos != std::string::npos) {
		pos = path.find_first_of("/\\", pos + 1);

		std::string spath = path.substr(0, pos + 1);
		struct _stat statbuf;
		if (_stat(spath.c_str(), &statbuf) < 0 && errno == ENOENT)
			MkDir(path.substr(0, pos));
	}
}

static std::string GetNSISInstallPath(void)
{
	HKEY hKey;
	//TODO: Change hardcoded key
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Icinga Development Team\\ICINGA2", 0,
		KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
		BYTE pvData[MAX_PATH];
		DWORD cbData = sizeof(pvData) - 1;
		DWORD lType;
		if (RegQueryValueEx(hKey, nullptr, nullptr, &lType, pvData, &cbData) == ERROR_SUCCESS && lType == REG_SZ) {
			pvData[cbData] = '\0';

			return (char *)pvData;
		}

		RegCloseKey(hKey);
	}

	return "";
}

static bool CopyDirectory(const std::string& source, const std::string& destination)
{
	// SHFileOperation requires file names to be terminated with two \0s
	std::string tmpSource = source + std::string(1, '\0');
	std::string tmpDestination = destination + std::string(1, '\0');

	SHFILEOPSTRUCT fop;
	fop.wFunc = FO_COPY;
	fop.pFrom = tmpSource.c_str();
	fop.pTo = tmpDestination.c_str();
	fop.fFlags = FOF_NO_UI;

	return (SHFileOperation(&fop) == 0);
}

static bool DeleteDirectory(const std::string& dir)
{
	// SHFileOperation requires file names to be terminated with two \0s
	std::string tmpDir = dir + std::string(1, '\0');

	SHFILEOPSTRUCT fop;
	fop.wFunc = FO_DELETE;
	fop.pFrom = tmpDir.c_str();
	fop.fFlags = FOF_NO_UI;

	return (SHFileOperation(&fop) == 0);
}

static int UpgradeNSIS(void)
{
	std::string installPath = GetNSISInstallPath();

	if (installPath.empty())
		return 0;

	std::string uninstallerPath = installPath + "\\uninstall.exe";

	if (!PathExists(uninstallerPath))
		return 0;

	std::string dataPath = GetIcingaDataPath();

	if (dataPath.empty())
		return 1;

	bool moveUserData = !PathExists(dataPath);

	/* perform open heart surgery on the user's data dirs - yay */
	if (moveUserData) {
		MkDir(dataPath.c_str());

		std::string oldNameEtc = installPath + "\\etc";
		std::string newNameEtc = dataPath + "\\etc";
		if (!CopyDirectory(oldNameEtc, newNameEtc))
			return 1;

		std::string oldNameVar = installPath + "\\var";
		std::string newNameVar = dataPath + "\\var";
		if (!CopyDirectory(oldNameVar, newNameVar))
			return 1;
	}

	ExecuteCommand(uninstallerPath, "/S _?=" + installPath);

	_unlink(uninstallerPath.c_str());

	if (moveUserData) {
		std::string oldNameEtc = installPath + "\\etc";
		if (!DeleteDirectory(oldNameEtc))
			return 1;

		std::string oldNameVar = installPath + "\\var";
		if (!DeleteDirectory(oldNameVar))
			return 1;

		_rmdir(installPath.c_str());
	}

	return 0;
}

static int InstallIcinga(std::string dataDir, std::string serviceName, std::string instanceId)
{
	std::string installDir = GetIcingaInstallPath();
	std::string skelDir = installDir + "\\share\\skel";

	if (instanceId.empty())
		instanceId = "Default";

	/* Resolution order: MSI property -> value persisted by a previous install -> per-instance default. */
	if (dataDir.empty())
		dataDir = ReadPersistedString(instanceId, "DataDir");
	if (serviceName.empty())
		serviceName = ReadPersistedString(instanceId, "ServiceName");

	std::string defaultDataDir = GetIcingaDataPath();
	std::string suffix = InstanceSuffix(instanceId);
	std::string instanceDataDir = suffix.empty() ? defaultDataDir : defaultDataDir + "-" + suffix;
	std::string instanceServiceName = suffix.empty() ? "icinga2" : "icinga2-" + suffix;

	if (dataDir.empty())
		dataDir = instanceDataDir;
	if (serviceName.empty())
		serviceName = instanceServiceName;

	if (!PathExists(dataDir)) {
		std::string sourceDir = skelDir + std::string(1, '\0');
		std::string destinationDir = dataDir + std::string(1, '\0');

		SHFILEOPSTRUCT fop;
		fop.wFunc = FO_COPY;
		fop.pFrom = sourceDir.c_str();
		fop.pTo = destinationDir.c_str();
		fop.fFlags = FOF_NO_UI | FOF_NOCOPYSECURITYATTRIBS;

		if (SHFileOperation(&fop) != 0)
			return 1;

		MkDirP(dataDir + "/etc/icinga2/pki");
		MkDirP(dataDir + "/var/cache/icinga2");
		MkDirP(dataDir + "/var/lib/icinga2/certs");
		MkDirP(dataDir + "/var/lib/icinga2/certificate-requests");
		MkDirP(dataDir + "/var/lib/icinga2/agent/inventory");
		MkDirP(dataDir + "/var/lib/icinga2/api/config");
		MkDirP(dataDir + "/var/lib/icinga2/api/log");
		MkDirP(dataDir + "/var/lib/icinga2/api/zones");
		MkDirP(dataDir + "/var/log/icinga2/compat/archive");
		MkDirP(dataDir + "/var/log/icinga2/crash");
		MkDirP(dataDir + "/var/run/icinga2/cmd");
		MkDirP(dataDir + "/var/spool/icinga2/perfdata");
		MkDirP(dataDir + "/var/spool/icinga2/tmp");
	}

	// Upgrade from versions older than 2.13 by making the windowseventlog feature available,
	// enable it by default and disable the old mainlog feature.
	if (!PathExists(dataDir + "/etc/icinga2/features-available/windowseventlog.conf")) {
		// Disable the old mainlog feature as it is replaced by windowseventlog by default.
		std::string mainlogEnabledFile = dataDir + "/etc/icinga2/features-enabled/mainlog.conf";
		if (PathExists(mainlogEnabledFile)) {
			if (DeleteFileA(mainlogEnabledFile.c_str()) == 0) {
				throw std::runtime_error("deleting '" + mainlogEnabledFile + "' failed");
			}
		}

		// Install the new windowseventlog feature. As features-available/windowseventlog.conf is used as a marker file,
		// copy it as the last step, so that this is run again should the upgrade be interrupted.
		for (const std::string& d : {"features-enabled", "features-available"}) {
			std::string sourceFile = skelDir + "/etc/icinga2/" + d + "/windowseventlog.conf";
			std::string destinationFile = dataDir + "/etc/icinga2/" + d + "/windowseventlog.conf";

			if (CopyFileA(sourceFile.c_str(), destinationFile.c_str(), false) == 0) {
				throw std::runtime_error("copying '" + sourceFile + "' to '" + destinationFile + "' failed");
			}
		}
	}

	// TODO: In Icinga 2.14, rename features-available/mainlog.conf to mainlog.conf.deprecated
	//       so that it's no longer listed as an available feature.

	if (!ExecuteCommand("icacls", "\"" + dataDir + "\" /grant *S-1-5-20:(oi)(ci)m")){
		throw std::runtime_error("failed to set ACLs for " + dataDir);
	}
	if (!ExecuteCommand("icacls", "\"" + dataDir + "\\etc\" /inheritance:r /grant:r *S-1-5-20:(oi)(ci)m *S-1-5-32-544:(oi)(ci)f")) {
		throw std::runtime_error("failed to set ACLs for " + dataDir + "\\etc");
	}
	if (!ExecuteCommand("icacls", "\"" + dataDir + "\\var\" /inheritance:r /grant:r *S-1-5-20:(oi)(ci)m *S-1-5-32-544:(oi)(ci)f")) {
		throw std::runtime_error("failed to set ACLs for " + dataDir + "\\var");
	}

	/* Make icinga2.exe use the custom paths and persist them into the service's
	 * environment (the child process inherits our environment). Secondary instances
	 * always need ICINGA2_INSTALL_PATH as the runtime MSI product lookup only
	 * matches the default instance's product name.
	 */
	if (dataDir != defaultDataDir)
		SetEnvironmentVariable("ICINGA2_DATA_PATH", dataDir.c_str());

	if (instanceId != "Default")
		SetEnvironmentVariable("ICINGA2_INSTALL_PATH", installDir.c_str());

	std::string scmArgs = "--scm-install";
	if (serviceName != "icinga2")
		scmArgs += " --scm-name " + serviceName;
	scmArgs += " daemon";

	ExecuteIcingaCommand(scmArgs);

	/* Persist explicitly overridden settings for upgrades and uninstalls; remove
	 * them when back at the per-instance defaults, which are derived again there.
	 */
	PersistString(instanceId, "DataDir", dataDir != instanceDataDir ? dataDir : "");
	PersistString(instanceId, "ServiceName", serviceName != instanceServiceName ? serviceName : "");

	return 0;
}

static int UninstallIcinga(std::string instanceId)
{
	if (instanceId.empty())
		instanceId = "Default";

	std::string dataDir = ReadPersistedString(instanceId, "DataDir");
	std::string serviceName = ReadPersistedString(instanceId, "ServiceName");

	std::string defaultDataDir = GetIcingaDataPath();
	std::string suffix = InstanceSuffix(instanceId);

	if (dataDir.empty() && !suffix.empty())
		dataDir = defaultDataDir + "-" + suffix;
	if (serviceName.empty() && !suffix.empty())
		serviceName = "icinga2-" + suffix;

	if (!dataDir.empty() && dataDir != defaultDataDir)
		SetEnvironmentVariable("ICINGA2_DATA_PATH", dataDir.c_str());

	std::string scmArgs = "--scm-uninstall";
	if (!serviceName.empty() && serviceName != "icinga2")
		scmArgs += " --scm-name " + serviceName;

	ExecuteIcingaCommand(scmArgs);

	return 0;
}

/**
* Entry point for the installer application.
*/
int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	//AllocConsole();
	int rc;

	int argc = 0;
	LPWSTR *argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

	std::vector<std::string> args;
	if (argvW) {
		for (int i = 1; i < argc; i++)
			args.push_back(WideToNarrow(argvW[i]));
		LocalFree(argvW);
	}

	if (!args.empty() && args[0] == "install") {
		std::string dataDir = args.size() > 1 ? TrimField(args[1]) : "";
		std::string serviceName = args.size() > 2 ? TrimField(args[2]) : "";
		std::string instanceId = args.size() > 3 ? TrimField(args[3]) : "";
		rc = InstallIcinga(dataDir, serviceName, instanceId);
	} else if (!args.empty() && args[0] == "uninstall") {
		std::string instanceId = args.size() > 1 ? TrimField(args[1]) : "";
		rc = UninstallIcinga(instanceId);
	} else if (!args.empty() && args[0] == "upgrade-nsis") {
		rc = UpgradeNSIS();
	} else {
		MessageBox(nullptr, "This application should only be run by the MSI installer package.", "Icinga 2 Installer", MB_ICONWARNING);
		rc = 1;
	}

	//::Sleep(3000s);

	return rc;
}
