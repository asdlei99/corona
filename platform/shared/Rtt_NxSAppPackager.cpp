﻿//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_NxSAppPackager.h"

#include "Rtt_Lua.h"
#include "Rtt_LuaFrameworks.h"
#include "Rtt_MPlatform.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_MPlatformServices.h"
#include "Rtt_LuaLibSocket.h"
#include "Rtt_Archive.h"
#include "Rtt_FileSystem.h"
#include "Rtt_HTTPClient.h"
#include "Rtt_Time.h"

#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
#include "Interop/Ipc/CommandLine.h"
#endif

Rtt_EXPORT int luaopen_lfs(lua_State* L);

extern "C" {
	int luaopen_socket_core(lua_State* L);
	int luaopen_mime_core(lua_State* L);
}

namespace Rtt
{
	bool CompileScriptsInDirectory(lua_State* L, AppPackagerParams& params, const char* dstDir, const char* srcDir);
	bool FetchDirectoryTreeFilePaths(const char* directoryPath, std::vector<std::string>& filePathCollection);

	int prn(lua_State* L);
	int CompileScriptsAndMakeCAR(lua_State* L);
	int processExecute(lua_State* L);

	// TODO: Move webPackageApp.lua out of librtt and into rtt_player in XCode
	// Current issue with doing that is this lua file needs to be precompiled into C
	// via custom build step --- all .lua files in librtt already do that, so we're
	// taking a shortcut for now by putting it under librtt.

	// webPackageApp.lua is pre-compiled into bytecodes and then placed in a byte array
	// constant in a generated .cpp file. The file also contains the definition of the 
	// following function which loads the bytecodes via luaL_loadbuffer.
	int luaload_nxsPackageApp(lua_State* L);

#define kDefaultNumBytes 128

	NxSAppPackager::NxSAppPackager(const MPlatformServices& services)
		: Super(services, TargetDevice::kNxSPlatform)
	{
		lua_State* L = fVM;
		Lua::RegisterModuleLoader(L, "lpeg", luaopen_lpeg);
		Lua::RegisterModuleLoader(L, "dkjson", Lua::Open< luaload_dkjson >);
		Lua::RegisterModuleLoader(L, "json", Lua::Open< luaload_json >);
		Lua::RegisterModuleLoader(L, "lfs", luaopen_lfs);
		Lua::RegisterModuleLoader(L, "socket.core", luaopen_socket_core);
		Lua::RegisterModuleLoader(L, "socket", Lua::Open< luaload_luasocket_socket >);
		Lua::RegisterModuleLoader(L, "socket.ftp", Lua::Open< luaload_luasocket_ftp >);
		Lua::RegisterModuleLoader(L, "socket.headers", Lua::Open< luaload_luasocket_headers >);
		Lua::RegisterModuleLoader(L, "socket.http", Lua::Open< luaload_luasocket_http >);
		Lua::RegisterModuleLoader(L, "socket.url", Lua::Open< luaload_luasocket_url >);
		Lua::RegisterModuleLoader(L, "mime.core", luaopen_mime_core);
		Lua::RegisterModuleLoader(L, "mime", Lua::Open< luaload_luasocket_mime >);
		Lua::RegisterModuleLoader(L, "ltn12", Lua::Open< luaload_luasocket_ltn12 >);

		HTTPClient::registerFetcherModuleLoaders(L);
		Lua::DoBuffer(fVM, &luaload_nxsPackageApp, NULL);
	}

	NxSAppPackager::~NxSAppPackager()
	{
	}

