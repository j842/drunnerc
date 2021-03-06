#ifndef __GENERATE_VALIDATOR_IMAGE_H
#define __GENERATE_VALIDATOR_IMAGE_H

#include "enums.h"
#include "params.h"
#include "utils.h"
#include "generate.h"

void generate_validator_image(Poco::Path supportpath)
{
// should really just validate with C++. :/

   std::string vdata = R"EOF(#!/bin/bash
set -o nounset
set -e
# Validate the image I'm running in as being Docker Runner compatible

function die { echo "Not dRunner compatible - $1"; exit 1 ; }

if [ "$UID" -eq 0 ]; then die "the container runs as root." ; fi

# Check mandatory files in image (global var IMAGENAME) before touching host. Is it a valid dService?
[ -e "/drunner/servicerunner" ] || die "does not have servicerunner."

[ ! -e "/drunner/servicecfg.sh" ] || \
   die "Outdated dService with servicecfg.sh (no longer supported)."

exit 0
}
)EOF";

   supportpath.setFileName("validator-image");
   generate(supportpath,S_755,vdata);
}


#endif
