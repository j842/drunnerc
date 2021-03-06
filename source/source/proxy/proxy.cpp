#include <fstream>
#include <cereal/archives/json.hpp>
#include <memory>
#include "Poco/String.h"

#include "proxy.h"
#include "utils.h"
#include "drunner_paths.h"
#include "dassert.h"
#include "utils_docker.h"
#include "caddy.h"

proxy::proxy()
{
}

// only call after load() is called (so mData is filled).
std::unique_ptr<proxyplugin> proxy::getPlugin()
{
   switch (s2i(GlobalContext().getSettings()->getProxy().c_str()))
   {
   case s2i("none") :
      return NULL;
   case s2i("caddy") :
      return std::unique_ptr<proxyplugin>(new caddy(mData.mProxyData));
   default:
      logmsg(kLERROR, "Invalid proxy setting: " + GlobalContext().getSettings()->getProxy());
   }
   return NULL;
}


cResult proxy::proxyenable(proxydatum pd)
{
   logmsg(kLINFO, "Enabling HTTPS reverse proxy for " + pd.servicename);

   cResult r = pd.valid();
   if (r.error())
      return r;

   // load existing settings.
   r = load();
   if (r.error())
      return r;

   for (unsigned int i = 0; i < mData.mProxyData.size(); ++i)
      if (Poco::icompare(mData.mProxyData[i].servicename, pd.servicename) == 0)
      {
         if (mData.mProxyData[i] == pd)
            return kRNoChange; // already enabled.

         mData.mProxyData.erase(mData.mProxyData.begin() + i);
      }

   mData.mProxyData.push_back(pd);

   r = proxyconfigchanged();
   return r;
}

cResult proxy::proxydisable(std::string service)
{
   // load existing settings.
   cResult r = load();
   if (r.error())
      return r;

   int todelete = -1;
   for (unsigned int i = 0; i < mData.mProxyData.size(); ++i)
      if (Poco::icompare(mData.mProxyData[i].servicename, service) == 0)
         todelete = i;

   if (todelete == -1)
      return kRNoChange; // already disabled.

   mData.mProxyData.erase(mData.mProxyData.begin() + todelete);

   r = proxyconfigchanged();
   return r;
}

// called when "drunner proxy" entered on command line.
cResult proxy::handledrunnerproxycommand()
{
   cResult r = load();
   if (r.success())
      r += getPlugin()->restart();
   return r;
}

cResult proxy::proxyconfigchanged()
{
   // save updated settings
   cResult r = save();
   if (r.error())
      return r;

   // Restart the proxy.
   r += getPlugin()->restart();
   return r;
}


cResult proxy::load()
{
   if (!utils::fileexists(saveFilePath()))
      return kRNoChange;

   // read the settings.
   std::ifstream is(saveFilePath().toString());
   if (is.bad())
      return cError("Unable to open " + saveFilePath().toString() + " for reading.");
   try
   {
      mData.mProxyData.clear();
      cereal::JSONInputArchive archive(is);
      archive(mData);
   }
   catch (const cereal::Exception & e)
   {
      return cError("Cereal exception on reading settings: " + std::string(e.what()));
   }

   return kRSuccess;
}

cResult proxy::save()
{
   logdbg("Creating " + saveFilePath().toString());
   std::ofstream os(saveFilePath().toString());
   if (os.bad() || !os.is_open())
      return cError("Unable to open " + saveFilePath().toString() + " for writing.");

   try
   {
      cereal::JSONOutputArchive archive(os);
      archive(mData);
   }
   catch (const cereal::Exception & e)
   {
      return cError("Cereal exception on writing settings: " + std::string(e.what()));
   }
   drunner_assert(utils::fileexists(saveFilePath()), "Failed to create settings at " + saveFilePath().toString());
   return kRSuccess;
}

Poco::Path proxy::saveFilePath()
{
   return drunnerPaths::getPath_Settings().setFileName("dRunner_Proxy.json");
}

cResult proxydatum::valid()
{
   if (servicename.length() == 0)
      return cError("Service name not set.");
   if (domain.length() == 0)
      return cError("Domain name not set.");
   if (container.length() == 0)
      return cError("Container not set.");
   if (port.length() == 0 || atoi(port.c_str())<=0)
      return cError("Port not set.");
   switch (s2i(mode.c_str()))
   {
   case s2i("fake"):
      return kRSuccess;

   case s2i("staging"):
      return cError("Staging not currently supported.");

   case s2i("production"):
      if (email.length() == 0)
         return cError("Email not set, required for staging and production modes.");
      if (email.find("@") == std::string::npos)
         return cError("Email field not set to an email address.");
      break;

   default:
      return cError("Unkown mode - must be fake, staging or production");
   }

   return kRSuccess;
}

bool proxydatum::operator ==(const proxydatum &b) const
{
   if (Poco::icompare(servicename, b.servicename) != 0) return false;
   if (Poco::icompare(domain, b.domain) != 0) return false;
   if (Poco::icompare(container, b.container) != 0) return false;
   if (Poco::icompare(port, b.port) != 0) return false;
   if (Poco::icompare(email, b.email) != 0) return false;
   if (Poco::icompare(mode, b.mode) != 0) return false;

   return true;
}