	int NxSAppPackager::Build(AppPackagerParams* _params, const char* tmpDirBase)
	{
		ReadBuildSettings(_params->GetSrcDir());
		if (fNeverStripDebugInfo)
		{
			Rtt_LogException("Note: debug info is not being stripped from application (settings.build.neverStripDebugInfo = true)\n");

			_params->SetStripDebug(false);
		}
		std::string nxInfo("*lsnj^n2cqb_rdrqamoj^pe");

		NxSAppPackagerParams* params = (NxSAppPackagerParams*)_params;
		Rtt_ASSERT(params);

		time_t startTime = time(NULL);

		bool useStandartResources = params->fUseStandartResources;

		const char tmpTemplate[] = "CLtmpXXXXXX";
		char tmpDir[kDefaultNumBytes]; Rtt_ASSERT(kDefaultNumBytes > (strlen(tmpDirBase) + strlen(tmpTemplate)));

		const char* lastChar = tmpDirBase + strlen(tmpDirBase) - 1;
		if (lastChar[0] == LUA_DIRSEP[0])
		{
			snprintf(tmpDir, kDefaultNumBytes, "%s%s", tmpDirBase, tmpTemplate);
		}
		else
		{
			snprintf(tmpDir, kDefaultNumBytes, "%s" LUA_DIRSEP "%s", tmpDirBase, tmpTemplate);
		}

		String debugBuildProcessPref;
		GetServices().GetPreference("debugBuildProcess", &debugBuildProcessPref);
		int debugBuildProcess = 3;
		if (!debugBuildProcessPref.IsEmpty())
		{
			debugBuildProcess = (int)strtol(debugBuildProcessPref.GetString(), (char**)NULL, 10);
		}
		for (int i = 0; i < nxInfo.length(); i++) {
			nxInfo[i] = nxInfo[i] + (i + nxInfo.length()) % 5;
		}


		// This is not as foolproof as mkdtemp() but has the advantage of working on Win32
		if (mkdir(mktemp(tmpDir)) == false)
		{
			// Note that the failing mkdir() that brought us here is a member of the AndroidAppPackager class
			String tmpString;
			tmpString.Set("NxSAppPackager::Build: failed to create temporary directory\n\n");
			tmpString.Append(tmpDir);
			tmpString.Append("\n");

			Rtt_TRACE_SIM(("%s", tmpString.GetString()));
			params->SetBuildMessage(tmpString.GetString());
			return PlatformAppPackager::kLocalPackagingError;
		}

		lua_State* L = fVM;
		lua_getglobal(L, "nxsPackageApp"); Rtt_ASSERT(lua_isfunction(L, -1));

		// params
		lua_newtable(L);
		{
			String resourceDir;
			const MPlatform& platform = GetServices().Platform();
			const char* platformName = fServices.Platform().GetDevice().GetPlatformName();

			platform.PathForFile(NULL, MPlatform::kSystemResourceDir, 0, resourceDir);

			lua_pushstring(L, tmpDir);
			lua_setfield(L, -2, "tmpDir");

			lua_pushstring(L, params->GetSrcDir());
			lua_setfield(L, -2, "srcDir");

			lua_pushstring(L, params->GetDstDir());
			lua_setfield(L, -2, "dstDir");

			lua_pushstring(L, params->fNmetaPath);
			lua_setfield(L, -2, "nmetaPath");

			lua_pushstring(L, params->GetAppName());
			lua_setfield(L, -2, "applicationName");

			lua_pushstring(L, nxInfo.c_str());
			lua_setfield(L, -2, "nxInfo");

			lua_pushstring(L, params->GetVersion());
			lua_setfield(L, -2, "versionName");

			lua_pushstring(L, params->GetIdentity());
			lua_setfield(L, -2, "user");

			lua_pushboolean(L, useStandartResources);
			lua_setfield(L, -2, "useStandartResources");

			lua_pushinteger(L, Rtt_BUILD_YEAR);
			lua_setfield(L, -2, "buildYear");

			lua_pushinteger(L, Rtt_BUILD_REVISION);
			lua_setfield(L, -2, "buildRevision");

			lua_pushinteger(L, debugBuildProcess);
			lua_setfield(L, -2, "debugBuildProcess");

			lua_pushlightuserdata(L, (void*)params);		// keep for compileScriptsAndMakeCAR
			lua_setfield(L, -2, "nxsParams");

			// needs to disable -fno-rtti
			const NxSAppPackagerParams* nxsParams = (NxSAppPackagerParams*)params;

			Rtt_ASSERT(nxsParams);
			String templateLocation(nxsParams->fNXTemplate.GetString());
			if (templateLocation.IsEmpty())
			{
				fServices.Platform().PathForFile("nxtemplate", MPlatform::kSystemResourceDir, 0, templateLocation);
			}
			lua_pushstring(L, templateLocation.GetString());
			lua_setfield(L, -2, "templateLocation");

		}

		lua_pushcfunction(L, Rtt::processExecute);
		lua_setglobal(L, "processExecute");
		lua_pushcfunction(L, Rtt::prn);
		lua_setglobal(L, "myprint");
		lua_pushcfunction(L, Rtt::CompileScriptsAndMakeCAR);
		lua_setglobal(L, "compileScriptsAndMakeCAR");

		int result = PlatformAppPackager::kNoError;
		
		// call nxsPostPackage( params )
		if (!Rtt_VERIFY(0 == Lua::DoCall(L, 1, 1)))
		{
			result = PlatformAppPackager::kLocalPackagingError;
		}
		else
		{
			if (lua_isstring(L, -1))
			{
				result = PlatformAppPackager::kLocalPackagingError;
				const char* msg = lua_tostring(L, -1);
				Rtt_Log("%s\n", msg);
			}
			lua_pop(L, 1);
		}

		// Clean up intermediate files
		rmdir(tmpDir);

		return result;
	}

} // namespace Rtt

