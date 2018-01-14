/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include <glib/gerror.h>
#include <string>

namespace GstStatic
{
    GError* getInitError();
    std::string getVersion();
}
