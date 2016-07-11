#include <sstream>

#include "drunner_setup.h"
#include "utils.h"
#include "utils_docker.h"
#include "drunner_settings.h"
#include "globallogger.h"
#include "globalcontext.h"
#include "generate_validator_image.h"
#include "drunner_compose.h"
#include "drunner_paths.h"

#include <Poco/Process.h>
#include <Poco/Path.h>
#include <Poco/File.h>

namespace drunnerSetup
{

   cResult check_setup(bool forceUpdate)
   {
      const params & p(*GlobalContext::getParams().get());

      // -----------------------------------------------------------------------------
      // create rootpath if it doesn't exist.
      if (!utils::fileexists(drunnerPaths::getPath_Root()))
         utils::makedirectory(drunnerPaths::getPath_Root(), S_755);
      else if (!forceUpdate)
         return kRNoChange; // nothing needed.

      // -----------------------------------------------------------------------------
      // create bin directory
      utils::makedirectory(drunnerPaths::getPath_Bin(), S_700);
      
      // -----------------------------------------------------------------------------
      // generate plugin scripts
      GlobalContext::getPlugins()->generate_plugin_scripts();

      // -----------------------------------------------------------------------------
      // get latest root util image.
      utils_docker::pullImage(drunnerPaths::getdrunnerUtilsImage());

      // -----------------------------------------------------------------------------
      // create services, support and temp directories
      utils::makedirectory(drunnerPaths::getPath_dServices(), S_755);
      utils::makedirectory(drunnerPaths::getPath_Support(), S_755);
      utils::makedirectory(drunnerPaths::getPath_Temp(), S_755);
      utils::makedirectory(drunnerPaths::getPath_HostVolumes(), S_755);

      // create the validator script that is run inside containers
      generate_validator_image(drunnerPaths::getPath_Support());

      // write settings.
      GlobalContext::getSettings()->writeSettings();

      // -----------------------------------------------------------------------------
      // Finished!
      if (forceUpdate)
         logmsg(kLINFO, "Update of drunner to " + p.getVersion() + " completed succesfully.");
      else
         logmsg(kLINFO, "Initial setup of drunner " + p.getVersion() + " completed succesfully.");

      return kRSuccess;
   }

   int update()
   {
      const params & p(*GlobalContext::getParams().get());
      const drunnerSettings & s(*GlobalContext::getSettings().get());

      logmsg(kLDEBUG, "Updating dRunner in " + drunnerPaths::getPath_Root().toString());

      std::string url(s.getdrunnerInstallURL());
      Poco::Path trgt(drunnerPaths::getPath_Root());
      trgt.setFileName("drunner-install");

      utils::downloadexe(url, trgt);

      logmsg(kLINFO, "Updating...");

      tVecStr args;
      args.push_back("drunner-install");
      for (auto opt : p.getOptions())
         args.push_back(opt);
      args.push_back("setup");
      args.push_back(drunnerPaths::getPath_Root().toString());
      
      std::ostringstream oss;
      for (auto arg : args)
         oss << arg << " ";
      logmsg(kLDEBUG, utils::trim_copy(oss.str()));

      Poco::ProcessHandle ph = Poco::Process::launch(trgt.toString(), args);
      int result = ph.wait();
      return result;
   }

}
