#ifndef GEOLIFE_HPP
#define GEOLIFE_HPP

#include "geodb/trajectory.hpp"
#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_external.hpp"

#include <tpie/progress_indicator_base.h>
#include <tpie/serialization_stream.h>

#include <vector>

using labels_map = geodb::string_map<geodb::string_map_external>;

/// Parses the geolife dataset located at `path`.
/// Maps string labels to integers using the `labels` object.
/// Trajectories are written to `out`.
void parse_geolife(const geodb::fs::path& path, labels_map& labels,
                                                   tpie::serialization_writer& out,
                                                   tpie::progress_indicator_base& progress);

#endif // GEOLIFE_HPP
