#ifndef PARSER_HPP
#define PARSER_HPP

#include "geodb/common.hpp"
#include "geodb/date_time.hpp"
#include "geodb/point.hpp"
#include "geodb/trajectory.hpp"

#include <istream>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace geodb {

class parse_error : public std::runtime_error {
public:
    using runtime_error::runtime_error;
};

struct plt_point {
    double latitude;
    double longitude;
    time::ptime time;
};

inline std::ostream& operator<<(std::ostream& o, const plt_point& p) {
    return o << "lat: " << p.latitude << " lng: " << p.longitude << " time: " << p.time;
}

struct activity {
    time::ptime begin;
    time::ptime end;    // inclusive!
    std::string name;
};

inline std::ostream& operator<<(std::ostream& o, const activity& a) {
    return o << "begin: " << a.begin << " end: " << a.end << " name: " << a.name;
}

/// Parses a file in PLT format. The result is a sequence of points (x, y, time).
void parse_plt(std::istream& in, std::vector<plt_point>& out);

/// Parses a labels file. The result is a vector of (begin, end, mode)
/// tuples where begin and end mark the time interval in which the transportation
/// mode `m` was active.
void parse_labels(std::istream& in, std::vector<activity>& out);

} // namespace geodb

#endif // PARSER_HPP
