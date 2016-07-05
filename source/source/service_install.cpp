#include <sys/stat.h>

#include <Poco/String.h>
#include <Poco/File.h>

#include "command_dev.h"
#include "utils.h"
#include "utils_docker.h"
#include "globallogger.h"
#include "globalcontext.h"
#include "exceptions.h"
#include "settingsbash.h"
#include "command_setup.h"
#include "generate_utils_sh.h"
#include "service.h"
#include "servicehook.h"
#include "sh_servicevars.h"
#include "drunnercompose.h"
#include "chmod.h"

std::string service::getUserID(std::string imagename) const
{
   std::vector<std::string> args = { "run","--rm","-i",imagename, "/bin/bash","-c","id -u" };
   std::string op;
	if (0 != utils::runcommand("docker",args, op, true))
		logmsg(kLERROR, "Unable to determine the user id in container " + imagename);

	logmsg(kLDEBUG, imagename+" is running under userID " + op + ".");
	return op;
}

void service::createLaunchScript() const
{
	Poco::Path target = utils::get_usersbindir().setFileName(getName());

	// remove stale script if present
   if (utils::fileexists(target))
   {
      Poco::File tf(target);
      tf.remove();
   }
	// write out new one.
	std::ofstream ofs;
	ofs.open(target.toString());
	if (!ofs.is_open())
		logmsg(kLERROR, "Couldn't write launch script at " + target.toString());
	ofs << "#!/bin/bash" << std::endl;
	ofs << "drunner servicecmd " << getName() << " \"$@\"" << std::endl;
	ofs.close();

	// fix permissions
	if (xchmod(target.toString().c_str(), S_700) != 0)
		logmsg(kLERROR, "Unable to change permissions on " + target.toString());

	logmsg(kLDEBUG, "Created launch script at " + target.toString());
}

void service::createVolumes(const drunnerCompose * const drc)
{
   if (drc == NULL)
      logmsg(kLERROR, "createVolumes passed NULL drunnerCompose.");

   std::string dname = "docker-volume-maker";

   for (const auto & svc : drc->getServicesInfo())
   {
      // each service may be running under a different userid.
      std::string userid = getUserID(svc.mImageName);
      if (userid == "0")
         logmsg(kLERROR, svc.mImageName + " is running as root user! Verboten."); // should never happen as we've validated the image already.

      for (const auto & entry : svc.mVolumes)
      {
         if (utils::dockerVolExists(entry.mDockerVolumeName))
            logmsg(kLINFO, "A docker volume already exists for " + entry.mDockerVolumeName + ", reusing it for " + svc.mImageName + ".");
         else
         {
            std::vector<std::string> args = { "volume","create","--name=\"" + entry.mDockerVolumeName + "\"" };
            int rval = utils::runcommand("docker", args);
            if (rval != 0)
               logmsg(kLERROR, "Unable to create docker volume " + entry.mDockerVolumeName);
            logmsg(kLDEBUG, "Created docker volume " + entry.mDockerVolumeName + " for " + svc.mImageName);
         }

         // set permissions on volume.
         tVecStr args;
         args.push_back("run");
         args.push_back("--name=\"" + dname + "\"");
         args.push_back("-v");
         args.push_back(entry.mDockerVolumeName + ":" + "/tempmount");
         args.push_back("drunner/rootutils");
         args.push_back("chown");
         args.push_back(userid + ":root");
         args.push_back("/tempmount");

         utils::dockerrun dr("/usr/bin/docker", args, dname);

         logmsg(kLDEBUG, "Set permissions to allow user " + userid + " access to volume " + entry.mDockerVolumeName);
      }
   }
}

void service::recreate(bool updating)
{
   if (updating)
      utils_docker::pullImage(getImageName());
   
   try
   {
      // nuke any existing dService files on host (but preserve volume containers!).
      if (utils::fileexists(getPath()))
      {
         Poco::File spath(getPath());
         spath.remove(true); // recursively delete.
      }

      // notice for hostVolumes.
      if (utils::fileexists(getPathHostVolume()))
         logmsg(kLINFO, "A drunner hostVolume already exists for " + getName() + ", reusing it.");

      // create the basic directories.
      ensureDirectoriesExist();

      // copy files to service directory on host.
      std::vector<std::string> args = { "run","--rm","-i","-v",
         getPathdRunner().toString() + ":/tempcopy", getImageName(), "/bin/bash", "-c" ,
         "cp -r /drunner/* /tempcopy/ && chmod a+rx /tempcopy/*" };
      std::string op;
      if (0 != utils::runcommand("docker", args, op, false))
         logmsg(kLERROR, "Couldn't copy the service files. You will need to reinstall the service.\nError:\n" + op);

      // write out variables.sh for the dService.
      drunnerCompose drc(*this);
      if (drc.readOkay()==kRError)
         fatal("Unexpected error - docker-compose.yml is broken.");
      
      // write out servicevars.sh for ourselves.
      sh_servicevars svcvars;
      svcvars.create(getImageName());
      if (!svcvars.writeSettings(svcvars.getPathFromParent(getPath())))
         fatal("Unexpected error - couldn't write out servicevars.sh.");

      // make sure we have the latest of all exra containers.
      for (const auto & entry : drc.getServicesInfo())
         if (entry.mImageName != getImageName()) // don't pull main image again.
            utils_docker::pullImage(entry.mImageName);

      // create the utils.sh file for the dService.
      generate_utils_sh(getPathdRunner());

      // create launch script
      createLaunchScript();

      // create volumes
      createVolumes(&drc);
   }

   catch (const eExit & e) {
      // tidy up.
      if (utils::fileexists(getPath()))
         utils::deltree(getPath());

      throw (e);
   }
}

