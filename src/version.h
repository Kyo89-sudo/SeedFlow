// Shared by C++, the resource compiler and packaging checks.
// From 0.8.6 the version is x.y.z (FileVersion x.y.z.0); fixes are x.y.z.N and then
// SF_VERSION_STRING becomes "x.y.z.N". The old "rN" revision label is no longer used.
#pragma once
#define SF_VERSION_MAJOR 0
#define SF_VERSION_MINOR 8
#define SF_VERSION_PATCH 7
#define SF_VERSION_BUILD 2       // maintenance build: 0.8.7.N (also the 4th digit of the peer ID)
#define SF_VERSION_STRING "0.8.7.2"
#define SF_VERSION_WSTRING L"0.8.7.2"
#define SF_FILE_VERSION 0,8,7,2
#define SF_FILE_VERSION_STRING "0.8.7.2"
#define SF_USER_AGENT "SeedFlow/" SF_VERSION_STRING
#define SF_USER_AGENT_W L"SeedFlow/" SF_VERSION_WSTRING
#define SF_CLIENT_NAME "SeedFlow " SF_VERSION_STRING
#define SF_DISPLAY_VERSION_W L"SeedFlow " SF_VERSION_WSTRING
