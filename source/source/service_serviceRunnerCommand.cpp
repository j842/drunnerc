#include <stdio.h>

#include <Poco/String.h>

#include "lua.hpp"

#include "service.h"
#include "globalcontext.h"
#include "globallogger.h"
#include "utils.h"
#include "utils_docker.h"



// ---------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------

cResult service::_launchCommandLine(const CommandLine & operation) const
{
   int result = utils::runcommand_stream(
      operation,
      GlobalContext::getParams()->serviceCmdMode(),
      getPathdRunner(),
      mServiceCfg.getVariables().getEnv()
   );
   return (result == 0 ? kRSuccess : kRError);
}

cResult service::_dstop(const CommandLine & operation) const
{
   if (operation.args.size() != 1)
      fatal("dstop requires exactly one argument: the docker container to stop and remove.");
   std::string cname = operation.args[0];
   if (utils_docker::dockerContainerExists(cname))
   {
      logmsg(kLDEBUG, "Stopping and removing container " + cname);
      std::string out;
      CommandLine cl("docker", { "stop",cname });
      if (utils::runcommand(cl, out, utils::kRC_LogCmd) != 0)
         return kRError;
      cl.args[0] = "rm";
      if (utils::runcommand(cl, out, utils::kRC_LogCmd) != 0)
         return kRError;
      return kRSuccess;
   }

   logmsg(kLDEBUG, "Container " + cname + " is not running.");
   return kRNoChange;
}


cResult service::_handleStandardCommands(const CommandLine & operation, bool & processed) const
{
   processed = true;

   for (const auto & y : mServiceYml.getCommands())
      if (utils::stringisame(y.name, operation.command))
         return _runserviceRunnerCommand(y, operation);  // link to another command.
      
   // check other commands.
   {
      using namespace utils;
      switch (str2int(operation.command.c_str()))
      {
      case str2int("dstop"):
         return _dstop(operation);
         break;
      default:
         break;
      }
   }
   processed = false;
   return kRNoChange;
}


cResult service::_runserviceRunnerCommand(const serviceyml::CommandDefinition & x, const CommandLine & serviceCmd) const
{
   cResult rval = kRNoChange;

   lua_State * L = luaL_newstate();
   luaL_openlibs(L);

   variables v(mServiceCfg.getVariables());

   // $0 is script name (serviceCmd)
   v.setVal(std::to_string(0), serviceCmd.command);

   // $1, $2, .... $n for individual arguments.
   for (unsigned int i = 0; i < serviceCmd.args.size(); ++i)
      v.setVal(std::to_string(i+1), serviceCmd.args[i]);

   // $# is number of args.
   v.setVal("#", std::to_string(serviceCmd.args.size()));
   
   // $@ is all args (but not script command).
   std::ostringstream allargs;
   for (const auto & sca : serviceCmd.args)
      allargs << sca << " ";
   v.setVal("@", Poco::trim(allargs.str()));

   // loop through all the operations in the command.
   for (const auto & rawoperation : x.operations)
   {
      // do variable substitution on all the arguments of the operation
      std::string lualine(v.substitute(rawoperation));
      logmsg(kLDEBUG, "Running command " + lualine);

      int ls = luaL_loadstring(L, lualine.c_str());
      if (!ls)
         ls = lua_pcall(L, 0, LUA_MULTRET, 0);

      if (ls)
         logmsg(kLERROR, "Error " + std::string(lua_tostring(L, -1)));
   }

   if (L) 
      lua_close(L);
   return rval;
}


cResult service::serviceRunnerCommand(const CommandLine & serviceCmd) const
{
   if (serviceCmd.command.length()==0 || utils::stringisame(serviceCmd.command, "help"))
   { // show help
      std::cout << std::endl << mServiceYml.getHelp() << std::endl;
      return kRSuccess;
   }

   std::ostringstream oss;
   oss << serviceCmd.command;
   for (const auto & x : serviceCmd.args) oss << " " << x;
   logmsg(kLDEBUG, "serviceRunner - serviceCmd is: " + oss.str());

   // find the command in our command list and run it.
   for (const auto & y : mServiceYml.getCommands())
      if (utils::stringisame(y.name, serviceCmd.command) == 0)
         return _runserviceRunnerCommand(y, serviceCmd);

   return kRNotImplemented;
}