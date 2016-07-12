#include "service_paths.h"
#include "drunner_paths.h"


Poco::Path servicePaths::getPath() const
{
   Poco::Path p = drunnerPaths::getPath_dServices().pushDirectory(mName);
   poco_assert(p.isDirectory());
   return p;
}

Poco::Path servicePaths::getPathdRunner() const
{
   return getPath().pushDirectory("drunner");
}

Poco::Path servicePaths::getPathHostVolume() const
{
   return drunnerPaths::getPath_HostVolumes().pushDirectory(mName);
}

Poco::Path servicePaths::getPathServiceYml() const
{
   return getPathdRunner().setFileName("service.yml");
}

Poco::Path servicePaths::getPathServiceConfig() const
{
   return getPathHostVolume().setFileName("serviceconfig.yml");
}

Poco::Path servicePaths::getPathLaunchScript() const
{
   return drunnerPaths::getPath_Bin().setFileName(getName());
}

std::string servicePaths::getName() const
{
   return mName;
}