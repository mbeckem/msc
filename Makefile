MAKEFLAGS := $(MAKEFLAGS) --no-print-directory

EVAL_NODE_BUILDING := \
	results/node_building_random_walk.txt \
	results/node_building_random_walk.json \
	results/node_building_others.txt \
	results/node_building_others.json

EVAL_TREE_BUILDING := results/tree_building.txt results/tree_building.json
EVAL_LARGE_DATASET := results/large_dataset.txt results/large_dataset.json
EVAL_QUERIES := results/queries.json results/tree_stats.json

EVAL_CHEAP_QUICKLOAD := results/cheap_quickload.txt results/cheap_quickload.json
EVAL_FANOUT_CONSTRUCTION = results/fanouts.json

QUICKLOAD_PROFILE_GEOLIFE_16M := results/quickload-geolife-16.txt
QUICKLOAD_PROFILE_GEOLIFE_256M := results/quickload-geolife-256.txt
QUICKLOAD_PROFILE_OSM_16M := results/quickload-osm-16.txt
QUICKLOAD_PROFILE_OSM_256M := results/quickload-osm-256.txt
QUICKLOAD_PROFILES := \
	$(QUICKLOAD_PROFILE_GEOLIFE_16M) \
	$(QUICKLOAD_PROFILE_GEOLIFE_256M) \
	$(QUICKLOAD_PROFILE_OSM_16M) \
	$(QUICKLOAD_PROFILE_OSM_256M)

EXAMPLE_LEAVES := results/hilbert_leaves.pdf results/str_leaves.pdf
IRWI_EXAMPLE_BOXES := results/irwi_example_boxes.pdf

QUERY_GRAPHICS := \
	results/query.pdf \
	results/query_beta_strategies.pdf \
	results/query_beta_values.pdf \
	results/query_bloom_filters.pdf \
	results/query_str_variants.pdf \
	results/query_fanout.pdf

TREE_CONSTRUCTION_GRAPHICS := \
	results/construction.pdf \
	results/construction_logscale.pdf \
	results/construction_fanout.pdf

INDEX_CONSTRUCTION_GRAPHICS := \
	results/index_construction.pdf

EXAMPLES := \
	$(HILBERT_CURVE) \
	$(EXAMPLE_LEAVES) \
	$(IRWI_EXAMPLE_BOXES)


# Note: delete any of the result files (e.g. results/tree_building.txt)
# to repeat the associated experiment. 
all:
	@truncate -s 0 make.log
	@echo "Note: Log will be written into make.log"

	@echo "Making sure that the project is compiled ..."
	@$(MAKE) compile >> make.log

	@echo "Generating dataset ..."
	@$(MAKE) dataset >> make.log

	@echo "Creating trees ..."
	@$(MAKE) trees >> make.log

	@echo "Building trees with different fanouts ..."
	@$(MAKE) $(EVAL_FANOUT_CONSTRUCTION) >> make.log

	@echo "Evaluating cheap quickload variant ..."
	@$(MAKE) $(EVAL_CHEAP_QUICKLOAD) >> make.log

	@echo "Evaluating quickload memory scaling ..."
	@$(MAKE) $(EVAL_LARGE_DATASET) >> make.log

	@echo "Creating quickload trace files ..."
	@$(MAKE) $(QUICKLOAD_PROFILES) >> make.log

	@echo "Evaluating scalable node building algorithm ..."
	@$(MAKE) $(EVAL_NODE_BUILDING) >> make.log

	@echo "Running queries ..."
	@$(MAKE) queries >> make.log

	@echo "Creating graphics ..."
	@$(MAKE) graphics >> make.log

.PHONY: all

dataset:
	scripts/generate_datasets.py

.PHONY: dataset

compile:
	mkdir -p build
	cd build && cmake -DCMAKE_BUILD_TYPE=Release ../code && $(MAKE) -j5

.PHONY: compile

trees:
	@$(MAKE) $(EVAL_TREE_BUILDING)
	scripts/build_tree_variants.py

.PHONY: trees

