#ifndef GEOLIFE_HPP
#define GEOLIFE_HPP

#include "geodb/trajectory.hpp"
#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_external.hpp"

#include <vector>

using labels_map = geodb::string_map<geodb::string_map_external>;

std::vector<geodb::point_trajectory> parse_geolife(const geodb::fs::path& path, labels_map& labels);

#endif // GEOLIFE_HPP
