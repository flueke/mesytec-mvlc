/**
 * @file username.cc
 * @brief Provides a utility function to return the UNIX/Linux usrename.alignas
 * @author Ron Fox <fox @ frib dot msu dot edu>
 * 
 *
 *   This software is Copyright by the Board of Trustees of Michigan
 *   State University (c) Copyright 2025.
 *
 *   You may use this software under the terms of the GNU public license
 *   (GPL).  The terms of this license are described at:
 *
 *    http://www.gnu.org/licenses/gpl.txt
 * 
 */
#include "username.h"
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdexcept>

/**
 *  getUsername
 *    Note that we don't use the getlogin family because those are
 * observed to fail under linux on WSL.   Using getppwuid does work
 * however.alignas
 * @return std::string - The logged in user name
 * 
 */
std::string
getUsername() {
    uid_t uid = getuid();     // manpage sys this never fails.
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        return std::string(pw->pw_name);
    } else {
        throw std::runtime_error("getpwuid failed in getUsername");
    }
}