queries:
# Files for the translation of label string <-> label index.
	build/strings --input "data/geolife.strings" > "output/geolife.strings.txt"
	build/strings --input "data/geolife.strings" --json > "output/geolife.strings.json"
	build/strings --input "data/osm.strings" > "output/osm.strings.txt"
	build/strings --input "data/osm.strings" --json > "output/osm.strings.json"
	$(MAKE) $(EVAL_QUERIES)

.PHONY: queries

graphics: $(EXAMPLES) \
	$(TREE_CONSTRUCTION_GRAPHICS) \
	$(INDEX_CONSTRUCTION_GRAPHICS) \
	$(QUERY_GRAPHICS)

.PHONY: graphics

clean:
	rm -rf build/*
	rm -rf results/*
	rm -rf output/*
	rm -rf tmp/*
	@echo "Datasets will not be deleted. Delete files in data/ manually instead."

.PHONY: clean

# Generate a rule that produces multiple outputs.
# Args: 1. The list of output files.
#		2. The command to produce the output files.
#		3. A unique filename prefix for an intermediate target.
#		4. A set of dependencies for the output files.
define multi_target =
$(1): $(3).intermediate

$(3).intermediate: $(4)
	$(2)
	touch $(3).intermediate

.INTERMEDIATE: $(3).intermediate
endef

# --- Experiments
$(eval $(call multi_target,$(EVAL_NODE_BUILDING),scripts/eval_node_building.py,node-building,))

$(eval $(call multi_target,$(EVAL_TREE_BUILDING),scripts/eval_tree_building.py,tree-building,))

$(eval $(call multi_target,$(EVAL_LARGE_DATASET),scripts/eval_large_dataset.py,large-dataset,))

$(eval $(call multi_target,$(EVAL_QUERIES),scripts/eval_query.py,queries,))

$(eval $(call multi_target,$(EVAL_CHEAP_QUICKLOAD),scripts/eval_cheap_quickload.py,cheap-quickload,))

$(EVAL_FANOUT_CONSTRUCTION):
	scripts/eval_fanout.py

# --- Tree Construction
$(TREE_CONSTRUCTION_GRAPHICS): tree-construction-graphics.intermediate

tree-construction-graphics.intermediate: results/tree_building.json results/fanouts.json
	scripts/tree_construction_graphics.py
	touch $@

.INTERMEDIATE: tree-construction-graphics.intermediate

$(INDEX_CONSTRUCTION_GRAPHICS): index-construction-graphics.intermediate

# --- Index Construction
index-construction-graphics.intermediate: results/node_building_random_walk.json results/node_building_others.json
	scripts/index_construction_graphics.py
	touch $@

.INTERMEDIATE: index-construction-graphics.intermediate

$(QUERY_GRAPHICS): query-graphics.intermediate

# --- Query graphics
query-graphics.intermediate: results/queries.json
	scripts/query_graphics.py
	touch $@

.INTERMEDIATE: query-graphics.intermediate

# --- Example graphics
$(HILBERT_CURVE):
	scripts/hilbert_curve.py

$(IRWI_EXAMPLE_BOXES):
	scripts/irwi_example.py

$(eval $(call multi_target,$(EXAMPLE_LEAVES),scripts/leaves.py,leaves,))

# 1: max memory
# 2: entry file
# 3: output file
define quickload_profile =
	scripts/compile.py --debug-stats
	rm -rf tmp/quickload-profile-tree
	ulimit -Sn 64000 && \
	build/loader --algorithm quickload \
		--tree tmp/quickload-profile-tree \
		--max-memory $(1) \
		--entries $(2) > $(3)
endef

$(QUICKLOAD_PROFILE_GEOLIFE_16M):
	$(call quickload_profile,16,data/geolife.entries,$@)

$(QUICKLOAD_PROFILE_GEOLIFE_256M):
	$(call quickload_profile,256,data/geolife.entries,$@)

$(QUICKLOAD_PROFILE_OSM_16M):
	$(call quickload_profile,16,data/osm.entries,$@)

$(QUICKLOAD_PROFILE_OSM_256M):
	$(call quickload_profile,256,data/osm.entries,$@)
