#ifndef GEODB_DATE_TIME_HPP
#define GEODB_DATE_TIME_HPP

#include "geodb/common.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>

/// \file
/// Date and time utilities.

namespace geodb {

namespace time = boost::posix_time;

namespace gregorian = boost::gregorian;

} // namespace geodb

#endif // GEODB_DATE_TIME_HPP
