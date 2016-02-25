#ifndef __LOGSTATEMENT_H
#define __LOGSTATEMENT_H

#include "enums.h"
#include "params.h"
#include <string>

void logmsg(eLogLevel level, std::string s, eLogLevel cutoff=kLINFO);
void logverbatim(eLogLevel level, std::string s, eLogLevel cutoff);




#endif