void service::install()
{
   logmsg(kLDEBUG, "Installing " + getName() + " at " + getPath().toString() + ", using image " + getImageName());
	if (utils::fileexists(getPath()))
		logmsg(kLERROR, "Service already exists. Try:   drunner update " + getName());

	// make sure we have the latest version of the service.
   utils_docker::pullImage(getImageName());

	logmsg(kLDEBUG, "Attempting to validate " + getImageName());
   validateImage(getImageName());

   recreate(false);

   servicehook hook(this, "install");
   hook.endhook();

   logmsg(kLINFO, "Installation complete - try running " + getName()+ " now!");
}

eResult service::uninstall()
{
   if (!utils::fileexists(getPath()))
      logmsg(kLERROR, "Can't uninstall " + getName() + " - it does not exist.");

   servicehook hook(this, "uninstall");
   hook.starthook();

   // delete the service tree.
   logmsg(kLINFO, "Obliterating all of the dService files");
   utils::deltree(getPath());

   // delete launch script
   logmsg(kLINFO, "Deleting launch script");
   utils::delfile(getPathLaunchScript());

   if (utils::fileexists(getPath()))
      logmsg(kLERROR, "Uninstall failed - couldn't delete " + getPath().toString());

   hook.endhook();

   logmsg(kLINFO, "Uninstalled " + getName());
   return kRSuccess;
}

eResult service::obliterate()
{
   poco_assert(utils::fileexists(getPath()));

   servicehook hook(this, "obliterate");
   hook.starthook(); 

   logmsg(kLDEBUG, "Obliterating all the docker volumes - data will be gone forever.");
   {// [start] deleting docker volumes.
      drunnerCompose drc(*this);
      if (drc.readOkay()!=kRError)
      {
         tVecStr docvolnames;
         drc.getDockerVolumeNames(docvolnames);
         for (const auto & vol : docvolnames)
         {
            logmsg(kLINFO, "Obliterating docker volume " + vol);
            std::string op;
            std::vector<std::string> args = { "rm",vol };
            if (0 != utils::runcommand("docker", args, op, false))
            {
               logmsg(kLWARN, "Failed to remove " + vol + ":");
               logmsg(kLWARN, op);
            }
         }
      }
      else
         logmsg(kLDEBUG, "Couldn't read configuration to delete the associated docker volumes. :/");
   }// [end] deleting docker volumes.

   hook.endhook();
   return kRSuccess;
}


eResult service_obliterate::obliterate()
{
   eResult rval = kRNoChange;

   if (utils::fileexists(getPath()))
   {
      try
      {
         rval = kRError;
         service svc(getName());
         rval = svc.obliterate();
      }
      catch (const eExit &)
      {
      }
   }
   else
      logmsg(kLWARN, "There's no "+getName()+" directory, so can't obliterate its Docker volumes.");

   if (utils::fileexists(getPath()))
   { // delete the service tree.
      logmsg(kLINFO, "Obliterating all of the dService files");
      utils::deltree(getPath());
      rval = kRSuccess;
   }

   // delete the host volumes
   if (utils::fileexists(getPathHostVolume()))
   {
      logmsg(kLINFO, "Obliterating the hostVolumes (environment and servicerunner)");
      utils::deltree(getPathHostVolume());
      rval = kRSuccess;
   }

   // delete launch script
   if (utils::fileexists(getPathLaunchScript()))
   {
      logmsg(kLINFO, "Obliterating launch script");
      utils::delfile(getPathLaunchScript());
      rval = kRSuccess;
   }

   if (rval == kRNoChange)
      logmsg(kLWARN, "Couldn't find any trace of dService " + getName() + " - no changes made.");
   else
      logmsg(kLINFO, "Obliterated " + getName());
   return rval;
}



eResult service::recover()
{
   if (utils::fileexists(getPath()))
      uninstall();

   install();

   logmsg(kLINFO, getName() + " recovered.");
   return kRSuccess;
}