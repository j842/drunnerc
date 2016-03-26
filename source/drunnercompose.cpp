#include <sstream>
#include <fstream>
#include <string>

#include "yaml-cpp/yaml.h"

#include "drunnercompose.h"
#include "utils.h"
#include "logmsg.h"
#include "service.h"


// ---------------------------------------------------------------------------------------------------
//

class sh_variables : public settingsbash_reader
{
public:

   // creating ctor
   sh_variables(const drunnerCompose & drc) // sets defaults and reads the file if present.
      : settingsbash_reader(drc.getService().getPathVariables())
   {
      setDefaults();
      populate(drc);
      writeSettings(drc.getService().getPathVariables());
   }

protected:
   void setDefaults()
   {
      std::vector<std::string> nothing;
      setVec("VOLUMES", nothing);
      setVec("EXTRACONTAINERS", nothing);
      setString("SERVICENAME", "not set");
      setString("IMAGENAME", "not set");
      setString("SERVICETEMPDIR", "not set");
      setVec("DOCKERVOLS", nothing);
      setVec("DOCKEROPTS", nothing);

      setString("INSTALLTIME", utils::getTime());
      setString("HOSTIP", utils::getHostIP());
   }

private:

   bool populate(const drunnerCompose & drc)
   {
      std::vector<std::string> volumes, extracontainers, dockervols, dockeropts;

      for (const auto & entry : drc.getServicesInfo())
      {
         extracontainers.push_back(entry.mImageName);

         for (const auto & vol : entry.mVolumes)
         {
            volumes.push_back(vol.mMountPath);
            dockervols.push_back(vol.mDockerVolumeName);
            dockeropts.push_back("-v");
            dockeropts.push_back(vol.mDockerVolumeName + ":" + vol.mMountPath);
         }
      }

      setVec("VOLUMES", volumes);
      setVec("EXTRACONTAINERS", extracontainers);
      setVec("DOCKERVOLS", dockervols);
      setVec("DOCKEROPTS", dockeropts);

      setString("SERVICENAME", drc.getService().getName());
      setString("IMAGENAME", drc.getService().getImageName());
      setString("SERVICETEMPDIR", drc.getService().getPathTemp());

      return true;
   }
}; // sh_variables



// ---------------------------------------------------------------------------------------------------
//

class sh_legacy_servicecfg : public settingsbash_reader
{
public:
   // read ctor
   sh_legacy_servicecfg(std::string fullpath)
      : settingsbash_reader(fullpath)
   {
      setDefaults();
      read();
   } // ctor

   void setDefaults()
   {
      std::vector<std::string> nothing;
      setVec("VOLUMES", nothing);
      setVec("EXTRACONTAINERS", nothing);
      setString("VERSION", "1");
   }

   const std::vector<std::string> & getVolumes() const
   {
      return getVec("VOLUMES");
   }

   const std::vector<std::string> & getExtraContainers() const
   {
      return getVec("EXTRACONTAINERS");
   }

   int getVersion() const
   {
      return atoi(getString("VERSION").c_str());
   }
};

// ---------------------------------------------------------------------------------------------------
//


void InstallDockerCompose(const params & p)
{
   std::string url("https://github.com/docker/compose/releases/download/1.6.2/docker-compose-Linux-x86_64");
   std::string trgt(utils::get_usersbindir() + "/docker-compose");

   logmsg(kLDEBUG, "Downloading docker-compose...", p);
   utils::downloadexe(url, trgt, p);

   logmsg(kLDEBUG, "docker-compose installed.", p);
}

drunnerCompose::drunnerCompose(const service & svc, const params & p) : 
   mService(svc), 
   mParams(p), 
   mReadOkay(false),
   mVersion(0)
{
   if (!load_docker_compose_yml() && !load_servicecfg_sh())
      logmsg(kLDEBUG, "drunnerCompose - couldn't load either docker-compose.yml or servicecfg.sh for service "+svc.getName(), p);
}

void drunnerCompose::writeVariables()
{
   sh_variables shv(*this);

}

bool drunnerCompose::readOkay() const
{
   return mReadOkay;
}

const std::vector<cServiceInfo>& drunnerCompose::getServicesInfo() const
{
   return mServicesInfo;
}

const std::vector<cVolInfo>& drunnerCompose::getVolumes() const
{
   return mVolumes;
}

void drunnerCompose::getDockerVols(tVecStr & dv) const
{
   for (const auto & entry : mVolumes)
      dv.push_back(entry.mDockerVolumeName);
}

