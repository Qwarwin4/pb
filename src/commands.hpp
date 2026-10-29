#pragma once
#include <string>

int cmdInstall();
int cmdUninstall(bool purge);
int cmdRepair();
int cmdUpdate(bool yes);
int cmdClearCache();
int cmdDebug(const std::string& target);