std::string drunnerCompose::getImageName() const
{
   return mService.getImageName();
}

const service & drunnerCompose::getService() const
{
   return mService;
}

int drunnerCompose::getVersion() const
{
   return mVersion;
}

std::string makevolname(
   std::string mainServiceName,
   std::string mountpath
   )
{
   return "drunner-" + mainServiceName + "-" + utils::alphanumericfilter(mountpath, false);
}

bool drunnerCompose::load_docker_compose_yml()
{
   if (!utils::fileexists(mService.getPathDockerCompose()))
      return false;

   logmsg(kLDEBUG, "Parsing " + mService.getPathDockerCompose(), mParams);

   mVersion = 3;
   YAML::Node config = YAML::LoadFile(mService.getPathDockerCompose());
   if (config.IsNull())
      logmsg(kLERROR, "Failed to load the docker-compose.yml file. Parse error?", mParams);

   if (!config["version"] || config["version"].as<int>() != 2)
      logmsg(kLERROR, "dRunner requires the docker-compose.yml file to be Version 2 format.", mParams);

   // parse volumes.
   if (!config["volumes"])
      logmsg(kLERROR, "docker-compose.yml is missing the required volumes section.", mParams);

   // parse services.
   if (!config["services"])
      logmsg(kLERROR, "docker-compose.yml is missing services section. Use servicecfg.sh not docker-compose.yml if there are no Docker services.", mParams);
   YAML::Node services = config["services"];

   if (services.Type() != YAML::NodeType::Map)
      logmsg(kLERROR, "services is not a map?!", mParams);

   for (auto it = services.begin(); it != services.end(); ++it)
   {
      cServiceInfo sinf;

      sinf.mServiceName = it->first.as<std::string>();
      logmsg(kLDEBUG, "Docker-compose service " + sinf.mServiceName + " found.", mParams);

      if (!it->second["image"])
         logmsg(kLERROR, "Service " + sinf.mServiceName + " is missing image definition.", mParams);
      sinf.mImageName = it->second["image"].as<std::string>();
      logmsg(kLDEBUG, sinf.mServiceName + " - Image:  " + sinf.mImageName, mParams);

      YAML::Node volumes = it->second["volumes"];
      if (volumes.IsNull())
         logmsg(kLDEBUG, sinf.mServiceName + " - No volumes defined.",mParams);
      else
         for (auto vol = volumes.begin(); vol != volumes.end(); ++vol)
         {
            std::string volinfo = vol->as<std::string>();
            logmsg(kLDEBUG, sinf.mServiceName + " - Volume: " + volinfo,mParams);

         }

   }

   if (!config["volumes"])
      logmsg(kLDEBUG, "docker-compose.yml has no volumes section.", mParams);

//   YAML::Node 

   logmsg(kLERROR, "Debug.", mParams);
   mReadOkay = false;
   return mReadOkay;
}


bool drunnerCompose::load_servicecfg_sh()
{
   sh_legacy_servicecfg svcfg(mService.getPathServiceCfg());
   if (!svcfg.readOkay())
   {
      logmsg(kLDEBUG, "Couldn't read servicecfg.sh from " + mService.getPathServiceCfg(),mParams);
      return false;
   }

   mVersion = svcfg.getVersion();
   mReadOkay = true;

   // load from servicecfg.
   const tVecStr & mtpts = svcfg.getVolumes();
   const tVecStr & ectrs = svcfg.getExtraContainers();

   cServiceInfo ci;
   ci.mImageName = mService.getImageName();
   ci.mServiceName = mService.getName();

   for (const auto & mtpt : mtpts)
   {
      cServiceVolInfo vsi;
      vsi.mDockerVolumeName = makevolname(mService.getName(), mtpt);
      vsi.mMountPath = mtpt;
      vsi.mLabel = vsi.mDockerVolumeName;
      ci.mVolumes.push_back(vsi);

      cVolInfo vi;
      vi.mDockerVolumeName = vsi.mDockerVolumeName;
      vi.mLabel = vsi.mLabel;
      mVolumes.push_back(vi);
   }
   mServicesInfo.push_back(ci);

   logmsg(kLDEBUG, "Successfully read " + mService.getPathServiceCfg(), mParams);
   return true;
}

// std::string dvol = "drunner-" + drc.getService().getName() + "-" + entry.mServiceName + "-" + utils::alphanumericfilter(vol.mMountPath[i], false);

